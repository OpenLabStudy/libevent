/**
 * @file tcpCln.c
 * @brief EVENT_SOURCE + netTcp 기반 TCP Client (NO GLOBAL VARIABLES, stdin 이벤트 기반)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <event2/event.h>

#include "eventSource.h"
#include "eventEngine.h"
#include "netTcp.h"
#include "netCore.h"
#include "frame.h"
#include "icdCommand.h"
 
/* ============================================================
  * 서버 → 클라이언트 수신 콜백
  * ============================================================ */
static void readCallback(struct bufferevent* pstBufferEvent, void* pvData)
{
    unsigned char* puchRecvData;
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    struct evbuffer* pstInputBuffer = bufferevent_get_input(pstBufferEvent);
    size_t ulDataLen = evbuffer_get_length(pstInputBuffer);
    fprintf(stderr, "[Client] Received %zu bytes\n", ulDataLen);
    puchRecvData = (unsigned char*)malloc(ulDataLen);
    if (!puchRecvData)
        return;

    evbuffer_copyout(pstInputBuffer, puchRecvData, ulDataLen);
    MSG_ID stMsgId = { TCP_CLN_ID, TCP_SVR_ID };
    
    responseFrame(puchRecvData, &stMsgId, ulDataLen);

    evbuffer_drain(pstInputBuffer, ulDataLen);
    free(puchRecvData);
}
 
 /* ============================================================
  * 서버 이벤트 콜백 (EOF / ERROR)
  * ============================================================ */
static void eventCallback(struct bufferevent* pstBufferEvent,
    short nEvents, void* pvData)
{
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    (void)pstBufferEvent;

    if (nEvents & BEV_EVENT_EOF) {
        fprintf(stderr, "[TCP-Client] Server disconnected\n");
    } else if (nEvents & BEV_EVENT_ERROR) {
        fprintf(stderr, "[TCP-Client] Client socket error\n");
    }

    /* 실제 close/free 는 eventSession 의 eventCallbackWrapper 에서 수행 */
    eventSourceDestroy(pstIoChannel);
    /* 이벤트 루프 종료 지시 */
    
    if (pstIoChannel->pstEventBase)
        event_base_loopexit(pstIoChannel->pstEventBase, NULL);
}

/* ============================================================
* stdin 이벤트 콜백
* ============================================================ */
static void stdinReadCb(int iFd, short nEvents, void* pvData)
{
    (void)nEvents;
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    char achInput[1024];
    unsigned char auSendBuf[1024];
    int iSendLen = 0;
    FRAME_ERR eErr;

    if (!fgets(achInput, sizeof(achInput), stdin)) {
        event_base_loopexit(pstIoChannel->pstEventBase, NULL);
        return;
    }

    achInput[strcspn(achInput, "\n")] = '\0';

    MSG_ID stMsgId = { TCP_CLN_ID, TCP_SVR_ID };

    if (!strcmp(achInput, "keepalive")) {
        fprintf(stderr,"[Client] REQ_KEEP_ALIVE\n");
        eErr = makeReqFrame(CMD_KEEP_ALIVE, &stMsgId, auSendBuf, &iSendLen);

    } else if (!strcmp(achInput, "ibit")) {
        fprintf(stderr,"[Client] REQ_IBIT\n");
        eErr = makeReqFrame(CMD_IBIT, &stMsgId, auSendBuf, &iSendLen);

    } else if (!strcmp(achInput, "quit") || !strcmp(achInput, "exit")) {
        event_base_loopexit(pstIoChannel->pstEventBase, NULL);
        return;

    } else {
        fprintf(stderr, "Available commands:\n  keepalive\n  ibit\n  quit\n");
        return;
    }

    if (eErr == FRAME_OK && iSendLen > 0) {
        bufferevent_write(pstIoChannel->pstBufferEvent, 
            auSendBuf, (size_t)iSendLen);
    }
}


int run()
{
    EVENT_ENGINE   stEventEngine;
    // baseContextInit(&stBaseCtx, TCP_CLN_ID);

    stEventEngine.pstEventBase = event_base_new();
    if (!stEventEngine.pstEventBase) {
        printf("[CLI] event_base_new failed\n");
        return -1;
    }
    eventEngineInit(&stEventEngine);

    /* ------------------- */
    /* TCP 연결            */
    /* ------------------- */
    int iClientSock = netTcpCreateClient("127.0.0.1", SERVER_PORT);
    if (iClientSock < 0) {
        perror("netTcpCreateClient");
        event_base_free(stEventEngine.pstEventBase);
        return -1;
    }

    printf("[CLI] Connecting to 127.0.0.1:5000...\n");

    /* ------------------- */
    /* EVENT_SOURCE 생성   */
    /* ------------------- */
    netSetNonblock(iClientSock);

    eventSourceCreateWithBev(
        &stEventEngine,
        iClientSock,
        SRC_TYPE_TCP_CLIENT,
        SRC_ROLE_WORKER,
        readCallback,
        eventCallback
    );

    /* ------------------- */
    /* stdin 이벤트 등록   */
    /* ------------------- */
    struct event* evStdin = event_new(
        stEventEngine.pstEventBase,
        STDIN_FILENO,
        EV_READ | EV_PERSIST,
        stdinReadCb,
        stEventEngine.pstIoChannel);  // arg로 EVENT_SOURCE 전달

    if (!evStdin) {
        printf("[CLI] evStdin create failed\n");
        // eventSourceDestroy(pstEventSrc);
        event_base_free(stEventEngine.pstEventBase);
        return -1;
    }

    event_add(evStdin, NULL);

    /* ------------------- */
    /* 이벤트 루프 실행    */
    /* ------------------- */
    event_base_dispatch(stEventEngine.pstEventBase);

    /* clean-up */
    event_free(evStdin);
    event_base_free(stEventEngine.pstEventBase);

    return 0;
}

/* === main === */
#ifndef GOOGLE_TEST
int main(int argc, char** argv)
{
    return run();
}
#endif