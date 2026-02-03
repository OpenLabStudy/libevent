#include "tcpServerRuntime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/un.h>

#include "netTcp.h"
#include "netCore.h"
#include "eventSource.h"
#include "cmdRegistry.h"
#include "icdCommand.h"


/* ============================================================
 * accept callback
 * ============================================================ */
static void tcpServerAcceptCb(evutil_socket_t iListenFd, short nEvent, void *pvArg)
{
    (void)nEvent;

    TCP_SERVER_RUNTIME *pstTcpSvrRuntime = (TCP_SERVER_RUNTIME *)pvArg;
    EVENT_ENGINE *pstEventEngine = pstTcpSvrRuntime->pstEventEngine;

    struct sockaddr_un addr;
    socklen_t len = sizeof(addr);

    int clientFd = accept(iListenFd, (struct sockaddr *)&addr, &len);
    if (clientFd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            perror("[TCP-SVR] accept");
        return;
    }

    netSetNonblock(clientFd);

    IO_CHANNEL *pstNewIo = eventSourceCreateWithBev(pstEventEngine, clientFd,
        TYPE_TCP_SVR, ROLE_REQUESTER,
        NULL, pstTcpSvrRuntime->pfWrite, pstTcpSvrRuntime->pfIoHandler
    );

    if (!pstNewIo) {
        close(clientFd);
        return;
    }

    pstNewIo->iWorkerId    = pstTcpSvrRuntime->iSelfWorkerId;
    pstNewIo->chFdCloseSet = FD_OPENED;

    fprintf(stderr, "[%s] client accepted fd=%d\n",
        pstTcpSvrRuntime->pchTag ? pstTcpSvrRuntime->pchTag : "TCP-SVR",
            clientFd);
    
    REQ_ID stReqId;
    MSG_ID stMsgId = {  (char)pstTcpSvrRuntime->iSelfWorkerId, 
                        (char)pstTcpSvrRuntime->iDstWorkerId };
    unsigned char auSendBuf[64];            
    stReqId.chTmp = 0x01;
    if(createCmdRequest(CMD_ID_INFO, &stMsgId, &stReqId, auSendBuf) == FRAME_OK){
        evbuffer_add(pstNewIo->pstWriteBuffer, auSendBuf, getFrameSizeWithCmd(CMD_ID_INFO, FRAME_TYPE_REQUEST));
        event_add(pstNewIo->pstWriteEvent, NULL);
    }
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
}

/* ============================================================
 * public API
 * ============================================================ */
TCP_SERVER_RUNTIME *
tcpServerRuntimeCreate(EVENT_ENGINE *pstEventEngine,
                       const TCP_SERVER_RUNTIME_CFG *pstCfg,
                       void *pvUserCtx)
{
    if (!pstEventEngine || !pstEventEngine->pstEventBase || !pstCfg)
        return NULL;

    TCP_SERVER_RUNTIME *pstTcpSvrRuntime = calloc(1, sizeof(TCP_SERVER_RUNTIME));
    if (!pstTcpSvrRuntime)
        return NULL;

    pstTcpSvrRuntime->pstEventEngine    = pstEventEngine;
    pstTcpSvrRuntime->unPort        = pstCfg->unPort;
    pstTcpSvrRuntime->pchTag            = pstCfg->pchTag;
    pstTcpSvrRuntime->iSelfWorkerId     = pstCfg->iSelfWorkerId;
    pstTcpSvrRuntime->pfWrite           = pstCfg->pfWrite;
    pstTcpSvrRuntime->pfIoHandler       = pstCfg->pfIoHandler;
    pstTcpSvrRuntime->pvUserCtx         = pvUserCtx;

    int listenFd = netTcpCreateServer(pstTcpSvrRuntime->unPort);

    pstTcpSvrRuntime->iListenFd = listenFd;
    pstTcpSvrRuntime->pstAcceptEvent = event_new(
        pstEventEngine->pstEventBase,
        listenFd,
        EV_READ | EV_PERSIST,
        tcpServerAcceptCb,
        pstTcpSvrRuntime
    );

    if (!pstTcpSvrRuntime->pstAcceptEvent) {
        close(listenFd);
        free(pstTcpSvrRuntime);
        return NULL;
    }

    event_add(pstTcpSvrRuntime->pstAcceptEvent, NULL);

    fprintf(stderr, "[%s] TCP server listening (%d)\n",
        pstTcpSvrRuntime->pchTag ? pstTcpSvrRuntime->pchTag : "TCP-SVR",
        pstTcpSvrRuntime->unPort);

    return pstTcpSvrRuntime;
}

void tcpServerRuntimeDestroy(TCP_SERVER_RUNTIME **ppstTcpSvrRuntime)
{
    if (!ppstTcpSvrRuntime || !*ppstTcpSvrRuntime)
        return;

    TCP_SERVER_RUNTIME *pstTcpSvrRuntime = *ppstTcpSvrRuntime;

    if (pstTcpSvrRuntime->pstAcceptEvent) {
        event_del(pstTcpSvrRuntime->pstAcceptEvent);
        event_free(pstTcpSvrRuntime->pstAcceptEvent);
    }

    if (pstTcpSvrRuntime->iListenFd >= 0)
        close(pstTcpSvrRuntime->iListenFd);

    free(pstTcpSvrRuntime);
    *ppstTcpSvrRuntime = NULL;
}
