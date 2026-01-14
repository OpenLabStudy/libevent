#include "eventEngine.h"
#include "frame.h"

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
static REQUEST_CONTEXT* eventEngineFindReq(EVENT_ENGINE* pstEventEngine, unsigned int uiRequestId);
static void eventEngineFreeReq(REQUEST_CONTEXT* pstReqCtx)
{
    if (!pstReqCtx)
        return;

    if (pstReqCtx->pstTimeoutEvent) {
        evtimer_del(pstReqCtx->pstTimeoutEvent);
        event_free(pstReqCtx->pstTimeoutEvent);
    }

    if (pstReqCtx->pstFinalizeEvent) {
        event_free(pstReqCtx->pstFinalizeEvent);
    }

    for (int i = 0; i < WORKER_MAX; i++) {
        if (pstReqCtx->apstWorkerBuf[i]) {
            evbuffer_free(pstReqCtx->apstWorkerBuf[i]);
        }
    }

    free(pstReqCtx);
}

/* ============================================================
 * 초기화
 * ============================================================ */
void eventEngineInit(EVENT_ENGINE* pstEventEngine)
{
    pstEventEngine->pstIoChannelList    = NULL;
    pstEventEngine->pstReqList          = NULL;
    pstEventEngine->pvSharedData        = NULL;

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
        eventEngineFreeReq(pstReqCtx);
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
    fprintf(stderr, "attach: ch=%p next=%p head=%p\n",
        (void*)pstIoChannel,
        (void*)pstIoChannel->pstNextIoChannel,
        (void*)pstEventEngine->pstIoChannelList);

    pstIoChannel->pstNextIoChannel      = pstEventEngine->pstIoChannelList;
    pstEventEngine->pstIoChannelList    = pstIoChannel;
}


/* ============================================================ */
static REQUEST_CONTEXT* eventEngineFindReq(EVENT_ENGINE* pstEventEngine,
                   unsigned int uiRequestId)
{
    REQUEST_CONTEXT* pstReqCtx = pstEventEngine->pstReqList;

    while (pstReqCtx) {
        if (pstReqCtx->uiRequestId == uiRequestId)
            return pstReqCtx;

        pstReqCtx = pstReqCtx->pstNextReqCtx;
    }
    return NULL;
}


/* ============================================================ */
static void eventEngineFlushCb(int iFd, short nEvent, void* pvArg)
{
    // EVENT_ENGINE* pstEventEngine = (EVENT_ENGINE*)pvArg;

    // unsigned char auchBuffer[4096];
    // IO_CHANNEL* pstIoChannel;
    // int iLen;

}

/* ============================================================ */
static void eventEngineReqTimeoutCb(int iFd, short nEvent, void* pvArg)
{
    (void)iFd;
    (void)nEvent;
    REQUEST_CONTEXT* pstReqCtx = (REQUEST_CONTEXT*)pvArg;
    if (pstReqCtx->iFinalizeQueued)
        return;

    fprintf(stderr, "[DISP] RequestId=%u TIMEOUT\n", pstReqCtx->uiRequestId);

    pstReqCtx->iTimedOut = 1;
    pstReqCtx->iFinalizeQueued = 1;

    if (pstReqCtx->pstFinalizeEvent)
        event_active(pstReqCtx->pstFinalizeEvent, 0, 0);
}

static void eventEngineRemoveReq(EVENT_ENGINE* pstEventEngine, REQUEST_CONTEXT* pstReq)
{
    REQUEST_CONTEXT** ppReqCtx = &pstEventEngine->pstReqList;

    while (*ppReqCtx) {
        if (*ppReqCtx == pstReq) {
            *ppReqCtx = pstReq->pstNextReqCtx;
            pstReq->pstNextReqCtx = NULL;
            return;
        }
        ppReqCtx = &((*ppReqCtx)->pstNextReqCtx);
    }
}

/* ============================================================
 * Fan-in 결과를 하나의 TCP 응답으로 통합
 * ============================================================ */
/* ============================================================
 * Fan-in 결과를 하나의 TCP 응답으로 통합 (goto 없는 버전)
 * ============================================================ */
