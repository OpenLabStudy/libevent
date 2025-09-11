// udsSvr.c
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#include <unistd.h>
#include <stddef.h>     /* offsetof */

#include "frame.h"
#include "sockSession.h"

/* === 내부에서 루프 중인 컨텍스트에 접근하기 위한 정지 훅 === */
#ifndef UDS_SVR_STANDALONE
static EVENT_CONTEXT* g_pRunningCtx = NULL;
#endif

/* === SIGINT: 루프 종료 및 소켓 파일 제거 === */
static void signalCb(evutil_socket_t sig, short ev, void* pvData)
{
    (void)sig;
    (void)ev;
    EVENT_CONTEXT* pstEventCtx = (EVENT_CONTEXT*)pvData;
    event_base_loopexit(pstEventCtx->pstEventBase, NULL);
}

#ifndef UDS_SVR_STANDALONE
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

int run(void)   /* ← 반환형을 int로 명확화 */
{
    EVENT_CONTEXT stEventCtx = (EVENT_CONTEXT){0};
    int iFd;

    unlink(UDS_COMMAND_PATH);

    signal(SIGPIPE, SIG_IGN);
    stEventCtx.pstEventBase = event_base_new();
    if (!stEventCtx.pstEventBase) {
        fprintf(stderr, "Could not initialize libevent!\n");
        return 1;
    }

    iFd = createUdsListenSocket(UDS_COMMAND_PATH);
    if (iFd == -1) {
        event_base_free(stEventCtx.pstEventBase);
        return 1;
    }

    stEventCtx.pstEventListener =
        evconnlistener_new(stEventCtx.pstEventBase,
                           listenerCb, &stEventCtx,
                           LEV_OPT_REUSEABLE | LEV_OPT_CLOSE_ON_FREE,
                           -1, /* ignored */
                           iFd);
    if (!stEventCtx.pstEventListener) {
        fprintf(stderr, "Could not create a UDS listener! (%s)\n", strerror(errno));
        evutil_closesocket(iFd);
        event_base_free(stEventCtx.pstEventBase);
        unlink(UDS_COMMAND_PATH);
        return 1;
    }

    /* SIGINT(CTRL+C) 처리 */
    stEventCtx.pstEvent = evsignal_new(stEventCtx.pstEventBase, SIGINT, signalCb, &stEventCtx);
    if (!stEventCtx.pstEvent || event_add(stEventCtx.pstEvent, NULL) < 0) {
        fprintf(stderr, "Could not create/add SIGINT event!\n");
        evconnlistener_free(stEventCtx.pstEventListener);
        event_base_free(stEventCtx.pstEventBase);
        return 1;
    }

    fprintf(stderr, "UDS Server Start\n");

    /* 러닝 컨텍스트 노출 → 테스트에서 udsSvr_stop()로 중단 */
#ifndef UDS_SVR_STANDALONE
    g_pRunningCtx = &stEventCtx;
#endif 
    event_base_dispatch(stEventCtx.pstEventBase);
#ifndef UDS_SVR_STANDALONE
    g_pRunningCtx = NULL;
#endif

    evconnlistener_free(stEventCtx.pstEventListener);
    event_free(stEventCtx.pstEvent);
    event_base_free(stEventCtx.pstEventBase);
    printf("done\n");
    return 0;
}

/* === main ===
 * 프로덕션 바이너리 빌드에서만 포함되도록 가드
 */
#ifdef UDS_SVR_STANDALONE
int main(int argc, char** argv)
{
    return run();
}
#endif
