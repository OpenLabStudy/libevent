#include "eventSession.h"
#include <string.h>

void baseContextInit(BASE_CONTEXT* pstCtx, uint16_t usMyId)
{
    if (!pstCtx) return;
    memset(pstCtx, 0, sizeof(BASE_CONTEXT));
    pstCtx->usMyId = usMyId;
}

void baseContextCleanup(BASE_CONTEXT* pstCtx)
{
    if (!pstCtx)
        return;

    if (pstCtx->pstSignalEvent) {
        event_free(pstCtx->pstSignalEvent);
        pstCtx->pstSignalEvent = NULL;
    }
    if (pstCtx->pstMainTimer) {
        event_free(pstCtx->pstMainTimer);
        pstCtx->pstMainTimer = NULL;
    }

    /* pstEventBase는 main()에서 event_base_free() */
    pstCtx->pstEventBase = NULL;
}
