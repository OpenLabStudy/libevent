#include "netModule/protocols/uds.h"
#include <event2/event.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

static void signalCallBack(evutil_socket_t sig, short ev, void *pvData)
{
    (void)sig; (void)ev;
    UDS_SERVER_CTX *pstUdsCtx = (UDS_SERVER_CTX *)pvData;
    printf("\n[UDS SERVER] SIGINT caught. Exiting...\n");
    event_base_loopbreak(pstUdsCtx->stNetBase.stCoreCtx.pstEventBase);
}

int main(int argc, char *argv[])
{
    const char *pchPath = (argc > 1) ? argv[1] : "/tmp/uds_server.sock";
    struct event_base *pstEventBase = event_base_new();

    UDS_SERVER_CTX stUdsCtx;
    udsSvrInit(&stUdsCtx, pstEventBase, 30, UDS_SERVER);

    if (udsServerStart(&stUdsCtx, pchPath) < 0) {
        fprintf(stderr, "Failed to start UDS server\n");
        return 1;
    }

    /* SIGINT(CTRL+C) 처리 */
    signal(SIGPIPE, SIG_IGN);    
    stUdsCtx.stNetBase.stCoreCtx.pstSignalEvent = evsignal_new(
        stUdsCtx.stNetBase.stCoreCtx.pstEventBase, 
        SIGINT, 
        signalCallBack, 
        &stUdsCtx);
    if (!stUdsCtx.stNetBase.stCoreCtx.pstSignalEvent || 
            event_add(stUdsCtx.stNetBase.stCoreCtx.pstSignalEvent, NULL) < 0) {
        fprintf(stderr, "Could not create/add SIGINT event!\n");
        udsSvrStop(&stUdsCtx);
        return 1;
    }   

    printf("[UDS SERVER] Listening on %s\n", pchPath);
    event_base_dispatch(pstEventBase);

    event_free(stUdsCtx.stNetBase.stCoreCtx.pstSignalEvent);
    udsSvrStop(&stUdsCtx);
    return 0;
}
