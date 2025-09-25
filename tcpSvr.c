#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#include "frame.h"
#include "sockSession.h"

#ifdef GOOGLE_TEST
static EVENT_CONTEXT* g_pRunningCtx = NULL;
/* 외부(테스트)에서 호출: 이벤트 루프 중단 */
void tcpSvrStop(void)
{
    if (g_pRunningCtx && g_pRunningCtx->pstEventBase) {
        event_base_loopbreak(g_pRunningCtx->pstEventBase);
    }
}

/* 서버가 돌고 있는지 확인용 */
int tcpSvrIsRunning(void)
{
    return g_pRunningCtx != NULL;
}
#endif

/* === SIGINT: 루프 종료 및 소켓 파일 제거 === */
static void signalCb(evutil_socket_t sig, short ev, void* pvData)
{
    (void)sig;
    (void)ev;
    EVENT_CONTEXT* pstEventCtx = (EVENT_CONTEXT*)pvData;
    event_base_loopexit(pstEventCtx->pstEventBase, NULL);
}

int run(void)
{
    EVENT_CONTEXT stEventCtx;    
    initEventContext(&stEventCtx, ROLE_SERVER, TCP_TRACKING_CTRL_ID);

    stEventCtx.pstSockCtx = (SOCK_CONTEXT*)calloc(1, sizeof(SOCK_CONTEXT));
    if(stEventCtx.pstSockCtx == NULL){
        fprintf(stderr, "SOCK_CONTEXT memory allocation fail.\n");
        return -1;
    }
    initSocketContext(stEventCtx.pstSockCtx, TCP_SERVER_ADDR, TCP_SERVER_PORT, RESPONSE_ENABLED);
    
    stEventCtx.pstEventBase = event_base_new();
    if (!stEventCtx.pstEventBase) {
        fprintf(stderr, "Could not initialize libevent!\n");
        return 1;
    }

    stEventCtx.iSockFd = createTcpUdpServerSocket(stEventCtx.pstSockCtx, SOCK_TYPE_TCP);
    if(stEventCtx.iSockFd == -1){
        fprintf(stderr,"Error Create Listen socket!\n");
        free(stEventCtx.pstSockCtx);
        event_base_free(stEventCtx.pstEventBase);
        return 1;
    }

    stEventCtx.pstAcceptEvent = event_new(stEventCtx.pstEventBase,
                                          stEventCtx.iSockFd,
                                          EV_READ | EV_PERSIST,
                                          acceptCb, &stEventCtx);
    if (!stEventCtx.pstAcceptEvent || event_add(stEventCtx.pstAcceptEvent, NULL) < 0) {
        fprintf(stderr, "Could not create/add accept event!\n");
        free(stEventCtx.pstSockCtx);
        evutil_closesocket(stEventCtx.iSockFd);
        event_base_free(stEventCtx.pstEventBase);
        return 1;
    }
    
    /* SIGINT(CTRL+C) 처리 */
    signal(SIGPIPE, SIG_IGN);
    stEventCtx.pstEvent = evsignal_new(stEventCtx.pstEventBase, SIGINT, signalCb, &stEventCtx);
    if (!stEventCtx.pstEvent || event_add(stEventCtx.pstEvent, NULL) < 0) {
        fprintf(stderr, "Could not create/add SIGINT event!\n");
        event_free(stEventCtx.pstAcceptEvent);
        evutil_closesocket(stEventCtx.iSockFd);
        event_base_free(stEventCtx.pstEventBase);
        return 1;
    }    
    fprintf(stderr,"TCP Server Start\n");
#ifdef GOOGLE_TEST
    g_pRunningCtx = &stEventCtx;
#endif 
    event_base_dispatch(stEventCtx.pstEventBase);
#ifdef GOOGLE_TEST
    g_pRunningCtx = NULL;
#endif
    /* 정리 */
    if (stEventCtx.pstAcceptEvent)
        event_free(stEventCtx.pstAcceptEvent);
    if (stEventCtx.pstEvent)
        event_free(stEventCtx.pstEvent);
    if (stEventCtx.iSockFd >= 0)
        evutil_closesocket(stEventCtx.iSockFd);
    event_base_free(stEventCtx.pstEventBase);
    printf("done\n");
    return 0;
}

/* === main === */
#ifndef GOOGLE_TEST
int main(int argc, char** argv)
{
    return run();
}
#endif