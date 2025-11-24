/**
 * @file requestContext.h
 * @brief 전송 요청 및 응답 수집 관리를 수행하는 상태 컨트롤 모듈
 *
 * 이 모듈은 TCP를 통해 전달된 요청을 UDS 클라이언트에게 전달하고,
 * 비트마스크로 설정된 타겟 클라이언트의 응답을 수집/검증한다.
 *
 * ### 처리 단계
 * 1. `reqCtxStart()` 호출 → 요청 정보 등록 + 타임아웃 설치
 * 2. UDS 응답 도착 → `reqCtxOnUdsResponse()` 호출
 * 3. 모든 응답 수신 또는 타임아웃 → `reqCtxEvaluateAndComplete()` 호출
 *
 * ### 타임아웃 모델
 * - 지정된 시간 내 모든 대상이 응답해야 성공으로 처리
 *
 * @see bridgeRouter.c
 */

#ifndef REQUEST_CONTEXT_H
#define REQUEST_CONTEXT_H

#include <event2/event.h>
#include <stdbool.h>
#include "frame.h"


/**
 * @enum REQUEST_STATE
 * @brief 요청 상태 머신 정의
 */
typedef enum
{
    REQ_IDLE = 0,     /**< 요청 없음 */
    REQ_WAITING,      /**< 응답 대기 중 */
    REQ_SUCCESS,      /**< 모든 응답 정상 */
    REQ_FAILED,       /**< 응답은 있으나 실패 발생 */
    REQ_TIMEOUT       /**< 제한 시간 내 응답 부족 */
} REQUEST_STATE;


/**
 * @struct REQUEST_CONTEXT
 * @brief TCP → UDS 브로드캐스트 요청 처리 상태 구조체
 */
typedef struct
{
    bool                    bActive;                    /**< 활성화 여부 */
    REQUEST_STATE           eState;                     /**< 현재 요청 상태 */

    unsigned short          unCmdCode;                  /**< 명령 코드 */
    unsigned int            unTargetMask;               /**< 브로드캐스트 대상(UDS Client Bitmask) */

    int                     iTotalTargetCount;          /**< 명령 대상 총 개수 */
    int                     iResponseCount;             /**< 응답 수신 카운트 */

    bool                    abSuccess[32];              /**< UDS별 성공 여부 저장 */

    unsigned char           auchTcpSrcId;               /**< TCP→UDS 메시지 Src ID */
    struct bufferevent*     pstTcpBev;                  /**< TCP 송신 대상 */

    struct event*           pstTimeoutEvent;            /**< 타임아웃 이벤트 */
    struct event_base*      pstEvBase;                 /**< 이벤트 루프 */

    int                     iTimeoutMs;                 /**< 제한 시간(ms) */

} REQUEST_CONTEXT;


/* ========================================================================== */
/* API Prototypes                                                             */
/* ========================================================================== */

/**
 * @brief 요청 Context 초기화
 *
 * @param[out] pstCtx 초기화 대상 Context
 */
void reqCtxInit(REQUEST_CONTEXT* pstCtx);


/**
 * @brief 새 요청 시작
 *
 * @param pstCtx        Context 포인터
 * @param unCmd         요청 명령 코드
 * @param unMask        대상 UDS 비트마스크
 * @param pstTcpBev     TCP bufferevent 핸들
 * @param pEventBase    Libevent 기반
 * @param iTimeoutMs    제한 시간
 * @return true 성공 / false 실패(이미 요청 진행 중)
 */
bool reqCtxStart(REQUEST_CONTEXT* pstCtx,
                unsigned short unCmd,
                unsigned int unMask,
                struct bufferevent* pstTcpBev,
                struct event_base* pEventBase,
                int iTimeoutMs);


/**
 * @brief UDS 응답 도착 시 호출되는 함수
 *
 * @param pstCtx    Context 포인터
 * @param uchClientId 응답한 UDS Client ID
 * @param bResult   true=성공 / false=실패
 */
void reqCtxOnUdsResponse(REQUEST_CONTEXT* pstCtx,
                        unsigned char uchClientId,
                        bool bResult);


/**
 * @brief 타임아웃 또는 모든 응답 도착 시 최종 결과 판단
 *
 * @param pstCtx Context 포인터
 * @return REQUEST_STATE 최종 상태
 */
REQUEST_STATE reqCtxEvaluateAndComplete(REQUEST_CONTEXT* pstCtx);

#endif // REQUEST_CONTEXT_H
 