#include "netCore.h"
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>

static void signalCallback(evutil_socket_t sig, short ev, void* ctx) {
    (void)sig;
    (void)ev;
    NET_CORE* pstNetCore = (NET_CORE*)ctx;
    if (pstNetCore && pstNetCore->pstEventBase) 
        event_base_loopexit(pstNetCore->pstEventBase, NULL);
}

NET_CORE* netCoreCreate(void) {
    NET_CORE* pstNetCore = (NET_CORE*)calloc(1, sizeof(NET_CORE));
    if (!pstNetCore)
        return NULL;

    pstNetCore->pstEventBase = event_base_new();
    if (!pstNetCore->pstEventBase) { 
        free(pstNetCore); 
        return NULL; 
    }

    signal(SIGPIPE, SIG_IGN);
    pstNetCore->pstSigIntEvent = evsignal_new(pstNetCore->pstEventBase, SIGINT, signalCallback, pstNetCore);
    if (!pstNetCore->pstSigIntEvent || event_add(pstNetCore->pstSigIntEvent, NULL) < 0) {
        if (pstNetCore->pstSigIntEvent) 
            event_free(pstNetCore->pstSigIntEvent);
        event_base_free(pstNetCore->pstEventBase);
        free(pstNetCore);
        return NULL;
    }
    return pstNetCore;
}

void netCoreRun(NET_CORE* pstNetCore) {
    if (!pstNetCore || !pstNetCore->pstEventBase)
        return;
    event_base_dispatch(pstNetCore->pstEventBase);
}

void netCoreDestroy(NET_CORE* pstNetCore) {
    if (!pstNetCore) 
        return;

    if (pstNetCore->pstSigIntEvent) 
        event_free(pstNetCore->pstSigIntEvent);

    if (pstNetCore->pstEventBase) 
        event_base_free(pstNetCore->pstEventBase);

    free(pstNetCore);
}
