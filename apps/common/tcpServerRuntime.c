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

static void tcpRecvSocketDisconnect(int iFd, short nEvent, void* pvData)
{
    (void)iFd;
    (void)nEvent;
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    EVENT_ENGINE* pstEventEngine = pstIoChannel->pstEventEngine;
    IO_EVENT_TYPE eEventType = pstIoChannel->ePendingLogicEvent;

    char achRecvBuffer[2048];
    unsigned short unCmd = 0;
    FRAME_ERR eErr;
    switch (eEventType) {
    case IO_EVT_RX_DATA:
        fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
        break;

    case IO_EVT_CHANNEL_CLOSED:
    case IO_EVT_ERROR:
        printf("[TC_SND_AZ_EL_TO_CTRL_PC] channel closed fd=%d\n", pstIoChannel->iFd);
        event_active(pstIoChannel->pstShutdownEvent, 0, 0);
        break;

    default:
        break;
    }
    pstIoChannel->ePendingLogicEvent = IO_EVENT_NONE;
}

/* ============================================================
 * accept callback
 * ============================================================ */
static void tcpServerAcceptCb(int iFd, short nEvent, void* pvData)
{
    (void)nEvent;
    TCP_SERVER_RUNTIME *pstTcpSvrRt = (TCP_SERVER_RUNTIME *)pvData;
    EVENT_ENGINE *pstEventEngine = pstTcpSvrRt->pstEventEngine;
    void (*pfIoHandler)(int iFd, short nEvent, void *pvData);

    struct sockaddr_un addr;
    socklen_t len = sizeof(addr);

    int clientFd = accept(iFd, (struct sockaddr *)&addr, &len);
    if (clientFd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            perror("[TCP-SVR] accept");
        return;
    }
    netSetNonblock(clientFd);
    if(pstTcpSvrRt->pfIoHandler == NULL){
        pfIoHandler = tcpRecvSocketDisconnect;
    }else{
        pfIoHandler = pstTcpSvrRt->pfIoHandler;
    }

    IO_CHANNEL *pstNewIo = eventSourceCreateWithBev(pstEventEngine, clientFd,
        pstTcpSvrRt->eType, pstTcpSvrRt->eRole,
        NULL, pstTcpSvrRt->pfWrite, pfIoHandler);
    if (!pstNewIo) {
        close(clientFd);
        return;
    }

    pstNewIo->chWorkerId    = pstTcpSvrRt->chWorkerId;
    pstNewIo->chDstWorkerId = pstTcpSvrRt->chDstWorkerId;
    pstNewIo->chFdCloseSet = FD_OPENED;

    fprintf(stderr, "[%s] client accepted fd=%d\n",
        pstTcpSvrRt->pchTag ? pstTcpSvrRt->pchTag : "TCP-SVR", clientFd);
    
    REQ_ID stReqId = {.chTmp = 0x01};
    MSG_ID stMsgId = {pstTcpSvrRt->chWorkerId, pstTcpSvrRt->chDstWorkerId};
    unsigned char auSendBuf[64];            
    stReqId.chTmp = 0x01;
    if(createCmdRequest(CMD_ID_INFO, &stMsgId, &stReqId, auSendBuf) == FRAME_OK){
        evbuffer_add(pstNewIo->pstWriteBuffer, auSendBuf, getFrameSizeWithCmd(CMD_ID_INFO, FRAME_TYPE_REQUEST));
        event_add(pstNewIo->pstWriteEvent, NULL);
    }
}

/* ============================================================
 * public API
 * ============================================================ */
TCP_SERVER_RUNTIME *
tcpServerRuntimeCreate(EVENT_ENGINE *pstEventEngine,
                       const TCP_SERVER_RUNTIME_CFG *pstTcpSvrRtCfg,
                       void *pvUserCtx)
{
    if (!pstEventEngine || !pstEventEngine->pstEventBase || !pstTcpSvrRtCfg)
        return NULL;

    TCP_SERVER_RUNTIME *pstTcpSvrRuntime = calloc(1, sizeof(TCP_SERVER_RUNTIME));
    if (!pstTcpSvrRuntime)
        return NULL;

    pstTcpSvrRuntime->pstEventEngine    = pstEventEngine;
    pstTcpSvrRuntime->unPort            = pstTcpSvrRtCfg->unPort;
    pstTcpSvrRuntime->pchTag            = pstTcpSvrRtCfg->pchTag;
    pstTcpSvrRuntime->chWorkerId        = pstTcpSvrRtCfg->chWorkerId;
    pstTcpSvrRuntime->chDstWorkerId     = pstTcpSvrRtCfg->chDstWorkerId;
    pstTcpSvrRuntime->eRole             = pstTcpSvrRtCfg->eRole;
    pstTcpSvrRuntime->eType             = pstTcpSvrRtCfg->eType;
    pstTcpSvrRuntime->pfWrite           = pstTcpSvrRtCfg->pfWrite;
    pstTcpSvrRuntime->pfIoHandler       = pstTcpSvrRtCfg->pfIoHandler;
    pstTcpSvrRuntime->pvUserCtx         = pvUserCtx;

    int listenFd = netTcpCreateServer(pstTcpSvrRuntime->unPort);

    pstTcpSvrRuntime->iListenFd = listenFd;
    pstTcpSvrRuntime->pstAcceptEvent = event_new(
        pstEventEngine->pstEventBase, listenFd,
        EV_READ | EV_PERSIST,
        tcpServerAcceptCb, pstTcpSvrRuntime);
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

void tcpServerRuntimeDestroy(TCP_SERVER_RUNTIME **ppstTcpSvrRt)
{
    if (!ppstTcpSvrRt || !*ppstTcpSvrRt)
        return;

    TCP_SERVER_RUNTIME *pstTcpSvrRt = *ppstTcpSvrRt;

    if (pstTcpSvrRt->pstAcceptEvent) {
        event_del(pstTcpSvrRt->pstAcceptEvent);
        event_free(pstTcpSvrRt->pstAcceptEvent);
    }
    if (pstTcpSvrRt->iListenFd >= 0)
        close(pstTcpSvrRt->iListenFd);

    free(pstTcpSvrRt);
    pstTcpSvrRt = NULL;
}
