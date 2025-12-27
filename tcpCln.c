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
    unsigned char uchResult[sizeof(IPC_FRAME)];
    IPC_FRAME *pstIpcFrame = (IPC_FRAME *)uchResult;

    unsigned short unCmd = 0;
    FRAME_ERR eErr;

    switch (eEventType) {
    case IO_EVT_RX_DATA:
        while (1) {
            size_t tRecvLen = evbuffer_get_length(pstIoChannel->pstReadBuffer);
            /* 최소 헤더 */
            if (tRecvLen < sizeof(FRAME_HEADER))
                break;

            if (tRecvLen > sizeof(auchRecvBuffer))
                tRecvLen = sizeof(auchRecvBuffer);

            int iCopyLen = evbuffer_copyout(pstIoChannel->pstReadBuffer, auchRecvBuffer, tRecvLen);
            fprintf(stderr,"### %s():%d %d ###\n",__func__,__LINE__, iCopyLen);
            /* === CMD 추출 === */
            eErr = getCmdFromFrame(auchRecvBuffer, iCopyLen, &unCmd);
            if (eErr == FRAME_ERR_NEED_MORE_DATA)
                break;

            if (eErr != FRAME_OK) {
                evbuffer_drain(pstIoChannel->pstReadBuffer, 1);
                continue;
            }

            int iFrameSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_RESPONSE);
            if (iFrameSize <= 0) {
                evbuffer_drain(pstIoChannel->pstReadBuffer, 1);
                continue;
            }

            if (iCopyLen < iFrameSize)
                break;

            /* === 프레임 소비 === */
            evbuffer_drain(pstIoChannel->pstReadBuffer, iFrameSize);

            /* === 프레임 검증 === */
            eErr = frameDecode(auchRecvBuffer, iFrameSize, FRAME_TYPE_RESPONSE, &unCmd);
            if (eErr != FRAME_OK) {
                fprintf(stderr, "[TCP-CLI] frameDecode ERR: %s\n", frameErrToStr(eErr));
                continue;
            }
            parseAndDumpResponse(auchRecvBuffer, pstIpcFrame->auchResult);

            /* === IPC_FRAME 구성 === */
            memset(pstIpcFrame, 0x00, sizeof(IPC_FRAME));
            pstIpcFrame->unStx = STX_CONST;
            pstIpcFrame->unCmd = unCmd;
            pstIpcFrame->iResultSize = getDataSize(unCmd, FRAME_TYPE_RESPONSE);
            memcpy(pstIpcFrame->auchResult, auchRecvBuffer + sizeof(FRAME_HEADER), pstIpcFrame->iResultSize);
            pstIpcFrame->unEtx = ETX_CONST;

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
    fprintf(stderr, "[TCP-CLI] stdin fired\n");
    (void)nEvents;

    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;

    char achInput[1024];
    unsigned char auSendBuf[1024];
    FRAME_ERR eErr;
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);

    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    if (!fgets(achInput, sizeof(achInput), stdin)) {
        fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
        pstIoChannel->ePendingLogicEvent = IO_EVT_CHANNEL_CLOSED;
        event_active(pstIoChannel->pstLogicEvent, 0, 0);
        fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
        return;
    }
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);

    achInput[strcspn(achInput, "\n")] = '\0';
    MSG_ID stMsgId = { TCP_CLN_ID, TCP_SVR_ID };

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

    fprintf(stderr,"\n[TCP-CLI] SIGINT → shutdown\n");
    if(pstEventEngine->pstEventBase)
        event_base_loopexit(pstEventEngine->pstEventBase, NULL);
}

int run()
{
    EVENT_ENGINE   stEventEngine;
    fprintf(stderr, "[TCP-CLI] isatty(stdin)=%d\n", isatty(STDIN_FILENO));
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
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    eventSourceCreateWithBev(&stEventEngine, iClientSock,
        TYPE_TCP_CLI, ROLE_WORKER,
        NULL, NULL, ioChannelHandleEvent
    );
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);

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
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    event_add(evStdin, NULL);
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);

    struct event   *pstSignalEvent;
    /* SIGINT 처리 등록 */
    pstSignalEvent = evsignal_new(stEventEngine.pstEventBase, 
        SIGINT, signalCb, &stEventEngine);
    event_add(pstSignalEvent, NULL);
fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
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