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

/* ========================================================================== */
/* Application-Level Read Processing                                          */
/* ========================================================================== */
static void readCallback(struct bufferevent* pstBufferEvent, void* pvData)
{
    unsigned char auchRecvBuffer[2048];    
    unsigned char auCmdResult[1000];
    unsigned char auSendBuf[1024];
    unsigned short unCmd = 0;
    FRAME_ERR eErr;
    int iSendLen = 0;
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;

    memset(auchRecvBuffer, 0x0, sizeof(auchRecvBuffer));
    struct evbuffer* pstInputBuffer = bufferevent_get_input(pstBufferEvent);
    while (1) {    
        size_t tRecvLen = evbuffer_get_length(pstInputBuffer);
        if (tRecvLen < FRAME_HEADER_MIN_SIZE)
            break;

        if (tRecvLen > sizeof(auchRecvBuffer))
            tRecvLen = sizeof(auchRecvBuffer);

        int iCopyLen = evbuffer_copyout(pstInputBuffer, auchRecvBuffer, tRecvLen);
        int iFrameSize = getFrameSize(auchRecvBuffer);
        if (iFrameSize <= 0) {
            evbuffer_drain(pstInputBuffer, 1);
            continue;
        }

        if (iCopyLen < iFrameSize)
            break;

        evbuffer_drain(pstInputBuffer, iFrameSize);
        MSG_ID stMsgId = { TCP_SVR_ID, TCP_CLN_ID };

        /* === 헤더 및 명령 추출 === */
        eErr = requestFrame(auchRecvBuffer, &stMsgId, iFrameSize, &unCmd);
        if (eErr != FRAME_OK) {
            fprintf(stderr, "[APP] requestFrame ERR: %s\n", frameErrToStr(eErr));
            continue;
        }

        /* === 명령 처리 === */
        eErr = commandHandler(auchRecvBuffer, &stMsgId, iFrameSize, auCmdResult, &iSendLen);
        if (eErr != FRAME_OK || iSendLen <= 0)
            continue;

        /* === 응답 프레임 생성 === */
        eErr = makeResFrame(unCmd, &stMsgId, auCmdResult, auSendBuf);
        if (eErr != FRAME_OK)
            continue;

        fprintf(stderr, "[APP] Send CMD=%04X, size=%d\n", unCmd, iSendLen);

        if (bufferevent_write(pstBufferEvent, auSendBuf, iSendLen) < 0) {
            fprintf(stderr, "[APP] bufferevent_write() failed\n");
        }
    }
}

/* ========================================================================== */
/* Application-Level Event Callback                                           */
/* ========================================================================== */
static void eventCallback(struct bufferevent* pstBufferEvent,
    short nEvents, void* pvData)
{
    EVENT_SOURCE* pstEventSrc = (EVENT_SOURCE *)pvData;
    (void)pstBufferEvent;

    if (nEvents & BEV_EVENT_EOF) {
        fprintf(stderr, "[TCP-Server] Client disconnected\n");
    } else if (nEvents & BEV_EVENT_ERROR) {
        fprintf(stderr, "[TCP-Server] Client socket error\n");
    }

    /* 실제 close/free 는 eventSession 의 eventCallbackWrapper 에서 수행 */
    eventSourceDestroy(pstEventSrc);
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
        SRC_TYPE_TCP_CLIENT,
        SRC_ROLE_REQUESTER,
        readCallback,
        eventCallback
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
    // baseContextInit(&stBaseCtx, TCP_SVR_ID);
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

    /* 종료 처리 */
    eventEngineCleanup(&stEventEngine);

    event_free(stEventAccept);
    netClose(iListenFd);
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