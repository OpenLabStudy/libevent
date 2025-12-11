#include "dispatcher.h"
#include "eventSource.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

/* timeout */
#define DISPATCHER_REQ_TIMEOUT_SEC      0
#define DISPATCHER_REQ_TIMEOUT_MSEC     (300*1000)

/* forward declarations */
static void dispatcherFlushCb(evutil_socket_t fd, short what, void* arg);
static void dispatcherReqTimeoutCb(evutil_socket_t fd, short what, void* arg);
static REQUEST_CONTEXT* dispatcherFindReq(
    DISPATCHER* d, uint32_t reqId, REQUEST_CONTEXT** ppPrev);

/* ============================================================
 * Dispatcher 초기화
 * ============================================================ */
void eventEngineInit(EVENT_ENGINE* pstEventEngine, EVENT_BASE* pstEventBase)
{
    memset(pstEventEngine, 0, sizeof(EVENT_ENGINE));
    pstEventEngine->pstEventBase = pstEventBase;

    /* Request ID 시퀀스 초기화 */
    pstEventEngine->uiRequestSeq = 1;

    txQueueInit(&pstEventEngine->stTxQueue);

    pstEventEngine->pstFlushEvent = event_new(
        pstEventBase,
        -1,
        EV_TIMEOUT,
        dispatcherFlushCb,
        pstEventEngine);
}

/* ============================================================ */
void dispatcherCleanup(DISPATCHER* pstDispatcher)
{
    REQUEST_CONTEXT* pstReqCtx = pstDispatcher->pstReqList;
    while (pstReqCtx) {
        REQUEST_CONTEXT* pstNextReqCtx = pstReqCtx->pstNext;
        if (pstReqCtx->pstTimeoutEvent)
            event_free(pstReqCtx->pstTimeoutEvent);
        free(pstReqCtx);
        pstReqCtx = pstNextReqCtx;
    }

    pstDispatcher->pstReqList = NULL;
    txQueueClear(&pstDispatcher->stTxQueue);

    if (pstDispatcher->pstFlushEvent)
        event_free(pstDispatcher->pstFlushEvent);
}

/* ============================================================ */
void dispatcherAttachSource(DISPATCHER* pstDispatcher, EVENT_SOURCE* pstEventSrc)
{
    pstEventSrc->pstNext = pstDispatcher->pstEventSrc;
    pstDispatcher->pstEventSrc = pstEventSrc;
}

void dispatcherDetachSource(DISPATCHER* pstDispatcher, EVENT_SOURCE* pstEventSrc)
{
    EVENT_SOURCE** ppstEventSrc = &pstDispatcher->pstEventSrc;
    while (*ppstEventSrc) {
        if (*ppstEventSrc == pstEventSrc) {
            *ppstEventSrc = pstEventSrc->pstNext;
            return;
        }
        ppstEventSrc = &((*ppstEventSrc)->pstNext);
    }
}

/* ============================================================ */
static REQUEST_CONTEXT* dispatcherFindReq(
    DISPATCHER* pstDispatcher, unsigned int uiReqId, REQUEST_CONTEXT** ppstPrevReqCtx)
{
    if (ppstPrevReqCtx) 
        *ppstPrevReqCtx = NULL;

    REQUEST_CONTEXT* pstReqCtx = pstDispatcher->pstReqList;
    REQUEST_CONTEXT* pstPrevReqCtx = NULL;

    while (pstReqCtx) {
        if (pstReqCtx->uiRequestId == uiReqId) {
            if (ppstPrevReqCtx) 
                *ppstPrevReqCtx = pstPrevReqCtx;
            return pstReqCtx;
        }
        pstPrevReqCtx = pstReqCtx;
        pstReqCtx = pstReqCtx->pstNext;
    }
    return NULL;
}

/* ============================================================ */
static void dispatcherFlushCb(evutil_socket_t fd, short what, void* pvArg)
{
    DISPATCHER* pstDispatcher = (DISPATCHER*)pvArg;

    unsigned char auchBuffer[4096];
    EVENT_SOURCE* pstEventDst;
    int iLen;

    while ((iLen = txQueuePop(&pstDispatcher->stTxQueue, 
        &pstEventDst, auchBuffer, sizeof(auchBuffer))) > 0) {

        if (pstEventDst->pstBufferEvent)
            bufferevent_write(pstEventDst->pstBufferEvent, auchBuffer, iLen);

        else if (pstEventDst->iFd >= 0)
            if(write(pstEventDst->iFd, auchBuffer, iLen) != iLen){
                fprintf(stderr,"### %s():%d Error write ###\n",__func__,__LINE__);
            }
    }
}

/* ============================================================ */
static void dispatcherReqTimeoutCb(evutil_socket_t fd, short what, void* pvArg)
{
    (void)fd, (void)what;

    REQUEST_CONTEXT* pstReqCtx = (REQUEST_CONTEXT*)pvArg;
    fprintf(stderr, "[DISP] RequestId=%u TIMEOUT\n", pstReqCtx->uiRequestId);
}

