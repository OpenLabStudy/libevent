#ifndef UDS_CLIENT_RUNTIME_H
#define UDS_CLIENT_RUNTIME_H

#include "eventEngine.h"
#include "ioChannelUtil.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char            chWorkerId;
    char            chDstWorkerId;
    const char*     pchUdsPath;
    const char*     pchTag;
    IO_ROLE         eRole;
    IO_TYPE         eType;
} UDS_CLIENT_RUNTIME_CFG;

typedef struct {
    EVENT_ENGINE*   pstEventEngine;
    struct event*   pstReconnectEvent;
    IO_CHANNEL*     pstIoChannel;

    char            chWorkerId;
    char            chDstWorkerId;
    const char*     pchUdsPath;
    const char*     pchTag;
    IO_ROLE         eRole;
    IO_TYPE         eType;

    void (*pfWriteRespCb)(int, short, void*);
    void (*pfRecvCommandCb)(int, short, void*);
}UDS_CLIENT_RUNTIME;

/* 생성 / 파괴 */
UDS_CLIENT_RUNTIME *
udsClientRuntimeCreate(EVENT_ENGINE *pstEventEngine, const UDS_CLIENT_RUNTIME_CFG *pstUdsClnRtCfg,
                        void (*pfRecvCommandCb)(int, short, void*),
                        void (*pfWriteRespCb)(int, short, void*) );

void udsClientRuntimeDestroy(UDS_CLIENT_RUNTIME **ppstUdsClnRt);

#ifdef __cplusplus
}
#endif

#endif /* UDS_CLIENT_RUNTIME_H */
