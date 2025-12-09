/**
 * @file tcpSvr.c
 * @brief Dispatcher + EVENT_SOURCE + netTcp 기반 TCP Echo Server (No global variables)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>

#include "dispatcher.h"
#include "tcpSvr.h"

#define SERVER_PORT 5000

/* ========================================================================== */
/* Application-Level Read Processing                                          */
/* ========================================================================== */
static void readCallback(struct bufferevent* pstBufferEvent, void* pvData)
{
    unsigned char auchRecvBuffer[2048];
    memset(auchRecvBuffer, 0x0, sizeof(auchRecvBuffer));
    struct evbuffer* pstInputBuffer = bufferevent_get_input(pstBufferEvent);
    while (1) {
        size_t ulRecvLen = evbuffer_get_length(pstInputBuffer);
        fprintf(stderr,"### %s():%d %zu###\n",__func__,__LINE__, ulRecvLen);        
        if (ulRecvLen <= 0)
            break;
            
        int iCopyLen   = evbuffer_copyout(pstInputBuffer, auchRecvBuffer, ulRecvLen);
        evbuffer_drain(pstInputBuffer, iCopyLen);
        fprintf(stderr,"### %s():%d read %s###\n",__func__,__LINE__,auchRecvBuffer);
    }
}

/* ========================================================================== */
/* Application-Level Event Callback                                           */
/* ========================================================================== */
static void eventCallback(struct bufferevent* pstBufferEvent,
    short nEvents, void* pvData)
{
    EVENT_SOURCE* pstEventSrc = (EVENT_SOURCE *)pvData;
    (void)pstBufferEvent;

    if (nEvents & BEV_EVENT_EOF) {
        fprintf(stderr, "[TCP-Server] Client disconnected\n");
    } else if (nEvents & BEV_EVENT_ERROR) {
        fprintf(stderr, "[TCP-Server] Client socket error\n");
    }

    /* 실제 close/free 는 eventSession 의 eventCallbackWrapper 에서 수행 */
    eventSourceDestroy(pstEventSrc);
}

/* ============================================================
* Accept 콜백
* ============================================================ */
static void acceptCb(evutil_socket_t iListenFd, short nKindOfEvent, void* pvArg)
{
    (void)nKindOfEvent;
    BASE_CONTEXT* pstBaseCtx = (BASE_CONTEXT*)pvArg;
    DISPATCHER* pstDispatcher  = (DISPATCHER*)pstBaseCtx->pvUserCtx;

    struct sockaddr_in stClientAddr;
    socklen_t uiClientLen = sizeof(stClientAddr);

    int iClientSock = accept(iListenFd, (struct sockaddr*)&stClientAddr, &uiClientLen);
    if (iClientSock < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            perror("[TCP-SVR] accept");
        return;
    }

    printf("[TCP-SVR] New client FD=%d\n", iClientSock);

    netSetNonblock(iClientSock);

    eventSourceCreateWithBev(
        pstDispatcher,
        iClientSock,
        SRC_TYPE_TCP_CLIENT,
        SRC_ROLE_REQUESTER,
        readCallback,
        eventCallback
    );
}

/* ============================================================
* SIGINT 콜백
* ============================================================ */
static void signalCb(evutil_socket_t sig, short events, void* pvArg)
{
    BASE_CONTEXT* pstBaseCtx = (BASE_CONTEXT*)pvArg;

    printf("\n[TCP-SVR] SIGINT → shutdown\n");
    event_base_loopexit(pstBaseCtx->pstEventBase, NULL);
}

/* ============================================================
* main()
* ============================================================ */
int main()
{
    BASE_CONTEXT stBaseCtx;
    DISPATCHER   stDispatcher;

    /* BASE_CONTEXT 초기화 */
    baseContextInit(&stBaseCtx, 0x77);
    stBaseCtx.pstEventBase = event_base_new();
    if (!stBaseCtx.pstEventBase) {
        printf("event_base_new failed\n");
        return -1;
    }

    /* Dispatcher 초기화 */
    dispatcherInit(&stDispatcher, &stBaseCtx);
    stBaseCtx.pvUserCtx = &stDispatcher;

    /* TCP Listen 소켓 생성 */
    int iListenFd = netTcpCreateServer(SERVER_PORT);
    if (iListenFd < 0) {
        perror("netTcpCreateServer");
        return -1;
    }

    printf("[TCP-SVR] Listening on port %d\n", SERVER_PORT);

    /* Accept 이벤트 등록 */
    struct event* stEventAccept = event_new(
            stBaseCtx.pstEventBase, iListenFd, 
            EV_READ | EV_PERSIST, acceptCb, &stBaseCtx);
    event_add(stEventAccept, NULL);

    /* SIGINT 처리 등록 */
    stBaseCtx.pstSignalEvent = evsignal_new(
        stBaseCtx.pstEventBase, SIGINT,
        signalCb, &stBaseCtx);
    event_add(stBaseCtx.pstSignalEvent, NULL);

    /* 이벤트 루프 시작 */
    event_base_dispatch(stBaseCtx.pstEventBase);

    /* 종료 처리 */
    dispatcherCleanup(&stDispatcher);
    baseContextCleanup(&stBaseCtx);

    event_free(stEventAccept);
    netClose(iListenFd);
    event_base_free(stBaseCtx.pstEventBase);

    printf("[TCP-SVR] Terminated.\n");
    return 0;
}
