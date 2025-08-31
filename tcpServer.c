/*
 * Framed initiator UDS server (Libevent, Unix Domain Socket, no IBIT timer)
 * - 서버가 먼저 요청(REQ)을 보내고, 클라이언트가 응답(RES)을 회신하는 구조
 * - 전역 변수 없음: ServerCtx/tcp_ctx로 컨텍스트 관리
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

#define DEFAULT_PORT 9995
#define READ_HIGH_WM        (1u * 1024u * 1024u)   /* 1MB */
#define MAX_PAYLOAD         (4u * 1024u * 1024u)   /* 4MB */
#define MAX_RETRY           1

typedef struct app_ctx;
/* === Per-connection context === */
typedef struct tcp_ctx{
    struct bufferevent  *pstBufferEvent;

    /* 요청/응답 흐름 제어 */
    struct event        *pstSendKickTimer;   /* 큐에 있으면 즉시(or 지연) 전송 */

    int                 iSockFd;
    int                 iReqBusy;       /* 진행 중 요청이 있으면 1 */

    /* 간단 송신 큐(단일 슬롯). 필요 시 링버퍼로 확장 */
    unsigned char       uchHasQueued;
    unsigned short      unCmd;
    unsigned char       uchPayload[256];
    int                 iDataLength;

    /**/
    unsigned char       uchSrcId;
    unsigned char       uchDstId;

    char                achTcpIpInfo[INET6_ADDRSTRLEN];
    short               unTcpPort;
} TCP_SERVER_CTX;

typedef struct app_ctx{
    struct event_base       *pstEventBase;
    struct evconnlistener   *pstTcpEventListener;
    struct event            *pstEventSigint;
} APP_CTX;

/* === Forward declarations === */
static void tcpListenerCb(struct evconnlistener*, evutil_socket_t, struct sockaddr*, int, void*);
static void tcpReadCb(struct bufferevent*, void*);
static void tcpEventCb(struct bufferevent*, short, void*);

static void requestCb(evutil_socket_t, short, void*);
static void signalCb(evutil_socket_t, short, void*);

static void sendRequestNow(TCP_SERVER_CTX*);
static void requestQueue(TCP_SERVER_CTX*, unsigned short, const void*, int);
static void tcpCloseAndFree(TCP_SERVER_CTX*);

