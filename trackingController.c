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

#define SERVER_PORT 5000


void tcpWriteCallback(int iFd, short nEvent, void* pvData)
{
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    unsigned char auchWriteBuffer[2048];
    int iWriteSize;
    FRAME_ERR eErr;
    iWriteSize = evbuffer_get_length(pstIoChannel->pstWriteBuffer);    
    if (iWriteSize == 0) {
        event_del(pstIoChannel->pstWriteEvent);
        return;
    }    
    iWriteSize = evbuffer_remove(pstIoChannel->pstWriteBuffer, auchWriteBuffer, sizeof(auchWriteBuffer));
/* === 응답 프레임 생성 === */
    // eErr = makeResFrame(unCmd, &stMsgId, auCmdResult, auSendBuf);
    // if (eErr != FRAME_OK)
    //     continue;

    iWriteSize = write(pstIoChannel->iFd, auchWriteBuffer, iWriteSize);
    if (iWriteSize <= 0) {
        perror("write");
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
    unsigned char auchCmdResult[1000];
    unsigned char auchSendBuf[1024];
    unsigned short unCmd = 0;
    FRAME_ERR eErr;
    int iSendLen = 0;
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    switch (eEventType) {
    case IO_EVT_RX_DATA:
        while (1) {
            unsigned int uiRecvLen = evbuffer_get_length(pstIoChannel->pstReadBuffer);
            fprintf(stderr,"### %s():%d Len:%d###\n",__func__,__LINE__, uiRecvLen);
            if (uiRecvLen < FRAME_HEADER_MIN_SIZE)
                break;
                
            memset(auchRecvBuffer, 0x0, sizeof(auchRecvBuffer));
            int iCopyLen = evbuffer_copyout(pstIoChannel->pstReadBuffer, auchRecvBuffer, uiRecvLen);
            int iFrameSize = getFrameSize(auchRecvBuffer);
            if (iFrameSize <= 0) {
                evbuffer_drain(pstIoChannel->pstReadBuffer, 1);
                continue;
            }

            if (iCopyLen < iFrameSize)
                break;
                
            evbuffer_drain(pstIoChannel->pstReadBuffer, iFrameSize);            
            
            MSG_ID stMsgId = { TCP_SVR_ID, TCP_CLN_ID };
            /* === 헤더 및 명령 추출 === */
            eErr = chkRequestFrame(auchRecvBuffer, &stMsgId, iFrameSize, &unCmd);
            if (eErr != FRAME_OK) {
                fprintf(stderr, "[TCP-SVR] chkRequestFrame ERR: %s\n", frameErrToStr(eErr));
                continue;
            }
            //만약 제어PC에서의 연속적인 명령이 들어올 경우 문제가 있을수 있다.
            //하지만 제어PC는 연속적인 명령을 던질수 없다. 처음 보낸 명령에 대해서 응답이 없으면 두번째 명령을 보내지 않는 구조이기 때문에 그렇다.
            PROCESS_PATH eProcPath = decideProcessingPath(auchRecvBuffer);
            if(eProcPath == PROCESS_LOCAL){
                /* === 명령 처리 === */
                eErr = commandHandler(auchRecvBuffer, &stMsgId, iFrameSize, auchCmdResult, &iSendLen);
                if (eErr != FRAME_OK || iSendLen <= 0)
                    continue;

                /* === 응답 프레임 생성 === */
                eErr = makeResFrame(unCmd, &stMsgId, auchCmdResult, auchSendBuf);
                if (eErr != FRAME_OK)
                    continue;

                evbuffer_add(pstIoChannel->pstWriteBuffer, auchSendBuf, iSendLen);
                event_add(pstIoChannel->pstWriteEvent, NULL); 
            }else if(eProcPath == PROCESS_VIA_IPC){
                FRAME_HEADER *pstHeader = (FRAME_HEADER *)auchRecvBuffer;
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
    unsigned char uchReult[sizeof(IPC_FRAME)];
    IPC_FRAME *pstIpcFrame = uchReult;
    
    switch (eEventType) {
    case IO_EVT_RX_DATA:
        /* protocol / packet 처리 */
        fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
        while (1) {
            size_t tRecvLen = evbuffer_get_length(pstIoChannel->pstReadBuffer);
            if (tRecvLen < FRAME_HEADER_MIN_SIZE)
                break;

            if (tRecvLen > sizeof(auchRecvBuffer))
                tRecvLen = sizeof(auchRecvBuffer);

            int iCopyLen = evbuffer_copyout(pstIoChannel->pstReadBuffer, auchRecvBuffer, tRecvLen);
            MSG_ID stMsgId = { UDS_1_CLN1_ID, UDS_1_SVR_ID };
            pstIpcFrame->unStx = STX_CONST;
            responseFrame(auchRecvBuffer, &stMsgId, iCopyLen, pstIpcFrame->unCmd, pstIpcFrame->uchResult);
            pstIpcFrame->unEtx = ETX_CONST;
            evbuffer_drain(pstIoChannel->pstReadBuffer, iCopyLen);
            eventEngineHandleWorkerResponse(pstIoChannel->pstEventEngine,
                pstIoChannel, uchReult, sizeof(IPC_FRAME));
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
                    NULL, tcpWriteCallback, tcpIoChannelHandleEvent);
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