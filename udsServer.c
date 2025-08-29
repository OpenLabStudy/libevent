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

#define UDS_COMMAND_PATH    "/tmp/udsCommand.sock"
#define READ_HIGH_WM        (1u * 1024u * 1024u)   /* 1MB */
#define MAX_PAYLOAD         (4u * 1024u * 1024u)   /* 4MB */
#define MAX_RETRY           1

typedef struct app_ctx;
/* === Per-connection context === */
typedef struct uds_ctx{
    struct bufferevent  *pstBufferEvent;

    /* 요청/응답 흐름 제어 */
    struct event        *pstSendKickTimer;   /* 큐에 있으면 즉시(or 지연) 전송 */
    struct event        *pstRespTimeout;     /* 응답 타임아웃 (단발) */

    int                 iSockFd;
    int                 iReqBusy;       /* 진행 중 요청이 있으면 1 */
    unsigned char       uchRetries;    /* 재시도 횟수 */

    /* 간단 송신 큐(단일 슬롯). 필요 시 링버퍼로 확장 */
    unsigned char       uchHasQueued;
    unsigned short      unCmd;
    unsigned char       uchPayload[256];
    int                 iDataLength;

    /**/
    unsigned char       uchSrcId;
    unsigned char       uchDstId;

    /* --- 추가: 연결 리스트/역포인터 --- */
    struct uds_ctx      *pstUdsNext;
    struct app_ctx      *pstApp;
} UDS_SERVER_CTX;

typedef struct app_ctx{
    struct event_base       *pstEventBase;
    struct evconnlistener   *pstUdsEventListener;
    struct event            *pstEventSigint;

    UDS_SERVER_CTX          *pConnHead;
    struct event            *pstKeepAliveTimer;
} APP_CTX;

/* === Forward declarations === */
static void udsListenerCb(struct evconnlistener*, evutil_socket_t, struct sockaddr*, int, void*);
static void udsReadCb(struct bufferevent*, void*);
static void udsEventCb(struct bufferevent*, short, void*);

static void requestCb(evutil_socket_t, short, void*);
static void respTimeoutCb(evutil_socket_t, short, void*);
static void signalCb(evutil_socket_t, short, void*);

static void sendRequestNow(UDS_SERVER_CTX*);
static void requestQueue(UDS_SERVER_CTX*, unsigned short, const void*, int);
static void udsCloseAndFree(UDS_SERVER_CTX*);

/* === 프레임 송신 === */
static void sendUdsFrame(struct bufferevent* pstBufferEvent, unsigned short unCmd,
                       const MSG_ID* pstMsgId, unsigned char uchSubModule,
                       const void* pvPayload, int iDataLength)
{
    FRAME_HEADER stFrameHeader;
    FRAME_TAIL   stFrameTail;

    stFrameHeader.unStx             = htons(STX_CONST);
    stFrameHeader.iDataLength       = htonl(iDataLength);
    stFrameHeader.stMsgId.uchSrcId  = pstMsgId->uchSrcId;
    stFrameHeader.stMsgId.uchDstId  = pstMsgId->uchDstId;
    stFrameHeader.uchSubModule      = uchSubModule;
    stFrameHeader.unCmd             = htons(unCmd);

    stFrameTail.uchCrc              = proto_crc8_xor((const uint8_t*)pvPayload, (size_t)iDataLength);
    stFrameTail.unEtx               = htons(ETX_CONST);

    bufferevent_write(pstBufferEvent, &stFrameHeader, sizeof(stFrameHeader));
    if (iDataLength > 0 && pvPayload)
        bufferevent_write(pstBufferEvent, pvPayload, iDataLength);
    bufferevent_write(pstBufferEvent, &stFrameTail, sizeof(stFrameTail));
}

static void timeoutSet(struct event *pstTimeoutEvent, unsigned int uiSec, unsigned int uiUsec)
{
        struct timeval tv = {uiSec, uiUsec};
        evtimer_add(pstTimeoutEvent, &tv);
}

