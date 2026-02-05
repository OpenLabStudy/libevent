#ifndef TCP_SERVER_RUNTIME_H
#define TCP_SERVER_RUNTIME_H

#include "eventEngine.h"
#include "ioChannelUtil.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned short  unPort;   /* listen socket path */
    const char      *pchTag;       /* log tag */
    int             iSelfWorkerId;
    int             iDstWorkerId;
    IO_ROLE         eRole;
    IO_TYPE         eType;

    void (*pfWrite)(IO_CHANNEL *pstIo, void *pvUserCtx);
    void (*pfIoHandler)(int iFd, short nEvent, void *pvData);

} TCP_SERVER_RUNTIME_CFG;

typedef struct {
    EVENT_ENGINE    *pstEventEngine;
    struct event    *pstAcceptEvent;
    int             iListenFd;
    unsigned short  unPort;
    const char      *pchTag;
    int             iSelfWorkerId;
    int             iDstWorkerId;
    IO_ROLE         eRole;
    IO_TYPE         eType;

    void (*pfWrite)(IO_CHANNEL *, void *);
    void (*pfIoHandler)(int, short, void *);

    void            *pvUserCtx;
}TCP_SERVER_RUNTIME;

/* 생성 / 파괴 */
TCP_SERVER_RUNTIME* tcpServerRuntimeCreate(EVENT_ENGINE *pstEventEngine,
                       const TCP_SERVER_RUNTIME_CFG *pstCfg,
                       void *pvUserCtx);

void tcpServerRuntimeDestroy(TCP_SERVER_RUNTIME **ppstRt);

#ifdef __cplusplus
}
#endif

#endif /* UDS_SERVER_RUNTIME_H */
