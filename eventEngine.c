#include "eventEngine.h"
#include "eventSource.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

/* timeout */
#define REQ_TIMEOUT_SEC      0
#define REQ_TIMEOUT_MSEC     (300*1000)

/* forward declarations */
static void eventEngineFlushCb(int iFd, short nEvent, void* pvArg);
static void eventEngineReqTimeoutCb(int iFd, short nEvent, void* pvArg);
static REQUEST_CONTEXT* eventEngineFindReq(EVENT_ENGINE* pstEventEngine, 
    unsigned int uiRequestId, REQUEST_CONTEXT** ppstPrevReqCtx);

/* ============================================================
 * 초기화
 * ============================================================ */
void eventEngineInit(EVENT_ENGINE* pstEventEngine)
{
    pstEventEngine->pstIoChannelList    = NULL;
    pstEventEngine->pstReqList          = NULL;

    /* Request ID 시퀀스 초기화 */
    pstEventEngine->uiRequestSeq = 1;

    pstEventEngine->pstFlushEvent = event_new(
        pstEventEngine->pstEventBase,
        -1,
        EV_TIMEOUT,
        eventEngineFlushCb,
        pstEventEngine);
}

/* ============================================================ */
void eventEngineCleanup(EVENT_ENGINE* pstEventEngine)
{
    /* 1. REQUEST_CONTEXT 정리 */
    REQUEST_CONTEXT* pstReqCtx = pstEventEngine->pstReqList;
    while (pstReqCtx) {
        REQUEST_CONTEXT* pstNextReqCtx = pstReqCtx->pstNextReqCtx;
        if (pstReqCtx->pstTimeoutEvent)
            event_free(pstReqCtx->pstTimeoutEvent);
        free(pstReqCtx);
        pstReqCtx = pstNextReqCtx;
    }
    
    pstEventEngine->pstReqList = NULL;

    /* 3. Flush event 정리 */
    if (pstEventEngine->pstFlushEvent) {
        event_free(pstEventEngine->pstFlushEvent);
        pstEventEngine->pstFlushEvent = NULL;
    }
    
    /* ===================================================== */
    /* 4. ★ IO_CHANNEL / EVENT_SOURCE 정리 (필수 추가) ★ */
    /* ===================================================== */
    IO_CHANNEL* pstIoChannel = pstEventEngine->pstIoChannelList;
    while (pstIoChannel) {
        IO_CHANNEL* pstNextIoChannel = pstIoChannel->pstNextIoChannel;
        eventSourceDestroy(pstIoChannel);
        pstIoChannel = pstNextIoChannel;
    }
    pstEventEngine->pstIoChannelList = NULL;
}


/* ============================================================ */
void eventEngineAttachSource(EVENT_ENGINE* pstEventEngine, IO_CHANNEL* pstIoChannel)
{
    pstIoChannel->pstNextIoChannel      = pstEventEngine->pstIoChannelList;
    pstEventEngine->pstIoChannelList    = pstIoChannel;
}


/* ============================================================ */
static REQUEST_CONTEXT* eventEngineFindReq(EVENT_ENGINE* pstEventEngine, 
    unsigned int uiRequestId, REQUEST_CONTEXT** ppstPrevReqCtx)
{
    if (ppstPrevReqCtx) 
        *ppstPrevReqCtx = NULL;

    REQUEST_CONTEXT* pstReqCtx = pstEventEngine->pstReqList;
    REQUEST_CONTEXT* pstPrevReqCtx = NULL;

    while (pstReqCtx) {
        if (pstReqCtx->uiRequestId == uiRequestId) {
            if (ppstPrevReqCtx) 
                *ppstPrevReqCtx = pstPrevReqCtx;
            return pstReqCtx;
        }
        pstPrevReqCtx = pstReqCtx;
        pstReqCtx = pstReqCtx->pstNextReqCtx;
    }
    return NULL;
}

/* ============================================================ */
static void eventEngineFlushCb(int iFd, short nEvent, void* pvArg)
{
    EVENT_ENGINE* pstEventEngine = (EVENT_ENGINE*)pvArg;

    unsigned char auchBuffer[4096];
    IO_CHANNEL* pstIoChannel;
    int iLen;

}

/* ============================================================ */
static void eventEngineReqTimeoutCb(int iFd, short nEvent, void* pvArg)
{
    (void)iFd;
    (void)nEvent;

    REQUEST_CONTEXT* pstReqCtx = (REQUEST_CONTEXT*)pvArg;
    fprintf(stderr, "[DISP] RequestId=%u TIMEOUT\n", pstReqCtx->uiRequestId);
}

