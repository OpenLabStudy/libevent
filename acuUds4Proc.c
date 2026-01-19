#include "ipcUtil.h"
#include "acuCtrl.h"

static void sendCurrentAzElValue(int iFd, short nEvent, void* pvData)
{
    
}

static void acceptUds4Cb(evutil_socket_t iListenFd, short nKindOfEvent, void* pvArg)
{
    (void)nKindOfEvent;
    EVENT_ENGINE* pstEventEngine = (EVENT_ENGINE *)pvArg;

    struct sockaddr_in stClientAddr;
    socklen_t uiClientLen = sizeof(stClientAddr);

    int iClientSock = accept(iListenFd, (struct sockaddr*)&stClientAddr, &uiClientLen);
    if (iClientSock < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            perror("[UDS_4_SVR] accept");
        return;
    }

    printf("[UDS_4_SVR] New client FD=%d\n", iClientSock);
    netSetNonblock(iClientSock);

    eventSourceCreateWithBev(pstEventEngine, iClientSock,
        TYPE_TCP_SVR, ROLE_REQUESTER,
        NULL, NULL, sendCurrentAzElValue);
}

void createUds4EventEngine(EVENT_ENGINE *pstEventEngine)
{
    int iListenUds4Fd = netUdsCreateServer(UDS_4_PATH);
    if (iListenUds4Fd < 0) {
        fprintf(stderr, "[ACU_CTRL] netUdsCreateServer() failed\n");
        return EXIT_FAILURE;
    }

    struct event*   pstEventAcceptUds4 = event_new(pstEventEngine->pstEventBase, iListenUds4Fd,
            EV_READ | EV_PERSIST, acceptUds4Cb, pstEventEngine);
    event_add(pstEventAcceptUds4, NULL);

}