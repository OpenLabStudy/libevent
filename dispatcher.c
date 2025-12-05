/* dispatcher.c */

#include "dispatcher.h"
#include "txQueue.h"
#include <event2/event.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define REQ_TIMEOUT_SEC 3

/* ===== 내부 유틸 함수 ===== */

static void removeRequest(DISPATCHER_CONTEXT* pstDisp,
                          REQUEST_CONTEXT* pstReq)
{
    REQUEST_CONTEXT** ppCur = &pstDisp->pstReqList;
    while (*ppCur) {
        if (*ppCur == pstReq) {
            *ppCur = pstReq->pstNext;
            pstReq->pstNext = NULL;
            return;
        }
        ppCur = &(*ppCur)->pstNext;
    }
}

static void timeoutCb(evutil_socket_t fd, short events, void* arg)
{
    (void)fd; (void)events;
    REQUEST_CONTEXT* pstReq = arg;
    DISPATCHER_CONTEXT* pstDisp =
        (DISPATCHER_CONTEXT*)pstReq->pstTcpRequester->pstBaseCtx->pvUserCtx;

    fprintf(stderr, "[Dispatcher] Timeout ReqId=%u\n", pstReq->unRequestId);

    /* 타임아웃 시 TCP로 TIMEOUT 문자열 송신 결정 enqueue */
    TX_ITEM* item = calloc(1, sizeof(TX_ITEM));
    item->eDest   = TX_TO_TCP_ONE;
    item->pstTarget = pstReq->pstTcpRequester;
    const char* msg = "TIMEOUT";
    item->tLen = strlen(msg);
    memcpy(item->auchBuf, msg, item->tLen);
    txQueuePush(&pstDisp->stTxQueue, item);

    /* flush 이벤트 활성화 */
    event_active(pstDisp->pstFlushEvent, 0, 0);

    /* 요청 제거 */
    removeRequest(pstDisp, pstReq);
    if (pstReq->pstTimeoutEvent)
        event_free(pstReq->pstTimeoutEvent);
    free(pstReq);
}

static REQUEST_CONTEXT* createRequest(DISPATCHER_CONTEXT* pstDisp,
                                      SOCK_CONTEXT* pstTcpSock)
{
    REQUEST_CONTEXT* pstReq = calloc(1, sizeof(REQUEST_CONTEXT));
    pstReq->unRequestId     = (uint32_t)rand();
    pstReq->pstTcpRequester = pstTcpSock;
    pstReq->iPendingUds     = 0;
    pstReq->iRespLen        = 0;

    /* 리스트에 추가 */
    pstReq->pstNext = pstDisp->pstReqList;
    pstDisp->pstReqList = pstReq;

    /* 타임아웃 이벤트 생성 */
    struct timeval tv = { REQ_TIMEOUT_SEC, 0 };
    pstReq->pstTimeoutEvent = evtimer_new(
        pstDisp->pstBaseCtx->pstEventBase,
        timeoutCb,
        pstReq
    );
    if (pstReq->pstTimeoutEvent)
        evtimer_add(pstReq->pstTimeoutEvent, &tv);

    return pstReq;
}

/* ===== TxQueue Flush 이벤트 콜백 ===== */
/* 실제 bufferevent_write()는 여기서만 수행 → Dispatcher는 순수 라우터 역할 유지 */

static void flushTxQueueCb(evutil_socket_t fd, short events, void* arg)
{
    (void)fd; (void)events;
    DISPATCHER_CONTEXT* pstDisp = arg;

    TX_ITEM* item;
    while ((item = txQueuePop(&pstDisp->stTxQueue)) != NULL) {

        switch (item->eDest) {

        case TX_TO_TCP_ONE:
            if (item->pstTarget && item->pstTarget->pstBufferEvent) {
                bufferevent_write(item->pstTarget->pstBufferEvent,
                                  item->auchBuf, item->tLen);
            }
            break;

        case TX_TO_TCP_ALL:
            if (pstDisp->pstTcpServer) {
                SOCK_CONTEXT* c = pstDisp->pstTcpServer->pstClientList;
                while (c) {
                    if (c->pstBufferEvent)
                        bufferevent_write(c->pstBufferEvent,
                                          item->auchBuf, item->tLen);
                    c = c->pstNextSockCtx;
                }
            }
            break;

        case TX_TO_UDS_ONE:
            if (item->pstTarget && item->pstTarget->pstBufferEvent) {
                bufferevent_write(item->pstTarget->pstBufferEvent,
                                  item->auchBuf, item->tLen);
            }
            break;

        case TX_TO_UDS_ALL:
            if (pstDisp->pstUdsServer) {
                SOCK_CONTEXT* u = pstDisp->pstUdsServer->pstClientList;
                while (u) {
                    if (u->pstBufferEvent)
                        bufferevent_write(u->pstBufferEvent,
                                          item->auchBuf, item->tLen);
                    u = u->pstNextSockCtx;
                }
            }
            break;
        }

        free(item);
    }
}


/* ===== Public API 구현 ===== */

