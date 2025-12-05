/* tcpUdsBridge.c */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#include <event2/event.h>
#include <event2/bufferevent.h>

#include "eventSession.h"
#include "dispatcher.h"
#include "netTcp.h"
#include "netUds.h"

#define TCP_PORT   5000
#define UDS_PATH   "/tmp/bridge_uds.sock"

/* 전방 선언 */
static void bridgeReadCb(struct bufferevent* bev, void* ctx);
static void bridgeEventCb(struct bufferevent* bev, short events, void* ctx);
static void signalCb(evutil_socket_t sig, short events, void* ctx);

int main(int argc, char** argv)
{
    (void)argc; (void)argv;

    BASE_CONTEXT       stBase;
    SERVER_CONTEXT     stTcp;
    SERVER_CONTEXT     stUds;
    DISPATCHER_CONTEXT stDisp;

    /* 1. Base 초기화 */
    baseContextInit(&stBase, 1);
    stBase.pstEventBase = event_base_new();
    if (!stBase.pstEventBase) {
        fprintf(stderr, "event_base_new() failed\n");
        return EXIT_FAILURE;
    }

    /* 2. 서버 초기화 */
    serverContextInit(&stTcp, &stBase, ROLE_TCP_SERVER);
    serverContextInit(&stUds, &stBase, ROLE_UDS_SERVER);

    stTcp.iListenFd = netTcpCreateServer(TCP_PORT);
    if (stTcp.iListenFd < 0) {
        fprintf(stderr, "TCP server create failed\n");
        return EXIT_FAILURE;
    }

    stUds.iListenFd = netUdsCreateServer(UDS_1_PATH);
    if (stUds.iListenFd < 0) {
        fprintf(stderr, "UDS server create failed\n");
        return EXIT_FAILURE;
    }

    /* 3. Dispatcher 초기화 & Base.userCtx 연결 */
    dispatcherInit(&stDisp, &stBase, &stTcp, &stUds);
    stBase.pvUserCtx = &stDisp;

    /* 4. App Handler 등록 */
    stBase.stHandler.pfReadCb  = bridgeReadCb;
    stBase.stHandler.pfWriteCb = NULL;          /* 필요시 사용할 수 있음 */
    stBase.stHandler.pfEventCb = bridgeEventCb;

    /* 5. Accept 이벤트 설정 */
    setupServerAcceptEvent(&stTcp);
    setupServerAcceptEvent(&stUds);

    /* 6. SIGINT 처리 */
    signal(SIGPIPE, SIG_IGN);
    stBase.pstSignalEvent = evsignal_new(stBase.pstEventBase,
                                         SIGINT,
                                         signalCb,
                                         &stBase);
    event_add(stBase.pstSignalEvent, NULL);

    fprintf(stderr, "[Bridge] TCP:%d, UDS:%s\n", TCP_PORT, UDS_1_PATH);

    /* 7. 이벤트 루프 */
    event_base_dispatch(stBase.pstEventBase);

    /* 8. 종료 처리 */
    dispatcherCleanup(&stDisp);

    /* 클라이언트 세션 해제, event_free, close 등은
       네가 기존에 만든 closeAndFree / cleanup 루틴 재사용 */

    return EXIT_SUCCESS;
}


/* ====== Read Callback (TCP/UDS 공용) ====== */

static void bridgeReadCb(struct bufferevent* bev, void* ctx)
{
    SOCK_CONTEXT* pstSock = (SOCK_CONTEXT*)ctx;
    if (!pstSock || !pstSock->pstServerCtx)
        return;

    unsigned char buf[2048];
    int len = bufferevent_read(bev, buf, sizeof(buf));
    if (len <= 0)
        return;

    DISPATCHER_CONTEXT* pstDisp =
        (DISPATCHER_CONTEXT*)pstSock->pstBaseCtx->pvUserCtx;

    if (pstSock->pstServerCtx->eRole == ROLE_TCP_SERVER) {
        fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
        dispatcherOnTcpRequest(pstDisp, pstSock, buf, len);
    } else if (pstSock->pstServerCtx->eRole == ROLE_UDS_SERVER) {
        fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
        dispatcherOnUdsResponse(pstDisp, pstSock, buf, len);
    }
}


/* ====== Event Callback (TCP/UDS 공용) ====== */

static void bridgeEventCb(struct bufferevent* bev, short events, void* ctx)
{
    (void)bev;
    SOCK_CONTEXT* pstSock = (SOCK_CONTEXT*)ctx;

    if (events & BEV_EVENT_EOF) {
        fprintf(stderr, "[Bridge] Connection closed\n");
    } else if (events & BEV_EVENT_ERROR) {
        fprintf(stderr, "[Bridge] Connection error\n");
    }

    /* 여기서 closeAndFree(pstSock) 호출 여부는
       네 구조에 맞게 조절하면 됨
    */
}


/* ====== Signal Handler ====== */

static void signalCb(evutil_socket_t sig, short events, void* ctx)
{
    (void)sig; (void)events;
    BASE_CONTEXT* pstBase = (BASE_CONTEXT*)ctx;
    if (pstBase && pstBase->pstEventBase) {
        event_base_loopexit(pstBase->pstEventBase, NULL);
    }
}
