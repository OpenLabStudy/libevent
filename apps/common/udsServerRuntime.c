#include "udsServerRuntime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/un.h>

#include "netUds.h"
#include "netCore.h"
#include "eventSource.h"
#include "cmdRegistry.h"
#include "icdCommand.h"


/* ============================================================
 * accept callback
 * ============================================================ */
static void udsServerAcceptCb(evutil_socket_t listenFd,
                              short nEvent,
                              void *pvArg)
{
    (void)nEvent;

    UDS_SERVER_RUNTIME *rt = (UDS_SERVER_RUNTIME *)pvArg;
    EVENT_ENGINE *ee = rt->pstEventEngine;

    struct sockaddr_un addr;
    socklen_t len = sizeof(addr);

    int clientFd = accept(listenFd, (struct sockaddr *)&addr, &len);
    if (clientFd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            perror("[UDS-SVR] accept");
        return;
    }

    netSetNonblock(clientFd);

    IO_CHANNEL *pstNewIo = eventSourceCreateWithBev(ee, clientFd,
        TYPE_UDS_SVR, ROLE_REQUESTER,
        NULL, NULL, rt->pfIoHandler
    );

    if (!pstNewIo) {
        close(clientFd);
        return;
    }

    pstNewIo->iWorkerId    = rt->iSelfWorkerId;
    pstNewIo->chFdCloseSet = FD_OPENED;

    fprintf(stderr, "[%s] client accepted fd=%d\n",
            rt->pchTag ? rt->pchTag : "UDS-SVR",
            clientFd);
    
    REQ_ID stReqId;
    MSG_ID stMsgId = {(char)rt->iSelfWorkerId,  (char)rt->iDstWorkerId};
    unsigned char auSendBuf[64];            
    stReqId.chTmp = 0x01;        
    if(createCmdRequest(CMD_ID_INFO, &stMsgId, &stReqId, auSendBuf) == FRAME_OK){
        evbuffer_add(pstNewIo->pstWriteBuffer, auSendBuf, getFrameSizeWithCmd(CMD_ID_INFO, FRAME_TYPE_REQUEST));
        event_add(pstNewIo->pstWriteEvent, NULL);
    }

    // if (rt->pfOnAccept)
    //     rt->pfOnAccept(pstNewIo, rt->pvUserCtx);
}

/* ============================================================
 * public API
 * ============================================================ */
UDS_SERVER_RUNTIME *
udsServerRuntimeCreate(EVENT_ENGINE *pstEventEngine,
                       const UDS_SERVER_RUNTIME_CFG *pstCfg,
                       void *pvUserCtx)
{
    if (!pstEventEngine || !pstEventEngine->pstEventBase || !pstCfg)
        return NULL;

    UDS_SERVER_RUNTIME *rt = calloc(1, sizeof(UDS_SERVER_RUNTIME));
    if (!rt)
        return NULL;

    rt->pstEventEngine = pstEventEngine;
    rt->pchUdsPath     = pstCfg->pchUdsPath;
    rt->pchTag         = pstCfg->pchTag;
    rt->iSelfWorkerId      = pstCfg->iSelfWorkerId;
    rt->pfOnAccept     = pstCfg->pfOnAccept;
    rt->pfIoHandler    = pstCfg->pfIoHandler;
    rt->pvUserCtx      = pvUserCtx;

    /* 기존 소켓 파일 제거 */
    unlink(rt->pchUdsPath);

    int listenFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listenFd < 0) {
        perror("[UDS-SVR] socket");
        free(rt);
        return NULL;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, rt->pchUdsPath, sizeof(addr.sun_path) - 1);

    if (bind(listenFd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[UDS-SVR] bind");
        close(listenFd);
        free(rt);
        return NULL;
    }

    if (listen(listenFd, 5) < 0) {
        perror("[UDS-SVR] listen");
        close(listenFd);
        free(rt);
        return NULL;
    }

    netSetNonblock(listenFd);

    rt->iListenFd = listenFd;
    rt->pstAcceptEvent = event_new(
        pstEventEngine->pstEventBase,
        listenFd,
        EV_READ | EV_PERSIST,
        udsServerAcceptCb,
        rt
    );

    if (!rt->pstAcceptEvent) {
        close(listenFd);
        free(rt);
        return NULL;
    }

    event_add(rt->pstAcceptEvent, NULL);

    fprintf(stderr, "[%s] UDS server listening (%s)\n",
            rt->pchTag ? rt->pchTag : "UDS-SVR",
            rt->pchUdsPath);

    return rt;
}

void udsServerRuntimeDestroy(UDS_SERVER_RUNTIME **ppstRt)
{
    if (!ppstRt || !*ppstRt)
        return;

    UDS_SERVER_RUNTIME *rt = *ppstRt;

    if (rt->pstAcceptEvent) {
        event_del(rt->pstAcceptEvent);
        event_free(rt->pstAcceptEvent);
    }

    if (rt->iListenFd >= 0)
        close(rt->iListenFd);

    if (rt->pchUdsPath)
        unlink(rt->pchUdsPath);

    free(rt);
    *ppstRt = NULL;
}