/* === 프레임 송신 === */
static void sendTcpFrame(struct bufferevent* pstBufferEvent, unsigned short unCmd,
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


/* === 요청 큐 적재 === */
static void requestQueue(TCP_SERVER_CTX* pstTcpCtx, unsigned short unCmd, const void* pvPayload, int iDataLength) 
{
    pstTcpCtx->unCmd = unCmd;
    pstTcpCtx->iDataLength = (iDataLength > (int32_t)sizeof(pstTcpCtx->uchPayload)) ? (int32_t)sizeof(pstTcpCtx->uchPayload) : iDataLength;
    if (iDataLength > 0 && pvPayload) 
        memcpy(pstTcpCtx->uchPayload, pvPayload, (size_t)pstTcpCtx->iDataLength);
    pstTcpCtx->uchHasQueued = 1;

    /* 즉시 전송 트리거 */
    if (pstTcpCtx->pstSendKickTimer) {
        timeoutSet(pstTcpCtx->pstSendKickTimer, 0, 1000);
    }
}

/* === 실제 요청 전송 === */
static void sendRequestNow(TCP_SERVER_CTX* pstTcpCtx) {
    if (!pstTcpCtx->uchHasQueued || pstTcpCtx->iReqBusy)
        return;

    MSG_ID stMsgId;
    stMsgId.uchSrcId = pstTcpCtx->uchSrcId;
    stMsgId.uchDstId = pstTcpCtx->uchDstId;
    pstTcpCtx->iReqBusy = 1;
    sendTcpFrame(pstTcpCtx->pstBufferEvent, pstTcpCtx->unCmd, &stMsgId, 0, pstTcpCtx->uchPayload, pstTcpCtx->iDataLength);
    pstTcpCtx->uchHasQueued = 0;
}

static void requestCb(evutil_socket_t fd, short ev, void* pvData) {
    (void)fd; (void)ev;
    TCP_SERVER_CTX* pstTcpCtx = pvData;
    sendRequestNow(pstTcpCtx);
}

/* === 프레임 하나 파싱 & 응답 처리 ===
 * return: 1 consumed, 0 need more, -1 fatal
 */
static int try_consume_one_frame(struct evbuffer* pstEvBuffer, TCP_SERVER_CTX* pstTcpCtx) {    
    int iLength = evbuffer_get_length(pstEvBuffer);
    fprintf(stderr,"### %s():%d Recv Data Length is %d\n", __func__, __LINE__, iLength);
    if (iLength < sizeof(FRAME_HEADER))
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
    if (!pstTcpCtx->iReqBusy) {
        /* 명령별 응답 후크 (필요시 바꾸기) */
        switch (unCmd) {
            case CMD_REQ_ID: {
                if (iDataLength == (int32_t)sizeof(RES_ID)) {
                    const RES_ID* res = (const RES_ID*)uchPayload;
                    fprintf(stderr, "[SockFd=%d] RES_ID RES: result=%d\n", pstTcpCtx->iSockFd, res->chResult);
                    pstTcpCtx->uchDstId = res->chResult;
                } else {
                    fprintf(stderr, "[SockFd=%d] RES_ID RES len=%d\n", pstTcpCtx->iSockFd, iDataLength);
                    pstTcpCtx->uchDstId = 0;
                }
                break;
            }
            case CMD_KEEP_ALIVE: {
                if (iDataLength == (int32_t)sizeof(RES_KEEP_ALIVE)) {
                    const RES_KEEP_ALIVE* res = (const RES_KEEP_ALIVE*)uchPayload;
                    fprintf(stderr, "[SockFd=%d] KEEP_ALIVE RES: result=%d\n", pstTcpCtx->iSockFd, res->chResult);
                } else {
                    fprintf(stderr, "[SockFd=%d] KEEP_ALIVE RES len=%d\n", pstTcpCtx->iSockFd, iDataLength);
                }
                break;
            }
            case CMD_IBIT: {
                if (iDataLength == (int32_t)sizeof(RES_IBIT)) {
                    const RES_IBIT* res = (const RES_IBIT*)uchPayload;
                    fprintf(stderr, "[SockFd=%d] IBIT RES: tot=%d pos=%d\n",
                            pstTcpCtx->iSockFd, res->chBitTotResult, res->chPositionResult);
                } else {
                    fprintf(stderr, "[SockFd=%d] IBIT RES len=%d\n", pstTcpCtx->iSockFd, iDataLength);
                }
                break;
            }
            default:
                fprintf(stderr, "[SockFd=%d] RES cmd=%d len=%d\n", pstTcpCtx->iSockFd, unCmd, iDataLength);
                break;
        }

        pstTcpCtx->iReqBusy = 0;
        free(uchPayload);

        /* 큐에 대기 중이면 다음 요청 */
        if (pstTcpCtx->uchHasQueued && pstTcpCtx->pstSendKickTimer) {
            timeoutSet(pstTcpCtx->pstSendKickTimer, 0, 1000);
        }
        return 1;
    }

    /* 진행 중 요청이 없거나, 예상 외 프레임이면: (상대 push일 수 있음) */
    fprintf(stderr, "[SockFd=%d] Unexpected frame cmd=%d len=%d status=%d(no pending req or mismatched)\n",
            pstTcpCtx->iSockFd, unCmd, iDataLength, pstTcpCtx->iReqBusy);
    free(uchPayload);
    return 1;
}

/* === Libevent callbacks === */
static void tcpReadCb(struct bufferevent *pstBufferEvent, void *pvData) {
    TCP_SERVER_CTX* pstTcpCtx = (TCP_SERVER_CTX*)pvData;
    struct evbuffer *pstEvBuffer = bufferevent_get_input(pstBufferEvent);
    for (;;) {
        int r = try_consume_one_frame(pstEvBuffer, pstTcpCtx);
        if (r == 0) 
            break;
        if (r < 0) { 
            tcpCloseAndFree(pstTcpCtx);
            return; 
        }
    }
}

static void tcpEventCb(struct bufferevent *bev, short nEvents, void *pvData) {
    TCP_SERVER_CTX* pstTcpCtx = (TCP_SERVER_CTX*)pvData;
    (void)bev;
    if (nEvents & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
        tcpCloseAndFree(pstTcpCtx);
    }
}

/* === SIGINT: 루프 종료 및 소켓 파일 제거 === */
static void signalCb(evutil_socket_t sig, short ev, void* pvData)
{
    (void)sig;
    (void)ev;
    APP_CTX* pstAppCtx = (APP_CTX*)pvData;
    event_base_loopexit(pstAppCtx->pstEventBase, NULL);
}

/* === Listener: accept → tcp_ctx 생성/설정 === */
static void tcpListenerCb(struct evconnlistener *listener, evutil_socket_t iSockFd,
                        struct sockaddr *pstSockAddr, int iSockLen, void *pvData)
{
    (void)listener;
    (void)pstSockAddr;
    (void)iSockLen;
    APP_CTX* pstAppCtx = (APP_CTX*)pvData;
    struct event_base *pstEventBase = pstAppCtx->pstEventBase;
    struct bufferevent *pstBufferEvent = bufferevent_socket_new(pstEventBase, iSockFd, BEV_OPT_CLOSE_ON_FREE);
    if (!pstBufferEvent) 
        return;

    TCP_SERVER_CTX* pstTcpCtx = (TCP_SERVER_CTX*)calloc(1, sizeof(TCP_SERVER_CTX));
    if (!pstTcpCtx) { 
        bufferevent_free(pstBufferEvent);
        return; 
    }

    struct sockaddr_in *sin = (struct sockaddr_in*)pstSockAddr;
    inet_ntop(pstSockAddr->sa_family, &(sin->sin_addr), pstTcpCtx->achTcpIpInfo, sizeof(pstTcpCtx->achTcpIpInfo));

    pstTcpCtx->pstBufferEvent = pstBufferEvent;
    pstTcpCtx->iSockFd = (int)iSockFd;
    /* 타이머 생성 (응답 타임아웃/송신 킥만 유지) */
    pstTcpCtx->pstSendKickTimer = evtimer_new(pstEventBase, requestCb,   pstTcpCtx);

    bufferevent_setcb(pstBufferEvent, tcpReadCb, NULL, tcpEventCb, pstTcpCtx);
    bufferevent_enable(pstBufferEvent, EV_READ|EV_WRITE);
    bufferevent_setwatermark(pstBufferEvent, EV_READ, 0, READ_HIGH_WM);

    printf("Accepted TCP client (iSockFd=%d)\n", pstTcpCtx->iSockFd);
}

/* === Helper: 연결 자원 정리 === */
static void tcpCloseAndFree(TCP_SERVER_CTX* pstTcpCtx)
{
    if (!pstTcpCtx)
        return;    

    if (pstTcpCtx->pstSendKickTimer){
        event_free(pstTcpCtx->pstSendKickTimer);
        pstTcpCtx->pstSendKickTimer = NULL;
    }
        
    if (pstTcpCtx->pstBufferEvent){
        bufferevent_free(pstTcpCtx->pstBufferEvent);
        pstTcpCtx->pstBufferEvent = NULL;
    }
    free(pstTcpCtx);
    pstTcpCtx = NULL;
}



/* === main === */
int main(int argc, char** argv)
{
    APP_CTX stAppCtx = (APP_CTX){0};

    signal(SIGPIPE, SIG_IGN);

    stAppCtx.pstEventBase = event_base_new();
    if (!stAppCtx.pstEventBase) {
        fprintf(stderr, "Could not initialize libevent!\n");
        return 1;
    }

    /* TCP 주소 준비 */
    struct sockaddr_in stSocketIn;
    stSocketIn.sin_family = AF_INET;
    stSocketIn.sin_port   = htons((unsigned short)DEFAULT_PORT);
    stSocketIn.sin_addr.s_addr = htonl(INADDR_ANY);


    stAppCtx.pstTcpEventListener =
        evconnlistener_new_bind(stAppCtx.pstEventBase, tcpListenerCb, &stAppCtx,
            LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_FREE, -1,
            (struct sockaddr*)&stSocketIn, sizeof(stSocketIn));
    if (!stAppCtx.pstTcpEventListener) {
        fprintf(stderr, "Could not create a TCP listener! (%s)\n", strerror(errno));
        event_base_free(stAppCtx.pstEventBase);
        return 1;
    }

    /* SIGINT(CTRL+C) 처리 */
    stAppCtx.pstEventSigint = evsignal_new(stAppCtx.pstEventBase, SIGINT, signalCb, &stAppCtx);
    if (!stAppCtx.pstEventSigint || event_add(stAppCtx.pstEventSigint, NULL) < 0) {
        fprintf(stderr, "Could not create/add SIGINT event!\n");
        evconnlistener_free(stAppCtx.pstTcpEventListener);
        event_base_free(stAppCtx.pstEventBase);
        return 1;
    }    
    fprintf(stderr,"TCP Server Start\n");
    event_base_dispatch(stAppCtx.pstEventBase);

    evconnlistener_free(stAppCtx.pstTcpEventListener);
    event_free(stAppCtx.pstEventSigint);
    event_base_free(stAppCtx.pstEventBase);
    printf("done\n");
    return 0;
}
