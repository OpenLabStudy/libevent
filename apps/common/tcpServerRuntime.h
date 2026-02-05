#ifndef TCP_SERVER_RUNTIME_H
#define TCP_SERVER_RUNTIME_H

#include "eventEngine.h"
#include "ioChannelUtil.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char            chWorkerId;
    char            chDstWorkerId;
    unsigned short  unPort;
    const char*     pchTag;
    IO_ROLE         eRole;
    IO_TYPE         eType;

    void (*pfWrite)(int iFd, short nEvent, void *pvData);
    void (*pfIoHandler)(int iFd, short nEvent, void *pvData);
} TCP_SERVER_RUNTIME_CFG;

typedef struct {
    EVENT_ENGINE*   pstEventEngine;
    struct event*   pstAcceptEvent;
    int             iListenFd;
    char            chWorkerId;
    char            chDstWorkerId;
    unsigned short  unPort;
    const char*     pchTag;
    IO_ROLE         eRole;
    IO_TYPE         eType;

    void (*pfWrite)(int iFd, short nEvent, void *pvData);
    void (*pfIoHandler)(int iFd, short nEvent, void *pvData);

    void            *pvUserCtx;
}TCP_SERVER_RUNTIME;

/* 생성 / 파괴 */
TCP_SERVER_RUNTIME* tcpServerRuntimeCreate(EVENT_ENGINE *pstEventEngine,
                       const TCP_SERVER_RUNTIME_CFG *pstTcpSvrRtCfg,
                       void *pvUserCtx);

void tcpServerRuntimeDestroy(TCP_SERVER_RUNTIME **ppstTcpSvrRt);

#ifdef __cplusplus
}
#endif

#endif /* UDS_SERVER_RUNTIME_H */
