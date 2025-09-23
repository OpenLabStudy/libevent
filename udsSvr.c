// udsSvr.c
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#include <unistd.h>

#include "frame.h"
#include "sockSession.h"

/* === 내부에서 루프 중인 컨텍스트에 접근하기 위한 정지 훅 === */
#ifdef GOOGLE_TEST
static EVENT_CONTEXT* g_pRunningCtx = NULL;
/* 외부(테스트)에서 호출: 이벤트 루프 중단 */
void udsSvrStop(void)
{
    if (g_pRunningCtx && g_pRunningCtx->pstEventBase) {
        event_base_loopbreak(g_pRunningCtx->pstEventBase);
    }
}

/* (선택) 서버가 돌고 있는지 확인용 */
int udsSvrIsRunning(void)
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

int run(void)   /* ← 반환형을 int로 명확화 */
{
    EVENT_CONTEXT stEventCtx = (EVENT_CONTEXT){0};
    unlink(UDS1_PATH);
    signal(SIGPIPE, SIG_IGN);
    stEventCtx.pstSockCtx = NULL;
    stEventCtx.pstEventBase = event_base_new();
    if (!stEventCtx.pstEventBase) {
        fprintf(stderr, "Could not initialize libevent!\n");
        return 1;
    }

    stEventCtx.iListenFd = createUdsListenSocket(UDS1_PATH);
    if (stEventCtx.iListenFd == -1) {
        event_base_free(stEventCtx.pstEventBase);
        return 1;
    }

    evutil_make_socket_nonblocking(stEventCtx.iListenFd);
    evutil_make_socket_closeonexec(stEventCtx.iListenFd);

    stEventCtx.pstAcceptEvent = event_new(stEventCtx.pstEventBase,
                                          stEventCtx.iListenFd,
                                          EV_READ | EV_PERSIST,
                                          acceptCb, &stEventCtx);
    if (!stEventCtx.pstAcceptEvent || event_add(stEventCtx.pstAcceptEvent, NULL) < 0) {
        fprintf(stderr, "Could not create/add accept event!\n");
        evutil_closesocket(stEventCtx.iListenFd);
        event_base_free(stEventCtx.pstEventBase);
        return 1;
    }

    stEventCtx.uchMyId = UDS1_SERVER_ID;
    stEventCtx.iClientCount = 0;
    /* SIGINT(CTRL+C) 처리 */
    stEventCtx.pstEvent = evsignal_new(stEventCtx.pstEventBase, SIGINT, signalCb, &stEventCtx);
    if (!stEventCtx.pstEvent || event_add(stEventCtx.pstEvent, NULL) < 0) {
        fprintf(stderr, "Could not create/add SIGINT event!\n");
        event_free(stEventCtx.pstAcceptEvent);
        evutil_closesocket(stEventCtx.iListenFd);
        event_base_free(stEventCtx.pstEventBase);
        return 1;
    }
    fprintf(stderr, "UDS Server Start\n");
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
    if (stEventCtx.iListenFd >= 0)
        evutil_closesocket(stEventCtx.iListenFd);
    event_base_free(stEventCtx.pstEventBase);
    printf("done\n");
    return 0;
}

/* === main ===
 * 프로덕션 바이너리 빌드에서만 포함되도록 가드
 */
#ifndef GOOGLE_TEST
int main(int argc, char** argv)
{
    return run();
}
#endif