void buildFinalResponseAndQueueTcp(EVENT_ENGINE* pstEventEngine, REQUEST_CONTEXT* pstReqCtx)
{
    (void)pstEventEngine;
    IO_CHANNEL* pstTcpCh = pstReqCtx->pstTcpIoChannel;

    unsigned char auchResult[1024];
    int iResultLen = 0;

    unsigned char auchSendBuf[2048];
    MSG_ID stMsgId = { 0x77, 0x55 };

    /* TODO: 요청 시점에 저장해둔 cmd를 사용하는 것이 이상적 */
    unsigned short unCmd = 0x00FF;//CMD_COMMAND_FAIL

    /* ========================================================
     * 1. TIMEOUT 처리
     * ======================================================== */
    if (pstReqCtx->iTimedOut) {
        /* 예시: timeout 결과 코드 */
        auchResult[0] = 0x01;   /* RESULT_TIMEOUT */
        iResultLen = 1;

    } else {
        /* ====================================================
         * 2. Worker 응답 통합
         * ==================================================== */
        for (int iWorkId = 0; iWorkId < WORKER_MAX; iWorkId++) {

            /* 이 요청에 포함되지 않은 worker */
            if (!(pstReqCtx->uiExpectedMask & (1u << iWorkId)))
                continue;

            /* 응답 미수신 worker */
            if (!(pstReqCtx->uiReceivedMask & (1u << iWorkId))) {
                /* 정책 예: partial failure 표시 */
                auchResult[iResultLen++] = 0x02; /* RESULT_PARTIAL */
                continue;
            }
            fprintf(stderr,"### %s():%d WorkId is %d###\n",__func__,__LINE__,iWorkId);

            struct evbuffer* pstEventBuffer = pstReqCtx->apstWorkerBuf[iWorkId];
            if (!pstEventBuffer)
                continue;

            int iGetDataSize = evbuffer_get_length(pstEventBuffer);

            unsigned char auchBuffer[512];
            int iSize = evbuffer_remove(pstEventBuffer, auchBuffer, iGetDataSize);
            if (iSize <= 0)
                continue;

            for(int i=1; i<=iSize; i++){
                if(i&16 == 0)
                    fprintf(stderr,"\n");
                fprintf(stderr,"%02x ", auchBuffer[i-1]);
            }

            memcpy(auchResult, auchBuffer, iSize);
            iResultLen += iSize;
        }
    }

    evbuffer_add(pstTcpCh->pstWriteBuffer, auchResult, iResultLen);
}

void eventEngineFinalizeRequestCb(int iFd, short nEvent, void* pvArg)
{
    (void)iFd;
    (void)nEvent;
    REQUEST_CONTEXT* pstReqCtx = (REQUEST_CONTEXT *)pvArg;
    EVENT_ENGINE* pstEventEngine = pstReqCtx->pstTcpIoChannel->pstEventEngine; // 또는 req에 저장

    /* 무거운 분석/통합은 여기서 */
    buildFinalResponseAndQueueTcp(pstEventEngine, pstReqCtx);

    /* TCP write 트리거 */
    event_active(pstReqCtx->pstTcpIoChannel->pstWriteEvent, 0, 0);

    /* req 제거/정리 */
    eventEngineRemoveReq(pstEventEngine, pstReqCtx);

    /* === ADD: 리소스 정리 === */
    eventEngineFreeReq(pstReqCtx);
}

