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

#include <event2/listener.h>
#include <event2/buffer.h>
#include <event2/util.h>

#include "frame.h"

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
static void signalCb(evutil_socket_t, short, void*);
static void tcpCloseAndFree(TCP_SERVER_CTX*);

/* === Libevent callbacks === */
static void tcpReadCb(struct bufferevent *pstBufferEvent, void *pvData) {
    TCP_SERVER_CTX* pstTcpCtx = (TCP_SERVER_CTX*)pvData;
    MSG_ID stMsgId;
    struct evbuffer *pstEvBuffer = bufferevent_get_input(pstBufferEvent);
    stMsgId.uchSrcId = 0x01;
    stMsgId.uchDstId = 0x00;    
    sleep(1);
    for (;;) {
        int r = responseFrame(pstEvBuffer, pstTcpCtx->pstBufferEvent, &stMsgId, 0x01);
        if(r == 1){
            break;
        }
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

    bufferevent_setcb(pstBufferEvent, tcpReadCb, NULL, tcpEventCb, pstTcpCtx);
    bufferevent_enable(pstBufferEvent, EV_READ|EV_WRITE);
    bufferevent_setwatermark(pstBufferEvent, EV_READ, sizeof(FRAME_HEADER), READ_HIGH_WM);

    printf("Accepted TCP client (iSockFd=%d)\n", pstTcpCtx->iSockFd);
}

/* === Helper: 연결 자원 정리 === */
static void tcpCloseAndFree(TCP_SERVER_CTX* pstTcpCtx)
{
    if (!pstTcpCtx)
        return;    
        
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
