#ifndef EVENT_SOURCE_H
#define EVENT_SOURCE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <stdint.h>

/* FD 타입 */
typedef enum {
    SRC_TYPE_TCP_CLIENT = 1,
    SRC_TYPE_UDS_CLIENT,
    SRC_TYPE_UART,
    SRC_TYPE_UDP,
    SRC_TYPE_OTHER
} IO_TYPE;

/* 역할 */
typedef enum {
    SRC_ROLE_NONE = 0,
    SRC_ROLE_REQUESTER,
    SRC_ROLE_WORKER
} IO_ROLE;

typedef struct _IO_CHANNEL IO_CHANNEL;
typedef struct _IO_CHANNEL {
    int                 iFd;
    struct event_base*  pstEventBase;
    struct bufferevent* pstBufferEvent;

    IO_TYPE             eType;
    IO_ROLE             eRole;
    
    struct _IO_CHANNEL* pstNext;
} IO_CHANNEL;


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
    IO_TYPE        eType,
    IO_ROLE        eRole,
    bufferevent_data_cb      pfRead,
    bufferevent_event_cb     pfEvent);


EVENT_SOURCE* eventSourceCreateBevStandalone(
    struct event_base* base,
    int                fd,
    IO_TYPE           eType,
    bufferevent_data_cb         pfRead,
    bufferevent_event_cb        pfEvent);

/**
 * @brief raw FD 기반 EVENT_SOURCE 생성 (UART/UDP 등)
 */
EVENT_SOURCE* eventSourceCreateWithFd(
    DISPATCHER*     pstDisp,
    int             fd,
    IO_TYPE        eType,
    IO_ROLE        eRole,
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
