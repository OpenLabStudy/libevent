#include "eventEngine.h"
#include "eventSource.h"
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
static REQUEST_CONTEXT* eventEngineFindReq(EVENT_ENGINE* pstEventEngine, 
    unsigned int uiRequestId, REQUEST_CONTEXT** ppstPrevReqCtx);

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
void eventEngineHandleRequest(int iFd, short nEvent, void* pvData)
{
    (void)iFd;
    (void)nEvent;    

    IO_CHANNEL* pstRequester = (IO_CHANNEL*)pvData;
    EVENT_ENGINE* pstEventEngine = pstRequester->pstEventEngine;

    while (1) {
        unsigned int iLen = evbuffer_get_length(pstRequester->pstRequestBuffer);
        if (iLen < sizeof(FRAME_HEADER))
            break;

        unsigned char auchBuf[2048];
        unsigned int uiCopySize = evbuffer_copyout(pstRequester->pstRequestBuffer,
            auchBuf, sizeof(auchBuf));

        int iFrameSize = getFrameSizeWithData(auchBuf, FRAME_TYPE_REQUEST);
        if (iFrameSize <= 0 || uiCopySize < (size_t)iFrameSize)
            break;

        evbuffer_drain(pstRequester->pstRequestBuffer, iFrameSize);

        /* === REQUEST_CONTEXT 생성 === */
        REQUEST_CONTEXT* pstReq = calloc(1, sizeof(REQUEST_CONTEXT));
        pstReq->uiRequestId = pstEventEngine->uiRequestSeq++;
        pstReq->eState = REQ_STATE_WAITING;
        pstReq->pstIoReqList = pstRequester;
        pstReq->pstRespEvBuffer = evbuffer_new();

        /* === Worker 수 계산 === */
        int iWorkerCnt = 0;
        IO_CHANNEL* pstIoChannel = pstEventEngine->pstIoChannelList;
        while (pstIoChannel) {
            if (pstIoChannel->eRole == ROLE_WORKER)
                iWorkerCnt++;
            pstIoChannel = pstIoChannel->pstNextIoChannel;
        }
        pstReq->iPendingCount = iWorkerCnt;

        /* === Timeout 등록 === */
        pstReq->pstTimeoutEvent = evtimer_new(
            pstEventEngine->pstEventBase,
            eventEngineReqTimeoutCb,
            pstReq);
        
        /* === Request 리스트 연결 === */
        pstReq->pstNextReqCtx = pstEventEngine->pstReqList;
        pstEventEngine->pstReqList = pstReq;

        /* === Worker 브로드캐스트 === */
        unsigned char uchaSendBuf[4096];        
        memcpy(uchaSendBuf, auchBuf, iFrameSize);
        memcpy(uchaSendBuf + iFrameSize, &pstReq->uiRequestId, sizeof(unsigned int));

        pstIoChannel = pstEventEngine->pstIoChannelList;
        fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
        struct timeval stTimeOut = { 0, 500 * 1000 };
        evtimer_add(pstReq->pstTimeoutEvent, &stTimeOut);
        while (pstIoChannel) {
            if (pstIoChannel->eRole == ROLE_WORKER && pstIoChannel->pstWriteBuffer) {
                evbuffer_add(pstIoChannel->pstWriteBuffer,
                             uchaSendBuf, iFrameSize + sizeof(unsigned int));
                event_add(pstIoChannel->pstWriteEvent, NULL);
                usleep(10);//UDS수신데이터의 병목현상을 피하기 위해서 약간의 시간 지연을 통해 수신데이터 처리를 할수있는 시간을 준다.
            }
            pstIoChannel = pstIoChannel->pstNextIoChannel;
        }
    }
}

void eventEngineHandleWorkerResponse(EVENT_ENGINE* pstEventEngine, IO_CHANNEL* pstIoChannel,
                                    const unsigned char* puchData, int iLen)
{
    IPC_FRAME *pstIpcFrame = puchData;
    if (iLen < sizeof(unsigned int))
        return;

    REQUEST_CONTEXT* pstPrevReq = NULL;
    REQUEST_CONTEXT* pstRequest = eventEngineFindReq(pstEventEngine, pstIpcFrame->uiRequestId, &pstPrevReq);    
    if (!pstRequest)
        return;  // 이미 timeout / unknown
        
    /* === 응답 누적 === */
    fprintf(stderr,"### %s():%d %d %d %d Result:%02x###\n",__func__,__LINE__, pstIpcFrame->uiResultSize, pstIpcFrame->uiRequestId, pstIpcFrame->unCmd, pstIpcFrame->auchResult[0]);
    evbuffer_add(pstRequest->pstRespEvBuffer, pstIpcFrame->auchResult, pstIpcFrame->uiResultSize);
    pstRequest->iPendingCount--;
    /* === 모든 Worker 응답 수신 === */
    if (pstRequest->iPendingCount <= 0) {
        /* timeout 해제 */
        if (pstRequest->pstTimeoutEvent) {
            evtimer_del(pstRequest->pstTimeoutEvent);
            event_free(pstRequest->pstTimeoutEvent);
        }
        unsigned char uchFinalResult = 0x01;  /* 기본값: 정상 */
        unsigned char uchResult;
        fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
        while (evbuffer_get_length(pstRequest->pstRespEvBuffer) > 0) {
            /* 1바이트씩 꺼냄 */
            fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
            if (evbuffer_remove(pstRequest->pstRespEvBuffer, &uchResult, 1) != 1) {
                uchFinalResult = 0x00;
                break;
            }
            fprintf(stderr,"### %s():%d Result:%02x###\n",__func__,__LINE__, uchResult);

            if (uchResult == 0x00) {
                uchFinalResult = 0x00;
                break;
            }
        }
        

        /* TCP requester에게 전달 */
        IO_CHANNEL* tcpCh = pstRequest->pstIoReqList;
        IPC_FRAME stIpcFrame;
        stIpcFrame.unStx = STX_CONST;
        stIpcFrame.uiRequestId = 0;
        stIpcFrame.uiResultSize = pstIpcFrame->uiResultSize;
        memcpy(stIpcFrame.auchResult, &uchFinalResult, stIpcFrame.uiResultSize);
        stIpcFrame.unCmd = pstIpcFrame->unCmd;
        stIpcFrame.unEtx = ETX_CONST;
        evbuffer_add(tcpCh->pstWriteBuffer, &stIpcFrame, sizeof(stIpcFrame));
        event_add(tcpCh->pstWriteEvent, NULL);
        /* 리스트에서 제거 */
        if (pstPrevReq)
            pstPrevReq->pstNextReqCtx = pstRequest->pstNextReqCtx;
        else
            pstEventEngine->pstReqList = pstRequest->pstNextReqCtx;

        evbuffer_free(pstRequest->pstRespEvBuffer);
        free(pstRequest);
    }
}
