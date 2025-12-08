#ifndef DISPATCHER_H
#define DISPATCHER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <event2/event.h>

#include "txQueue.h"
#include "eventSession.h"

struct _REQUEST_CONTEXT;
typedef struct _REQUEST_CONTEXT REQUEST_CONTEXT;

/* ============================================================
 * Request Context
 * ============================================================ */
struct _REQUEST_CONTEXT {
    unsigned int    uiRequestId;
    EVENT_SOURCE    *pstEventSrcReqest;

    int             iPending;
    unsigned char   auchRespBuf[4096];
    int             iRespLen;

    struct event    *pstTimeoutEvent;

    REQUEST_CONTEXT *pstNext;
};

/* ============================================================
 * Dispatcher 구조체
 * ============================================================ */
typedef struct _DISPATCHER {
    BASE_CONTEXT*    pstBaseCtx;

    EVENT_SOURCE*    pstEventSrc;
    REQUEST_CONTEXT* pstReqList;

    TX_QUEUE         stTxQueue;
    struct event*    pstFlushEvent;

    /* <-- 전역변수 제거 후, 각 Dispatcher마다 독립적인 시퀀스 */
    uint32_t         unReqSeq;

} DISPATCHER;

/* API */
void dispatcherInit(DISPATCHER* pstDisp, BASE_CONTEXT* pstBase);
void dispatcherCleanup(DISPATCHER* pstDisp);

void dispatcherAttachSource(DISPATCHER* pstDisp, EVENT_SOURCE* pstSrc);
void dispatcherDetachSource(DISPATCHER* pstDisp, EVENT_SOURCE* pstSrc);

void dispatcherHandleRequest(DISPATCHER* pstDisp,
                             EVENT_SOURCE* pstRequester,
                             const unsigned char* data, int len);

void dispatcherHandleWorkerResponse(DISPATCHER* pstDisp,
                                    EVENT_SOURCE* pstWorker,
                                    const unsigned char* data, int len);

#ifdef __cplusplus
}
#endif

#endif /* DISPATCHER_H */
