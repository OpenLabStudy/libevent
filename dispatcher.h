#ifndef DISPATCHER_H
#define DISPATCHER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <event2/event.h>
#include "eventSource.h"

#include "txQueue.h"

struct _REQUEST_CONTEXT;
typedef struct _REQUEST_CONTEXT REQUEST_CONTEXT;
typedef struct event_base       EVENT_BASE;

/* ================================================================ */
/* 요청 상태                                                         */
/* ================================================================ */
typedef enum {
    REQ_STATE_WAITING = 0,     /* UDS 응답 대기 중 */
    REQ_STATE_COMPLETED,       /* 응답 수신 완료 */
    REQ_STATE_TIMEOUT          /* 타임아웃 발생 */
} REQ_STATE;

/* ============================================================
 * Request Context
 * ============================================================ */
struct _REQUEST_CONTEXT {
    unsigned int    uiRequestId;
    EVENT_SOURCE    *pstEventSrcReqest;

    struct event    *pstTimeoutEvent;

    unsigned char   auchRespBuf[4096];
    int             iRespLen;

    REQ_STATE        iState;               /* 요청 상태 */    

    REQUEST_CONTEXT *pstNext;
};


typedef struct _EVENT_ENGINE {
    struct event_base*  pstEventBase;
    IO_CHANNEL*         pstIoChannelList;
    REQUEST_CONTEXT*    pstReqList;
    TX_QUEUE            stTxQueue;
    struct event*       pstFlushEvent;
    unsigned int        uiRequestSeq;
} EVENT_ENGINE;

/* API */
void eventEngineInit(EVENT_ENGINE* pstEventEngine, EVENT_BASE* pstEventBase);
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
