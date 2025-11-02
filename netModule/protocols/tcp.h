#ifndef TCP_H
#define TCP_H

#include "netContext.h"
#include "commonSession.h"

void tcpSvrInit(TCP_SERVER_CTX* pstTcpCtx, struct event_base* pstEventBase,
        unsigned char uchMyId, NET_MODE eMode);
int  tcpServerStart(TCP_SERVER_CTX* pstTcpCtx, unsigned short unPort);
void tcpSvrStop(TCP_SERVER_CTX *pstTcpCtx);

void tcpClnInit(TCP_CLIENT_CTX* pstTcpCtx, struct event_base* pstEventBase,
    unsigned char uchMyId, NET_MODE eMode);
int  tcpClientConnect(TCP_CLIENT_CTX* pstTcpCtx, const char* pchIp, unsigned short unPort);
void tcpClnStop(TCP_CLIENT_CTX *pstTcpCtx);

#endif
