/**
 * @file udsSvr.c
 * @brief Libevent 기반 UDS(UNIX Domain Socket) 서버 Application Layer
 *
 * TCP 서버(tcpSvr.c)와 완전히 동일한 구조를 사용하되,
 * 소켓 생성만 AF_UNIX 기반(netUdsCreateServer)으로 변경한 버전이다.
 *
 * - EVENT_CONTEXT + SOCK_CONTEXT 구조 사용
 * - acceptCb, readCallbackWrapper, eventCallbackWrapper는 eventSession.c 사용
 * - appReadCb / appEventCb 는 Application 레이어 콜백
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <signal.h>
 #include <unistd.h>
 #include <errno.h>
 
 #include "eventEngine.h"
 #include "udsSvr.h"



/* ========================================================================== */
/* Application-Level Read Processing (UDS Server)                             */
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
        MSG_ID stMsgId = { 0x01, 0x11 };

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
/* Application-Level Event Callback (UDS Server)                              */
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


/* ========================================================================== */
/* Main Entry Point                                                           */
/* ========================================================================== */

int run(void)
{
    EVENT_ENGINE   stEventEngine;
    struct event   *pstSignalEvent;
    stEventEngine.pstEventBase = event_base_new();
    if (!stEventEngine.pstEventBase) {
        fprintf(stderr, "[UDS-Server] event_base_new() failed\n");
        return EXIT_FAILURE;
    }

    /* Dispatcher 초기화 */
    eventEngineInit(&stEventEngine);

    int iListenFd = netUdsCreateServer(UDS_1_PATH);
    if (iListenFd < 0) {
        fprintf(stderr, "[UDS-Server] netUdsCreateServer() failed\n");
        return EXIT_FAILURE;
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

    fprintf(stderr, "[UDS-Server] Listening at %s\n", UDS_1_PATH);
    
    event_base_dispatch(stEventEngine.pstEventBase);

    /* === 종료 처리 === */
    eventEngineCleanup(&stEventEngine);

    event_free(stEventAccept);
    netClose(iListenFd);
    event_base_free(stEventEngine.pstEventBase);

    fprintf(stderr,"[UDS-SVR] Terminated.\n");
    return 0;
}


/* === main === */
#ifndef GOOGLE_TEST
int main(int argc, char** argv)
{
    (void)argc; (void)argv;
    return run();
}
#endif
