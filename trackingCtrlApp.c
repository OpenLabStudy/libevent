#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <event2/event.h>
#include <event2/listener.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/util.h>

#include "protocol.h"  /* 공통 프로토콜 정의만 포함 */

#define DEFAULT_PORT 9995
#define READ_HIGH_WM (1u * 1024u * 1024u)   /* 1MB */
#define MAX_PAYLOAD  (4u * 1024u * 1024u)   /* 4MB */

/* === Per-connection context === */
typedef struct {
    struct bufferevent *pstBufEvent;
    struct event       *pstBitEventTimer;     /* 주기 BIT 타이머 (EV_PERSIST) */
    char                achTcpIpInfo[INET6_ADDRSTRLEN];
    short               unTcpPort;

    /* BIT 스케줄 제어 */
    int                 iCmdState;          /* 명령 처리 중이면 1 */
    int                 iBitPending;   /* 바쁠 때 BIT 요청이 들어오면 1로 보류 */
} TCP_CTX;

static void listenerCb(struct evconnlistener*, evutil_socket_t, struct sockaddr*, int, void*);
static void tcpReadCb(struct bufferevent*, void*);
static void tcpWriteCb(struct bufferevent*, void*);
static void tcpEventCb(struct bufferevent*, short, void*);
static void signalCb(evutil_socket_t, short, void*);
static void bitTimerCb(evutil_socket_t, short, void*);
static void runBitNow(TCP_CTX* pstTcpCtx);
static void tcpCloseAndFree(TCP_CTX* pstTcpCtx);


static void sendTcpFrame(struct bufferevent* pstBufEvent, int16_t nCmd,
                       const MSG_ID* pstMsgId, char chSubmodule,
                       const void* pvPayload, int32_t iPayloadLen)
{
    FRAME_HEADER h;
    FRAME_TAIL   t;

    h.unStx       = htons(STX_CONST);
    h.iLength     = htonl(iPayloadLen);
    h.stMsgId     = *pstMsgId;            /* 1-byte fields */
    h.chSubModule = chSubmodule;
    h.nCmd        = htons(nCmd);

    t.chCrc       = proto_crc8_xor((const uint8_t*)pvPayload, (size_t)iPayloadLen);
    t.unEtx       = htons(ETX_CONST);

    bufferevent_write(pstBufEvent, &h, sizeof(h));
    if (iPayloadLen > 0 && pvPayload) 
        bufferevent_write(pstBufEvent, pvPayload, iPayloadLen);
    bufferevent_write(pstBufEvent, &t, sizeof(t));
}

/* Handlers */
static void handle_keepalive(TCP_CTX* pstTcpCtx, const MSG_ID* pstMsgId, const REQ_KEEP_ALIVE* req) {
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    (void)req;
    RES_KEEP_ALIVE res = { .chResult = 1 };
    sendTcpFrame(pstTcpCtx->pstBufEvent, CMD_KEEP_ALIVE, pstMsgId, 0, &res, (int32_t)sizeof(res));
}
static void handle_ibit(TCP_CTX* pstTcpCtx, const MSG_ID* pstMsgId, const REQ_IBIT* req) {
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    (void)req; /* 여기선 받은 값과 무관하게 정상 응답 */
    RES_IBIT res = { .chBitTotResult = 1, .chPositionResult = 0 };
    sendTcpFrame(pstTcpCtx->pstBufEvent, CMD_IBIT, pstMsgId, 0, &res, (int32_t)sizeof(res));
}

