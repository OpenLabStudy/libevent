#ifndef UDP_H
#define UDP_H

#include "commonSession.h"
#include "netContext.h"

typedef struct {
    CORE_CTX stCoreCtx;
    NET_MODE eMode;
    int iSockFd;
    struct event* pstRecvEvent;
    struct event* pstStdinEvent;
    struct sockaddr_in stSrvAddr;
} UDP_CTX;

void udpInit(UDP_CTX* pstUdpCtx, struct event_base* pstBase, unsigned char uchMyId, NET_MODE eMode);
int  udpServerStart(UDP_CTX* pstUdpCtx, unsigned short unPort);
int  udpClientStart(UDP_CTX* pstUdpCtx, const char* ip, unsigned short srvPort, unsigned short myPort);
void udpStop(UDP_CTX* pstUdpCtx);

#endif
