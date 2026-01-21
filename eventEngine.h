#ifndef EVENT_ENGINE_H
#define EVENT_ENGINE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "eventSource.h"


struct _REQUEST_CONTEXT;
typedef struct _REQUEST_CONTEXT REQUEST_CONTEXT;
typedef struct event_base       EVENT_BASE;


#define WORKER_MASK(id) (1u << (id))

typedef struct _WORKER_INFO {
    int                 iWorkerId;
    struct evbuffer*    pstWorkerBuf;
} WORKER_INFO;

/* ============================================================
 * Request Context
 * ============================================================ */
typedef struct _REQUEST_CONTEXT {
    unsigned int                uiRequestId;
    IO_CHANNEL*                 pstTcpIoChannel;

    unsigned int                uiExpectedMask;
    unsigned int                uiReceivedMask;

    unsigned short              unCmd;
    unsigned int                uiWorkerCount;

    WORKER_INFO*                pstWorkerInfoList;

    /* finalize 분리용 */
    struct event*               pstFinalizeEvent;
    int                         iFinalizeQueued;

    /* timeout용 */
    struct event*               pstTimeoutEvent;
    int                         iTimedOut;

    struct _REQUEST_CONTEXT*    pstNextReqCtx;
} REQUEST_CONTEXT;


typedef struct _EVENT_ENGINE {
    struct event_base*  pstEventBase;
    IO_CHANNEL*         pstIoChannelList;
    REQUEST_CONTEXT*    pstReqList;
    struct event*       pstFlushEvent;
    unsigned int        uiRequestSeq;
    unsigned int        uiMaxWorkers;
    void*               pvSharedData;
} EVENT_ENGINE;

/* API */
void eventEngineInit(EVENT_ENGINE* pstEventEngine, unsigned int uiMaxWorkers);
void eventEngineCleanup(EVENT_ENGINE* pstEventEngine);

void eventEngineAttachSource(EVENT_ENGINE* pstEventEngine, IO_CHANNEL* pstIoChannel);

void eventEngineHandleRequest(int iFd, short nEvent, void* pvData);

void eventEngineHandleWorkerResponse(EVENT_ENGINE* pstEventEngine, IO_CHANNEL* pstIoChannel,
                                    int iReqId, const unsigned char* puchData, int iLen);

#ifdef __cplusplus
}
#endif

#endif
