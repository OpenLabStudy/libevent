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
 
 #include "dispatcher.h"
 #include "udsSvr.h"



/* ========================================================================== */
/* Application-Level Read Processing (UDS Server)                             */
/* ========================================================================== */

static void readCallback(struct bufferevent* pstBufferEvent, void* pvData)
{
    MSG_ID          stMsgId;
    unsigned char   auchRecvBuf[2048];
    unsigned char   auchCmdResult[1024];
    unsigned char   auchSendBuf[2048];
    unsigned short  unCmd      = 0;
    FRAME_ERR       eErr;
    int             iSendLen   = 0;
    EVENT_SOURCE* pstEventSrc = (EVENT_SOURCE *)pvData;
    if (!pstEventSrc || !pstEventSrc->pstDispatcher->pstBaseCtx)
        return;

    struct evbuffer* pstInputBuf = bufferevent_get_input(pstBufferEvent);    

    while (1) {
        size_t tRecvLen = evbuffer_get_length(pstInputBuf);
        if (tRecvLen < FRAME_HEADER_MIN_SIZE)
            break;

        if (tRecvLen > sizeof(auchRecvBuf))
            tRecvLen = sizeof(auchRecvBuf);

        int iCopyLen = evbuffer_copyout(pstInputBuf, auchRecvBuf, tRecvLen);
        int iFrameSize = getFrameSize(auchRecvBuf);
        if (iFrameSize <= 0) {
            /* 프레임 헤더 손상 → 1바이트씩 버리며 재동기화 */
            evbuffer_drain(pstInputBuf, 1);
            continue;
        }

        if (iCopyLen < iFrameSize)
            break;
        
        evbuffer_drain(pstInputBuf, iFrameSize);

        stMsgId.uchSrcId = pstEventSrc->pstDispatcher->pstBaseCtx->usMyId;
        stMsgId.uchDstId = 0x00;

        /* Step 1: 요청 프레임 파싱 */
        eErr = requestFrame(auchRecvBuf, &stMsgId, iFrameSize, &unCmd);
        if (eErr != FRAME_OK || unCmd == 0xFFFF) {
            fprintf(stderr, "[UDS-Server] requestFrame() failed, err=%d\n", eErr);
            continue;
        }

        /* Step 2: 명령 처리 */
        eErr = commandHandler(auchRecvBuf, &stMsgId,
                              iFrameSize, auchCmdResult, &iSendLen);
        if (eErr != FRAME_OK || iSendLen <= 0) {
            continue;
        }

        /* Step 3: 응답 프레임 생성 */
        eErr = makeResFrame(unCmd, &stMsgId, auchCmdResult, auchSendBuf);
        if (eErr == FRAME_OK && iSendLen > 0) {            
            /* UDS 클라이언트로 응답 전송 */
            fprintf(stderr, "[UDS-Server] Send CMD=%04X, size=%d\n", unCmd, iSendLen);
            if (bufferevent_write(pstBufferEvent, auchSendBuf, iSendLen) < 0) {
                fprintf(stderr, "[UDS-Server] bufferevent_write() failed\n");
                break;
            }
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
    BASE_CONTEXT* pstBaseCtx = (BASE_CONTEXT*)pvArg;
    DISPATCHER* pstDispatcher  = (DISPATCHER*)pstBaseCtx->pvUserCtx;

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
        pstDispatcher,
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
    BASE_CONTEXT* pstBaseCtx = (BASE_CONTEXT*)pvArg;

    fprintf(stderr,"\n[TCP-SVR] SIGINT → shutdown\n");
    if(pstBaseCtx->pstEventBase)
        event_base_loopexit(pstBaseCtx->pstEventBase, NULL);
}



/* ========================================================================== */
/* Main Entry Point                                                           */
/* ========================================================================== */

int run(void)
{
    BASE_CONTEXT stBaseCtx;
    DISPATCHER   stDispatcher;

    baseContextInit(&stBaseCtx, UDS_1_SVR_ID);
    stBaseCtx.pstEventBase = event_base_new();
    if (!stBaseCtx.pstEventBase) {
        fprintf(stderr, "[UDS-Server] event_base_new() failed\n");
        return EXIT_FAILURE;
    }

    /* Dispatcher 초기화 */
    dispatcherInit(&stDispatcher, &stBaseCtx);
    stBaseCtx.pvUserCtx = &stDispatcher;

    int iListenFd = netUdsCreateServer(UDS_1_PATH);
    if (iListenFd < 0) {
        fprintf(stderr, "[UDS-Server] netUdsCreateServer() failed\n");
        return EXIT_FAILURE;
    }

    /* Accept 이벤트 등록 */
    struct event* stEventAccept = event_new(
            stBaseCtx.pstEventBase, iListenFd, 
            EV_READ | EV_PERSIST, acceptCb, &stBaseCtx);
    event_add(stEventAccept, NULL);

    /* SIGINT 처리 등록 */
    stBaseCtx.pstSignalEvent = evsignal_new(stBaseCtx.pstEventBase, 
        SIGINT, signalCb, &stBaseCtx);
    event_add(stBaseCtx.pstSignalEvent, NULL);

    fprintf(stderr, "[UDS-Server] Listening at %s\n", UDS_1_PATH);
    
    event_base_dispatch(stBaseCtx.pstEventBase);

    /* === 종료 처리 === */
    dispatcherCleanup(&stDispatcher);
    baseContextCleanup(&stBaseCtx);

    event_free(stEventAccept);
    netClose(iListenFd);
    event_base_free(stBaseCtx.pstEventBase);

    fprintf(stderr,"[TCP-SVR] Terminated.\n");
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
