#ifndef SOCK_SESSION_H
#define SOCK_SESSION_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <arpa/inet.h>

#include <event2/bufferevent.h>  
#include <event2/listener.h>
#include <event2/buffer.h>
#include <event2/util.h>

#define DEFAULT_PORT            9995
#define READ_HIGH_WM            (1u * 1024u * 1024u)   /* 1MB */
#define MAX_PAYLOAD             (4u * 1024u * 1024u)   /* 4MB */
#define MAX_RETRY               1

#define TCP_TRACKING_CTRL_ID    0xB1

#define UDS1_PATH               "/tmp/uds1Command.sock"
#define UDS1_SERVER_ID          0x10
#define UDS1_CLIENT1_ID         0x11
#define UDS1_CLIENT2_ID         0x12
#define UDS1_CLIENT3_ID         0x13

#define UDS2_PATH               "/tmp/uds2SensorData.sock"
#define UDS2_SERVER_ID          0x20
#define UDS2_CLIENT1_ID         0x21
#define UDS2_CLIENT2_ID         0x22
#define UDS2_CLIENT3_ID         0x23

#define UDS3_PATH               "/tmp/uds3SensorResult.sock"
#define UDS3_SERVER_ID          0x30
#define UDS3_CLIENT1_ID         0x31
#define UDS3_CLIENT2_ID         0x32
#define UDS3_CLIENT3_ID         0x33

#define UDS4_PATH               "/tmp/uds4AzElData.sock"
#define UDS4_SERVER_ID          0x40
#define UDS4_CLIENT1_ID         0x41
#define UDS4_CLIENT2_ID         0x42
#define UDS4_CLIENT3_ID         0x43


typedef enum { ROLE_SERVER = 0, ROLE_CLIENT = 1 } APP_ROLE;

typedef struct app_ctx      EVENT_CONTEXT;
typedef struct sock_context SOCK_CONTEXT;
/* === Per-connection context === */
struct sock_context{
    struct bufferevent*  pstBufferEvent;
    EVENT_CONTEXT*      pstEventCtx;
    unsigned short      unCmd;
    int                 iDataLength;

    unsigned char       uchSrcId;
    unsigned char       uchDstId;
    unsigned char       uchIsRespone;

    unsigned short      unPort;
    char                achSockAddr[INET6_ADDRSTRLEN];
    
    SOCK_CONTEXT        *pstNextSockCtx;
} ;

struct app_ctx{
    APP_ROLE            eRole;             // ★ 서버/클라 구분
    int                 iListenFd;
    struct event_base*  pstEventBase;
    struct event*       pstEvent;
    struct event*       pstAcceptEvent;
    
    SOCK_CONTEXT*       pstSockCtx;
    int                 iClientCount;
    unsigned char       uchMyId;
};

void acceptCb(int iListenFd, short nKindOfEvent, void* pvData);
void listenerCb(struct evconnlistener*, evutil_socket_t, struct sockaddr*, int, void*);
void readCallback(struct bufferevent*, void*);
void eventCallback(struct bufferevent*, short, void*);
void closeAndFree(void *pvData);
int createTcpListenSocket(char* chAddr, unsigned short unPort);
int createUdsListenSocket(char* chAddr);
#endif