/**
 * @file bridgeRouter.h
 * @brief TCP ↔ UDS 간 프레임 기반 라우팅 로직 모듈
 *
 * 이 모듈은 TCP로부터 수신된 명령을 분석 후 UDS 대상 클라이언트에
 * 브로드캐스트 전송하고, 그 응답을 TCP로 다시 돌려주는 역할을 수행한다.
 *
 * ### 처리 규칙
 * - TCP → UDS : Mask 기반 멀티캐스트 전송
 * - UDS → TCP : 단일 응답이지만 RequestContext에서 수집 후 TCP 반환
 *
 * ### 핵심 기능
 * - TCP Read Callback (`bridgeTcpReadCb()`)
 * - UDS Read Callback (`bridgeUdsReadCb()`)
 * - 이벤트 상태 처리 (`bridgeTcpEventCb()`, `bridgeUdsEventCb()`)
 *
 * @note TCP 요청은 항상 단일 outstanding model로 처리된다.
 */

#ifndef BRIDGE_ROUTER_H
#define BRIDGE_ROUTER_H

#include <event2/bufferevent.h>
#include "udsClientTable.h"
#include "requestContext.h"
#include "frame.h"
#include "eventSession.h"

/**
 * @struct BRIDGE_CONTEXT
 * @brief Unified routing environment shared by TCP + UDS event handlers
 */
typedef struct
{
    UDS_CLIENT_TABLE*       pstUdsTable;    /**< UDS client registry */
    REQUEST_CONTEXT*        pstReqCtx;      /**< pending request context */
    struct event_base*      pstEvBase;      /**< event loop instance */

    unsigned char           uchTcpSrcId;    /**< TCP-side logical ID */
    int                     iTimeoutMs;     /**< UDS wait timeout */

} BRIDGE_CONTEXT;


/* ========================================================================== */
/* APIs                                                                       */
/* ========================================================================== */

/**
 * @brief Bridge 환경 초기화
 */
void bridgeInit(BRIDGE_CONTEXT* pstCtx,
                UDS_CLIENT_TABLE* pstUdsTable,
                REQUEST_CONTEXT* pstReqCtx,
                struct event_base* pstEvBase,
                unsigned char uchTcpSrcId,
                int iTimeoutMs);

/**
 * @brief TCP Read Callback → UDS 전송 트리거
 */
void bridgeTcpReadCb(struct bufferevent* pstBev, void* pvData);

/**
 * @brief UDS Read Callback → TCP 응답 누적 처리
 */
void bridgeUdsReadCb(struct bufferevent* pstBev, void* pvData);

/**
 * @brief TCP 연결 이벤트 처리
 */
void bridgeTcpEventCb(struct bufferevent* pstBev, short events, void* ctx);

/**
 * @brief UDS 연결 이벤트 처리
 */
void bridgeUdsEventCb(struct bufferevent* pstBev, short events, void* ctx);

#endif // BRIDGE_ROUTER_H
 