#ifndef UDS_SERVER_RUNTIME_H
#define UDS_SERVER_RUNTIME_H

#include "eventEngine.h"
#include "ioChannelUtil.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char        chWorkerId;
    char        chDstWorkerId;
    const char* pchUdsPath;
    const char* pchTag;
    IO_ROLE     eRole;
    IO_TYPE     eType;

    void (*pfWrite)(int iFd, short nEvent, void *pvData);
    void (*pfIoHandler)(int iFd, short nEvent, void *pvData);
} UDS_SERVER_RUNTIME_CFG;

typedef struct {
    EVENT_ENGINE*   pstEventEngine;
    struct event*   pstAcceptEvent;
    int             iListenFd;
    char            chWorkerId;
    char            chDstWorkerId;
    const char*     pchUdsPath;
    const char*     pchTag;
    int             iSelfWorkerId;
    int             iDstWorkerId;
    IO_ROLE         eRole;
    IO_TYPE         eType;

    void (*pfWrite)(int iFd, short nEvent, void *pvData);
    void (*pfIoHandler)(int iFd, short nEvent, void *pvData);

    void*           pvUserCtx;
}UDS_SERVER_RUNTIME;

/* 생성 / 파괴 */
UDS_SERVER_RUNTIME *
udsServerRuntimeCreate(EVENT_ENGINE *pstEventEngine,
                       const UDS_SERVER_RUNTIME_CFG *pstUdsSvrRtCfg,
                       void *pvUserCtx);

void udsServerRuntimeDestroy(UDS_SERVER_RUNTIME **ppstUdsSvrRt);

#ifdef __cplusplus
}
#endif

#endif /* UDS_SERVER_RUNTIME_H */
