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
#include "eventEngine.h"
#include "netUds.h"
#include "netCore.h"
#include "frame.h"
#include "icdCommand.h"

static void ioChannelHandleEvent(int iFd, short nEvent, void* pvData)
{
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    IO_EVENT_TYPE eEventType = pstIoChannel->ePendingLogicEvent;
    unsigned char auchRecvBuffer[2048];
    unsigned char auchResultBuffer[64];
    unsigned char uchReult[sizeof(IPC_FRAME)];
    IPC_FRAME *pstIpcFrame = (IPC_FRAME *)uchReult;

    unsigned short unCmd = 0;
    FRAME_ERR eErr;
    
    switch (eEventType) {
    case IO_EVT_RX_DATA:
        /* protocol / packet 처리 */        
        while (1) {
            size_t tRecvLen = evbuffer_get_length(pstIoChannel->pstReadBuffer);
            /* 최소 헤더 */
            if (tRecvLen < sizeof(FRAME_HEADER))
                break;

            if (tRecvLen > sizeof(auchRecvBuffer))
                tRecvLen = sizeof(auchRecvBuffer);

            int iCopyLen = evbuffer_copyout(pstIoChannel->pstReadBuffer, auchRecvBuffer, tRecvLen);
            eErr = frameDecode(auchRecvBuffer, iCopyLen, FRAME_TYPE_REQUEST, &unCmd);
            if (eErr != FRAME_OK) {
                fprintf(stderr, "[TCP-SVR] frameDecode ERR: %s\n", frameErrToStr(eErr));
                evbuffer_drain(pstIoChannel->pstReadBuffer, 1);
                continue;
            }
            /* === CMD 먼저 추출 (가벼운 파싱) === */
            getCmdFromFrame(auchRecvBuffer, iCopyLen, &unCmd);
            int iFrameSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_REQUEST);
            if (iCopyLen < iFrameSize)
                break;
            MSG_ID stMsgId = { UDS_1_CLN1_ID, UDS_1_SVR_ID };

            /* === 프레임 하나 소비 === */
            evbuffer_drain(pstIoChannel->pstReadBuffer, iFrameSize);
            parseAndDumpResponse(auchRecvBuffer, auchResultBuffer);
        }
        break;

    case IO_EVT_CHANNEL_CLOSED:
        printf("[UDS-CLI] channel closed fd=%d\n", pstIoChannel->iFd);
        event_active(pstIoChannel->pstShutdownEvent, 0, 0);
        break;

    case IO_EVT_ERROR:
        printf("[UDS-CLI] channel error fd=%d\n", pstIoChannel->iFd);
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
    FRAME_ERR eErr;

    if (!fgets(achInput, sizeof(achInput), stdin)) {
        pstIoChannel->ePendingLogicEvent = IO_EVT_CHANNEL_CLOSED;
        event_active(pstIoChannel->pstLogicEvent, 0, 0);
        return;
    }

    achInput[strcspn(achInput, "\n")] = '\0';
    MSG_ID stMsgId = { UDS_1_CLN1_ID, UDS_1_SVR_ID };

    if (!strcmp(achInput, "keepalive")) {
        fprintf(stderr,"[TCP-CLI] REQ_KEEP_ALIVE\n");
        eErr = makeRequestFrame(CMD_KEEP_ALIVE, &stMsgId, auSendBuf);

    } else if (!strcmp(achInput, "ibit")) {
        fprintf(stderr,"[TCP-CLI] REQ_IBIT\n");
        eErr = makeRequestFrame(CMD_IBIT, &stMsgId, auSendBuf);

    } else if (!strcmp(achInput, "quit") || !strcmp(achInput, "exit")) {
        pstIoChannel->ePendingLogicEvent = IO_EVT_CHANNEL_CLOSED;
        event_active(pstIoChannel->pstShutdownEvent, 0, 0);
        return;
    } else {
        fprintf(stderr, "Available commands:\n" 
            " keepalive\n"
            "  ibit\n"
            "  quit\n");
        return;
    }

    if (eErr == FRAME_OK) {
        int iSendLen = getFrameSizeWithData(auSendBuf, FRAME_TYPE_REQUEST);
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

    fprintf(stderr,"\n[UDS-CLI] SIGINT → shutdown\n");
    if(pstEventEngine->pstEventBase)
        event_base_loopexit(pstEventEngine->pstEventBase, NULL);
}

/* ========================================================================== */
/* Main Entry Point                                                           */
/* ========================================================================== */
int run(int iId)
{
    EVENT_ENGINE   stEventEngine;
    unsigned char uchMyId = 0x00;
    if(iId == 1)
        uchMyId = UDS_1_CLN1_ID;
    else if(iId == 2)
        uchMyId = UDS_1_CLN2_ID;
    else if(iId == 3)
        uchMyId = UDS_1_CLN3_ID;
    else if(iId == 4)
        uchMyId = UDS_1_CLN4_ID;

    stEventEngine.pstEventBase = event_base_new();
    if (!stEventEngine.pstEventBase) {
        printf("[UDS-CLI] event_base_new failed\n");
        return -1;
    }
    eventEngineInit(&stEventEngine);
    
    int iClientSock = netUdsCreateClient(UDS_1_PATH);
    if (iClientSock < 0) {
        fprintf(stderr, "[UDS-CLI] Failed to create UDS client socket\n");
        return EXIT_FAILURE;
    }

    printf("[UDS-CLI] Connecting to %s\n", UDS_1_PATH);

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


#ifndef GOOGLE_TEST
int main(int argc, char** argv)
{
    (void)argc; (void)argv;
    if(argc != 2)
        return 0;
    return run(atoi(argv[1]));
}
#endif
