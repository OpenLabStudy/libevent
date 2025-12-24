/**
 * @file tcpCln.c
 * @brief EVENT_SOURCE + netTcp 기반 TCP Client (NO GLOBAL VARIABLES, stdin 이벤트 기반)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>

#include <event2/event.h>

#include "eventEngine.h"
#include "icdCommand.h"
#include "tcpSvr.h"

static void ioChannelHandleEvent(int iFd, short nEvent, void* pvData)
{
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    IO_EVENT_TYPE eEventType = pstIoChannel->ePendingLogicEvent;
    unsigned char auchRecvBuffer[2048];
    unsigned char uchReult[sizeof(IPC_FRAME)];
    IPC_FRAME *pstIpcFrame = uchReult;
    switch (eEventType) {
    case IO_EVT_RX_DATA:
        /* protocol / packet 처리 */        
        while (1) {
            size_t tRecvLen = evbuffer_get_length(pstIoChannel->pstReadBuffer);
            if (tRecvLen < FRAME_HEADER_MIN_SIZE)
                break;

            if (tRecvLen > sizeof(auchRecvBuffer))
                tRecvLen = sizeof(auchRecvBuffer);

            int iCopyLen = evbuffer_copyout(pstIoChannel->pstReadBuffer, auchRecvBuffer, tRecvLen);
            MSG_ID stMsgId = { TCP_CLN_ID, TCP_SVR_ID };
            //todo
            pstIpcFrame->unStx = STX_CONST;
            fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
            responseFrame(auchRecvBuffer, &stMsgId, iCopyLen, &pstIpcFrame->unCmd, pstIpcFrame->auchResult);
            pstIpcFrame->uiResultSize = getDataSize(pstIpcFrame->unCmd);-sizeof(FRAME_HEADER)-sizeof(FRAME_TAIL);
            pstIpcFrame->unEtx = ETX_CONST;
            evbuffer_drain(pstIoChannel->pstReadBuffer, iCopyLen);
        }
        break;

    case IO_EVT_CHANNEL_CLOSED:
        printf("[TCP-CLI] channel closed fd=%d\n", pstIoChannel->iFd);
        event_active(pstIoChannel->pstShutdownEvent, 0, 0);
        break;

    case IO_EVT_ERROR:
        printf("[TCP-CLI] channel error fd=%d\n", pstIoChannel->iFd);
        event_active(pstIoChannel->pstShutdownEvent, 0, 0);
        break;

    default:
        break;
    }
    pstIoChannel->ePendingLogicEvent = IO_EVENT_NONE;
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
        pstIoChannel->ePendingLogicEvent = IO_EVT_CHANNEL_CLOSED;
        event_active(pstIoChannel->pstLogicEvent, 0, 0);
        return;
    }

    achInput[strcspn(achInput, "\n")] = '\0';
    MSG_ID stMsgId = { TCP_CLN_ID, TCP_SVR_ID };

    if (!strcmp(achInput, "keepalive")) {
        fprintf(stderr,"[TCP-CLI] REQ_KEEP_ALIVE\n");
        eErr = makeReqFrame(CMD_KEEP_ALIVE, &stMsgId, auSendBuf, &iSendLen);

    } else if (!strcmp(achInput, "ibit")) {
        fprintf(stderr,"[TCP-CLI] REQ_IBIT\n");
        eErr = makeReqFrame(CMD_IBIT, &stMsgId, auSendBuf, &iSendLen);

    } else if (!strcmp(achInput, "quit") || !strcmp(achInput, "exit")) {
        pstIoChannel->ePendingLogicEvent = IO_EVT_CHANNEL_CLOSED;
        event_active(pstIoChannel->pstShutdownEvent, 0, 0);
        return;
    } else {
        fprintf(stderr, "Available commands:\n  keepalive\n  ibit\n  quit\n");
        return;
    }

    if (eErr == FRAME_OK && iSendLen > 0) {
        evbuffer_add(pstIoChannel->pstWriteBuffer, auSendBuf, iSendLen);
        event_add(pstIoChannel->pstWriteEvent, NULL); 
    } 
}

/* ============================================================
* SIGINT 콜백
* ============================================================ */
static void signalCb(evutil_socket_t sig, short events, void* pvArg)
{
    EVENT_ENGINE* pstEventEngine = (EVENT_ENGINE *)pvArg;

    fprintf(stderr,"\n[TCP-CLI] SIGINT → shutdown\n");
    if(pstEventEngine->pstEventBase)
        event_base_loopexit(pstEventEngine->pstEventBase, NULL);
}

int run()
{
    EVENT_ENGINE   stEventEngine;
    stEventEngine.pstEventBase = event_base_new();
    if (!stEventEngine.pstEventBase) {
        printf("[TCP-CLI] event_base_new failed\n");
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

    printf("[TCP-CLI] Connecting to 127.0.0.1:5000...\n");

    /* ------------------- */
    /* EVENT_SOURCE 생성   */
    /* ------------------- */
    netSetNonblock(iClientSock);

    eventSourceCreateWithBev(&stEventEngine, iClientSock,
        TYPE_TCP_CLI, ROLE_WORKER,
        NULL, NULL, ioChannelHandleEvent
    );

    /* ------------------- */
    /* stdin 이벤트 등록   */
    /* ------------------- */
    struct event* evStdin = event_new(
        stEventEngine.pstEventBase,
        STDIN_FILENO,
        EV_READ | EV_PERSIST,
        stdinReadCb,
        stEventEngine.pstIoChannelList);
    if (!evStdin) {
        printf("[TCP-CLI] evStdin create failed\n");
        event_base_free(stEventEngine.pstEventBase);
        return -1;
    }
    event_add(evStdin, NULL);

    struct event   *pstSignalEvent;
    /* SIGINT 처리 등록 */
    pstSignalEvent = evsignal_new(stEventEngine.pstEventBase, 
        SIGINT, signalCb, &stEventEngine);
    event_add(pstSignalEvent, NULL);

    /* ------------------- */
    /* 이벤트 루프 실행    */
    /* ------------------- */
    event_base_dispatch(stEventEngine.pstEventBase);

    if(evStdin){
        event_del(evStdin);
        event_free(evStdin);
        evStdin =  NULL;
    }
    if(pstSignalEvent){
        event_del(pstSignalEvent);
        event_free(pstSignalEvent);
        pstSignalEvent =  NULL;
    }
    
    eventEngineCleanup(&stEventEngine);
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