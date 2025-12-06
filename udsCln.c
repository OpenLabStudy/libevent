/**
 * @file udsCln.c
 * @brief Libevent 기반 UDS Client Application Layer
 *
 * TCP 클라이언트(tcpCln.c) 구조를 그대로 가져와서
 * netUdsCreateClient() 를 사용하는 버전.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include "netUds.h"
#include "netCore.h"
#include "eventSession.h"
#include "frame.h"
#include "icdCommand.h"

/* ========================================================================== */
/* Static Function Prototypes                                                 */
/* ========================================================================== */

static void appReadCb(struct bufferevent* pstBufferEvent, void* pvData);
static void appEventCb(struct bufferevent* pstBufferEvent,
                       short nEvents, void* pvData);
static void stdinReadCb(evutil_socket_t sig, short nEvents, void* pvData);
static void signalCb(evutil_socket_t sig, short ev, void* pvData);



/* ========================================================================== */
/* Application-level Read Callback (UDS Client)                               */
/* ========================================================================== */

static void appReadCb(struct bufferevent* pstBufferEvent, void* pvData)
{
    unsigned short unCmd = 0;
    unsigned char auCmdResult[1000];
    unsigned char auSendBuf[1024];
    int iSendLen = 0;
    FRAME_ERR eErr;
    MSG_ID stMsgId;
    SOCK_CONTEXT* pstSockCtx = (SOCK_CONTEXT*)pvData;
    if (!pstSockCtx)
        return;

    struct evbuffer* pstEvBuffer = bufferevent_get_input(pstBufferEvent);
    size_t tDataLen = evbuffer_get_length(pstEvBuffer);

    fprintf(stderr, "[UDS-Client] Received %zu bytes\n", tDataLen);

    if (!tDataLen)
        return;

    unsigned char* puchRecvData = malloc(tDataLen);
    if (!puchRecvData)
        return;

    evbuffer_copyout(pstEvBuffer, puchRecvData, tDataLen);
    
    stMsgId.uchSrcId = pstSockCtx->uchSrcId;
    stMsgId.uchDstId = getSrcId(puchRecvData);

    fprintf(stderr, "[UDS Client] %02x %02x\n", pstSockCtx->uchSrcId, pstSockCtx->uchDstId);
    int iFrameSize = getFrameSize(puchRecvData);

    /* === 헤더 및 명령 추출 === */
    eErr = requestFrame(puchRecvData, &stMsgId, iFrameSize, &unCmd);
    if (eErr != FRAME_OK) {
        fprintf(stderr, "[APP] requestFrame ERR: %s\n", frameErrToStr(eErr));
    }

    /* === 명령 처리 === */
    eErr = commandHandler(puchRecvData, &stMsgId, iFrameSize, auCmdResult, &iSendLen);

    /* === 응답 프레임 생성 === */
    eErr = makeResFrame(unCmd, &stMsgId, auCmdResult, auSendBuf);

    fprintf(stderr, "[APP] Send CMD=%04X, size=%d\n", unCmd, iSendLen);
    if (bufferevent_write(pstBufferEvent, auSendBuf, iSendLen) < 0) {
        fprintf(stderr, "[APP] bufferevent_write() failed\n");
    }

    evbuffer_drain(pstEvBuffer, tDataLen);
    free(puchRecvData);

    
}



/* ========================================================================== */
/* Application-level Event Callback (UDS Client)                              */
/* ========================================================================== */

