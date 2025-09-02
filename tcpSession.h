#ifndef TCP_SESSION_H
#define TCP_SESSION_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <event2/bufferevent.h>  
#include <event2/listener.h>
#include <event2/buffer.h>
#include <event2/util.h>

#define DEFAULT_PORT        9995
#define READ_HIGH_WM        (1u * 1024u * 1024u)   /* 1MB */
#define MAX_PAYLOAD         (4u * 1024u * 1024u)   /* 4MB */
#define MAX_RETRY           1

/* === Per-connection context === */
typedef struct {
    struct bufferevent  *pstBufferEvent;

    unsigned short      unCmd;
    int                 iDataLength;

    unsigned char       uchSrcId;
    unsigned char       uchDstId;
    unsigned char       uchIsRespone;

    unsigned short      unPort;
    char                achTcpIpInfo[INET6_ADDRSTRLEN];
} TCP_CONTEXT;

typedef struct app_ctx{
    struct event_base       *pstEventBase;
    struct evconnlistener   *pstEventListener;
    struct event            *pstEvent;
    TCP_CONTEXT             *pstTcpCtx;
} EVENT_CONTEXT;

void tcpListenerCb(struct evconnlistener*, evutil_socket_t, struct sockaddr*, int, void*);
void tcpReadCb(struct bufferevent *pstBufferEvent, void *pvData);
void tcpEventCb(struct bufferevent *bev, short nEvents, void *pvData);
void tcpCloseAndFree(TCP_CONTEXT* pstTcpCtx);

#endif