#ifndef UDS_H
#define UDS_H

#include "commonSession.h"
#include "netContext.h"

typedef struct {
    CORE_CTX stCoreCtx;
    NET_MODE eMode;
    int iSockFd;
    struct event* pstRecvEvent;
    struct event* pstStdinEvent;
} UDS_CTX;

void udsInit(UDS_CTX* C, struct event_base* base, unsigned char id, NET_MODE mode);
int  udsServerStart(UDS_CTX* C, const char* path);
int  udsClientStart(UDS_CTX* C, const char* path);
void udsStop(UDS_CTX* C);

#endif