void dispatcherInit(DISPATCHER_CONTEXT* pstDisp,
                    BASE_CONTEXT* pstBase,
                    SERVER_CONTEXT* pstTcp,
                    SERVER_CONTEXT* pstUds)
{
    pstDisp->pstBaseCtx   = pstBase;
    pstDisp->pstTcpServer = pstTcp;
    pstDisp->pstUdsServer = pstUds;
    pstDisp->pstReqList   = NULL;

    txQueueInit(&pstDisp->stTxQueue);

    /* TxQueue flush용 이벤트 fd = -1, EV_TIMEOUT 유형으로 생성 */
    pstDisp->pstFlushEvent = event_new(
        pstBase->pstEventBase,
        -1,
        0,  /* EV_TIMEOUT 없이 event_active로 직접 트리거 */
        flushTxQueueCb,
        pstDisp
    );
}

void dispatcherOnTcpRequest(DISPATCHER_CONTEXT* pstDisp,
                            SOCK_CONTEXT* pstTcpSock,
                            const unsigned char* data,
                            int len)
{
    if (!pstDisp->pstUdsServer || pstDisp->pstUdsServer->iClientCount <= 0) {
        fprintf(stderr, "[Dispatcher] No UDS clients.\n");
        return;
    }

    REQUEST_CONTEXT* pstReq = createRequest(pstDisp, pstTcpSock);
    pstReq->iPendingUds = pstDisp->pstUdsServer->iClientCount;

    /* UDS 전체 브로드캐스트를 TxQueue에 enqueue */
    TX_ITEM* item = calloc(1, sizeof(TX_ITEM));
    item->eDest = TX_TO_UDS_ALL;
    item->tLen  = (size_t)len;
    memcpy(item->auchBuf, data, item->tLen);
    txQueuePush(&pstDisp->stTxQueue, item);

    /* Flush 이벤트 트리거 */
    event_active(pstDisp->pstFlushEvent, 0, 0);

    fprintf(stderr, "[Dispatcher] ReqId=%u broadcast to %d UDS\n",
            pstReq->unRequestId, pstReq->iPendingUds);
}

void dispatcherOnUdsResponse(DISPATCHER_CONTEXT* pstDisp,
                             SOCK_CONTEXT* pstUdsSock,
                             const unsigned char* data,
                             int len)
{
    (void)pstUdsSock;

    /* 가장 단순한 정책: "pendingUds > 0"인 첫 요청에 붙인다 */
    REQUEST_CONTEXT* pstReq = pstDisp->pstReqList;
    while (pstReq) {
        if (pstReq->iPendingUds > 0)
            break;
        pstReq = pstReq->pstNext;
    }
    if (!pstReq) {
        fprintf(stderr, "[Dispatcher] No pending requests.\n");
        return;
    }

    if (pstReq->iRespLen + len <= (int)sizeof(pstReq->auchRespBuf)) {
        memcpy(pstReq->auchRespBuf + pstReq->iRespLen, data, len);
        pstReq->iRespLen += len;
    } else {
        fprintf(stderr, "[Dispatcher] RespBuf overflow, trunc.\n");
    }

    pstReq->iPendingUds--;
    fprintf(stderr, "[Dispatcher] ReqId=%u pending=%d\n",
            pstReq->unRequestId, pstReq->iPendingUds);

    if (pstReq->iPendingUds <= 0) {
        /* 타임아웃 이벤트 해제 */
        if (pstReq->pstTimeoutEvent) {
            evtimer_del(pstReq->pstTimeoutEvent);
            event_free(pstReq->pstTimeoutEvent);
            pstReq->pstTimeoutEvent = NULL;
        }

        /* TCP ONE 전송을 TxQueue에 enqueue */
        TX_ITEM* item = calloc(1, sizeof(TX_ITEM));
        item->eDest    = TX_TO_TCP_ONE;
        item->pstTarget = pstReq->pstTcpRequester;
        item->tLen     = (size_t)pstReq->iRespLen;
        memcpy(item->auchBuf, pstReq->auchRespBuf, item->tLen);
        txQueuePush(&pstDisp->stTxQueue, item);

        event_active(pstDisp->pstFlushEvent, 0, 0);

        /* 요청 제거 */
        removeRequest(pstDisp, pstReq);
        free(pstReq);
    }
}

void dispatcherCleanup(DISPATCHER_CONTEXT* pstDisp)
{
    REQUEST_CONTEXT* pstCur = pstDisp->pstReqList;
    while (pstCur) {
        REQUEST_CONTEXT* pstNext = pstCur->pstNext;
        if (pstCur->pstTimeoutEvent) {
            evtimer_del(pstCur->pstTimeoutEvent);
            event_free(pstCur->pstTimeoutEvent);
        }
        free(pstCur);
        pstCur = pstNext;
    }
    pstDisp->pstReqList = NULL;

    /* TxQueue 내부 TX_ITEM free */
    TX_ITEM* item;
    while ((item = txQueuePop(&pstDisp->stTxQueue)) != NULL) {
        free(item);
    }

    if (pstDisp->pstFlushEvent) {
        event_free(pstDisp->pstFlushEvent);
        pstDisp->pstFlushEvent = NULL;
    }
}
