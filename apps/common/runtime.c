#include "runtime.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

static void appSignalCb(int iFd, short nEvent, void* pvData)
{
    (void)iFd;
    (void)nEvent;

    APP_SIGNAL_HANDLE* pstAppSignalHandle = (APP_SIGNAL_HANDLE*)pvData;

    if (!pstAppSignalHandle || pstAppSignalHandle->iShuttingDown)
        return;

    pstAppSignalHandle->iShuttingDown = 1;

    fprintf(stderr, "\n[%s] SIGINT -> shutdown\n",
            pstAppSignalHandle->pchTag ? pstAppSignalHandle->pchTag : "APP");
    /* 즉시 이벤트 루프 탈출 */
    if (pstAppSignalHandle->pstEventEngine && pstAppSignalHandle->pstEventEngine->pstEventBase) {
        event_base_loopbreak(pstAppSignalHandle->pstEventEngine->pstEventBase);
    }
}

APP_SIGNAL_HANDLE* appSignalCreate(EVENT_ENGINE *pstEventEngine, const char *pchTag)
{
    if (!pstEventEngine || !pstEventEngine->pstEventBase)
        return NULL;

    APP_SIGNAL_HANDLE *pstAppSignalHandle = calloc(1, sizeof(APP_SIGNAL_HANDLE));
    if (!pstAppSignalHandle)
        return NULL;

    pstAppSignalHandle->pstEventEngine = pstEventEngine;
    pstAppSignalHandle->pchTag         = pchTag;
    pstAppSignalHandle->iShuttingDown  = 0;

    pstAppSignalHandle->pstSigEvent = evsignal_new(pstEventEngine->pstEventBase,
        SIGINT, appSignalCb, pstAppSignalHandle);
    if (!pstAppSignalHandle->pstSigEvent) {
        free(pstAppSignalHandle);
        return NULL;
    }
    event_add(pstAppSignalHandle->pstSigEvent, NULL);
    return pstAppSignalHandle;
}

void appSignalDestroy(APP_SIGNAL_HANDLE **ppstAppSignalHandle)
{
    if (!ppstAppSignalHandle || !*ppstAppSignalHandle)
        return;

    APP_SIGNAL_HANDLE* pstAppSignalHandle = *ppstAppSignalHandle;

    if (pstAppSignalHandle->pstSigEvent)
        event_free(pstAppSignalHandle->pstSigEvent);

    free(pstAppSignalHandle);
    *ppstAppSignalHandle = NULL;
}
