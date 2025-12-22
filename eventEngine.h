#ifndef EVENT_ENGINE_H
#define EVENT_ENGINE_H

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
    REQ_STATE           eState;
    unsigned int        uiRequestId;
    int                 iPendingCount;
    IO_CHANNEL*         pstIoReqList;
    struct event*       pstTimeoutEvent;
    struct evbufer*     pstRespEvBuffer;    
    REQUEST_CONTEXT*    pstNextReqCtx;
};


typedef struct _EVENT_ENGINE {
    struct event_base*  pstEventBase;
    IO_CHANNEL*         pstIoChannelList;
    REQUEST_CONTEXT*    pstReqList;
    struct event*       pstFlushEvent;
    unsigned int        uiRequestSeq;
    void*               pvSharedData;
} EVENT_ENGINE;

/* API */
void eventEngineInit(EVENT_ENGINE* pstEventEngine);
void eventEngineCleanup(EVENT_ENGINE* pstEventEngine);

void eventEngineAttachSource(EVENT_ENGINE* pstEventEngine, IO_CHANNEL* pstIoChannel);

void eventEngineHandleRequest(EVENT_ENGINE* pstEventEngine,
                             IO_CHANNEL* pstIoChannel,
                             const unsigned char* puchData, int iLen);

void eventEngineHandleWorkerResponse(EVENT_ENGINE* pstEventEngine,
                                    IO_CHANNEL* pstIoChannel,
                                    const unsigned char* puchData, int iLen);

#ifdef __cplusplus
}
#endif

#endif