/* === 10초마다: 모든 연결에 KEEP_ALIVE REQ 전송 === */
static void keepaliveCb(evutil_socket_t fd, short ev, void* pvData)
{
    (void)fd;
    (void)ev;
    APP_CTX* pstAppCtx = (APP_CTX*)pvData;

    /* 모든 연결 순회 */
    for (UDS_SERVER_CTX* c = pstAppCtx->pConnHead; c; c = c->pstUdsNext) {
        if (!c->iReqBusy) {
            REQ_KEEP_ALIVE ka = (REQ_KEEP_ALIVE){0};
            requestQueue(c, CMD_KEEP_ALIVE, &ka, sizeof(ka));
        }
    }

    /* 재무장 (10초 주기) */
    if (pstAppCtx->pstKeepAliveTimer) {
        timeoutSet(pstAppCtx->pstKeepAliveTimer, 5, 0);
    }
}

/* === 요청 큐 적재 === */
static void requestQueue(UDS_SERVER_CTX* pstUdsCtx, unsigned short unCmd, const void* pvPayload, int iDataLength) 
{
    pstUdsCtx->unCmd = unCmd;
    pstUdsCtx->iDataLength = (iDataLength > (int32_t)sizeof(pstUdsCtx->uchPayload)) ? (int32_t)sizeof(pstUdsCtx->uchPayload) : iDataLength;
    if (iDataLength > 0 && pvPayload) 
        memcpy(pstUdsCtx->uchPayload, pvPayload, (size_t)pstUdsCtx->iDataLength);
    pstUdsCtx->uchHasQueued = 1;

    /* 즉시 전송 트리거 */
    if (pstUdsCtx->pstSendKickTimer) {
        timeoutSet(pstUdsCtx->pstSendKickTimer, 0, 1000);
    }
}

/* === 실제 요청 전송 === */
static void sendRequestNow(UDS_SERVER_CTX* pstUdsCtx) {
    if (!pstUdsCtx->uchHasQueued || pstUdsCtx->iReqBusy)
        return;

    MSG_ID stMsgId;
    stMsgId.uchSrcId = pstUdsCtx->uchSrcId;
    stMsgId.uchDstId = pstUdsCtx->uchDstId;
    pstUdsCtx->iReqBusy = 1;
    pstUdsCtx->uchRetries = 0;
    sendUdsFrame(pstUdsCtx->pstBufferEvent, pstUdsCtx->unCmd, &stMsgId, 0, pstUdsCtx->uchPayload, pstUdsCtx->iDataLength);
    pstUdsCtx->uchHasQueued = 0;

    /* 응답 타임아웃 설정 (100msec) */
    if (pstUdsCtx->pstRespTimeout) {
        timeoutSet(pstUdsCtx->pstRespTimeout, 0, 100000);
    }
}

/* === 응답 타임아웃 === */
static void respTimeoutCb(evutil_socket_t fd, short ev, void* pvData) {
    (void)fd;
    (void)ev;

    UDS_SERVER_CTX* pstUdsCtx = pvData;
    MSG_ID stMsgId;
    if (!pstUdsCtx->iReqBusy)
        return;

    if (pstUdsCtx->uchRetries < MAX_RETRY) { /* 최대 2회 재시도 */
        pstUdsCtx->uchRetries++;

        /* 재송신: 간단히 동일 프레임을 payload 없이 재전송 예시(필요시 저장/재사용) */
        stMsgId.uchSrcId = pstUdsCtx->uchSrcId;
        stMsgId.uchDstId = pstUdsCtx->uchDstId;
        sendUdsFrame(pstUdsCtx->pstBufferEvent, pstUdsCtx->unCmd, &stMsgId, 0, pstUdsCtx->uchPayload, pstUdsCtx->iDataLength);
        timeoutSet(pstUdsCtx->pstRespTimeout, 0, 100000);
        fprintf(stderr, "Retry %u for cmd=%d\n", pstUdsCtx->uchRetries, pstUdsCtx->unCmd);
        return;
    }

    /* 실패 처리 */
    fprintf(stderr, "Request timeout cmd=%d\n", pstUdsCtx->unCmd);
    pstUdsCtx->iReqBusy = 0;

    /* 큐에 대기 중이면 다음 요청 시도 */
    if (pstUdsCtx->uchHasQueued && pstUdsCtx->pstSendKickTimer) {
        timeoutSet(pstUdsCtx->pstSendKickTimer, 0, 1000);
    }
}

static void requestCb(evutil_socket_t fd, short ev, void* pvData) {
    (void)fd; (void)ev;
    UDS_SERVER_CTX* pstUdsCtx = pvData;
    sendRequestNow(pstUdsCtx);
}

