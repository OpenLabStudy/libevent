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

#include "eventSource.h"
#include "dispatcher.h"
#include "netUds.h"
#include "netCore.h"
#include "frame.h"
#include "icdCommand.h"

/* ========================================================================== */
/* Application-level Read Callback (UDS Client)                               */
/* ========================================================================== */

static void readCallback(struct bufferevent* pstBufferEvent, void* pvData)
{
    unsigned char* puchRecvData;
    EVENT_SOURCE* pstEventSrc = (EVENT_SOURCE *)pvData;
    struct evbuffer* pstInputBuffer = bufferevent_get_input(pstBufferEvent);
    size_t ulDataLen = evbuffer_get_length(pstInputBuffer);
    fprintf(stderr, "[Client] Received %zu bytes\n", ulDataLen);
    puchRecvData = (unsigned char*)malloc(ulDataLen);
    if (!puchRecvData)
        return;

    evbuffer_copyout(pstInputBuffer, puchRecvData, ulDataLen);

    MSG_ID stMsgId;
    stMsgId.uchSrcId = pstEventSrc->pstDispatcher->pstBaseCtx->usMyId;
    stMsgId.uchDstId = UDS_1_SVR_ID;

    responseFrame(puchRecvData, &stMsgId, ulDataLen);

    evbuffer_drain(pstInputBuffer, ulDataLen);
    free(puchRecvData);
}



/* ========================================================================== */
/* Application-level Event Callback (UDS Client)                              */
/* ========================================================================== */

static void eventCallback(struct bufferevent* pstBufferEvent,
                       short nEvents, void* pvData)
{
    EVENT_SOURCE* pstEventSrc = (EVENT_SOURCE *)pvData;
    (void)pstBufferEvent;

    if (nEvents & BEV_EVENT_EOF) {
        fprintf(stderr, "[TCP-Client] Server disconnected\n");
    } else if (nEvents & BEV_EVENT_ERROR) {
        fprintf(stderr, "[TCP-Client] Client socket error\n");
    }

    /* 실제 close/free 는 eventSession 의 eventCallbackWrapper 에서 수행 */
    eventSourceDestroy(pstEventSrc);
    /* 이벤트 루프 종료 지시 */
    
    if (pstEventSrc->pstDispatcher->pstBaseCtx->pstEventBase)
        event_base_loopexit(pstEventSrc->pstDispatcher->pstBaseCtx->pstEventBase, NULL);
}



/* ========================================================================== */
/* STDIN → Request Frame builder                                             */
/* ========================================================================== */

static void stdinReadCb(evutil_socket_t sig, short nEvents, void* pvData)
{
    (void)nEvents;
    EVENT_SOURCE* pstEventSrc = (EVENT_SOURCE *)pvData;
    char achInput[1024];
    unsigned char auSendBuf[1024];
    int iSendLen = 0;
    FRAME_ERR eErr;

    if (!fgets(achInput, sizeof(achInput), stdin)) {
        event_base_loopexit(pstEventSrc->pstDispatcher->pstBaseCtx->pstEventBase, NULL);
        return;
    }

    achInput[strcspn(achInput, "\n")] = '\0';

    /* UDS 서버 ID를 목적지로 사용하는 예제 */
    MSG_ID stMsgId = { pstEventSrc->pstDispatcher->pstBaseCtx->usMyId, UDS_1_SVR_ID };

    if (!strcmp(achInput, "keepalive")) {
        fprintf(stderr,"[UDS-Client] REQ_KEEP_ALIVE\n");
        eErr = makeReqFrame(CMD_KEEP_ALIVE, &stMsgId, auSendBuf, &iSendLen);

    } else if (!strcmp(achInput, "ibit")) {
        fprintf(stderr,"[UDS-Client] REQ_IBIT\n");
        eErr = makeReqFrame(CMD_IBIT, &stMsgId, auSendBuf, &iSendLen);

    } else if (!strcmp(achInput, "quit") || !strcmp(achInput, "exit")) {
        event_base_loopexit(pstEventSrc->pstDispatcher->pstBaseCtx->pstEventBase, NULL);
        return;

    } else {
        fprintf(stderr, "Available commands:\n  keepalive\n  ibit\n  quit\n");
        return;
    }

    if (eErr == FRAME_OK && iSendLen > 0) {
        bufferevent_write(pstEventSrc->pstBufferEvent, 
            auSendBuf, (size_t)iSendLen);
    }
}


/* ========================================================================== */
/* Main Entry Point                                                           */
/* ========================================================================== */
int run(int iId)
{
    BASE_CONTEXT stBaseCtx;
    DISPATCHER   stDispatcher;
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
    stBaseCtx.pstEventBase = event_base_new();
    if (!stBaseCtx.pstEventBase) {
        fprintf(stderr, "[UDS-Client] event_base_new() failed\n");
        return EXIT_FAILURE;
    }
    dispatcherInit(&stDispatcher, &stBaseCtx);
    stBaseCtx.pvUserCtx = &stDispatcher;

    int iSockFd = netUdsCreateClient(UDS_1_PATH);
    if (iSockFd < 0) {
        fprintf(stderr, "[UDS-Client] Failed to create UDS client socket\n");
        return EXIT_FAILURE;
    }

    printf("[CLI] Connecting to %s\n", UDS_1_PATH);

    /* ------------------- */
    /* EVENT_SOURCE 생성   */
    /* ------------------- */
    netSetNonblock(iSockFd);

    eventSourceCreateWithBev(
        &stDispatcher,
        iSockFd,
        SRC_TYPE_UDS_CLIENT,
        SRC_ROLE_WORKER,
        readCallback,
        eventCallback
    );

    /* ------------------- */
    /* stdin 이벤트 등록   */
    /* ------------------- */
    struct event* evStdin = event_new(
        stBaseCtx.pstEventBase,
        STDIN_FILENO,
        EV_READ | EV_PERSIST,
        stdinReadCb,
        stDispatcher.pstEventSrc);  // arg로 EVENT_SOURCE 전달

    if (!evStdin) {
        printf("[CLI] evStdin create failed\n");
        // eventSourceDestroy(pstEventSrc);
        event_base_free(stBaseCtx.pstEventBase);
        return -1;
    }

    event_add(evStdin, NULL);

    /* ------------------- */
    /* 이벤트 루프 실행    */
    /* ------------------- */
    event_base_dispatch(stBaseCtx.pstEventBase);

    /* clean-up */
    event_free(evStdin);
    baseContextCleanup(&stBaseCtx);
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
