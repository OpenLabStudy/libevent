/*
 * Framed initiator UDS server (Libevent, Unix Domain Socket, no IBIT timer)
 * - 서버가 먼저 요청(REQ)을 보내고, 클라이언트가 응답(RES)을 회신하는 구조
 * - 전역 변수 없음: ServerCtx/UDS_CTX로 컨텍스트 관리
 *
 * Build: gcc -Wall -O2 -o udsServer udsServer.c -levent
 * Run  : ./udsServer /tmp/echo_ibit.sock
 *
 * 필요 전제: "protocol.h"에서 아래 심볼/타입이 정의되어 있어야 함
 *   - STX_CONST, ETX_CONST
 *   - FRAME_HEADER { uint16_t unStx; int32_t iLength; MSG_ID stMsgId; char chSubModule; int16_t nCmd; }
 *   - FRAME_TAIL   { char chCrc; uint16_t unEtx; }
 *   - MSG_ID       { uint8_t chSrcId, chDstId; ... (필요 시 확장) }
 *   - CMD_ECHO, CMD_KEEP_ALIVE, CMD_IBIT (명령 코드)
 *   - REQ_KEEP_ALIVE, RES_KEEP_ALIVE, REQ_IBIT, RES_IBIT (요청/응답 payload)
 *   - uint8_t proto_crc8_xor(const uint8_t* buf, size_t len)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stddef.h>     /* offsetof */

#include <event2/event.h>
#include <event2/listener.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/util.h>

#include "protocol.h"  /* 공통 프로토콜 정의 */

#define DEFAULT_UDS_PATH "/tmp/echo_ibit.sock"
#define READ_HIGH_WM (1u * 1024u * 1024u)   /* 1MB */
#define MAX_PAYLOAD  (4u * 1024u * 1024u)   /* 4MB */

/* === Per-connection context === */
typedef struct {
    struct bufferevent *pstBufEvent;

    /* 요청/응답 흐름 제어 */
    struct event *pstSendKickTimer;   /* 큐에 있으면 즉시(or 지연) 전송 */
    struct event *pstRespTimeout;     /* 응답 타임아웃 (단발) */

    int   iFd;            /* 디버깅용 */
    int   iReqBusy;       /* 진행 중 요청이 있으면 1 */
    int16_t nReqCmd;      /* 진행 중 요청 CMD (응답 매칭) */
    uint32_t uReqSeq;     /* 선택: 요청 시퀀스(원하면 payload/MSG_ID에 포함) */
    uint8_t  uRetries;    /* 재시도 횟수 */

    /* 간단 송신 큐(단일 슬롯). 필요 시 링버퍼로 확장 */
    int      hasQueued;
    int16_t  qCmd;
    uint8_t  qPayload[256];
    int32_t  qLen;
} UDS_CTX;

/* === Server context (전역 제거) === */
typedef struct {
    struct event_base     *base;
    struct evconnlistener *listener;
    struct event          *sigint;
    char udspath[sizeof(((struct sockaddr_un*)0)->sun_path)];
} APP_CTX;

/* === Forward declarations === */
static void listenerCb(struct evconnlistener*, evutil_socket_t, struct sockaddr*, int, void*);
static void tcpReadCb(struct bufferevent*, void*);
static void tcpWriteCb(struct bufferevent*, void*);
static void tcpEventCb(struct bufferevent*, short, void*);

static void send_kick_cb(evutil_socket_t, short, void*);
static void resp_timeout_cb(evutil_socket_t, short, void*);
static void signalCb(evutil_socket_t, short, void*);

static void send_request_now(UDS_CTX* ctx);
static void enqueue_request(UDS_CTX* ctx, int16_t cmd, const void* payload, int32_t len);
static void tcpCloseAndFree(UDS_CTX* ctx);

/* === 프레임 송신 === */
static void sendTcpFrame(struct bufferevent* bev, int16_t nCmd,
                       const MSG_ID* mid, char submodule,
                       const void* pl, int32_t len)
{
    FRAME_HEADER h;
    FRAME_TAIL   t;

    h.unStx       = htons(STX_CONST);
    h.iLength     = htonl(len);
    h.stMsgId     = *mid;     /* 1-byte fields 가정 */
    h.chSubModule = submodule;
    h.nCmd        = htons(nCmd);

    t.chCrc       = proto_crc8_xor((const uint8_t*)pl, (size_t)len);
    t.unEtx       = htons(ETX_CONST);

    bufferevent_write(bev, &h, sizeof(h));
    if (len > 0 && pl)
        bufferevent_write(bev, pl, len);
    bufferevent_write(bev, &t, sizeof(t));
}