/* === 프레임 하나 파싱 & 응답 처리 ===
 * return: 1 consumed, 0 need more, -1 fatal
 */
static int try_consume_one_frame(struct evbuffer* pstEvBuffer, UDS_SERVER_CTX* pstUdsCtx) {
    if (evbuffer_get_length(pstEvBuffer) < sizeof(FRAME_HEADER))
        return 0;

    FRAME_HEADER stFrameHeader;
    if (evbuffer_copyout(pstEvBuffer, &stFrameHeader, sizeof(stFrameHeader)) != (ssize_t)sizeof(stFrameHeader))
        return 0;

    unsigned short  unStx   = ntohs(stFrameHeader.unStx);
    int iDataLength = ntohl(stFrameHeader.iDataLength);
    unsigned short  unCmd  = ntohs(stFrameHeader.unCmd);
    MSG_ID stMsgId;
    stMsgId.uchSrcId = stFrameHeader.stMsgId.uchSrcId;
    stMsgId.uchDstId = stFrameHeader.stMsgId.uchDstId;

    if (unStx != STX_CONST || iDataLength < 0 || iDataLength > (int32_t)MAX_PAYLOAD)
        return -1;

    int iNeedSize = sizeof(FRAME_HEADER) + (size_t)iDataLength + sizeof(FRAME_TAIL);
    if (evbuffer_get_length(pstEvBuffer) < iNeedSize)
        return 0;

    evbuffer_drain(pstEvBuffer, sizeof(FRAME_HEADER));

    unsigned char *uchPayload = NULL;
    if (iDataLength > 0) {
        uchPayload = (unsigned char *)malloc((size_t)iDataLength);
        if (!uchPayload) 
            return -1;
        if (evbuffer_remove(pstEvBuffer, uchPayload, (size_t)iDataLength) != iDataLength) {
            free(uchPayload); 
            return -1;
        }
    }

    FRAME_TAIL stFrameTail;
    if (evbuffer_remove(pstEvBuffer, &stFrameTail, sizeof(stFrameTail)) != (ssize_t)sizeof(stFrameTail)) {
        free(uchPayload);
        return -1;
    }

    if (ntohs(stFrameTail.unEtx) != ETX_CONST ||
        proto_crc8_xor(uchPayload, (size_t)iDataLength) != (unsigned char)stFrameTail.uchCrc) {
        free(uchPayload); 
        return -1;
    }

    /* === 응답 처리 ===
       - 기본 가정: 요청 CMD와 응답 CMD가 동일
    */
    if (pstUdsCtx->iReqBusy) {
        /* 응답 매칭 성공 → 타임아웃 해제 */
        if (pstUdsCtx->pstRespTimeout) evtimer_del(pstUdsCtx->pstRespTimeout);

        /* 명령별 응답 후크 (필요시 바꾸기) */
        switch (unCmd) {
            case CMD_REQ_ID: {
                if (iDataLength == (int32_t)sizeof(RES_ID)) {
                    const RES_ID* res = (const RES_ID*)uchPayload;
                    fprintf(stderr, "[SockFd=%d] RES_ID RES: result=%d\n", pstUdsCtx->iSockFd, res->chResult);
                    pstUdsCtx->uchDstId = res->chResult;
                } else {
                    fprintf(stderr, "[SockFd=%d] RES_ID RES len=%d\n", pstUdsCtx->iSockFd, iDataLength);
                    pstUdsCtx->uchDstId = 0;
                }
                break;
            }
            case CMD_KEEP_ALIVE: {
                if (iDataLength == (int32_t)sizeof(RES_KEEP_ALIVE)) {
                    const RES_KEEP_ALIVE* res = (const RES_KEEP_ALIVE*)uchPayload;
                    fprintf(stderr, "[SockFd=%d] KEEP_ALIVE RES: result=%d\n", pstUdsCtx->iSockFd, res->chResult);
                } else {
                    fprintf(stderr, "[SockFd=%d] KEEP_ALIVE RES len=%d\n", pstUdsCtx->iSockFd, iDataLength);
                }
                break;
            }
            case CMD_IBIT: {
                if (iDataLength == (int32_t)sizeof(RES_IBIT)) {
                    const RES_IBIT* res = (const RES_IBIT*)uchPayload;
                    fprintf(stderr, "[SockFd=%d] IBIT RES: tot=%d pos=%d\n",
                            pstUdsCtx->iSockFd, res->chBitTotResult, res->chPositionResult);
                } else {
                    fprintf(stderr, "[SockFd=%d] IBIT RES len=%d\n", pstUdsCtx->iSockFd, iDataLength);
                }
                break;
            }
            default:
                fprintf(stderr, "[SockFd=%d] RES cmd=%d len=%d\n", pstUdsCtx->iSockFd, unCmd, iDataLength);
                break;
        }

        pstUdsCtx->iReqBusy = 0;
        free(uchPayload);

        /* 큐에 대기 중이면 다음 요청 */
        if (pstUdsCtx->uchHasQueued && pstUdsCtx->pstSendKickTimer) {
            timeoutSet(pstUdsCtx->pstSendKickTimer, 0, 1000);
        }
        return 1;
    }

    /* 진행 중 요청이 없거나, 예상 외 프레임이면: (상대 push일 수 있음) */
    fprintf(stderr, "[SockFd=%d] Unexpected frame cmd=%d len=%d (no pending req or mismatched)\n",
            pstUdsCtx->iSockFd, unCmd, iDataLength);
    free(uchPayload);
    return 1;
}

