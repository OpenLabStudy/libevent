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
typedef struct _IO_CHANNEL IO_CHANNEL;

/* FD 타입 */
typedef enum {
    TYPE_TCP_SVR = 1,
    TYPE_TCP_CLI,
    TYPE_UDS_SVR,
    TYPE_UDS_CLI,
    TYPE_UDP_SVR,
    TYPE_UDP_CLI,
    TYPE_UART,    
    TYPE_OTHER
} IO_TYPE;

/* 역할 */
typedef enum {
    ROLE_NONE = 0,
    ROLE_REQUESTER,
    ROLE_WORKER
} IO_ROLE;

typedef enum {
    IO_EVENT_NONE = 0,
    IO_EVT_RX_DATA,
    IO_EVT_TX_READY,
    IO_EVT_CHANNEL_CLOSED,
    IO_EVT_ERROR
} IO_EVENT_TYPE;


typedef struct _IO_CHANNEL {
    int                 iFd;
    char                chFdCloseSet;
    int                 iWorkerId;
    IO_TYPE             eType;
    IO_ROLE             eRole;

    struct event*       pstReadEvent;
    struct event*       pstWriteEvent;
    struct evbuffer*    pstReadBuffer;
    struct evbuffer*    pstWriteBuffer;

    struct event*       pstRequestEvent;
    struct evbuffer*    pstRequestBuffer;

    struct event*       pstShutdownEvent;
    struct event*       pstLogicEvent;
    IO_EVENT_TYPE       ePendingLogicEvent;
    struct _IO_CHANNEL* pstNextIoChannel;
    EVENT_ENGINE*       pstEventEngine;
} IO_CHANNEL;

/**
 * @brief bufferevent 기반 EVENT_SOURCE 생성 (TCP/UDS)
 */
IO_CHANNEL* eventSourceCreateWithBev( EVENT_ENGINE* pstEventEngine, int iFd,
    IO_TYPE eType, IO_ROLE eRole, 
    event_callback_fn pfRead, event_callback_fn pfWrite, event_callback_fn pfEvent);
    
void eventSourceDestroy(IO_CHANNEL* pstIoChannel);

#ifdef __cplusplus
}
#endif

#endif /* EVENT_SOURCE_H */
