#ifndef TCP_H
#define TCP_H

#include "netContext.h"
#include "commonSession.h"

void tcpInit(NET_CTX* pstTcpCtx, struct event_base* pstBase, unsigned char uchMyId, NET_MODE eMode);
int  tcpServerStart(NET_CTX* pstTcpCtx, unsigned short unPort);
int  tcpClientConnect(NET_CTX* pstTcpCtx, const char* pchIp, unsigned short unPort);
void tcpClientAttachStdin(NET_CTX* pstTcpCtx);
void tcpStop(NET_CTX* pstTcpCtx);

#endif