/* Try to consume exactly one frame; return 1 consumed, 0 need more, -1 fatal */
static int try_consume_one_frame(struct evbuffer* pstEventBuffer, TCP_CTX* pstTcpCtx) {
    if (evbuffer_get_length(pstEventBuffer) < sizeof(FRAME_HEADER))
        return 0;

    FRAME_HEADER h;
    if (evbuffer_copyout(pstEventBuffer, &h, sizeof(h)) != (ssize_t)sizeof(h))
        return 0;

    uint16_t stx  = ntohs(h.unStx);
    int32_t  plen = ntohl(h.iLength);
    int16_t  cmd  = ntohs(h.nCmd);
    MSG_ID   ids  = h.stMsgId;
    /* char sub = h.chSubModule; // 필요시 사용 */

    if (stx != STX_CONST) {
        fprintf(stderr,"[%s:%u] Bad STX\n", pstTcpCtx->achTcpIpInfo, pstTcpCtx->unTcpPort);
        return -1;
    }
    if (plen < 0 || plen > (int32_t)MAX_PAYLOAD) {
        fprintf(stderr,"[%s:%u] Bad length=%d\n", pstTcpCtx->achTcpIpInfo, pstTcpCtx->unTcpPort, plen);
        return -1;
    }

    size_t need = sizeof(FRAME_HEADER) + (size_t)plen + sizeof(FRAME_TAIL);
    if (evbuffer_get_length(pstEventBuffer) < need)
        return 0;

    /* pop header */
    evbuffer_drain(pstEventBuffer, sizeof(FRAME_HEADER));

    uint8_t* payload = NULL;
    if (plen > 0) {
        payload = (uint8_t*)malloc((size_t)plen);
        if (!payload) 
            return -1;
        if (evbuffer_remove(pstEventBuffer, payload, (size_t)plen) != (ssize_t)plen) {
            free(payload); 
            return -1;
        }
    }

    FRAME_TAIL t;
    if (evbuffer_remove(pstEventBuffer, &t, sizeof(t)) != (ssize_t)sizeof(t)) { 
        free(payload);
        return -1; 
    }

    if (ntohs(t.unEtx) != ETX_CONST) { 
        free(payload);
        return -1;
    }

    uint8_t exp = proto_crc8_xor(payload, (size_t)plen);
    if (exp != (uint8_t)t.chCrc) {
        free(payload);
        return -1; 
    }

    /* === 명령 처리 중 표시 === */
    pstTcpCtx->iCmdState = 1;

    /* Dispatch */
    switch (cmd) {        
        case CMD_KEEP_ALIVE: {
            if (plen == (int32_t)sizeof(REQ_KEEP_ALIVE)) {
                REQ_KEEP_ALIVE req; memcpy(&req, payload, sizeof(req));
                handle_keepalive(pstTcpCtx, &ids, &req);
            } else {
                /* 잘못된 길이: 빈 응답 */
                sendTcpFrame(pstTcpCtx->pstBufEvent, CMD_KEEP_ALIVE, &ids, 0, NULL, 0);
            }
            break;
        }
        case CMD_IBIT: {
            if (plen == (int32_t)sizeof(REQ_IBIT)) {
                REQ_IBIT req; memcpy(&req, payload, sizeof(req));
                handle_ibit(pstTcpCtx, &ids, &req);
            } else {
                sendTcpFrame(pstTcpCtx->pstBufEvent, CMD_IBIT, &ids, 0, NULL, 0);
            }
            break;
        }
        default:
            /* 알 수 없는 명령: 빈 payload 응답 */
            sendTcpFrame(pstTcpCtx->pstBufEvent, cmd, &ids, 0, NULL, 0);
            break;
    }

    /* === 명령 처리 종료 === */
    pstTcpCtx->iCmdState = 0;

    free(payload);

    /* 명령 처리 직후, 보류된 BIT가 있으면 즉시 실행 */
    if (pstTcpCtx->iBitPending) {
        pstTcpCtx->iBitPending = 0;
        runBitNow(pstTcpCtx);
    }

    return 1;
}


/* === Read: 완전한 프레임 단위로 파싱/디스패치 === */
static void tcpReadCb(struct bufferevent *pstBufEvent, void *pvData)
{
    TCP_CTX* pstTcpCtx = (TCP_CTX*)pvData;
    struct evbuffer *pstEventBuffer = bufferevent_get_input(pstBufEvent);    

    for (;;) {
        int r = try_consume_one_frame(pstEventBuffer, pstTcpCtx);
        if (r == 0){
            break;
        }else if (r < 0) { 
            tcpCloseAndFree(pstTcpCtx); 
            return; 
        }
    }
}

/* === Write: 현재 별도 동작 없음 === */
static void tcpWriteCb(struct bufferevent *pstBufEvent, void *pvData)
{
    (void)pstBufEvent;//컴파일에게 일부러 사용하지 않음을 알리기 위함
    (void)pvData;
}

/* === Event: EOF/ERROR 처리 === */
static void tcpEventCb(struct bufferevent *pstBufEvent, short nEvents, void *pvData)
{
    TCP_CTX* pstTcpCtx = (TCP_CTX*)pvData;
    (void)pstBufEvent;
    if (nEvents & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
        tcpCloseAndFree(pstTcpCtx);
    }
}

/* === SIGINT: 즉시 루프 종료 === */
static void signalCb(evutil_socket_t sig, short nEvents, void *pvData)
{
    (void)sig; 
    (void)nEvents;
    struct event_base *pstEventBase = (struct event_base*)pvData;
    event_base_loopexit(pstEventBase, NULL);
}

/* === BIT 타이머 콜백 ===
 * - 바쁘면 보류만, 아니면 즉시 IBIT 푸시 전송
 */
static void bitTimerCb(evutil_socket_t fd, short nEvents, void *pvData)
{
    (void)fd; 
    (void)nEvents;
    TCP_CTX* pstTcpCtx = (TCP_CTX*)pvData;

    if (pstTcpCtx->iCmdState) {
        pstTcpCtx->iBitPending = 1;
        return;
    }
    /* 한가하면: 주기 IBIT 푸시 */
    runBitNow(pstTcpCtx);
}

/* === 보류된 BIT를 즉시 실행 === */
static void runBitNow(TCP_CTX* pstTcpCtx)
{
    if (pstTcpCtx->iCmdState) { 
        pstTcpCtx->iBitPending = 1;
        return; 
    }

    /* 서버-initiated 푸시: ids는 임의(여기서는 1/1) */
    MSG_ID ids = { .chSrcId = 1, .chDstId = 1 };
    RES_IBIT res = { .chBitTotResult = 1, .chPositionResult = 0 };
    sendTcpFrame(pstTcpCtx->pstBufEvent, CMD_IBIT, &ids, 0, &res, (int32_t)sizeof(res));
}

