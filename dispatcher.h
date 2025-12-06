/* dispatcher.h */

#ifndef DISPATCHER_H
#define DISPATCHER_H

#include "eventSession.h"
#include "txQueue.h"
#include <stdint.h>

typedef struct _REQUEST_CONTEXT REQUEST_CONTEXT;


/* Dispatcher 인스턴스 */
typedef struct _DISPATCHER_CONTEXT
{
    BASE_CONTEXT*    pstBaseCtx;
    SERVER_CONTEXT*  pstTcpServer;
    SERVER_CONTEXT*  pstUdsServer;

    REQUEST_CONTEXT* pstReqList;      /**< 요청 리스트 head */

    TX_QUEUE         stTxQueue;       /**< 전송 결정 큐 */
    struct event*    pstFlushEvent;   /**< TxQueue flush 이벤트 */

} DISPATCHER_CONTEXT;


/* 요청 단위 컨텍스트 */
struct _REQUEST_CONTEXT
{
    uint32_t         unRequestId;
    SOCK_CONTEXT*    pstTcpRequester;

    int              iPendingUds;
    unsigned char    auchRespBuf[4096];
    int              iRespLen;

    struct event*    pstTimeoutEvent;  /**< 옵션: 타임아웃 관리 */

    REQUEST_CONTEXT* pstNext;
};

void dispatcherInit(DISPATCHER_CONTEXT* pstDispCtx,
                    BASE_CONTEXT* pstBase,
                    SERVER_CONTEXT* pstTcp,
                    SERVER_CONTEXT* pstUds);

void dispatcherOnTcpRequest(DISPATCHER_CONTEXT* pstDispCtx,
                            SOCK_CONTEXT* pstTcpSock,
                            const unsigned char* puchData,
                            int iLength);

void dispatcherOnUdsResponse(DISPATCHER_CONTEXT* pstDispCtx,
                             SOCK_CONTEXT* pstUdsSock,
                             const unsigned char* puchData,
                             int iLength);

void dispatcherCleanup(DISPATCHER_CONTEXT* pstDispCtx);

#endif /* DISPATCHER_H */
