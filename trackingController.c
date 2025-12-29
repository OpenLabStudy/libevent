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
#include "udsSvr.h"
#include "udsFrame.h"

#define SERVER_PORT 5000

void tcpWriteCallback(int iFd, short nEvent, void* pvData)
{
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    unsigned char auchWriteBuffer[2048];
    unsigned char auchSendBuf[1024];
    int iWriteSize;
    int iDataSize;
    FRAME_ERR eErr;
    iDataSize = evbuffer_get_length(pstIoChannel->pstWriteBuffer);    
    if (iDataSize == 0) {
        event_del(pstIoChannel->pstWriteEvent);
        return;
    }
    iDataSize = evbuffer_remove(pstIoChannel->pstWriteBuffer, auchWriteBuffer, sizeof(auchWriteBuffer));    
    IPC_FRAME *pstIpcFrame = (IPC_FRAME *)auchWriteBuffer;
    iWriteSize = getFrameSizeWithCmd(pstIpcFrame->unCmd, FRAME_TYPE_RESPONSE);
    fprintf(stderr, "[TCP-SVR] CMD=%04X, size=%d,%d,%d FD:%d\n", pstIpcFrame->unCmd, iDataSize, pstIpcFrame->iResultSize, iWriteSize, pstIoChannel->iFd);
    MSG_ID stMsgId = { TCP_SVR_ID, TCP_CLN_ID };
    eErr = makeResponseFrame(pstIpcFrame->unCmd, &stMsgId, pstIpcFrame->auchResult, auchSendBuf);

    iWriteSize = write(pstIoChannel->iFd, auchSendBuf, iWriteSize);
    if (iWriteSize <= 0) {
        perror("[TCP-SVR] write");
        event_active(pstIoChannel->pstShutdownEvent, 0, 0);
        return;
    }
    if (evbuffer_get_length(pstIoChannel->pstWriteBuffer) == 0)
        event_del(pstIoChannel->pstWriteEvent);
}

