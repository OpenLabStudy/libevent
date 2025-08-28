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

#define UDS_COMMAND_PATH "/tmp/udsCommand.sock"
#define READ_HIGH_WM (1u * 1024u * 1024u)   /* 1MB */
#define MAX_PAYLOAD  (4u * 1024u * 1024u)   /* 4MB */

typedef struct app_ctx;
/* === Per-connection context === */
typedef struct uds_ctx{
    struct bufferevent *pstBufEvent;

    /* 요청/응답 흐름 제어 */
    struct event *pstSendKickTimer;   /* 큐에 있으면 즉시(or 지연) 전송 */
    struct event *pstRespTimeout;     /* 응답 타임아웃 (단발) */

    int   iSockFd;
    int   iReqBusy;       /* 진행 중 요청이 있으면 1 */
    int16_t nReqCmd;      /* 진행 중 요청 CMD (응답 매칭) */
    uint32_t uReqSeq;     /* 선택: 요청 시퀀스(원하면 payload/MSG_ID에 포함) */
    uint8_t  uRetries;    /* 재시도 횟수 */

    /* 간단 송신 큐(단일 슬롯). 필요 시 링버퍼로 확장 */
    int      hasQueued;
    int16_t  qCmd;
    uint8_t  qPayload[256];
    int32_t  qLen;

    /* --- 추가: 연결 리스트/역포인터 --- */
    struct uds_ctx *pstUdsNext;
    struct app_ctx *pstApp;
} UDS_CTX;

typedef struct app_ctx{
    struct event_base       *pstEventBase;
    struct evconnlistener   *pstUdsEventListener;
    struct event            *pstEventSigint;

    UDS_CTX                 *pConnHead;
    struct event            *pstKeepAliveTimer;
} APP_CTX;

/* === Forward declarations === */
static void udsListenerCb(struct evconnlistener*, evutil_socket_t, struct sockaddr*, int, void*);
static void udsReadCb(struct bufferevent*, void*);
static void udsWriteCb(struct bufferevent*, void*);
static void udsEventCb(struct bufferevent*, short, void*);

static void send_kick_cb(evutil_socket_t, short, void*);
static void resp_timeout_cb(evutil_socket_t, short, void*);
static void signalCb(evutil_socket_t, short, void*);

static void send_request_now(UDS_CTX* ctx);
static void enqueue_request(UDS_CTX* ctx, int16_t cmd, const void* payload, int32_t len);
static void udsCloseAndFree(UDS_CTX* ctx);

