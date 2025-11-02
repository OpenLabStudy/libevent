#ifndef NET_CONTEXT_H
#define NET_CONTEXT_H


#include "commonSession.h"
#include <event2/listener.h>
#include <string.h>

typedef enum {
    TCP_SERVER,
    TCP_CLIENT,
    UDP_MODE,
    UDS_SERVER,
    UDS_CLIENT
} NET_MODE;

typedef struct {
    CORE_CTX        stCoreCtx;
    NET_MODE        eMode;
    int             iSockFd;
    unsigned char   uchMyId;
} NET_BASE;

typedef struct {
    NET_BASE                stNetBase;
    struct evconnlistener   *pstListener;
} TCP_SERVER_CTX;

typedef struct {
    NET_BASE            stNetBase;
    struct bufferevent  *pstBufferEvent;
} TCP_CLIENT_CTX;

typedef struct {
    NET_BASE        stNetBase;
    struct event    *pstRecvEvent;
} UDP_CTX;

typedef struct {
    NET_BASE        stNetBase;
    struct event    *pstClnConnectEvent;
} UDS_SERVER_CTX;

typedef struct {
    NET_BASE            stNetBase;
    struct bufferevent  *pstBufferEvent;
} UDS_CLIENT_CTX;

/* ============================================================
 * 공용 초기화 함수
 * ============================================================ */
static inline void netBaseInit(NET_BASE* pstNetBase,
    struct event_base* pstEventBase,
    unsigned char uchMyId,
    NET_MODE eMode)
{
    memset(pstNetBase, 0, sizeof(*pstNetBase));
    sessionInitCore(&pstNetBase->stCoreCtx, pstEventBase);
    pstNetBase->eMode = eMode;
    pstNetBase->uchMyId = uchMyId;
}

#endif