/* ============================================================ */
void dispatcherHandleRequest(DISPATCHER* pstDispatcher,
                             EVENT_SOURCE* pstEventSrc,
                             const unsigned char* puchData,
                             int iLen)
{
    EVENT_SOURCE* pstCurrEventSrc;
    if (iLen <= 0) 
        return;

    unsigned int uiReqId = pstDispatcher->unReqSeq++;
    if (pstDispatcher->unReqSeq == 0) 
        pstDispatcher->unReqSeq = 1;  // 0은 사용 금지

    /* RequestContext 생성 */
    REQUEST_CONTEXT* pstReqCtx = calloc(1, sizeof(REQUEST_CONTEXT));
    pstReqCtx->uiRequestId = uiReqId;
    pstReqCtx->pstEventSrcReqest = pstEventSrc;

    /* Worker 개수 계산 */
    int iWorkersCnt = 0;
    pstCurrEventSrc = pstDispatcher->pstEventSrc;
    for (; pstCurrEventSrc; pstCurrEventSrc = pstCurrEventSrc->pstNext)
        if (pstCurrEventSrc->eRole == SRC_ROLE_WORKER)
            iWorkersCnt++;

    pstReqCtx->iPending = iWorkersCnt;

    /* timeout 등록 */
    if (pstDispatcher->pstBaseCtx) {
        pstReqCtx->pstTimeoutEvent = evtimer_new(
            pstDispatcher->pstBaseCtx->pstEventBase,
            dispatcherReqTimeoutCb,
            pstReqCtx);
        struct timeval tv = {DISPATCHER_REQ_TIMEOUT_SEC, 
            DISPATCHER_REQ_TIMEOUT_SEC};
        evtimer_add(pstReqCtx->pstTimeoutEvent, &tv);
    }

    /* RequestContext 리스트에 추가 */
    pstReqCtx->pstNext = pstDispatcher->pstReqList;
    pstDispatcher->pstReqList = pstReqCtx;

    /* Worker들에게 브로드캐스트 */
    unsigned char uchPacket[4096];
    memcpy(uchPacket, &uiReqId, 4);
    memcpy(uchPacket + 4, puchData, iLen);

    pstCurrEventSrc = pstDispatcher->pstEventSrc;
    for (; pstCurrEventSrc; pstCurrEventSrc = pstCurrEventSrc->pstNext) {
        if (pstCurrEventSrc->eRole == SRC_ROLE_WORKER && pstCurrEventSrc->pstBufferEvent)
            bufferevent_write(pstCurrEventSrc->pstBufferEvent, uchPacket, iLen + 4);
    }

    printf("[DISP] RequestId=%u broadcast (workers=%d)\n", uiReqId, iWorkersCnt);
}
// void dispatcherHandleRequest(DISPATCHER* pstDispatcher,
//     EVENT_SOURCE* pstEventSrc,
//     const unsigned char* data,
//     int iLen)
/* ============================================================ */
void dispatcherHandleWorkerResponse(DISPATCHER* pstDispatcher,
                                    EVENT_SOURCE* pstWorkerEventSrc,
                                    const unsigned char* puchData,
                                    int iLen)
{
    if (iLen < 4) 
        return;

    unsigned int uiReqId;
    memcpy(&uiReqId, puchData, 4);

    REQUEST_CONTEXT* pstPrevReqCtx;
    REQUEST_CONTEXT* pstReqCtx = dispatcherFindReq(pstDispatcher, uiReqId, &pstPrevReqCtx);
    if (!pstReqCtx) 
        return;

    int iPayloadLen = iLen - 4;

    memcpy(pstReqCtx->auchRespBuf + pstReqCtx->iRespLen, puchData + 4, iPayloadLen);
    pstReqCtx->iRespLen += iPayloadLen;

    pstReqCtx->iPending--;

    if (pstReqCtx->iPending <= 0) {

        if (pstReqCtx->pstTimeoutEvent) {
            evtimer_del(pstReqCtx->pstTimeoutEvent);
            event_free(pstReqCtx->pstTimeoutEvent);
        }

        /* Requester에 응답 보내기 (TxQueue 사용) */
        txQueuePush(&pstDispatcher->stTxQueue, pstReqCtx->pstEventSrcReqest,
            pstReqCtx->auchRespBuf, pstReqCtx->iRespLen);

        event_active(pstDispatcher->pstFlushEvent, EV_TIMEOUT, 0);

        /* 리스트에서 제거 */
        if (!pstPrevReqCtx) 
            pstDispatcher->pstReqList = pstReqCtx->pstNext;
        else       
            pstPrevReqCtx->pstNext = pstReqCtx->pstNext;

        free(pstReqCtx);
    }
}