/* === 프레임 송신 === */
static void sendUdsFrame(struct bufferevent* bev, int16_t nCmd,
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

/* === 10초마다: 모든 연결에 KEEP_ALIVE REQ 전송 === */
static void keepaliveCb(evutil_socket_t fd, short ev, void* pvData)
{
    (void)fd;
    (void)ev;
    APP_CTX* pstAppCtx = (APP_CTX*)pvData;

    /* 모든 연결 순회 */
    for (UDS_CTX* c = pstAppCtx->pConnHead; c; c = c->pstUdsNext) {
        if (!c->iReqBusy) {
            REQ_KEEP_ALIVE ka = (REQ_KEEP_ALIVE){0};
            enqueue_request(c, CMD_KEEP_ALIVE, &ka, sizeof(ka));
        }
    }

    /* 재무장 (10초 주기) */
    if (pstAppCtx->pstKeepAliveTimer) {
        struct timeval tv = {10, 0};
        evtimer_add(pstAppCtx->pstKeepAliveTimer, &tv);
    }
}

/* === 요청 큐 적재 === */
static void enqueue_request(UDS_CTX* pstUdsCtx, int16_t cmd, const void* payload, int32_t len) {
    /* 단일 슬롯: 진행/대기 중이면 덮어씀(혹은 무시하도록 바꿔도 됨) */
    pstUdsCtx->qCmd = cmd;
    pstUdsCtx->qLen = (len > (int32_t)sizeof(pstUdsCtx->qPayload)) ? (int32_t)sizeof(pstUdsCtx->qPayload) : len;
    if (len > 0 && payload) 
        memcpy(pstUdsCtx->qPayload, payload, (size_t)pstUdsCtx->qLen);
    pstUdsCtx->hasQueued = 1;

    /* 즉시 전송 트리거 */
    if (pstUdsCtx->pstSendKickTimer) {
        struct timeval tv = {0, 1000}; /* 1ms 지연(사실상 즉시) */
        evtimer_add(pstUdsCtx->pstSendKickTimer, &tv);
    }
}

/* === 실제 요청 전송 === */
static void send_request_now(UDS_CTX* pstUdsCtx) {
    if (!pstUdsCtx->hasQueued || pstUdsCtx->iReqBusy)
        return;

    MSG_ID ids = { .chSrcId = 1, .chDstId = 1 }; /* 필요 시 규칙화/시퀀스 반영 */
    pstUdsCtx->nReqCmd = pstUdsCtx->qCmd;
    pstUdsCtx->iReqBusy = 1;
    pstUdsCtx->uRetries = 0;
    pstUdsCtx->uReqSeq++;

    sendUdsFrame(pstUdsCtx->pstBufEvent, pstUdsCtx->qCmd, &ids, 0, pstUdsCtx->qPayload, pstUdsCtx->qLen);
    pstUdsCtx->hasQueued = 0;

    /* 응답 타임아웃 설정 (500msec) */
    if (pstUdsCtx->pstRespTimeout) {
        struct timeval tmo = {0, 500000};
        evtimer_add(pstUdsCtx->pstRespTimeout, &tmo);
    }
}

/* === 응답 타임아웃 === */
static void resp_timeout_cb(evutil_socket_t fd, short ev, void* pvData) {
    (void)fd;
    (void)ev;
    UDS_CTX* pstUdsCtx = pvData;

    if (!pstUdsCtx->iReqBusy)
        return;

    if (pstUdsCtx->uRetries < 2) { /* 최대 2회 재시도 */
        pstUdsCtx->uRetries++;

        /* 재송신: 간단히 동일 프레임을 payload 없이 재전송 예시(필요시 저장/재사용) */
        MSG_ID ids = { .chSrcId = 1, .chDstId = 1 };
        sendUdsFrame(pstUdsCtx->pstBufEvent, pstUdsCtx->nReqCmd, &ids, 0, NULL, 0);
        struct timeval tmo = {0, 500000};
        evtimer_add(pstUdsCtx->pstRespTimeout, &tmo);
        fprintf(stderr, "Retry %u for cmd=%d\n", pstUdsCtx->uRetries, pstUdsCtx->nReqCmd);
        return;
    }

    /* 실패 처리 */
    fprintf(stderr, "Request timeout cmd=%d\n", pstUdsCtx->nReqCmd);
    pstUdsCtx->iReqBusy = 0;

    /* 큐에 대기 중이면 다음 요청 시도 */
    if (pstUdsCtx->hasQueued && pstUdsCtx->pstSendKickTimer) {
        struct timeval tv = {0, 1000};
        evtimer_add(pstUdsCtx->pstSendKickTimer, &tv);
    }
}

/* === 송신 킥(즉시 전송) === */
static void send_kick_cb(evutil_socket_t fd, short ev, void* pvData) {
    (void)fd; (void)ev;
    UDS_CTX* pstUdsCtx = pvData;
    send_request_now(pstUdsCtx);
}

/* === 프레임 하나 파싱 & 응답 처리 ===
 * return: 1 consumed, 0 need more, -1 fatal
 */
static int try_consume_one_frame(struct evbuffer* pstEventBuffer, UDS_CTX* pstUdsCtx) {
    if (evbuffer_get_length(pstEventBuffer) < sizeof(FRAME_HEADER))
        return 0;

    FRAME_HEADER stFrameHeader;
    if (evbuffer_copyout(pstEventBuffer, &stFrameHeader, sizeof(stFrameHeader)) != (ssize_t)sizeof(stFrameHeader))
        return 0;

    uint16_t stx  = ntohs(stFrameHeader.unStx);
    int32_t  plen = ntohl(stFrameHeader.iLength);
    int16_t  cmd  = ntohs(stFrameHeader.nCmd);
    MSG_ID   ids  = stFrameHeader.stMsgId;

    if (stx != STX_CONST || plen < 0 || plen > (int32_t)MAX_PAYLOAD)
        return -1;

    size_t need = sizeof(FRAME_HEADER) + (size_t)plen + sizeof(FRAME_TAIL);
    if (evbuffer_get_length(pstEventBuffer) < need)
        return 0;

    evbuffer_drain(pstEventBuffer, sizeof(FRAME_HEADER));

    uint8_t* payload = NULL;
    if (plen > 0) {
        payload = (uint8_t*)malloc((size_t)plen);
        if (!payload) return -1;
        if (evbuffer_remove(pstEventBuffer, payload, (size_t)plen) != (ssize_t)plen) {
            free(payload); return -1;
        }
    }

    FRAME_TAIL t;
    if (evbuffer_remove(pstEventBuffer, &t, sizeof(t)) != (ssize_t)sizeof(t)) {
        free(payload); return -1;
    }

    if (ntohs(t.unEtx) != ETX_CONST ||
        proto_crc8_xor(payload, (size_t)plen) != (uint8_t)t.chCrc) {
        free(payload); return -1;
    }

    /* === 응답 처리 ===
       - 기본 가정: 요청 CMD와 응답 CMD가 동일
    */
    if (pstUdsCtx->iReqBusy && cmd == pstUdsCtx->nReqCmd) {
        /* 응답 매칭 성공 → 타임아웃 해제 */
        if (pstUdsCtx->pstRespTimeout) evtimer_del(pstUdsCtx->pstRespTimeout);

        /* 명령별 응답 후크 (필요시 바꾸기) */
        switch (cmd) {
            case CMD_REQ_ID: {
                if (plen == (int32_t)sizeof(RES_ID)) {
                    const RES_ID* res = (const RES_ID*)payload;
                    fprintf(stderr, "[SockFd=%d] RES_ID RES: result=%d\n", pstUdsCtx->iSockFd, res->chResult);
                } else {
                    fprintf(stderr, "[SockFd=%d] RES_ID RES len=%d\n", pstUdsCtx->iSockFd, plen);
                }
                break;
            }
            case CMD_KEEP_ALIVE: {
                if (plen == (int32_t)sizeof(RES_KEEP_ALIVE)) {
                    const RES_KEEP_ALIVE* res = (const RES_KEEP_ALIVE*)payload;
                    fprintf(stderr, "[SockFd=%d] KEEP_ALIVE RES: result=%d\n", pstUdsCtx->iSockFd, res->chResult);
                } else {
                    fprintf(stderr, "[SockFd=%d] KEEP_ALIVE RES len=%d\n", pstUdsCtx->iSockFd, plen);
                }
                break;
            }
            case CMD_IBIT: {
                if (plen == (int32_t)sizeof(RES_IBIT)) {
                    const RES_IBIT* res = (const RES_IBIT*)payload;
                    fprintf(stderr, "[SockFd=%d] IBIT RES: tot=%d pos=%d\n",
                            pstUdsCtx->iSockFd, res->chBitTotResult, res->chPositionResult);
                } else {
                    fprintf(stderr, "[SockFd=%d] IBIT RES len=%d\n", pstUdsCtx->iSockFd, plen);
                }
                break;
            }
            default:
                fprintf(stderr, "[SockFd=%d] RES cmd=%d len=%d\n", pstUdsCtx->iSockFd, cmd, plen);
                break;
        }

        pstUdsCtx->iReqBusy = 0;
        free(payload);

        /* 큐에 대기 중이면 다음 요청 */
        if (pstUdsCtx->hasQueued && pstUdsCtx->pstSendKickTimer) {
            struct timeval tv = {0, 1000};
            evtimer_add(pstUdsCtx->pstSendKickTimer, &tv);
        }
        return 1;
    }

    /* 진행 중 요청이 없거나, 예상 외 프레임이면: (상대 push일 수 있음) */
    fprintf(stderr, "[SockFd=%d] Unexpected frame cmd=%d len=%d (no pending req or mismatched)\n",
            pstUdsCtx->iSockFd, cmd, plen);
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
            udsCloseAndFree(pstUdsCtx);
            return; 
        }
    }
}

