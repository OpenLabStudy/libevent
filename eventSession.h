#ifndef EVENT_SESSION_H
#define EVENT_SESSION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <event2/event.h>
#include <stdint.h>

/**
 * @brief 애플리케이션 공용 컨텍스트 (event_base + signal/timer)
 */
typedef struct _BASE_CONTEXT
{
    struct event_base* pstEventBase;   /**< libevent 메인 루프 */
    struct event*      pstSignalEvent; /**< SIGINT 등 */
    struct event*      pstMainTimer;   /**< 주기 타이머(옵션) */

    uint16_t           usMyId;         /**< 장비/프로세스 ID */
    void*              pvUserCtx;      /**< 사용자 확장용 포인터 */
} BASE_CONTEXT;

/**
 * @brief BASE_CONTEXT 초기화 (포인터만 세팅, event_base_new()는 외부에서)
 */
void baseContextInit(BASE_CONTEXT* pstCtx, uint16_t usMyId);

/**
 * @brief BASE_CONTEXT 정리 (signal, timer만 free, event_base는 외부에서 free)
 */
void baseContextCleanup(BASE_CONTEXT* pstCtx);

#ifdef __cplusplus
}
#endif

#endif /* EVENT_SESSION_H */
