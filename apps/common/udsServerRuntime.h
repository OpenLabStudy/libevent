#ifndef UDS_SERVER_RUNTIME_H
#define UDS_SERVER_RUNTIME_H

#include "eventEngine.h"
#include "ioChannelUtil.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *pchUdsPath;   /* listen socket path */
    const char *pchTag;       /* log tag */
    int          iSelfWorkerId;
    int          iDstWorkerId;

    /*
     * 새 client accept 후 호출됨
     *  - az/el 즉시 전송
     *  - client 초기화
     *  - client 리스트 등록 등
     */
    void (*pfOnAccept)(IO_CHANNEL *pstIo, void *pvUserCtx);

    /* client RX 처리 핸들러 (필수) */
    void (*pfIoHandler)(int iFd, short nEvent, void *pvData);

} UDS_SERVER_RUNTIME_CFG;

typedef struct {
    EVENT_ENGINE *pstEventEngine;

    int           iListenFd;
    struct event *pstAcceptEvent;

    const char   *pchUdsPath;
    const char   *pchTag;
    int          iSelfWorkerId;
    int          iDstWorkerId;

    void (*pfOnAccept)(IO_CHANNEL *, void *);
    void (*pfIoHandler)(int, short, void *);

    void         *pvUserCtx;
}UDS_SERVER_RUNTIME;

/* 생성 / 파괴 */
UDS_SERVER_RUNTIME *
udsServerRuntimeCreate(EVENT_ENGINE *pstEventEngine,
                       const UDS_SERVER_RUNTIME_CFG *pstCfg,
                       void *pvUserCtx);

void udsServerRuntimeDestroy(UDS_SERVER_RUNTIME **ppstRt);

#ifdef __cplusplus
}
#endif

#endif /* UDS_SERVER_RUNTIME_H */