/* ============================================================ */
void eventEngineHandleRequest(int iFd, short nEvent, void* pvData)
{
    (void)iFd;
    (void)nEvent;

    IO_CHANNEL* pstRequester = (IO_CHANNEL*)pvData;
    EVENT_ENGINE* pstEventEngine = pstRequester->pstEventEngine;
    PROCESS_PATH eProcPath = PROCESS_UNKNOWN;

    while (1) {
        if (evbuffer_get_length(pstRequester->pstRequestBuffer) < sizeof(PROCESS_PATH))
            break;
        evbuffer_remove(pstRequester->pstRequestBuffer, &eProcPath, sizeof(PROCESS_PATH));
        unsigned int uiRemain = evbuffer_get_length(pstRequester->pstRequestBuffer);
        unsigned char auchBuf[2048];
        unsigned int uiCopySize = evbuffer_remove(pstRequester->pstRequestBuffer, auchBuf, uiRemain);

        int iFrameSize = getFrameSizeWithData(auchBuf, FRAME_TYPE_REQUEST);
        if (iFrameSize <= 0 || uiCopySize < (unsigned int)iFrameSize)
            break;
        fprintf(stderr,"### %s():%d Processing Path:%d Copy Size:%d Frame Size:%d ###\n", __func__,__LINE__, eProcPath, uiCopySize, iFrameSize);        
        for(int i=1; i<=iFrameSize; i++){
            if(i&16 == 0)
                fprintf(stderr,"\n");
            fprintf(stderr,"%02x ", auchBuf[i-1]);
        }
        fprintf(stderr,"\n");
        /* =========================================================
         * REQUEST_CONTEXT 생성
         * ========================================================= */
        REQUEST_CONTEXT* pstReq = calloc(1, sizeof(REQUEST_CONTEXT));
        if (!pstReq)
            return;

        pstReq->uiRequestId      = pstEventEngine->uiRequestSeq++;
        pstReq->pstTcpIoChannel = pstRequester;
        pstReq->uiExpectedMask  = 0;
        pstReq->uiReceivedMask  = 0;
        pstReq->iFinalizeQueued = 0;
        pstReq->iTimedOut       = 0;
        pstReq->unCmd          = 0; // TODO: 프레임에서 cmd 추출하여 저장 auchBuf
        /* === ADD: finalize event 생성 === */
        pstReq->pstFinalizeEvent = event_new(pstEventEngine->pstEventBase, -1, 0,
            eventEngineFinalizeRequestCb, pstReq );
        if (!pstReq->pstFinalizeEvent) {
            free(pstReq);
            return;
        }        

        /* =========================================================
         * Worker 대상 결정 (Fan-out 대상 계산)
         * ========================================================= */
        IO_CHANNEL* pstIo = pstEventEngine->pstIoChannelList;
        while (pstIo) {
            if (pstIo->eRole == ROLE_WORKER) {
                int iWorkerId = pstIo->iWorkerId;
                if (iWorkerId >= 0 && iWorkerId < WORKER_MAX && iWorkerId == eProcPath) {
                    pstReq->uiExpectedMask |= (1u << iWorkerId);
                }
            }
            pstIo = pstIo->pstNextIoChannel;
        }

        /* Worker가 하나도 없으면 즉시 finalize 대상으로 넘겨도 됨 */
        if (pstReq->uiExpectedMask == 0) {
            /* 바로 finalize 큐에 넣는 것도 가능 */
        }

        /* =========================================================
         * Timeout 이벤트 등록
         * ========================================================= */
        pstReq->pstTimeoutEvent = evtimer_new(
            pstEventEngine->pstEventBase,
            eventEngineReqTimeoutCb,
            pstReq);

        struct timeval stTimeout = {
            .tv_sec  = REQ_TIMEOUT_SEC,
            .tv_usec = REQ_TIMEOUT_MSEC
        };
        evtimer_add(pstReq->pstTimeoutEvent, &stTimeout);

        /* =========================================================
         * Request 리스트에 연결
         * ========================================================= */
        pstReq->pstNextReqCtx = pstEventEngine->pstReqList;
        pstEventEngine->pstReqList = pstReq;

        /* =========================================================
         * Fan-out: 모든 대상 Worker에게 전송
         * ========================================================= */
        pstIo = pstEventEngine->pstIoChannelList;
        while (pstIo) {
            if (pstIo->eRole == ROLE_WORKER && pstIo->pstWriteBuffer && (pstReq->uiExpectedMask & (1u << pstIo->iWorkerId))) {                    
                if ((size_t)iFrameSize + sizeof(unsigned int) > sizeof(auchBuf))
                    continue;
                /* requestId를 프레임 끝에 부착 */
                evbuffer_add(pstIo->pstWriteBuffer, auchBuf, iFrameSize);
                evbuffer_add(pstIo->pstWriteBuffer, &pstReq->uiRequestId, sizeof(unsigned int));
                /* write 이벤트 트리거 */
                event_add(pstIo->pstWriteEvent, NULL);
            }
            pstIo = pstIo->pstNextIoChannel;
        }
    }
}


static inline int isReqAllDone(const REQUEST_CONTEXT* pstReqCtx) {
    return (
        (pstReqCtx->uiReceivedMask & pstReqCtx->uiExpectedMask) == 
        pstReqCtx->uiExpectedMask
    );
}

void eventEngineHandleWorkerResponse(
    EVENT_ENGINE* pstEventEngine, IO_CHANNEL* pstUdsIoCh,
    int iReqId, const unsigned char* data, int iLen)
{
    fprintf(stderr,"### %s():%d Request ID : %d###\n",__func__,__LINE__, iReqId);
    REQUEST_CONTEXT* pstReqCtx = eventEngineFindReq(pstEventEngine, iReqId);
    if (!pstReqCtx)
        return;

    int iWorkerId = pstUdsIoCh->iWorkerId;
    if (iWorkerId < 0 || iWorkerId >= WORKER_MAX)
        return;
    fprintf(stderr,"### %s():%d Worker ID:%d###\n",__func__,__LINE__, iWorkerId);
    unsigned int uiWorkerMask = WORKER_MASK(iWorkerId);
    if (!(pstReqCtx->uiExpectedMask & uiWorkerMask))
        return;
    
    if (pstReqCtx->uiReceivedMask & uiWorkerMask)
        return;
    
    if (!pstReqCtx->apstWorkerBuf[iWorkerId])
        pstReqCtx->apstWorkerBuf[iWorkerId] = evbuffer_new();

    evbuffer_add(pstReqCtx->apstWorkerBuf[iWorkerId], data, iLen);
    pstReqCtx->uiReceivedMask |= uiWorkerMask;

    /* 여기서 finalize를 직접 하지 않고 "예약"만 */
    if (isReqAllDone(pstReqCtx) && !pstReqCtx->iFinalizeQueued) {
        pstReqCtx->iFinalizeQueued = 1;
        if (pstReqCtx->pstTimeoutEvent)
            evtimer_del(pstReqCtx->pstTimeoutEvent);
        event_active(pstReqCtx->pstFinalizeEvent, 0, 0);
    }
}
