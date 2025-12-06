/* dispatcher.c */

#include "dispatcher.h"
#include "txQueue.h"
#include <event2/event.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define REQ_TIMEOUT_SEC 0
#define REQ_TIMEOUT_USEC (300*1000)

/* ===== 내부 유틸 함수 ===== */

static void removeRequest(DISPATCHER_CONTEXT* pstDisp,
                          REQUEST_CONTEXT* pstReq)
{
    REQUEST_CONTEXT** ppstReqCtx = &pstDisp->pstReqList;
    while (*ppstReqCtx) {
        if (*ppstReqCtx == pstReq) {
            *ppstReqCtx = pstReq->pstNext;
            pstReq->pstNext = NULL;
            return;
        }
        ppstReqCtx = &(*ppstReqCtx)->pstNext;
    }
}

static void timeoutCb(evutil_socket_t fd, short events, void* pvData)
{
    (void)fd; 
    (void)events;
    REQUEST_CONTEXT* pstReqCtx = pvData;
    DISPATCHER_CONTEXT* pstDispCtx =
        (DISPATCHER_CONTEXT *)pstReqCtx->pstTcpRequester->pstBaseCtx->pvUserCtx;

    fprintf(stderr, "[Dispatcher] Timeout ReqId=%u\n", pstReqCtx->unRequestId);

    /* 타임아웃 시 TCP로 TIMEOUT 문자열 송신 결정 enqueue */
    TX_ITEM* pstTxItem = calloc(1, sizeof(TX_ITEM));
    pstTxItem->eDest   = TX_TO_TCP_ONE;
    pstTxItem->pstTarget = pstReqCtx->pstTcpRequester;
    const char* pchMsg = "TIMEOUT";
    pstTxItem->tLen = strlen(pchMsg);
    memcpy(pstTxItem->auchBuf, pchMsg, pstTxItem->tLen);
    txQueuePush(&pstDispCtx->stTxQueue, pstTxItem);

    /* flush 이벤트 활성화 */
    event_active(pstDispCtx->pstFlushEvent, 0, 0);

    /* 요청 제거 */
    removeRequest(pstDispCtx, pstReqCtx);
    if (pstReqCtx->pstTimeoutEvent)
        event_free(pstReqCtx->pstTimeoutEvent);
    free(pstReqCtx);
}

static REQUEST_CONTEXT* createRequest(DISPATCHER_CONTEXT* pstDispCtx,
                                      SOCK_CONTEXT* pstTcpSock)
{
    REQUEST_CONTEXT* pstReqCtx = calloc(1, sizeof(REQUEST_CONTEXT));
    pstReqCtx->unRequestId     = (uint32_t)rand();
    pstReqCtx->pstTcpRequester = pstTcpSock;
    pstReqCtx->iPendingUds     = 0;
    pstReqCtx->iRespLen        = 0;

    /* 리스트에 추가 */
    pstReqCtx->pstNext = pstDispCtx->pstReqList;
    pstDispCtx->pstReqList = pstReqCtx;

    /* 타임아웃 이벤트 생성 */
    struct timeval tv = { REQ_TIMEOUT_SEC, REQ_TIMEOUT_USEC };
    pstReqCtx->pstTimeoutEvent = evtimer_new(
        pstDispCtx->pstBaseCtx->pstEventBase,
        timeoutCb,
        pstReqCtx
    );
    if (pstReqCtx->pstTimeoutEvent)
        evtimer_add(pstReqCtx->pstTimeoutEvent, &tv);

    return pstReqCtx;
}

/* ===== TxQueue Flush 이벤트 콜백 ===== */
/* 실제 bufferevent_write()는 여기서만 수행 → Dispatcher는 순수 라우터 역할 유지 */

