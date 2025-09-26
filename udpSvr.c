#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>

#include <event2/event.h>
#include <event2/util.h>

#include "frame.h"
#include "sockSession.h"

/* SIGINT 처리 */
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
    initEventContext(&stEventCtx, ROLE_SERVER, UDP_SERVER_ID);

    stEventCtx.pstSockCtx = (SOCK_CONTEXT*)calloc(1, sizeof(SOCK_CONTEXT));
    if(stEventCtx.pstSockCtx == NULL){
        fprintf(stderr, "SOCK_CONTEXT memory allocation fail.\n");
        return -1;
    }
    initSocketContext(stEventCtx.pstSockCtx, RESPONSE_ENABLED);
    stEventCtx.pstSockCtx->pstEventCtx = &stEventCtx;
       
    stEventCtx.pstEventBase   = event_base_new();
    if (!stEventCtx.pstEventBase) {
        fprintf(stderr, "Could not initialize libevent!\n");
        return 1;
    }

    /* 수신 포트를 명확히 하기위해 bind까지 진행 */
    stEventCtx.iSockFd = createTcpUdpServerSocket(UDP_SERVER_PORT, SOCK_TYPE_UDP);
    if (stEventCtx.iSockFd <= 0) {
        fprintf(stderr, "Error Create UDP socket!\n");
        event_base_free(stEventCtx.pstEventBase);
        return 1;
    }   

    stEventCtx.pstSockCtx->pstBufferEvent = bufferevent_socket_new(stEventCtx.pstEventBase, stEventCtx.iSockFd, BEV_OPT_CLOSE_ON_FREE);
    if (!stEventCtx.pstSockCtx->pstBufferEvent) { 
        fprintf(stderr, "bufferevent_socket_new failed\n");
        free(stEventCtx.pstSockCtx);
        close(stEventCtx.iSockFd);          // ★ 추가: fd 닫기
        event_base_free(stEventCtx.pstEventBase);
        return 1;
    }
    bufferevent_setcb(stEventCtx.pstSockCtx->pstBufferEvent, readCallback, NULL, eventCallback, stEventCtx.pstSockCtx);
    bufferevent_enable(stEventCtx.pstSockCtx->pstBufferEvent, EV_READ|EV_WRITE);
    bufferevent_setwatermark(stEventCtx.pstSockCtx->pstBufferEvent, EV_READ, sizeof(FRAME_HEADER), READ_HIGH_WM);  

    /* SIGINT 핸들러 */
    signal(SIGPIPE, SIG_IGN);
    stEventCtx.pstEvent = evsignal_new(stEventCtx.pstEventBase, SIGINT, signalCb, &stEventCtx);
    if (!stEventCtx.pstEvent || event_add(stEventCtx.pstEvent, NULL) < 0) {
        fprintf(stderr, "Could not create/add SIGINT event!\n");
        bufferevent_free(stEventCtx.pstSockCtx->pstBufferEvent);
        free(stEventCtx.pstSockCtx);
        event_base_free(stEventCtx.pstEventBase);
        return 1;
    }

    fprintf(stderr, "UDP Server Start Port %d\n", UDP_SERVER_PORT);

    event_base_dispatch(stEventCtx.pstEventBase);

    /* 정리 */
    if (stEventCtx.pstEvent)
        event_free(stEventCtx.pstEvent);    

    if (stEventCtx.pstSockCtx){
        if(stEventCtx.pstSockCtx->pstBufferEvent)
            bufferevent_free(stEventCtx.pstSockCtx->pstBufferEvent);
        free(stEventCtx.pstSockCtx);
        stEventCtx.pstSockCtx = NULL;
    }

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