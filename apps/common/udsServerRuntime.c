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
static void udsServerAcceptCb(evutil_socket_t iListenFd, short nEvent, void *pvArg)
{
    (void)nEvent;

    UDS_SERVER_RUNTIME *pstUdsSvrRuntime = (UDS_SERVER_RUNTIME *)pvArg;
    EVENT_ENGINE *pstEventEngine = pstUdsSvrRuntime->pstEventEngine;

    struct sockaddr_un stSockAddr;
    socklen_t sockAddrLen = sizeof(stSockAddr);

    int iClientFd = accept(iListenFd, (struct sockaddr *)&stSockAddr, &sockAddrLen);
    if (iClientFd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            perror("[UDS-SVR] accept");
        return;
    }
    netSetNonblock(iClientFd);

    IO_CHANNEL *pstNewIo = eventSourceCreateWithBev(pstEventEngine, iClientFd,
        pstUdsSvrRuntime->eType, pstUdsSvrRuntime->eRole,
        NULL, pstUdsSvrRuntime->pfWrite, pstUdsSvrRuntime->pfIoHandler);
    if (!pstNewIo) {
        close(iClientFd);
        return;
    }

    pstNewIo->chWorkerId    = pstUdsSvrRuntime->chWorkerId;
    pstNewIo->chDstWorkerId = pstUdsSvrRuntime->chDstWorkerId;
    pstNewIo->chFdCloseSet  = FD_OPENED;

    fprintf(stderr, "[%s] client accepted fd=%d\n",
        pstUdsSvrRuntime->pchTag ? pstUdsSvrRuntime->pchTag : "UDS-SVR", iClientFd);
    
    REQ_ID stReqId={.chTmp = 0x01};
    MSG_ID stMsgId = {pstUdsSvrRuntime->iSelfWorkerId, pstUdsSvrRuntime->iDstWorkerId};
    unsigned char auSendBuf[64];
    if(createCmdRequest(CMD_ID_INFO, &stMsgId, &stReqId, auSendBuf) == FRAME_OK){
        evbuffer_add(pstNewIo->pstWriteBuffer, auSendBuf, getFrameSizeWithCmd(CMD_ID_INFO, FRAME_TYPE_REQUEST));
        event_add(pstNewIo->pstWriteEvent, NULL);
    }
}

/* ============================================================
 * public API
 * ============================================================ */
UDS_SERVER_RUNTIME *
udsServerRuntimeCreate(EVENT_ENGINE *pstEventEngine,
                       const UDS_SERVER_RUNTIME_CFG *pstUdsSvrRtCfg,
                       void *pvUserCtx)
{
    if (!pstEventEngine || !pstEventEngine->pstEventBase || !pstUdsSvrRtCfg)
        return NULL;

    UDS_SERVER_RUNTIME *pstUdsSvrRuntime = calloc(1, sizeof(UDS_SERVER_RUNTIME));
    if (!pstUdsSvrRuntime)
        return NULL;

    pstUdsSvrRuntime->pstEventEngine    = pstEventEngine;
    pstUdsSvrRuntime->pchUdsPath        = pstUdsSvrRtCfg->pchUdsPath;
    pstUdsSvrRuntime->pchTag            = pstUdsSvrRtCfg->pchTag;
    pstUdsSvrRuntime->chWorkerId        = pstUdsSvrRtCfg->chWorkerId;
    pstUdsSvrRuntime->chDstWorkerId     = pstUdsSvrRtCfg->chDstWorkerId;
    pstUdsSvrRuntime->eRole             = pstUdsSvrRtCfg->eRole;
    pstUdsSvrRuntime->eType             = pstUdsSvrRtCfg->eType;
    pstUdsSvrRuntime->pfWrite           = pstUdsSvrRtCfg->pfWrite;
    pstUdsSvrRuntime->pfIoHandler       = pstUdsSvrRtCfg->pfIoHandler;
    pstUdsSvrRuntime->pvUserCtx         = pvUserCtx;

    int listenFd = netUdsCreateServer(pstUdsSvrRuntime->pchUdsPath);

    pstUdsSvrRuntime->iListenFd = listenFd;
    pstUdsSvrRuntime->pstAcceptEvent = event_new(
        pstEventEngine->pstEventBase, listenFd,
        EV_READ | EV_PERSIST,
        udsServerAcceptCb, pstUdsSvrRuntime);
    if (!pstUdsSvrRuntime->pstAcceptEvent) {
        close(listenFd);
        free(pstUdsSvrRuntime);
        return NULL;
    }
    event_add(pstUdsSvrRuntime->pstAcceptEvent, NULL);
    fprintf(stderr, "[%s] UDS server listening (%s)\n",
        pstUdsSvrRuntime->pchTag ? pstUdsSvrRuntime->pchTag : "UDS-SVR",
        pstUdsSvrRuntime->pchUdsPath);

    return pstUdsSvrRuntime;
}

void udsServerRuntimeDestroy(UDS_SERVER_RUNTIME **ppstUdsSvrRt)
{
    if (!ppstUdsSvrRt || !*ppstUdsSvrRt)
        return;

    UDS_SERVER_RUNTIME *pstUdsSvrRt = *ppstUdsSvrRt;

    if (pstUdsSvrRt->pstAcceptEvent) {
        event_del(pstUdsSvrRt->pstAcceptEvent);
        event_free(pstUdsSvrRt->pstAcceptEvent);
    }

    if (pstUdsSvrRt->iListenFd >= 0)
        close(pstUdsSvrRt->iListenFd);

    if (pstUdsSvrRt->pchUdsPath)
        unlink(pstUdsSvrRt->pchUdsPath);

    free(pstUdsSvrRt);
    pstUdsSvrRt = NULL;
}