static void flushTxQueueCb(evutil_socket_t fd, short events, void* pvData)
{
    (void)fd; 
    (void)events;
    DISPATCHER_CONTEXT* pstDispCtx = pvData;

    TX_ITEM* pstTxItem;
    while ((pstTxItem = txQueuePop(&pstDispCtx->stTxQueue)) != NULL) {
        switch (pstTxItem->eDest) {
        case TX_TO_TCP_ONE:
            if (pstTxItem->pstTarget && pstTxItem->pstTarget->pstBufferEvent) {
                bufferevent_write(pstTxItem->pstTarget->pstBufferEvent,
                    pstTxItem->auchBuf, pstTxItem->tLen);
            }
            break;

        case TX_TO_TCP_ALL:
            if (pstDispCtx->pstTcpServer) {
                SOCK_CONTEXT* pstSockCtx = pstDispCtx->pstTcpServer->pstClientList;
                while (pstSockCtx) {
                    if (pstSockCtx->pstBufferEvent){
                        bufferevent_write(pstSockCtx->pstBufferEvent,
                            pstTxItem->auchBuf, pstTxItem->tLen);
                    }
                    pstSockCtx = pstSockCtx->pstNextSockCtx;
                }
            }
            break;

        case TX_TO_UDS_ONE:
            if (pstTxItem->pstTarget && pstTxItem->pstTarget->pstBufferEvent) {
                bufferevent_write(pstTxItem->pstTarget->pstBufferEvent,
                    pstTxItem->auchBuf, pstTxItem->tLen);
            }
            break;

        case TX_TO_UDS_ALL:
            if (pstDispCtx->pstUdsServer) {
                SOCK_CONTEXT* pstSockCtx = pstDispCtx->pstUdsServer->pstClientList;
                while (pstSockCtx) {
                    if (pstSockCtx->pstBufferEvent){
                        bufferevent_write(pstSockCtx->pstBufferEvent,
                            pstTxItem->auchBuf, pstTxItem->tLen);
                    }
                    pstSockCtx = pstSockCtx->pstNextSockCtx;
                }
            }
            break;
        }

        free(pstTxItem);
    }
}


/* ===== Public API 구현 ===== */

void dispatcherInit(DISPATCHER_CONTEXT* pstDispCtx,
                    BASE_CONTEXT* pstBase,
                    SERVER_CONTEXT* pstTcp,
                    SERVER_CONTEXT* pstUds)
{
    pstDispCtx->pstBaseCtx   = pstBase;
    pstDispCtx->pstTcpServer = pstTcp;
    pstDispCtx->pstUdsServer = pstUds;
    pstDispCtx->pstReqList   = NULL;

    txQueueInit(&pstDispCtx->stTxQueue);

    /* TxQueue flush용 이벤트 fd = -1, EV_TIMEOUT 유형으로 생성 */
    pstDispCtx->pstFlushEvent = event_new(
        pstBase->pstEventBase,
        -1,
        0,  /* EV_TIMEOUT 없이 event_active로 직접 트리거 */
        flushTxQueueCb,
        pstDispCtx
    );
}

void dispatcherOnTcpRequest(DISPATCHER_CONTEXT* pstDispCtx,
                            SOCK_CONTEXT* pstTcpSock,
                            const unsigned char* puchData,
                            int iLength)
{
    if (!pstDispCtx->pstUdsServer || pstDispCtx->pstUdsServer->iClientCount <= 0) {
        fprintf(stderr, "[Dispatcher] No UDS clients.\n");
        return;
    }

    REQUEST_CONTEXT* pstReqCtx = createRequest(pstDispCtx, pstTcpSock);
    pstReqCtx->iPendingUds = pstDispCtx->pstUdsServer->iClientCount;

    /* UDS 전체 브로드캐스트를 TxQueue에 enqueue */
    TX_ITEM* pstTxItem = calloc(1, sizeof(TX_ITEM));
    pstTxItem->eDest = TX_TO_UDS_ALL;
    pstTxItem->tLen  = (size_t)iLength;
    memcpy(pstTxItem->auchBuf, puchData, pstTxItem->tLen);
    txQueuePush(&pstDispCtx->stTxQueue, pstTxItem);

    /* Flush 이벤트 트리거 */
    event_active(pstDispCtx->pstFlushEvent, 0, 0);

    fprintf(stderr, "[Dispatcher] ReqId=%u broadcast to %d UDS\n",
        pstReqCtx->unRequestId, pstReqCtx->iPendingUds);
}

