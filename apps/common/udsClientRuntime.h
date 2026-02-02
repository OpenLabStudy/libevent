#ifndef UDS_CLIENT_RUNTIME_H
#define UDS_CLIENT_RUNTIME_H

#include "eventEngine.h"
#include "ioChannelUtil.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int          iSelfWorkerId;
    int          iDstWorkerId;
    const char  *pchUdsPath;
    const char  *pchTag;
} UDS_CLIENT_RUNTIME_CFG;

typedef struct {
    EVENT_ENGINE *pstEventEngine;
    struct event *pstReconnectEvent;
    IO_CHANNEL   *pstIoChannel;   /* 현재 연결된 채널 (없을 수 있음) */

    int          iSelfWorkerId;
    int          iDstWorkerId;
    const char  *pchUdsPath;
    const char  *pchTag;

    void (*pfWriteRespCb)(int, short, void*);
    void (*pfRecvCommandCb)(int, short, void*);
}UDS_CLIENT_RUNTIME;

/* 생성 / 파괴 */
UDS_CLIENT_RUNTIME *
udsClientRuntimeCreate(EVENT_ENGINE *pstEventEngine, const UDS_CLIENT_RUNTIME_CFG *pstCfg,
                        void (*pfRecvCommandCb)(int, short, void*),
                        void (*pfWriteRespCb)(int, short, void*) );

void udsClientRuntimeDestroy(UDS_CLIENT_RUNTIME **ppstUdsClnRuntime);

#ifdef __cplusplus
}
#endif

#endif /* UDS_CLIENT_RUNTIME_H */