/* === Listener: accept → ConnCtx 생성/설정 === */
static void listenerCb(struct evconnlistener *listener, evutil_socket_t fd,
                        struct sockaddr *sa, int socklen, void *pvData)
{
    struct event_base *pstEventBase = (struct event_base*)pvData;
    (void)listener;
    (void)socklen;

    struct bufferevent *pstBufEvent = bufferevent_socket_new(pstEventBase, fd, BEV_OPT_CLOSE_ON_FREE);
    if (!pstBufEvent) { 
        event_base_loopbreak(pstEventBase);
        return;
    }

    TCP_CTX* pstTcpCtx = (TCP_CTX*)calloc(1, sizeof(TCP_CTX));
    if (!pstTcpCtx) { 
        bufferevent_free(pstBufEvent);
        return;
    }
    pstTcpCtx->pstBufEvent = pstBufEvent;
    pstTcpCtx->iCmdState = 0;
    pstTcpCtx->iBitPending = 0;

    /* 피어 IP/포트 기록 */
    void *addr_ptr = NULL;
    uint16_t unTcpPort = 0;
    if (sa->sa_family == AF_INET) {
        struct sockaddr_in *sin = (struct sockaddr_in*)sa;
        addr_ptr = &(sin->sin_addr);
        unTcpPort = ntohs(sin->sin_port);
    } else if (sa->sa_family == AF_INET6) {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6*)sa;
        addr_ptr = &(sin6->sin6_addr);
        unTcpPort = ntohs(sin6->sin6_port);
    }
    if (addr_ptr) 
        inet_ntop(sa->sa_family, addr_ptr, pstTcpCtx->achTcpIpInfo, sizeof(pstTcpCtx->achTcpIpInfo));
    pstTcpCtx->unTcpPort = unTcpPort;
    
    /* 콜백/옵션 */
    bufferevent_setcb(pstBufEvent, tcpReadCb, tcpWriteCb, tcpEventCb, pstTcpCtx);
    bufferevent_enable(pstBufEvent, EV_READ|EV_WRITE);
    bufferevent_setwatermark(pstBufEvent, EV_READ, 0, READ_HIGH_WM);

    /* 주기 IBIT 타이머 (예: 60초) */
    struct timeval interval = {60, 0};
    pstTcpCtx->pstBitEventTimer = event_new(pstEventBase, -1, EV_PERSIST, bitTimerCb, pstTcpCtx);
    if (pstTcpCtx->pstBitEventTimer) 
        event_add(pstTcpCtx->pstBitEventTimer, &interval);

    printf("Accepted %s:%u\n", pstTcpCtx->achTcpIpInfo, pstTcpCtx->unTcpPort);
}

/* === Helper: 연결 자원 일괄 정리 === */
static void tcpCloseAndFree(TCP_CTX* pstTcpCtx)
{
    if (!pstTcpCtx) return;
    if (pstTcpCtx->pstBitEventTimer) { 
        event_free(pstTcpCtx->pstBitEventTimer);
        pstTcpCtx->pstBitEventTimer=NULL;
    }
    if (pstTcpCtx->pstBufEvent) {
        bufferevent_free(pstTcpCtx->pstBufEvent);
        pstTcpCtx->pstBufEvent=NULL;
    }
    free(pstTcpCtx);
}


int main(int argc, char** argv)
{
    uint16_t unTcpPort = (argc > 1) ? (uint16_t)atoi(argv[1]) : DEFAULT_PORT;
    if (unTcpPort == 0 || unTcpPort > 65535) { 
        fprintf(stderr,"Bad port\n");
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);

    struct event_base *pstEventBase = event_base_new();
    if (!pstEventBase) {
        fprintf(stderr, "Could not initialize libevent!\n");
        return 1;
    }

    struct sockaddr_in sin; memset(&sin,0,sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port   = htons(unTcpPort);
    sin.sin_addr.s_addr = htonl(INADDR_ANY);

    struct evconnlistener *listener =
        evconnlistener_new_bind(pstEventBase, listenerCb, pstEventBase,
            LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_FREE, -1,
            (struct sockaddr*)&sin, sizeof(sin));

    if (!listener) {
        fprintf(stderr, "Could not create a listener!\n"); 
        event_base_free(pstEventBase);
        return 1;
    }

    struct event *sigint = evsignal_new(pstEventBase, SIGINT, signalCb, pstEventBase);
    if (!sigint || event_add(sigint, NULL)<0) {
        fprintf(stderr, "Could not create/add SIGINT event!\n");
        evconnlistener_free(listener);
        event_base_free(pstEventBase);
        return 1;
    }

    printf("Framed ECHO+IBIT server listening on 0.0.0.0:%d\n", unTcpPort);
    event_base_dispatch(pstEventBase);

    evconnlistener_free(listener);
    event_free(sigint);
    event_base_free(pstEventBase);
    printf("done\n");
    return 0;
}