static void tcpIoChannelHandleEvent(int iFd, short nEvent, void* pvData)
{
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    IO_EVENT_TYPE eEventType = pstIoChannel->ePendingLogicEvent;

    unsigned char auchRecvBuffer[2048];
    unsigned short unCmd = 0;
    FRAME_ERR eErr;
    int iSendLen = 0;

    switch (eEventType) {

    case IO_EVT_RX_DATA:
        while (1) {
            unsigned int uiRecvLen = evbuffer_get_length(pstIoChannel->pstReadBuffer);
            /* 최소 헤더도 안 왔으면 중단 */
            if (uiRecvLen < sizeof(FRAME_HEADER))
                break;

            memset(auchRecvBuffer, 0x00, sizeof(auchRecvBuffer));
            int iCopyLen = evbuffer_copyout(pstIoChannel->pstReadBuffer, auchRecvBuffer, uiRecvLen);
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

            /* === 프레임 하나 소비 === */
            evbuffer_drain(pstIoChannel->pstReadBuffer, iFrameSize);

            /* === 처리 경로 결정 === */
            PROCESS_PATH eProcPath = decideProcessingPath(auchRecvBuffer);
            if (eProcPath == PROCESS_LOCAL) {
                /* === 명령 처리 === */
                unsigned char auchResult[128];
                unsigned char auchSendBuf[1024];
                int iResultSize;
                eErr = commandHandler(auchRecvBuffer, auchResult, &iResultSize);
                if (eErr != FRAME_OK || iResultSize <= 0)
                    continue;
                MSG_ID stMsgId = { TCP_SVR_ID, TCP_CLN_ID };
                eErr = makeResponseFrame(unCmd, &stMsgId, auchResult, auchSendBuf);

                evbuffer_add(pstIoChannel->pstWriteBuffer, auchSendBuf, getFrameSizeWithCmd(unCmd, FRAME_TYPE_RESPONSE));
                event_add(pstIoChannel->pstWriteEvent, NULL);
            } else if (eProcPath == PROCESS_VIA_IPC) {
                /* === IPC 전달 (Fan-out 진입점) === */
                evbuffer_add(pstIoChannel->pstRequestBuffer, auchRecvBuffer, iFrameSize);
                event_active(pstIoChannel->pstRequestEvent, 0, 0);
            }
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

static void udsIoChannelHandleEvent(int iFd, short nEvent, void* pvData)
{
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    IO_EVENT_TYPE eEventType = pstIoChannel->ePendingLogicEvent;

    unsigned char auchRecvBuffer[2048];
    unsigned short unCmd = 0;
    FRAME_ERR eErr;

    switch (eEventType) {

    case IO_EVT_RX_DATA:
        while (1) {
            size_t tRecvLen = evbuffer_get_length(pstIoChannel->pstReadBuffer);
            /* 최소 헤더도 없으면 중단 */
            if (tRecvLen < sizeof(FRAME_HEADER))
                break;

            memset(auchRecvBuffer, 0x00, sizeof(auchRecvBuffer));
            int iCopyLen = evbuffer_copyout(pstIoChannel->pstReadBuffer,
                                            auchRecvBuffer, tRecvLen);
            /* frameDecode에 대한 처리가 완전한지 확인 필요*/                                            
            eErr = frameDecode(auchRecvBuffer, iCopyLen, FRAME_TYPE_RESPONSE, &unCmd);
            if (eErr != FRAME_OK) {
                fprintf(stderr, "[TCP-SVR] %s():%d frameDecode ERR: %s\n", __func__,__LINE__,frameErrToStr(eErr));
                evbuffer_drain(pstIoChannel->pstReadBuffer, 1);
                continue;
            }
            int iFrameSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_RESPONSE);
            /* === 프레임 소비 === */
            evbuffer_drain(pstIoChannel->pstReadBuffer, iFrameSize + sizeof(unsigned int));
            if(unCmd == 0x0001){
                //CMD_ID_INFO
                pstIoChannel->iWorkerId = (int)getIdInfo(auchRecvBuffer);
                fprintf(stderr,"ID is %d\n", pstIoChannel->iWorkerId);
            }else{
                /* Response Frame + RequestId(4B) */
                if (iCopyLen < iFrameSize + sizeof(unsigned int))
                    break;
                /* === Fan-in: 엔진에 위임 (저장만) === */
                eventEngineHandleWorkerResponse(pstIoChannel->pstEventEngine, pstIoChannel,
                    auchRecvBuffer, iFrameSize);
            }
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
* Accept 콜백
* ============================================================ */
static void acceptCb(evutil_socket_t iListenFd, short nKindOfEvent, void* pvArg)
{
    (void)nKindOfEvent;
    EVENT_ENGINE* pstEventEngine = (EVENT_ENGINE *)pvArg;
    IO_CHANNEL *pstCurIoChannel;

    struct sockaddr_storage stSockAddrStorage;
    struct sockaddr_in stClientAddr;
    unsigned int uiClientLen = sizeof(stClientAddr);
    unsigned int uiLen = sizeof(stSockAddrStorage);
    if (getsockname(iListenFd, (struct sockaddr*)&stSockAddrStorage, &uiLen) == 0) {
        int iClientSock = accept(iListenFd, (struct sockaddr*)&stClientAddr, &uiClientLen);
        if (iClientSock < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK){
                if (stSockAddrStorage.ss_family == AF_INET || stSockAddrStorage.ss_family == AF_INET6) {
                    perror("[TCP-SVR] accept");
                } else if (stSockAddrStorage.ss_family == AF_UNIX) {
                    perror("[UDS-SVR] accept");
                }
            }
            return;
        }

        netSetNonblock(iClientSock);
        if (stSockAddrStorage.ss_family == AF_INET || stSockAddrStorage.ss_family == AF_INET6) {
            fprintf(stderr, "[TCP-SVR] New client FD=%d\n", iClientSock);
            eventSourceCreateWithBev(pstEventEngine, iClientSock,
                    TYPE_TCP_SVR, ROLE_REQUESTER,
                    NULL, NULL, tcpIoChannelHandleEvent);
        } else if (stSockAddrStorage.ss_family == AF_UNIX) {
            fprintf(stderr, "[UDS-SVR] New client FD=%d\n", iClientSock);
            eventSourceCreateWithBev(pstEventEngine, iClientSock,
                    TYPE_UDS_SVR, ROLE_WORKER,
                    NULL, NULL, udsIoChannelHandleEvent);
        }
    }
}

/* ============================================================
* SIGINT 콜백
* ============================================================ */
static void signalCb(evutil_socket_t sig, short events, void* pvArg)
{
    EVENT_ENGINE* pstEventEngine = (EVENT_ENGINE *)pvArg;

    fprintf(stderr,"\n[TCP-UDS-SVR] SIGINT → shutdown\n");
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
    int iTcpListenFd, iUdsListenFd;
    struct event* pstTcpEventAccept, *pstUdsEventAccept;

    stEventEngine.pstEventBase = event_base_new();
    if (!stEventEngine.pstEventBase) {
        fprintf(stderr,"event_base_new failed\n");
        return -1;
    }
    eventEngineInit(&stEventEngine);

    /* TCP Listen 소켓 생성 */
    iTcpListenFd = netTcpCreateServer(SERVER_PORT);
    if (iTcpListenFd < 0) {
        perror("netTcpCreateServer");
        return -1;
    }
    iUdsListenFd = netUdsCreateServer(UDS_1_PATH);
    if (iUdsListenFd < 0) {
        fprintf(stderr, "[UDS-SVR] netUdsCreateServer() failed\n");
        return EXIT_FAILURE;
    }

    /* Accept 이벤트 등록 */
    pstTcpEventAccept = event_new(
            stEventEngine.pstEventBase, iTcpListenFd, 
            EV_READ | EV_PERSIST, acceptCb, &stEventEngine);
    event_add(pstTcpEventAccept, NULL);

    pstUdsEventAccept = event_new(
            stEventEngine.pstEventBase, iUdsListenFd, 
            EV_READ | EV_PERSIST, acceptCb, &stEventEngine);
    event_add(pstUdsEventAccept, NULL);


    /* SIGINT 처리 등록 */
    pstSignalEvent = evsignal_new(stEventEngine.pstEventBase,
        SIGINT, signalCb, &stEventEngine);
    event_add(pstSignalEvent, NULL);

    fprintf(stderr,"[TCP-SVR] Listening on port %d\n", SERVER_PORT);
    fprintf(stderr, "[UDS-SVR] Listening at %s\n", UDS_1_PATH);

    /* 이벤트 루프 시작 */
    event_base_dispatch(stEventEngine.pstEventBase);
    if(pstSignalEvent){
        event_del(pstSignalEvent);
        event_free(pstSignalEvent);
        pstSignalEvent =  NULL;
    }

    if(pstTcpEventAccept){
        event_del(pstTcpEventAccept);
        event_free(pstTcpEventAccept);
        pstTcpEventAccept =  NULL;
    }
    if(pstUdsEventAccept){
        event_del(pstUdsEventAccept);
        event_free(pstUdsEventAccept);
        pstUdsEventAccept =  NULL;
    }
    
    /* 종료 처리 */
    eventEngineCleanup(&stEventEngine);
    event_base_free(stEventEngine.pstEventBase);

    fprintf(stderr,"[TCP-SVR] Terminated.\n");
    fprintf(stderr,"[UDS-SVR] Terminated.\n");
    return 0;
}

/* === main === */
#ifndef GOOGLE_TEST
int main(int argc, char** argv)
{
    return run();
}
#endif