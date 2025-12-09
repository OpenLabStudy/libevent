#ifndef EVENT_SOURCE_H
#define EVENT_SOURCE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <stdint.h>

/* 전방 선언 */
struct _DISPATCHER;
typedef struct _DISPATCHER DISPATCHER;

struct _BASE_CONTEXT;
typedef struct _BASE_CONTEXT BASE_CONTEXT;

/* FD 타입 */
typedef enum {
    SRC_TYPE_TCP_CLIENT = 1,
    SRC_TYPE_UDS_CLIENT,
    SRC_TYPE_UART,
    SRC_TYPE_UDP,
    SRC_TYPE_OTHER
} SRC_TYPE;

/* 역할 */
typedef enum {
    SRC_ROLE_NONE = 0,
    SRC_ROLE_REQUESTER,
    SRC_ROLE_WORKER
} SRC_ROLE;

/* 콜백 타입 */
struct _EVENT_SOURCE;
typedef struct _EVENT_SOURCE EVENT_SOURCE;

typedef void (*READ_CB)(struct bufferevent* pstBufferEvent, void* pvData);

typedef void (*EVENT_CB)(struct bufferevent* pstBufferEvent,
    short nEvents, void* pvData);


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
 * @brief FD 하나를 대표하는 EVENT_SOURCE
 */
struct _EVENT_SOURCE
{
    int iFd;                            /**< FD 번호 */

    struct bufferevent* pstBufferEvent;         /**< TCP/UDS용 */
    struct event*       pstEvent;       /**< UART/UDP 등 raw FD용 */

    SRC_TYPE eType;
    SRC_ROLE eRole;

    struct _EVENT_SOURCE* pstNext;      /**< DISPATCHER 리스트 */
    DISPATCHER*           pstDispatcher;/**< 소속 Dispatcher */
};


/**
 * @brief BASE_CONTEXT 초기화 (포인터만 세팅, event_base_new()는 외부에서)
 */
void baseContextInit(BASE_CONTEXT* pstCtx, uint16_t usMyId);

/**
 * @brief BASE_CONTEXT 정리 (signal, timer만 free, event_base는 외부에서 free)
 */
void baseContextCleanup(BASE_CONTEXT* pstCtx);

/**
 * @brief bufferevent 기반 EVENT_SOURCE 생성 (TCP/UDS)
 */

EVENT_SOURCE* eventSourceCreateWithBev(
    DISPATCHER*     pstDisp,
    int             fd,
    SRC_TYPE        eType,
    SRC_ROLE        eRole,
    bufferevent_data_cb      pfRead,
    bufferevent_event_cb     pfEvent);


EVENT_SOURCE* eventSourceCreateBevStandalone(
    struct event_base* base,
    int                fd,
    SRC_TYPE           eType,
    bufferevent_data_cb         pfRead,
    bufferevent_event_cb        pfEvent);

/**
 * @brief raw FD 기반 EVENT_SOURCE 생성 (UART/UDP 등)
 */
EVENT_SOURCE* eventSourceCreateWithFd(
    DISPATCHER*     pstDisp,
    int             fd,
    SRC_TYPE        eType,
    SRC_ROLE        eRole,
    event_callback_fn     pfEvent);


void dispatcherHandleRequest(DISPATCHER* pstDispatcher,
    EVENT_SOURCE* pstEventSrc,
    const unsigned char* puchData,
    int iLen);
/**
 * @brief EVENT_SOURCE 파괴 (FD close + bufferevent/event free + Dispatcher detach)
 */
void eventSourceDestroy(EVENT_SOURCE* src);

#ifdef __cplusplus
}
#endif

#endif /* EVENT_SOURCE_H */
