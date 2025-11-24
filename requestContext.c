#include "requestContext.h"
#include <string.h>
#include <stdio.h>

static void timeoutCb(evutil_socket_t fd, short events, void* arg);


void reqCtxInit(REQUEST_CONTEXT* pstCtx)
{
    memset(pstCtx, 0, sizeof(*pstCtx));
    pstCtx->eState = REQ_IDLE;
}


bool reqCtxStart(REQUEST_CONTEXT* pstCtx,
                 unsigned short unCmd,
                 unsigned int unMask,
                 struct bufferevent* pstTcpBev,
                 struct event_base* pstEvBase,
                 int iTimeoutMs)
{
    if (pstCtx->bActive)
        return false;

    pstCtx->bActive           = true;
    pstCtx->eState            = REQ_WAITING;
    pstCtx->unCmdCode         = unCmd;
    pstCtx->unTargetMask      = unMask;
    pstCtx->pstTcpBev         = pstTcpBev;
    pstCtx->pstEvBase         = pstEvBase;
    pstCtx->iTimeoutMs        = iTimeoutMs;

    pstCtx->iTotalTargetCount = 0;
    pstCtx->iResponseCount    = 0;
    memset(pstCtx->abSuccess, 0, sizeof(pstCtx->abSuccess));

    /* Count how many targets exist */
    for (int i = 0; i < 32; i++)
    {
        if (unMask & (1U << i))
            pstCtx->iTotalTargetCount++;
    }

    /* Install timeout */
    struct timeval stTimeout;
    stTimeout.tv_sec  = iTimeoutMs / 1000;
    stTimeout.tv_usec = (iTimeoutMs % 1000) * 1000;

    pstCtx->pstTimeoutEvent = evtimer_new(pstEvBase, timeoutCb, pstCtx);
    evtimer_add(pstCtx->pstTimeoutEvent, &stTimeout);

    printf("[REQ] Start Request CMD=0x%04X Targets=%d Timeout=%dms\n",
           unCmd, pstCtx->iTotalTargetCount, iTimeoutMs);

    return true;
}


void reqCtxOnUdsResponse(REQUEST_CONTEXT* pstCtx,
                         unsigned char uchClientId,
                         bool bResult)
{
    if (!pstCtx->bActive)
        return;

    printf("[REQ] Response from UDS %d: %s\n",
           uchClientId, bResult ? "SUCCESS" : "FAIL");

    pstCtx->abSuccess[uchClientId] = bResult;
    pstCtx->iResponseCount++;

    if (pstCtx->iResponseCount >= pstCtx->iTotalTargetCount)
    {
        reqCtxEvaluateAndComplete(pstCtx);
    }
}


REQUEST_STATE reqCtxEvaluateAndComplete(REQUEST_CONTEXT* pstCtx)
{
    if (!pstCtx->bActive)
        return pstCtx->eState;

    evtimer_del(pstCtx->pstTimeoutEvent);

    // Evaluate response
    for (int i = 0; i < 32; i++)
    {
        if (pstCtx->unTargetMask & (1U << i))
        {
            if (!pstCtx->abSuccess[i])
            {
                pstCtx->eState = REQ_FAILED;
                break;
            }
        }
    }

    if (pstCtx->eState == REQ_WAITING)
        pstCtx->eState = REQ_SUCCESS;

    printf("[REQ] Completed State=%d\n", pstCtx->eState);

    pstCtx->bActive = false;
    return pstCtx->eState;
}


/* Timeout handler */
static void timeoutCb(evutil_socket_t fd, short events, void* arg)
{
    REQUEST_CONTEXT* pstCtx = (REQUEST_CONTEXT*)arg;

    if (!pstCtx->bActive)
        return;

    printf("[REQ] TIMEOUT (received %d / expected %d)\n",
           pstCtx->iResponseCount, pstCtx->iTotalTargetCount);

    pstCtx->eState = REQ_TIMEOUT;
    pstCtx->bActive = false;
}
