#ifndef SOCK_SESSION_H
#define SOCK_SESSION_H

#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <arpa/inet.h>

#include <event2/bufferevent.h>  
#include <event2/listener.h>
#include <event2/buffer.h>
#include <event2/util.h>

#define TCP_SERVER_ADDR         "127.0.0.1"
#define TCP_SERVER_PORT         9990
#define UDP_SERVER_ADDR         "127.0.0.1"
#define UDP_SERVER_PORT         9991
#define UDP_CLIENT_PORT         9992

#define RESPONSE_ENABLED        0x01
#define RESPONSE_DISABLED       0x00

#define READ_HIGH_WM            (1u * 1024u * 1024u)   /* 1MB */
#define MAX_PAYLOAD             (4u * 1024u * 1024u)   /* 4MB */
#define MAX_RETRY               1

#define TCP_TRACKING_CTRL_ID    0xB1
#define TCP_OPERATOR_PC_ID      0x10

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

/** @brief Socket Type 구분용 */
typedef enum {
    SOCK_TYPE_NONE = 0,         ///< 초기값 / 미지정
    SOCK_TYPE_TCP,              ///< TCP 소켓
    SOCK_TYPE_UDP,              ///< UDP 소켓
    SOCK_TYPE_UDP_NOT_CONNECT,  ///< UDP 소켓
} SOCK_TYPE;

typedef struct event_context    EVENT_CONTEXT;
typedef struct sock_context     SOCK_CONTEXT;
/* === Per-connection context === */
struct sock_context{
    struct bufferevent*  pstBufferEvent;
    EVENT_CONTEXT*      pstEventCtx;
    unsigned short      unCmd;
    int                 iDataLength;

    unsigned char       uchSrcId;
    unsigned char       uchDstId;
    unsigned char       uchIsResponse;

    unsigned short      unPort;
    char                achSockAddr[INET6_ADDRSTRLEN];
    
    SOCK_CONTEXT        *pstNextSockCtx;
} ;

struct event_context{
    APP_ROLE            eRole;             // ★ 서버/클라 구분
    int                 iSockFd;
    struct event_base*  pstEventBase;
    struct event*       pstEvent;
    struct event*       pstAcceptEvent;
    
    SOCK_CONTEXT*       pstSockCtx;
    int                 iClientCount;
    unsigned char       uchMyId;
};

void initEventContext(EVENT_CONTEXT* pstEventCtx, APP_ROLE eAppRole, unsigned char uchMyId);
void initSocketContext(SOCK_CONTEXT* pstSockCtx, char* pchSockAddr, unsigned short unPort, unsigned char uchIsResponse);
void acceptCb(int iListenFd, short nKindOfEvent, void* pvData);
void readCallback(struct bufferevent*, void*);
void eventCallback(struct bufferevent*, short, void*);
void closeAndFree(void *pvData);
int createTcpUdpServerSocket(SOCK_CONTEXT* pstSockCtx, SOCK_TYPE eSockType);
int createTcpUdpClientSocket(SOCK_CONTEXT* pstSockCtx, SOCK_TYPE eSockType);
int createUdsServerSocket(char* chAddr);
int createUdsClientSocket(char* chAddr);
#endif