/* ============================================================ */
void eventEngineHandleRequest(EVENT_ENGINE* pstEventEngine,
                             IO_CHANNEL* pstIoChannel,
                             const unsigned char* puchData,
                             int iLen)
{
    // IO_CHANNEL* pstCurrIoChannel;
    // if (iLen <= 0) 
    //     return;

    // unsigned int uiReqId = pstEventEngine->uiRequestSeq++;
    // if (pstEventEngine->uiRequestSeq == 0) 
    //     pstEventEngine->uiRequestSeq = 1;  // 0은 사용 금지

    // /* RequestContext 생성 */
    // REQUEST_CONTEXT* pstReqCtx = calloc(1, sizeof(REQUEST_CONTEXT));
    // pstReqCtx->uiRequestId = uiReqId;
    // pstReqCtx->pstIoChannelRequest = pstIoChannel;

    // /* Worker 개수 계산 */
    // int iWorkersCnt = 0;
    // pstCurrIoChannel = pstEventEngine->pstIoChannel;
    // for (; pstCurrIoChannel; pstCurrIoChannel = pstCurrIoChannel->pstNext)
    //     if (pstCurrIoChannel->eRole == SRC_ROLE_WORKER)
    //         iWorkersCnt++;

    // pstReqCtx->iPending = iWorkersCnt;

    // /* timeout 등록 */
    // if (pstEventEngine) {
    //     pstReqCtx->pstTimeoutEvent = evtimer_new(
    //         pstEventEngine->pstEventBase,
    //         eventEngineReqTimeoutCb,
    //         pstReqCtx);
    //     struct timeval tv = { REQ_TIMEOUT_SEC, REQ_TIMEOUT_MSEC};
    //     evtimer_add(pstReqCtx->pstTimeoutEvent, &tv);
    // }

    // /* RequestContext 리스트에 추가 */
    // pstReqCtx->pstNextReqCtx = pstEventEngine->pstReqList;
    // pstEventEngine->pstReqList = pstReqCtx;

    // /* Worker들에게 브로드캐스트 */
    // unsigned char uchPacket[4096];
    // memcpy(uchPacket, &uiReqId, 4);
    // memcpy(uchPacket + 4, puchData, iLen);

    // pstCurrIoChannel = pstEventEngine->pstIoChannel;
    // for (; pstCurrIoChannel; pstCurrIoChannel = pstCurrIoChannel->pstNext) {
    //     if (pstCurrIoChannel->eRole == SRC_ROLE_WORKER && pstCurrIoChannel->pstBufferEvent)
    //         bufferevent_write(pstCurrIoChannel->pstBufferEvent, uchPacket, iLen + 4);
    // }

    // printf("[DISP] RequestId=%u broadcast (workers=%d)\n", uiReqId, iWorkersCnt);
}

void eventEngineHandleWorkerResponse(EVENT_ENGINE* pstEventEngine,
                                    IO_CHANNEL* pstIoChannel,
                                    const unsigned char* puchData, int iLen)
{
    // if (iLen < 4) 
    //     return;

    // unsigned int uiReqId;
    // memcpy(&uiReqId, puchData, 4);

    // REQUEST_CONTEXT* pstPrevReqCtx;
    // REQUEST_CONTEXT* pstReqCtx = eventEngineFindReq(pstEventEngine, uiReqId, &pstPrevReqCtx);
    // if (!pstReqCtx) 
    //     return;

    // int iPayloadLen = iLen - 4;

    // memcpy(pstReqCtx->auchRespBuf + pstReqCtx->iRespLen, puchData + 4, iPayloadLen);
    // pstReqCtx->iRespLen += iPayloadLen;

    // pstReqCtx->iPending--;

    // if (pstReqCtx->iPending <= 0) {
    //     if (pstReqCtx->pstTimeoutEvent) {
    //         evtimer_del(pstReqCtx->pstTimeoutEvent);
    //         event_free(pstReqCtx->pstTimeoutEvent);
    //     }

    //     /* Requester에 응답 보내기 (TxQueue 사용) */
    //     txQueuePush(&pstEventEngine->stTxQueue, pstReqCtx->pstIoChannelRequest,
    //         pstReqCtx->auchRespBuf, pstReqCtx->iRespLen);

    //     event_active(pstEventEngine->pstFlushEvent, EV_TIMEOUT, 0);

    //     /* 리스트에서 제거 */
    //     if (!pstPrevReqCtx) 
    //         pstEventEngine->pstReqList = pstReqCtx->pstNextReqCtx;
    //     else       
    //         pstPrevReqCtx->pstNextReqCtx = pstReqCtx->pstNextReqCtx;

    //     free(pstReqCtx);
    // }
}
