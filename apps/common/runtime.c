#include "runtime.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

static void appSignalCb(evutil_socket_t sig, short events, void* arg)
{
    (void)sig;
    (void)events;

    APP_SIGNAL_HANDLE *h = (APP_SIGNAL_HANDLE*)arg;

    if (!h || h->bShuttingDown)
        return;

    h->bShuttingDown = 1;

    fprintf(stderr, "\n[%s] SIGINT -> shutdown\n",
            h->pchTag ? h->pchTag : "APP");

    /* 즉시 이벤트 루프 탈출 */
    if (h->pstEventEngine &&
        h->pstEventEngine->pstEventBase) {
        event_base_loopbreak(h->pstEventEngine->pstEventBase);
    }
}

APP_SIGNAL_HANDLE*
appSignalCreate(EVENT_ENGINE *pstEventEngine,
                const char *pchTag)
{
    if (!pstEventEngine || !pstEventEngine->pstEventBase)
        return NULL;

    APP_SIGNAL_HANDLE *h = calloc(1, sizeof(*h));
    if (!h)
        return NULL;

    h->pstEventEngine = pstEventEngine;
    h->pchTag         = pchTag;
    h->bShuttingDown  = 0;

    h->pstSigEvent = evsignal_new(
        pstEventEngine->pstEventBase,
        SIGINT,
        appSignalCb,
        h
    );

    if (!h->pstSigEvent) {
        free(h);
        return NULL;
    }

    event_add(h->pstSigEvent, NULL);
    return h;
}

void appSignalDestroy(APP_SIGNAL_HANDLE **ppstHandle)
{
    if (!ppstHandle || !*ppstHandle)
        return;

    APP_SIGNAL_HANDLE *h = *ppstHandle;

    if (h->pstSigEvent)
        event_free(h->pstSigEvent);  /* del 포함 */

    free(h);
    *ppstHandle = NULL;
}
