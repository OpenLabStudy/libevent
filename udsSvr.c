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

/* === SIGINT: 루프 종료 및 소켓 파일 제거 === */
static void signalCb(evutil_socket_t sig, short ev, void* pvData)
{
    (void)sig;
    (void)ev;
    EVENT_CONTEXT* pstEventCtx = (EVENT_CONTEXT*)pvData;
    event_base_loopexit(pstEventCtx->pstEventBase, NULL);
}

/* === main === */
int main(int argc, char** argv)
{
    EVENT_CONTEXT stEventCtx = (EVENT_CONTEXT){0};

    unlink(UDS_COMMAND_PATH);

    signal(SIGPIPE, SIG_IGN);
    stEventCtx.pstEventBase = event_base_new();
    if (!stEventCtx.pstEventBase) {
        fprintf(stderr, "Could not initialize libevent!\n");
        return 1;
    }

    /* UDS 주소 준비 */
    struct sockaddr_un stSocketUn;    
    memset(&stSocketUn, 0, sizeof(stSocketUn));
    stSocketUn.sun_family = AF_UNIX;
    strcpy(stSocketUn.sun_path, UDS_COMMAND_PATH);
    size_t ulSize = strlen(UDS_COMMAND_PATH);
    socklen_t uiSocketLength = (socklen_t)(offsetof(struct sockaddr_un, sun_path)+ulSize+1);

    stEventCtx.pstEventListener =
        evconnlistener_new_bind(stEventCtx.pstEventBase, listenerCb, &stEventCtx,
            LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_FREE, -1,
            (struct sockaddr*)&stSocketUn, uiSocketLength);
    if (!stEventCtx.pstEventListener) {
        fprintf(stderr, "Could not create a TCP listener! (%s)\n", strerror(errno));
        event_base_free(stEventCtx.pstEventBase);
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
    fprintf(stderr,"TCP Server Start\n");
    event_base_dispatch(stEventCtx.pstEventBase);

    evconnlistener_free(stEventCtx.pstEventListener);
    event_free(stEventCtx.pstEvent);
    event_base_free(stEventCtx.pstEventBase);
    printf("done\n");
    return 0;
}