static void udsWriteCb(struct bufferevent *bev, void *pvData) {
    (void)bev;
    (void)pvData;
}

static void udsEventCb(struct bufferevent *bev, short events, void *pvData) {
    UDS_CTX* pstUdsCtx = (UDS_CTX*)pvData;
    (void)bev;
    if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
        udsCloseAndFree(pstUdsCtx);
    }
}

/* === SIGINT: 루프 종료 및 소켓 파일 제거 === */
static void signalCb(evutil_socket_t sig, short ev, void* pvData)
{
    (void)sig;
    (void)ev;
    APP_CTX* pstAppCtx = (APP_CTX*)pvData;
    event_base_loopexit(pstAppCtx->pstEventBase, NULL);
    unlink(UDS_COMMAND_PATH);
}

/* === Listener: accept → UDS_CTX 생성/설정 === */
static void udsListenerCb(struct evconnlistener *listener, evutil_socket_t iSockFd,
                        struct sockaddr *sa, int socklen, void *pvData)
{
    (void)listener;
    (void)sa;
    (void)socklen;
    APP_CTX* pstAppCtx = (APP_CTX*)pvData;
    struct event_base *pstEventBase = pstAppCtx->pstEventBase;
    struct bufferevent *pstBufferEvent = bufferevent_socket_new(pstEventBase, iSockFd, BEV_OPT_CLOSE_ON_FREE);
    if (!pstBufferEvent) 
        return;

