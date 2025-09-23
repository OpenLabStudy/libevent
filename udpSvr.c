#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#include <unistd.h>
#include <fcntl.h>

#include <event2/event.h>
#include <event2/util.h>

#include "frame.h"
#include "sockSession.h"

/* SIGINT 처리 */
static void signalCb(evutil_socket_t sig, short ev, void* pvData)
{
    (void)sig; (void)ev;
    EVENT_CONTEXT* pstEventCtx = (EVENT_CONTEXT*)pvData;
    event_base_loopexit(pstEventCtx->pstEventBase, NULL);
}

int run(void)
{
    EVENT_CONTEXT stEventCtx = (EVENT_CONTEXT){0};

    signal(SIGPIPE, SIG_IGN);

    stEventCtx.pstSockCtx     = NULL;
    stEventCtx.pstEventBase   = event_base_new();
    if (!stEventCtx.pstEventBase) {
        fprintf(stderr, "Could not initialize libevent!\n");
        return 1;
    }

    /* --- TCP -> UDP: 리슨/accept 대신 바로 UDP 소켓 생성 --- */
    stEventCtx.iListenFd = createUdsServerSocket("127.0.0.1", DEFAULT_PORT, SOCK_TYPE_UDP);
    if (stEventCtx.iListenFd < 0) {
        fprintf(stderr, "Error Create UDP socket!\n");
        event_base_free(stEventCtx.pstEventBase);
        return 1;
    }




    /* READ 이벤트 달기 (UDP는 EV_READ 하나면 충분) */
    stEventCtx.pstAcceptEvent = event_new(stEventCtx.pstEventBase,
                                          stEventCtx.iListenFd,
                                          EV_READ | EV_PERSIST,
                                          udpReadCb, &stEventCtx);
    if (!stEventCtx.pstAcceptEvent || event_add(stEventCtx.pstAcceptEvent, NULL) < 0) {
        fprintf(stderr, "Could not create/add UDP read event!\n");
        evutil_closesocket(stEventCtx.iListenFd);
        event_base_free(stEventCtx.pstEventBase);
        return 1;
    }

    stEventCtx.uchMyId       = UDS1_SERVER_ID;   /* 필요 시 식별자 재사용 */
    stEventCtx.iClientCount  = 0;                /* UDP는 연결 개념 없지만 통계용으로 사용해도 됨 */

    /* SIGINT 핸들러 */
    stEventCtx.pstEvent = evsignal_new(stEventCtx.pstEventBase, SIGINT, signalCb, &stEventCtx);
    if (!stEventCtx.pstEvent || event_add(stEventCtx.pstEvent, NULL) < 0) {
        fprintf(stderr, "Could not create/add SIGINT event!\n");
        event_free(stEventCtx.pstAcceptEvent);
        evutil_closesocket(stEventCtx.iListenFd);
        event_base_free(stEventCtx.pstEventBase);
        return 1;
    }

    fprintf(stderr, "UDP Server Start 127.0.0.1:%d\n", DEFAULT_PORT);

    event_base_dispatch(stEventCtx.pstEventBase);

    /* 정리 */
    if (stEventCtx.pstAcceptEvent) event_free(stEventCtx.pstAcceptEvent);
    if (stEventCtx.pstEvent)       event_free(stEventCtx.pstEvent);
    if (stEventCtx.iListenFd >= 0) evutil_closesocket(stEventCtx.iListenFd);
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