static void appEventCb(struct bufferevent* pstBufferEvent,
                       short nEvents, void* pvData)
{
    (void)pstBufferEvent;
    SOCK_CONTEXT* pstSockCtx = (SOCK_CONTEXT*)pvData;
    BASE_CONTEXT* pstBaseCtx = pstSockCtx ? pstSockCtx->pstBaseCtx : NULL;

    if (nEvents & BEV_EVENT_CONNECTED) {
        fprintf(stderr,"[UDS-Client] Connected to server.\n");
    }

    if (nEvents & BEV_EVENT_EOF) {
        fprintf(stderr,"[UDS-Client] Server closed connection.\n");
        shutdownApp(pstBaseCtx);
    }

    if (nEvents & BEV_EVENT_ERROR) {
        fprintf(stderr,"[UDS-Client] Error: %s\n",
            evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
        shutdownApp(pstBaseCtx);
    }
}



/* ========================================================================== */
/* STDIN → Request Frame builder                                             */
/* ========================================================================== */

static void stdinReadCb(evutil_socket_t sig, short nEvents, void* pvData)
{
    (void)sig;
    (void)nEvents;

    SOCK_CONTEXT *pstSockCtx  = (SOCK_CONTEXT *)pvData;
    BASE_CONTEXT* pstBaseCtx = pstSockCtx ? pstSockCtx->pstBaseCtx : NULL;

    char           achInput[1024];
    unsigned char  auSendBuf[1024];
    int            iSendLen = 0;
    FRAME_ERR      eErr;

    if (!fgets(achInput, sizeof(achInput), stdin)) {
        if (pstBaseCtx && pstBaseCtx->pstEventBase)
            event_base_loopexit(pstBaseCtx->pstEventBase, NULL);
        return;
    }

    achInput[strcspn(achInput, "\n")] = '\0';

    /* UDS 서버 ID를 목적지로 사용하는 예제 */
    MSG_ID stMsgId = { UDS_1_CLN1_ID, UDS_1_SVR_ID };

    if (!strcmp(achInput, "keepalive")) {
        fprintf(stderr,"[UDS-Client] REQ_KEEP_ALIVE\n");
        eErr = makeReqFrame(CMD_KEEP_ALIVE, &stMsgId, auSendBuf, &iSendLen);

    } else if (!strcmp(achInput, "ibit")) {
        fprintf(stderr,"[UDS-Client] REQ_IBIT\n");
        eErr = makeReqFrame(CMD_IBIT, &stMsgId, auSendBuf, &iSendLen);

    } else if (!strcmp(achInput, "quit") || !strcmp(achInput, "exit")) {
        if (pstBaseCtx && pstBaseCtx->pstEventBase)
            event_base_loopexit(pstBaseCtx->pstEventBase, NULL);
        return;

    } else {
        fprintf(stderr, "Available commands:\n  keepalive\n  ibit\n  quit\n");
        return;
    }

    if (eErr == FRAME_OK && iSendLen > 0) {
        bufferevent_write(pstSockCtx->pstBufferEvent,
                          auSendBuf, (size_t)iSendLen);
    }
}



/* ========================================================================== */
/* Signal Handling (UDS Client)                                               */
/* ========================================================================== */

static void signalCb(evutil_socket_t sig, short ev, void* pvData)
{
    (void)ev;
    BASE_CONTEXT* pstBaseCtx = (BASE_CONTEXT*)pvData;

    if (sig == SIGINT) {
        fprintf(stderr, "\n[UDS-Client] SIGINT received. Stopping event loop...\n");
        shutdownApp(pstBaseCtx);
    }
}



/* ========================================================================== */
/* Main Entry Point                                                           */
/* ========================================================================== */
int run(int iId)
{
    BASE_CONTEXT stBaseCtx;
    unsigned char uchMyId = 0x00;
    if(iId == 1)
        uchMyId = UDS_1_CLN1_ID;
    else if(iId == 2)
        uchMyId = UDS_1_CLN2_ID;
    else if(iId == 3)
        uchMyId = UDS_1_CLN3_ID;
    else if(iId == 4)
        uchMyId = UDS_1_CLN4_ID;
    baseContextInit(&stBaseCtx, uchMyId);

    int iSockFd = netUdsCreateClient(UDS_1_PATH);
    if (iSockFd < 0) {
        fprintf(stderr, "[UDS-Client] Failed to create UDS client socket\n");
        return EXIT_FAILURE;
    }

    stBaseCtx.pstEventBase = event_base_new();
    if (!stBaseCtx.pstEventBase) {
        fprintf(stderr, "[UDS-Client] event_base_new() failed\n");
        close(iSockFd);
        return EXIT_FAILURE;
    }

    SOCK_CONTEXT* pstSockCtx = calloc(1, sizeof(SOCK_CONTEXT));
    if (!pstSockCtx) {
        perror("calloc");
        close(iSockFd);
        return EXIT_FAILURE;
    }
    
    initSocketContext(pstSockCtx, NULL, RESPONSE_ENABLED);
    pstSockCtx->pstBaseCtx = &stBaseCtx;    

    /* 3) bufferevent 생성 및 콜백 등록 */
    pstSockCtx->pstBufferEvent =
        bufferevent_socket_new(stBaseCtx.pstEventBase,
                               iSockFd,
                               BEV_OPT_CLOSE_ON_FREE);
    if (!pstSockCtx->pstBufferEvent) {
        fprintf(stderr, "[UDS-Client] bufferevent_socket_new() failed\n");
        event_base_free(stBaseCtx.pstEventBase);
        free(pstSockCtx);
        close(iSockFd);
        return EXIT_FAILURE;
    }

    bufferevent_setcb(
        pstSockCtx->pstBufferEvent,
        appReadCb,
        NULL,
        appEventCb,
        pstSockCtx
    );
    bufferevent_enable(pstSockCtx->pstBufferEvent, EV_READ | EV_WRITE);

    /* 4) STDIN 이벤트 등록 */
    stBaseCtx.pstEvent = event_new(
        stBaseCtx.pstEventBase,
        fileno(stdin),
        EV_READ | EV_PERSIST,
        stdinReadCb,
        pstSockCtx
    );
    if (!stBaseCtx.pstEvent ||
        event_add(stBaseCtx.pstEvent, NULL) < 0) {
        fprintf(stderr, "[UDS-Client] stdin event_new/event_add failed\n");
        event_base_free(stBaseCtx.pstEventBase);
        free(pstSockCtx);
        close(iSockFd);
        return EXIT_FAILURE;
    }

    /* 5) SIGINT 이벤트 등록 */
    stBaseCtx.pstSignalEvent = evsignal_new(
        stBaseCtx.pstEventBase,
        SIGINT,
        signalCb,
        &stBaseCtx
    );
    if (!stBaseCtx.pstSignalEvent ||
        event_add(stBaseCtx.pstSignalEvent, NULL) < 0) {
        fprintf(stderr, "[UDS-Client] evsignal_new/event_add failed\n");
        event_base_free(stBaseCtx.pstEventBase);
        free(pstSockCtx);
        close(iSockFd);
        return EXIT_FAILURE;
    }

    fprintf(stderr, "[UDS-Client] Connecting to %s ...\n", UDS_1_PATH);

    /* 6) 이벤트 루프 진입 */
    event_base_dispatch(stBaseCtx.pstEventBase);

    /* 7) 정리 */
    closeAndFree(pstSockCtx);

    if (stBaseCtx.pstSignalEvent)
        event_free(stBaseCtx.pstSignalEvent);

    if (stBaseCtx.pstEvent)
        event_free(stBaseCtx.pstEvent);

    if (stBaseCtx.pstEventBase)
        event_base_free(stBaseCtx.pstEventBase);

    return EXIT_SUCCESS;
}


#ifndef GOOGLE_TEST
int main(int argc, char** argv)
{
    (void)argc; (void)argv;
    if(argc != 2)
        return 0;
    return run(atoi(argv[1]));
}
#endif
