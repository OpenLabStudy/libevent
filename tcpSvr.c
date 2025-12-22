/**
 * @file tcpSvr.c
 * @brief Dispatcher + EVENT_SOURCE + netTcp 기반 TCP Echo Server (No global variables)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>

#include "eventEngine.h"
#include "tcpSvr.h"

#define SERVER_PORT 5000

static void ioChannelHandleEvent(int iFd, short nEvent, void* pvData)
{
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    IO_EVENT_TYPE eEventType = pstIoChannel->ePendingLogicEvent;
    unsigned char auchRecvBuffer[2048];    
    unsigned char auchCmdResult[1000];
    unsigned char auchSendBuf[1024];
    unsigned short unCmd = 0;
    FRAME_ERR eErr;
    int iSendLen = 0;
    
    switch (eEventType) {
    case IO_EVT_RX_DATA:
        while (1) {
            size_t tRecvLen = evbuffer_get_length(pstIoChannel->pstReadBuffer);
            if (tRecvLen < FRAME_HEADER_MIN_SIZE)
                break;

            if (tRecvLen > sizeof(auchRecvBuffer))
                tRecvLen = sizeof(auchRecvBuffer);

            int iCopyLen = evbuffer_copyout(pstIoChannel->pstReadBuffer, auchRecvBuffer, tRecvLen);
            int iFrameSize = getFrameSize(auchRecvBuffer);
            if (iFrameSize <= 0) {
                evbuffer_drain(pstIoChannel->pstReadBuffer, 1);
                continue;
            }

            if (iCopyLen < iFrameSize)
                break;

            evbuffer_drain(pstIoChannel->pstReadBuffer, iFrameSize);
            MSG_ID stMsgId = { TCP_SVR_ID, TCP_CLN_ID };

            /* === 헤더 및 명령 추출 === */
            eErr = requestFrame(auchRecvBuffer, &stMsgId, iFrameSize, &unCmd);
            if (eErr != FRAME_OK) {
                fprintf(stderr, "[TCP-SVR] requestFrame ERR: %s\n", frameErrToStr(eErr));
                continue;
            }

            /* === 명령 처리 === */
            eErr = commandHandler(auchRecvBuffer, &stMsgId, iFrameSize, auchCmdResult, &iSendLen);
            if (eErr != FRAME_OK || iSendLen <= 0)
                continue;

            /* === 응답 프레임 생성 === */
            eErr = makeResFrame(unCmd, &stMsgId, auchCmdResult, auchSendBuf);
            if (eErr != FRAME_OK)
                continue;

            fprintf(stderr, "[TCP-SVR] Send CMD=%04X, size=%d\n", unCmd, iSendLen);
            evbuffer_add(pstIoChannel->pstWriteBuffer, auchSendBuf, iSendLen);
            event_add(pstIoChannel->pstWriteEvent, NULL); 
        }        
        break;

    case IO_EVT_CHANNEL_CLOSED:
        printf("[TCP-SVR] channel closed fd=%d\n", pstIoChannel->iFd);
        event_active(pstIoChannel->pstShutdownEvent, 0, 0);
        break;

    case IO_EVT_ERROR:
        printf("[TCP-SVR] channel error fd=%d\n", pstIoChannel->iFd);
        event_active(pstIoChannel->pstShutdownEvent, 0, 0);
        break;

    default:
        break;
    }
    pstIoChannel->ePendingLogicEvent = IO_EVENT_NONE;
}


/* ============================================================
* Accept 콜백
* ============================================================ */
static void acceptCb(evutil_socket_t iListenFd, short nKindOfEvent, void* pvArg)
{
    (void)nKindOfEvent;
    EVENT_ENGINE* pstEventEngine = (EVENT_ENGINE *)pvArg;

    struct sockaddr_in stClientAddr;
    socklen_t uiClientLen = sizeof(stClientAddr);

    int iClientSock = accept(iListenFd, (struct sockaddr*)&stClientAddr, &uiClientLen);
    if (iClientSock < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            perror("[TCP-SVR] accept");
        return;
    }

    printf("[TCP-SVR] New client FD=%d\n", iClientSock);

    netSetNonblock(iClientSock);

    eventSourceCreateWithBev(
        pstEventEngine,
        iClientSock,
        TYPE_TCP_SVR,
        ROLE_REQUESTER,
        ioChannelHandleEvent
    );
}

/* ============================================================
* SIGINT 콜백
* ============================================================ */
static void signalCb(evutil_socket_t sig, short events, void* pvArg)
{
    EVENT_ENGINE* pstEventEngine = (EVENT_ENGINE *)pvArg;

    fprintf(stderr,"\n[TCP-SVR] SIGINT → shutdown\n");
    if(pstEventEngine->pstEventBase)
        event_base_loopexit(pstEventEngine->pstEventBase, NULL);
}

/* ============================================================
* main()
* ============================================================ */
int run()
{
    EVENT_ENGINE   stEventEngine;
    struct event   *pstSignalEvent;

    /* BASE_CONTEXT 초기화 */
    stEventEngine.pstEventBase = event_base_new();
    if (!stEventEngine.pstEventBase) {
        fprintf(stderr,"event_base_new failed\n");
        return -1;
    }

    /* Dispatcher 초기화 */
    eventEngineInit(&stEventEngine);

    /* TCP Listen 소켓 생성 */
    int iListenFd = netTcpCreateServer(SERVER_PORT);
    if (iListenFd < 0) {
        perror("netTcpCreateServer");
        return -1;
    }

    /* Accept 이벤트 등록 */
    struct event* stEventAccept = event_new(
            stEventEngine.pstEventBase, iListenFd, 
            EV_READ | EV_PERSIST, acceptCb, &stEventEngine);
    event_add(stEventAccept, NULL);

    /* SIGINT 처리 등록 */
    pstSignalEvent = evsignal_new(stEventEngine.pstEventBase,
        SIGINT, signalCb, &stEventEngine);
    event_add(pstSignalEvent, NULL);

    fprintf(stderr,"[TCP-SVR] Listening on port %d\n", SERVER_PORT);

    /* 이벤트 루프 시작 */
    event_base_dispatch(stEventEngine.pstEventBase);
    if(pstSignalEvent){
        event_del(pstSignalEvent);
        event_free(pstSignalEvent);
        pstSignalEvent =  NULL;
    }

    if(stEventAccept){
        event_del(stEventAccept);
        event_free(stEventAccept);
        stEventAccept =  NULL;
    }
    
    /* 종료 처리 */
    eventEngineCleanup(&stEventEngine);    
    event_base_free(stEventEngine.pstEventBase);

    fprintf(stderr,"[TCP-SVR] Terminated.\n");
    return 0;
}

/* === main === */
#ifndef GOOGLE_TEST
int main(int argc, char** argv)
{
    return run();
}
#endif