/* === 요청 큐 적재 === */
static void enqueue_request(UDS_CTX* ctx, int16_t cmd, const void* payload, int32_t len) {
    /* 단일 슬롯: 진행/대기 중이면 덮어씀(혹은 무시하도록 바꿔도 됨) */
    ctx->qCmd = cmd;
    ctx->qLen = (len > (int32_t)sizeof(ctx->qPayload)) ? (int32_t)sizeof(ctx->qPayload) : len;
    if (len > 0 && payload) memcpy(ctx->qPayload, payload, (size_t)ctx->qLen);
    ctx->hasQueued = 1;

    /* 즉시 전송 트리거 */
    if (ctx->pstSendKickTimer) {
        struct timeval tv = {0, 1000}; /* 1ms 지연(사실상 즉시) */
        evtimer_add(ctx->pstSendKickTimer, &tv);
    }
}

/* === 실제 요청 전송 === */
static void send_request_now(UDS_CTX* ctx) {
    if (!ctx->hasQueued || ctx->iReqBusy) return;

    MSG_ID ids = { .chSrcId = 1, .chDstId = 1 }; /* 필요 시 규칙화/시퀀스 반영 */
    ctx->nReqCmd = ctx->qCmd;
    ctx->iReqBusy = 1;
    ctx->uRetries = 0;
    ctx->uReqSeq++;

    sendTcpFrame(ctx->pstBufEvent, ctx->qCmd, &ids, 0, ctx->qPayload, ctx->qLen);
    ctx->hasQueued = 0;

    /* 응답 타임아웃 설정 (3초) */
    if (ctx->pstRespTimeout) {
        struct timeval tmo = {3, 0};
        evtimer_add(ctx->pstRespTimeout, &tmo);
    }
}

/* === 응답 타임아웃 === */
static void resp_timeout_cb(evutil_socket_t fd, short ev, void* data) {
    (void)fd; (void)ev;
    UDS_CTX* ctx = data;

    if (!ctx->iReqBusy) return;

    if (ctx->uRetries < 2) { /* 최대 2회 재시도 */
        ctx->uRetries++;

        /* 재송신: 간단히 동일 프레임을 payload 없이 재전송 예시(필요시 저장/재사용) */
        MSG_ID ids = { .chSrcId = 1, .chDstId = 1 };
        sendTcpFrame(ctx->pstBufEvent, ctx->nReqCmd, &ids, 0, NULL, 0);
        struct timeval tmo = {3, 0};
        evtimer_add(ctx->pstRespTimeout, &tmo);
        fprintf(stderr, "Retry %u for cmd=%d\n", ctx->uRetries, ctx->nReqCmd);
        return;
    }

    /* 실패 처리 */
    fprintf(stderr, "Request timeout cmd=%d\n", ctx->nReqCmd);
    ctx->iReqBusy = 0;

    /* 큐에 대기 중이면 다음 요청 시도 */
    if (ctx->hasQueued && ctx->pstSendKickTimer) {
        struct timeval tv = {0, 1000};
        evtimer_add(ctx->pstSendKickTimer, &tv);
    }
}

/* === 송신 킥(즉시 전송) === */
static void send_kick_cb(evutil_socket_t fd, short ev, void* data) {
    (void)fd; (void)ev;
    UDS_CTX* ctx = data;
    send_request_now(ctx);
}

/* === 프레임 하나 파싱 & 응답 처리 ===
 * return: 1 consumed, 0 need more, -1 fatal
 */
