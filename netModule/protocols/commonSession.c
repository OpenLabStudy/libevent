#include "commonSession.h"
#include "../core/frame.h"
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

void sessionAdd(SESSION_CTX *pstSessionCtx, CORE_CTX *pstCoreCtx)
{
    if (!pstSessionCtx || !pstSessionCtx->pstCoreCtx)
        return;
    pstSessionCtx->pstSockCtxNext               = pstSessionCtx->pstCoreCtx->pstSockCtxHead;
    pstSessionCtx->pstCoreCtx->pstSockCtxHead   = pstSessionCtx;
    pstSessionCtx->pstCoreCtx->iClientCount++;
}

void sessionRemove(SESSION_CTX* pstSessionCtx)
{
    if (!pstSessionCtx || !pstSessionCtx->pstCoreCtx)
        return;

    CORE_CTX* pstCoreCtx = pstSessionCtx->pstCoreCtx;
    SESSION_CTX** ppSessionCtx = &pstCoreCtx->pstSockCtxHead;
    while (*ppSessionCtx) {
        if (*ppSessionCtx == pstSessionCtx) {
            *ppSessionCtx = pstSessionCtx->pstSockCtxNext;
            pstCoreCtx->iClientCount--;
            return;
        }
        ppSessionCtx = &(*ppSessionCtx)->pstSockCtxNext;
    }
}

void sessionInitCore(CORE_CTX* pstCoreCtx, struct event_base* pstEventBase, unsigned char uchMyId)
{
    pstCoreCtx->pstEventBase = pstEventBase;
    pstCoreCtx->pstAcceptEvent = NULL;
    pstCoreCtx->iListenSock = -1;
    pstCoreCtx->iClientCount = 0;
    pstCoreCtx->uchMyId = uchMyId;
    pstCoreCtx->pstSockCtxHead = NULL;
}

void sessionCloseAndFree(void* pvData)
{
    SESSION_CTX* pSessionCtx = (SESSION_CTX*)pvData;
    if (!pSessionCtx)
        return;

    session_remove(pSessionCtx);
    if (pSessionCtx->pstBufferEvent) 
        bufferevent_free(pSessionCtx->pstBufferEvent);

    free(pSessionCtx);
}

void sessionReadCallback(struct bufferevent* pstBufferEvent, void* pvData)
{
    SESSION_CTX* pSessionCtx = (SESSION_CTX*)pvData;
    struct evbuffer* pstEventBuffer = bufferevent_get_input(pstBufferEvent);
    MSG_ID id; 
    id.uchSrcId = pSessionCtx->uchSrcId; 
    id.uchDstId = pSessionCtx->uchDstId;

    for (;;) {
        int r = responseFrame(pstEventBuffer, pstBufferEvent, &id, pSessionCtx->uchIsResponse);
        if (r == 1) break;     /* 더 읽을 게 없음 */
        if (r == 0) break;     /* 한 프레임 처리 완료 */
        if (r < 0) {           /* 에러: 연결 종료 */
            session_close_and_free(pvData);
            return;
        }
    }
}

void sessionEventCallback(struct bufferevent* pstBufferEvent, short nEvents, void* pvData)
{
    (void)pstBufferEvent;
    if (nEvents & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
        session_close_and_free(pvData);
    }
}
