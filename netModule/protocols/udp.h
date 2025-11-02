#ifndef UDP_H
#define UDP_H

#include "commonSession.h"
#include "netContext.h"

void udpInit(UDP_CTX* pstUdpCtx, struct event_base* pstEventBase, unsigned char uchMyId, NET_MODE eMode);
int  udpServerStart(UDP_CTX* pstUdpCtx, unsigned short unPort);
int  udpClientStart(UDP_CTX* pstUdpCtx, const char* pchIpAddr, unsigned short unSvrPort, unsigned short unMyPort);
void udpStop(UDP_CTX* pstUdpCtx);

#endif