static int try_consume_one_frame(struct evbuffer* buf, UDS_CTX* ctx) {
    if (evbuffer_get_length(buf) < sizeof(FRAME_HEADER))
        return 0;

    FRAME_HEADER h;
    if (evbuffer_copyout(buf, &h, sizeof(h)) != (ssize_t)sizeof(h))
        return 0;

    uint16_t stx  = ntohs(h.unStx);
    int32_t  plen = ntohl(h.iLength);
    int16_t  cmd  = ntohs(h.nCmd);
    MSG_ID   ids  = h.stMsgId;

    if (stx != STX_CONST || plen < 0 || plen > (int32_t)MAX_PAYLOAD)
        return -1;

    size_t need = sizeof(FRAME_HEADER) + (size_t)plen + sizeof(FRAME_TAIL);
    if (evbuffer_get_length(buf) < need)
        return 0;

    evbuffer_drain(buf, sizeof(FRAME_HEADER));

    uint8_t* payload = NULL;
    if (plen > 0) {
        payload = (uint8_t*)malloc((size_t)plen);
        if (!payload) return -1;
        if (evbuffer_remove(buf, payload, (size_t)plen) != (ssize_t)plen) {
            free(payload); return -1;
        }
    }

    FRAME_TAIL t;
    if (evbuffer_remove(buf, &t, sizeof(t)) != (ssize_t)sizeof(t)) {
        free(payload); return -1;
    }

    if (ntohs(t.unEtx) != ETX_CONST ||
        proto_crc8_xor(payload, (size_t)plen) != (uint8_t)t.chCrc) {
        free(payload); return -1;
    }

    /* === 응답 처리 ===
       - 기본 가정: 요청 CMD와 응답 CMD가 동일
    */
    if (ctx->iReqBusy && cmd == ctx->nReqCmd) {
        /* 응답 매칭 성공 → 타임아웃 해제 */
        if (ctx->pstRespTimeout) evtimer_del(ctx->pstRespTimeout);

        /* 명령별 응답 후크 (필요시 바꾸기) */
        switch (cmd) {
            case CMD_KEEP_ALIVE: {
                if (plen == (int32_t)sizeof(RES_KEEP_ALIVE)) {
                    const RES_KEEP_ALIVE* res = (const RES_KEEP_ALIVE*)payload;
                    fprintf(stderr, "[fd=%d] KEEP_ALIVE RES: result=%d\n", ctx->iFd, res->chResult);
                } else {
                    fprintf(stderr, "[fd=%d] KEEP_ALIVE RES len=%d\n", ctx->iFd, plen);
                }
                break;
            }
            case CMD_IBIT: {
                if (plen == (int32_t)sizeof(RES_IBIT)) {
                    const RES_IBIT* res = (const RES_IBIT*)payload;
                    fprintf(stderr, "[fd=%d] IBIT RES: tot=%d pos=%d\n",
                            ctx->iFd, res->chBitTotResult, res->chPositionResult);
                } else {
                    fprintf(stderr, "[fd=%d] IBIT RES len=%d\n", ctx->iFd, plen);
                }
                break;
            }
            default:
                fprintf(stderr, "[fd=%d] RES cmd=%d len=%d\n", ctx->iFd, cmd, plen);
                break;
        }

        ctx->iReqBusy = 0;
        free(payload);

        /* 큐에 대기 중이면 다음 요청 */
        if (ctx->hasQueued && ctx->pstSendKickTimer) {
            struct timeval tv = {0, 1000};
            evtimer_add(ctx->pstSendKickTimer, &tv);
        }
        return 1;
    }

    /* 진행 중 요청이 없거나, 예상 외 프레임이면: (상대 push일 수 있음) */
    fprintf(stderr, "[fd=%d] Unexpected frame cmd=%d len=%d (no pending req or mismatched)\n",
            ctx->iFd, cmd, plen);
    free(payload);
    return 1;
}

/* === Libevent callbacks === */
static void udsReadCb(struct bufferevent *pstBufferEvent, void *pvData) {
    UDS_CTX* pstUdsCtx = (UDS_CTX*)pvData;
    struct evbuffer *pstEventBuffer = bufferevent_get_input(pstBufferEvent);
    for (;;) {
        int r = try_consume_one_frame(pstEventBuffer, pstUdsCtx);
        if (r == 0) 
            break;
        if (r < 0) { 
            tcpCloseAndFree(pstUdsCtx);
            return; 
        }
    }
}

static void udsWriteCb(struct bufferevent *bev, void *pvData) {
    (void)bev;
    (void)pvData;
}

static void tcpEventCb(struct bufferevent *bev, short events, void *pvData) {
    UDS_CTX* pstUdsCtx = (UDS_CTX*)pvData;
    (void)bev;
    if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
        tcpCloseAndFree(pstUdsCtx);
    }
}

/* === SIGINT: 루프 종료 및 소켓 파일 제거 === */
static void signalCb(evutil_socket_t sig, short ev, void* pvData)
{
    (void)sig;
    (void)ev;
    APP_CTX* pstAppCtx = (APP_CTX*)pvData;
    event_base_loopexit(pstAppCtx->base, NULL);
    unlink(pstAppCtx->udspath);
}

/* === Listener: accept → UDS_CTX 생성/설정 === */
static void listenerCb(struct evconnlistener *listener, evutil_socket_t fd,
                        struct sockaddr *sa, int socklen, void *pvData)
{
    (void)listener;
    (void)sa;
    (void)socklen;
    struct event_base *pstEventBase = (struct event_base*)pvData;

    struct bufferevent *pstBufferEvent = bufferevent_socket_new(pstEventBase, fd, BEV_OPT_CLOSE_ON_FREE);
    if (!pstBufferEvent) 
        return;

    UDS_CTX* pstUdsCtx = (UDS_CTX*)calloc(1, sizeof(*pstUdsCtx));
    if (!pstUdsCtx) { 
        bufferevent_free(pstBufferEvent);
        return; 
    }

