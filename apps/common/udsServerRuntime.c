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

    struct sockaddr_un addr;
    socklen_t len = sizeof(addr);

    int clientFd = accept(iListenFd, (struct sockaddr *)&addr, &len);
    if (clientFd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            perror("[UDS-SVR] accept");
        return;
    }

    netSetNonblock(clientFd);

    IO_CHANNEL *pstNewIo = eventSourceCreateWithBev(pstEventEngine, clientFd,
        TYPE_UDS_SVR, ROLE_REQUESTER,
        NULL, NULL, pstUdsSvrRuntime->pfIoHandler
    );

    if (!pstNewIo) {
        close(clientFd);
        return;
    }

    pstNewIo->iWorkerId    = pstUdsSvrRuntime->iSelfWorkerId;
    pstNewIo->chFdCloseSet = FD_OPENED;

    fprintf(stderr, "[%s] client accepted fd=%d\n",
        pstUdsSvrRuntime->pchTag ? pstUdsSvrRuntime->pchTag : "UDS-SVR",
            clientFd);
    
    REQ_ID stReqId;
    MSG_ID stMsgId = {  (char)pstUdsSvrRuntime->iSelfWorkerId, 
                        (char)pstUdsSvrRuntime->iDstWorkerId };
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
UDS_SERVER_RUNTIME *
udsServerRuntimeCreate(EVENT_ENGINE *pstEventEngine,
                       const UDS_SERVER_RUNTIME_CFG *pstCfg,
                       void *pvUserCtx)
{
    if (!pstEventEngine || !pstEventEngine->pstEventBase || !pstCfg)
        return NULL;

    UDS_SERVER_RUNTIME *pstUdsSvrRuntime = calloc(1, sizeof(UDS_SERVER_RUNTIME));
    if (!pstUdsSvrRuntime)
        return NULL;

    pstUdsSvrRuntime->pstEventEngine    = pstEventEngine;
    pstUdsSvrRuntime->pchUdsPath        = pstCfg->pchUdsPath;
    pstUdsSvrRuntime->pchTag            = pstCfg->pchTag;
    pstUdsSvrRuntime->iSelfWorkerId     = pstCfg->iSelfWorkerId;
    pstUdsSvrRuntime->pfOnAccept        = pstCfg->pfOnAccept;
    pstUdsSvrRuntime->pfIoHandler       = pstCfg->pfIoHandler;
    pstUdsSvrRuntime->pvUserCtx         = pvUserCtx;

    /* 기존 소켓 파일 제거 */
    unlink(pstUdsSvrRuntime->pchUdsPath);

    int listenFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listenFd < 0) {
        perror("[UDS-SVR] socket");
        free(pstUdsSvrRuntime);
        return NULL;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, pstUdsSvrRuntime->pchUdsPath, sizeof(addr.sun_path) - 1);

    if (bind(listenFd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[UDS-SVR] bind");
        close(listenFd);
        free(pstUdsSvrRuntime);
        return NULL;
    }

    if (listen(listenFd, 5) < 0) {
        perror("[UDS-SVR] listen");
        close(listenFd);
        free(pstUdsSvrRuntime);
        return NULL;
    }

    netSetNonblock(listenFd);

    pstUdsSvrRuntime->iListenFd = listenFd;
    pstUdsSvrRuntime->pstAcceptEvent = event_new(
        pstEventEngine->pstEventBase,
        listenFd,
        EV_READ | EV_PERSIST,
        udsServerAcceptCb,
        pstUdsSvrRuntime
    );

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

void udsServerRuntimeDestroy(UDS_SERVER_RUNTIME **ppstUdsSvrRuntime)
{
    if (!ppstUdsSvrRuntime || !*ppstUdsSvrRuntime)
        return;

    UDS_SERVER_RUNTIME *pstUdsSvrRuntime = *ppstUdsSvrRuntime;

    if (pstUdsSvrRuntime->pstAcceptEvent) {
        event_del(pstUdsSvrRuntime->pstAcceptEvent);
        event_free(pstUdsSvrRuntime->pstAcceptEvent);
    }

    if (pstUdsSvrRuntime->iListenFd >= 0)
        close(pstUdsSvrRuntime->iListenFd);

    if (pstUdsSvrRuntime->pchUdsPath)
        unlink(pstUdsSvrRuntime->pchUdsPath);

    free(pstUdsSvrRuntime);
    *ppstUdsSvrRuntime = NULL;
}
