#ifndef EVENT_ENGINE_H
#define EVENT_ENGINE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <event2/event.h>
#include "eventSource.h"


struct _REQUEST_CONTEXT;
typedef struct _REQUEST_CONTEXT REQUEST_CONTEXT;
typedef struct event_base       EVENT_BASE;

/* ================================================================ */
/* 요청 상태                                                         */
/* ================================================================ */
typedef enum {
    WORKER_NONE = 0,
    WORKER_GPS,
    WORKER_IMU,
    WORKER_SP,
    WORKER_EXTERN,
    WORKER_MAX
} WORKER_ID;

#define WORKER_MASK(id) (1u << (id))


/* ============================================================
 * Request Context
 * ============================================================ */
// struct _REQUEST_CONTEXT {
//     REQ_STATE           eState;
//     unsigned int        uiRequestId;
//     int                 iPendingCount;
//     IO_CHANNEL*         pstIoReqList;
//     struct event*       pstTimeoutEvent;
//     struct evbufer*     pstRespEvBuffer;    
//     REQUEST_CONTEXT*    pstNextReqCtx;
// };
typedef struct _REQUEST_CONTEXT {
    unsigned int uiRequestId;
    IO_CHANNEL* pstTcpIoChannel;

    unsigned int uiExpectedMask;
    unsigned int uiReceivedMask;

    struct evbuffer* apstWorkerBuf[WORKER_MAX];

    /* finalize 분리용 */
    struct event* pstFinalizeEvent;
    int iFinalizeQueued;

    /* timeout용 */
    struct event* pstTimeoutEvent;
    int iTimedOut;

    struct _REQUEST_CONTEXT* pstNextReqCtx;
} REQUEST_CONTEXT;




typedef struct __attribute__((__packed__)) {
    unsigned short  unStx;          /**< 프레임 시작(STX) 값 */
    unsigned int    uiRequestId;
    unsigned short  unCmd;          /**< 명령 코드 */ 
    unsigned char   auchResult[64];
    int             iResultSize;
    unsigned short  unEtx;    /**< 프레임 종료(ETX) 값 */
} IPC_FRAME;


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

void eventEngineHandleRequest(int iFd, short nEvent, void* pvData);

// void eventEngineHandleWorkerResponse(EVENT_ENGINE* pstEventEngine,
//                                     IO_CHANNEL* pstIoChannel,
//                                     const unsigned char* puchData, int iLen);

#ifdef __cplusplus
}
#endif

#endif