    pstUdsCtx->pstBufEvent = pstBufferEvent;
    pstUdsCtx->iFd = (int)fd;

    bufferevent_setcb(pstBufferEvent, tcpReadCb, tcpWriteCb, tcpEventCb, pstUdsCtx);
    bufferevent_enable(pstBufferEvent, EV_READ|EV_WRITE);
    bufferevent_setwatermark(pstBufferEvent, EV_READ, 0, READ_HIGH_WM);

    /* 타이머 생성 (응답 타임아웃/송신 킥만 유지) */
    pstUdsCtx->pstRespTimeout   = evtimer_new(pstEventBase, resp_timeout_cb, pstUdsCtx);
    pstUdsCtx->pstSendKickTimer = evtimer_new(pstEventBase, send_kick_cb,   pstUdsCtx);

    printf("Accepted UDS client (fd=%d)\n", pstUdsCtx->iFd);

    /* 연결 직후 초기 요청 예시: KEEP_ALIVE */
    REQ_KEEP_ALIVE ka = {0};
    enqueue_request(pstUdsCtx, CMD_KEEP_ALIVE, &ka, sizeof(ka));
}

/* === Helper: 연결 자원 정리 === */
static void tcpCloseAndFree(UDS_CTX* pstUdsCtx)
{
    if (!pstUdsCtx) 
        return;
    if (pstUdsCtx->pstSendKickTimer)
        event_free(pstUdsCtx->pstSendKickTimer);
    if (pstUdsCtx->pstRespTimeout)
        event_free(pstUdsCtx->pstRespTimeout);
    if (pstUdsCtx->pstBufEvent)
        bufferevent_free(pstUdsCtx->pstBufEvent);
    free(pstUdsCtx);
}

/* === main === */
int main(int argc, char** argv)
{
    APP_CTX stAppCtx = (APP_CTX){0};
    const char* pchPath = (argc > 1 && argv[1] && argv[1][0]) ? argv[1] : DEFAULT_UDS_PATH;

    if (strlen(pchPath) >= sizeof(stAppCtx.udspath)) {
        fprintf(stderr,"UDS 경로가 너무 깁니다 (max %zu)\n", sizeof(stAppCtx.udspath)-1);
        return 1;
    }
    strncpy(stAppCtx.udspath, pchPath, sizeof(stAppCtx.udspath)-1);

    /* 기존 소켓 파일 제거 후 시작 */
    unlink(stAppCtx.udspath);
    /* 권한 제어를 위해 umask 완화(필요 시 조정) */
    umask(0);

    signal(SIGPIPE, SIG_IGN);

    stAppCtx.base = event_base_new();
    if (!stAppCtx.base) {
        fprintf(stderr, "Could not initialize libevent!\n");
        return 1;
    }

    /* UDS 주소 준비 */
    struct sockaddr_un sun; memset(&sun, 0, sizeof(sun));
    sun.sun_family = AF_UNIX;
    strncpy(sun.sun_path, stAppCtx.udspath, sizeof(sun.sun_path)-1);

    socklen_t slen = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + strlen(sun.sun_path) + 1);

    stAppCtx.listener =
        evconnlistener_new_bind(stAppCtx.base, listenerCb, stAppCtx.base,
            LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_FREE, -1,
            (struct sockaddr*)&sun, slen);

    if (!stAppCtx.listener) {
        fprintf(stderr, "Could not create a UDS listener! (%s)\n", strerror(errno));
        event_base_free(stAppCtx.base);
        unlink(stAppCtx.udspath);
        return 1;
    }

    /* 바인드 성공 후 접근권한 설정 (rw for owner/group) */
    chmod(stAppCtx.udspath, 0660);

    /* SIGINT(CTRL+C) 처리 */
    stAppCtx.sigint = evsignal_new(stAppCtx.base, SIGINT, signalCb, &stAppCtx);
    if (!stAppCtx.sigint || event_add(stAppCtx.sigint, NULL)<0) {
        fprintf(stderr, "Could not create/add SIGINT event!\n");
        evconnlistener_free(stAppCtx.listener);
        event_base_free(stAppCtx.base);
        unlink(stAppCtx.udspath);
        return 1;
    }

    printf("UDS server listening on %s\n", stAppCtx.udspath);
    event_base_dispatch(stAppCtx.base);

    evconnlistener_free(stAppCtx.listener);
    event_free(stAppCtx.sigint);
    event_base_free(stAppCtx.base);

    /* 소켓 파일 정리 */
    unlink(stAppCtx.udspath);

    printf("done\n");
    return 0;
}
