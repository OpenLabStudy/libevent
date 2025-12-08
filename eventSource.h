#ifndef EVENT_SOURCE_H
#define EVENT_SOURCE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <event2/event.h>
#include <event2/bufferevent.h>
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

typedef void (*ES_READ_CB)(struct bufferevent* pstBufferEvent, void* pvData);

typedef void (*ES_EVENT_CB)(struct bufferevent* pstBufferEvent,
    short nEvents, void* pvData);

/**
 * @brief FD 하나를 대표하는 EVENT_SOURCE
 */
struct _EVENT_SOURCE
{
    int iFd;                            /**< FD 번호 */

    struct bufferevent* pstBev;         /**< TCP/UDS용 */
    struct event*       pstEvent;       /**< UART/UDP 등 raw FD용 */

    SRC_TYPE eType;
    SRC_ROLE eRole;

    struct _EVENT_SOURCE* pstNext;      /**< DISPATCHER 리스트 */
    DISPATCHER*           pstDispatcher;/**< 소속 Dispatcher */
};

/**
 * @brief bufferevent 기반 EVENT_SOURCE 생성 (TCP/UDS)
 */
EVENT_SOURCE* eventSourceCreateWithBev(
    DISPATCHER*     pstDisp,
    int             fd,
    SRC_TYPE        eType,
    SRC_ROLE        eRole,
    ES_READ_CB      pfRead,
    ES_EVENT_CB     pfEvent);


EVENT_SOURCE* eventSourceCreateBevStandalone(
    struct event_base* base,
    int                fd,
    SRC_TYPE           eType,
    ES_READ_CB         pfRead,
    ES_EVENT_CB        pfEvent);

/**
 * @brief raw FD 기반 EVENT_SOURCE 생성 (UART/UDP 등)
 */
EVENT_SOURCE* eventSourceCreateWithFd(
    DISPATCHER*     pstDisp,
    int             fd,
    SRC_TYPE        eType,
    SRC_ROLE        eRole,
    ES_READ_CB      pfRead,
    ES_EVENT_CB     pfEvent);


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
