#ifndef EVENT_SOURCE_H
#define EVENT_SOURCE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <stdint.h>

struct _EVENT_ENGINE;
typedef struct _EVENT_ENGINE EVENT_ENGINE;

/* FD 타입 */
typedef enum {
    SRC_TYPE_TCP = 1,
    SRC_TYPE_UDS,
    SRC_TYPE_UDP,
    SRC_TYPE_UART,    
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
    struct event_base*  pstEventBase;

    struct bufferevent* pstBufferEvent;
    struct event*       pstEvent;

    int                 iFd;
    char                chMyId;
    IO_TYPE             eType;
    IO_ROLE             eRole;
    
    struct _IO_CHANNEL* pstNext;
} IO_CHANNEL;


/**
 * @brief bufferevent 기반 EVENT_SOURCE 생성 (TCP/UDS)
 */
IO_CHANNEL* eventSourceCreateWithBev(
    EVENT_ENGINE* pstEventEngine, int iFd,
    IO_TYPE eType, IO_ROLE eRole,
    bufferevent_data_cb  pfRead, bufferevent_event_cb pfEvent);
IO_CHANNEL* eventSourceCreateWithFd(EVENT_ENGINE* pstEventEngine, int iFd,
    IO_TYPE eType, IO_ROLE eRole,
    bufferevent_data_cb  pfRead, bufferevent_event_cb pfEvent);
void eventSourceDestroy(IO_CHANNEL* pstIoChannel);

#ifdef __cplusplus
}
#endif

#endif /* EVENT_SOURCE_H */