/* === Libevent callbacks === */
static void udsReadCb(struct bufferevent *pstBufferEvent, void *pvData) {
    UDS_SERVER_CTX* pstUdsCtx = (UDS_SERVER_CTX*)pvData;
    struct evbuffer *pstEvBuffer = bufferevent_get_input(pstBufferEvent);
    for (;;) {
        int r = try_consume_one_frame(pstEvBuffer, pstUdsCtx);
        if (r == 0) 
            break;
        if (r < 0) { 
            udsCloseAndFree(pstUdsCtx);
            return; 
        }
    }
}

static void udsEventCb(struct bufferevent *bev, short events, void *pvData) {
    UDS_SERVER_CTX* pstUdsCtx = (UDS_SERVER_CTX*)pvData;
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

    UDS_SERVER_CTX* pstUdsCtx = (UDS_SERVER_CTX*)calloc(1, sizeof(*pstUdsCtx));
    if (!pstUdsCtx) { 
        bufferevent_free(pstBufferEvent);
        return; 
    }

    pstUdsCtx->pstBufferEvent = pstBufferEvent;
    pstUdsCtx->iSockFd = (int)iSockFd;
    /* 타이머 생성 (응답 타임아웃/송신 킥만 유지) */
    pstUdsCtx->pstRespTimeout   = evtimer_new(pstEventBase, respTimeoutCb, pstUdsCtx);
    pstUdsCtx->pstSendKickTimer = evtimer_new(pstEventBase, requestCb,   pstUdsCtx);

    /* 연결 리스트 등록 */
    pstUdsCtx->pstApp = pstAppCtx;
    pstUdsCtx->pstUdsNext  = pstAppCtx->pConnHead;
    pstAppCtx->pConnHead = pstUdsCtx;

    bufferevent_setcb(pstBufferEvent, udsReadCb, NULL, udsEventCb, pstUdsCtx);
    bufferevent_enable(pstBufferEvent, EV_READ|EV_WRITE);
    bufferevent_setwatermark(pstBufferEvent, EV_READ, 0, READ_HIGH_WM);

    printf("Accepted UDS client (iSockFd=%d)\n", pstUdsCtx->iSockFd);

    /* 연결 직후 초기 요청 예시: KEEP_ALIVE */
    REQ_ID stReqId = {0};
    requestQueue(pstUdsCtx, CMD_REQ_ID, &stReqId, sizeof(stReqId));
}

/* === Helper: 연결 자원 정리 === */
static void udsCloseAndFree(UDS_SERVER_CTX* pstUdsCtx)
{
    if (!pstUdsCtx)
        return;

    /* 리스트에서 제거 */
    if (pstUdsCtx->pstApp) {
        APP_CTX* app = pstUdsCtx->pstApp;
        UDS_SERVER_CTX** pp = &app->pConnHead;
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
    if (pstUdsCtx->pstBufferEvent)
        bufferevent_free(pstUdsCtx->pstBufferEvent);
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