void dispatcherOnUdsResponse(DISPATCHER_CONTEXT* pstDispCtx,
                             SOCK_CONTEXT* pstUdsSock,
                             const unsigned char* puchData,
                             int iLength)
{
    (void)pstUdsSock;

    /* 가장 단순한 정책: "pendingUds > 0"인 첫 요청에 붙인다 */
    REQUEST_CONTEXT* pstReqCtx = pstDispCtx->pstReqList;
    while (pstReqCtx) {
        if (pstReqCtx->iPendingUds > 0)
            break;
        pstReqCtx = pstReqCtx->pstNext;
    }
    if (!pstReqCtx) {
        fprintf(stderr, "[Dispatcher] No pending requests.\n");
        return;
    }

    if (pstReqCtx->iRespLen + iLength <= (int)sizeof(pstReqCtx->auchRespBuf)) {
        memcpy(pstReqCtx->auchRespBuf + pstReqCtx->iRespLen, puchData, iLength);
        pstReqCtx->iRespLen += iLength;
    } else {
        fprintf(stderr, "[Dispatcher] RespBuf overflow, trunc.\n");
    }

    pstReqCtx->iPendingUds--;
    fprintf(stderr, "[Dispatcher] ReqId=%u pending=%d\n",
        pstReqCtx->unRequestId, pstReqCtx->iPendingUds);

    if (pstReqCtx->iPendingUds <= 0) {
        /* 타임아웃 이벤트 해제 */
        if (pstReqCtx->pstTimeoutEvent) {
            evtimer_del(pstReqCtx->pstTimeoutEvent);
            event_free(pstReqCtx->pstTimeoutEvent);
            pstReqCtx->pstTimeoutEvent = NULL;
        }

        /* TCP ONE 전송을 TxQueue에 enqueue */
        TX_ITEM* pstTxItem = calloc(1, sizeof(TX_ITEM));
        pstTxItem->eDest    = TX_TO_TCP_ONE;
        pstTxItem->pstTarget = pstReqCtx->pstTcpRequester;
        pstTxItem->tLen     = (size_t)pstReqCtx->iRespLen;
        memcpy(pstTxItem->auchBuf, pstReqCtx->auchRespBuf, pstTxItem->tLen);
        txQueuePush(&pstDispCtx->stTxQueue, pstTxItem);

        event_active(pstDispCtx->pstFlushEvent, 0, 0);

        /* 요청 제거 */
        removeRequest(pstDispCtx, pstReqCtx);
        free(pstReqCtx);
    }
}

void dispatcherCleanup(DISPATCHER_CONTEXT* pstDispCtx)
{
    REQUEST_CONTEXT* pstReqCtx = pstDispCtx->pstReqList;
    while (pstReqCtx) {
        REQUEST_CONTEXT* pstNextReqCtx = pstReqCtx->pstNext;
        if (pstReqCtx->pstTimeoutEvent) {
            evtimer_del(pstReqCtx->pstTimeoutEvent);
            event_free(pstReqCtx->pstTimeoutEvent);
        }
        free(pstReqCtx);
        pstReqCtx = pstNextReqCtx;
    }
    pstDispCtx->pstReqList = NULL;

    /* TxQueue 내부 TX_ITEM free */
    TX_ITEM* pstTxItem;
    while ((pstTxItem = txQueuePop(&pstDispCtx->stTxQueue)) != NULL) {
        free(pstTxItem);
    }

    if (pstDispCtx->pstFlushEvent) {
        event_free(pstDispCtx->pstFlushEvent);
        pstDispCtx->pstFlushEvent = NULL;
    }
}
