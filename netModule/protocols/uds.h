#ifndef UDS_H
#define UDS_H

#include "commonSession.h"
#include "netContext.h"

void udsSvrInit(UDS_SERVER_CTX *pstUdsSrvCtx, struct event_base* pstEventBase, 
    unsigned char uchMyId, NET_MODE eMode);
int  udsServerStart(UDS_SERVER_CTX *pstUdsSrvCtx, const char *pchPath);
void udsSvrStop(UDS_SERVER_CTX *pstUdsSrvCtx);

void udsClnInit(UDS_CLIENT_CTX *pstUdsClnCtx, struct event_base* pstEventBase, 
    unsigned char uchMyId, NET_MODE eMode);
int  udsClientStart(UDS_CLIENT_CTX *pstUdsClnCtx, const char *pchPath);
void udsClnStop(UDS_CLIENT_CTX *pstUdsClnCtx);

#endif