    UDS_CTX* pstUdsCtx = (UDS_CTX*)calloc(1, sizeof(*pstUdsCtx));
    if (!pstUdsCtx) { 
        bufferevent_free(pstBufferEvent);
        return; 
    }

    pstUdsCtx->pstBufEvent = pstBufferEvent;
    pstUdsCtx->iSockFd = (int)iSockFd;
    /* 타이머 생성 (응답 타임아웃/송신 킥만 유지) */
    pstUdsCtx->pstRespTimeout   = evtimer_new(pstEventBase, resp_timeout_cb, pstUdsCtx);
    pstUdsCtx->pstSendKickTimer = evtimer_new(pstEventBase, send_kick_cb,   pstUdsCtx);

    /* 연결 리스트 등록 */
    pstUdsCtx->pstApp = pstAppCtx;
    pstUdsCtx->pstUdsNext  = pstAppCtx->pConnHead;
    pstAppCtx->pConnHead = pstUdsCtx;

    bufferevent_setcb(pstBufferEvent, udsReadCb, udsWriteCb, udsEventCb, pstUdsCtx);
    bufferevent_enable(pstBufferEvent, EV_READ|EV_WRITE);
    bufferevent_setwatermark(pstBufferEvent, EV_READ, 0, READ_HIGH_WM);

    printf("Accepted UDS client (iSockFd=%d)\n", pstUdsCtx->iSockFd);

    /* 연결 직후 초기 요청 예시: KEEP_ALIVE */
    REQ_ID stReqId = {0};
    enqueue_request(pstUdsCtx, CMD_REQ_ID, &stReqId, sizeof(stReqId));
}

/* === Helper: 연결 자원 정리 === */
static void udsCloseAndFree(UDS_CTX* pstUdsCtx)
{
    if (!pstUdsCtx)
        return;

    /* 리스트에서 제거 */
    if (pstUdsCtx->pstApp) {
        APP_CTX* app = pstUdsCtx->pstApp;
        UDS_CTX** pp = &app->pConnHead;
        while (*pp) {
            if (*pp == pstUdsCtx) { 
                *pp = pstUdsCtx->pstUdsNext; 
                break;
            }
            pp = &((*pp)->pstUdsNext);
        }
    }

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
    /* 기존 소켓 파일 제거 후 시작 */
    unlink(UDS_COMMAND_PATH);

    signal(SIGPIPE, SIG_IGN);

    stAppCtx.pstEventBase = event_base_new();
    if (!stAppCtx.pstEventBase) {
        fprintf(stderr, "Could not initialize libevent!\n");
        return 1;
    }

    /* UDS 주소 준비 */
    struct sockaddr_un stSocketUn;
    memset(&stSocketUn, 0, sizeof(stSocketUn));
    stSocketUn.sun_family = AF_UNIX;
    size_t iSize = strlen(UDS_COMMAND_PATH);
    if (iSize >= sizeof(stSocketUn.sun_path)) {
        fprintf(stderr, "UDS path too long: %s\n", UDS_COMMAND_PATH);
        exit(1);
    }
    memcpy(stSocketUn.sun_path, UDS_COMMAND_PATH, iSize+1);
    socklen_t uiSocketLength = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + iSize + 1);

    stAppCtx.pstUdsEventListener =
        evconnlistener_new_bind(stAppCtx.pstEventBase, udsListenerCb, &stAppCtx,
            LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_FREE, -1,
            (struct sockaddr*)&stSocketUn, uiSocketLength);
    if (!stAppCtx.pstUdsEventListener) {
        fprintf(stderr, "Could not create a UDS listener! (%s)\n", strerror(errno));
        event_base_free(stAppCtx.pstEventBase);
        unlink(UDS_COMMAND_PATH);
        return 1;
    }

    /* SIGINT(CTRL+C) 처리 */
    stAppCtx.pstEventSigint = evsignal_new(stAppCtx.pstEventBase, SIGINT, signalCb, &stAppCtx);
    if (!stAppCtx.pstEventSigint || event_add(stAppCtx.pstEventSigint, NULL) < 0) {
        fprintf(stderr, "Could not create/add SIGINT event!\n");
        evconnlistener_free(stAppCtx.pstUdsEventListener);
        event_base_free(stAppCtx.pstEventBase);
        unlink(UDS_COMMAND_PATH);
        return 1;
    }
    /* --- 추가: 10초 주기 KEEPALIVE 타이머 설치 --- */
    stAppCtx.pstKeepAliveTimer = evtimer_new(stAppCtx.pstEventBase, keepaliveCb, &stAppCtx);
    if (!stAppCtx.pstKeepAliveTimer) {
        fprintf(stderr, "Could not create keepalive timer!\n");
        evconnlistener_free(stAppCtx.pstUdsEventListener);
        event_base_free(stAppCtx.pstEventBase);
        unlink(UDS_COMMAND_PATH);
        return 1;
    }
    {
        struct timeval tv = {10, 0};
        evtimer_add(stAppCtx.pstKeepAliveTimer, &tv);
    }

    printf("UDS server listening on %s\n", UDS_COMMAND_PATH);
    event_base_dispatch(stAppCtx.pstEventBase);
    if (stAppCtx.pstKeepAliveTimer)
        event_free(stAppCtx.pstKeepAliveTimer);

    evconnlistener_free(stAppCtx.pstUdsEventListener);
    event_free(stAppCtx.pstEventSigint);
    event_base_free(stAppCtx.pstEventBase);

    /* 소켓 파일 정리 */
    unlink(UDS_COMMAND_PATH);

    printf("done\n");
    return 0;
}
