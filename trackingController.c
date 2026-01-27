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
#include "netTcp.h"
#include "ipcUtil.h"

static COMMAND_PATH decideProcessingPath(unsigned short unCmd)
{
    switch(unCmd)
    {
        case CMD_KEEP_ALIVE:
            return COMMAND_PATH_NONE;
        case CMD_IBIT:
            return AC_CMD_RECEIVER|SF_CMD_REDEIVER;
        case CMD_RBIT:
            return AC_CMD_RECEIVER|SF_CMD_REDEIVER;
        case CMD_CBIT:
            return AC_CMD_RECEIVER|SF_CMD_REDEIVER;
        case CMD_POSITIONER_AZ_EL_SET:
            return AC_CMD_RECEIVER;
        case CMD_TRACKING_SELECT:
            return SF_CMD_REDEIVER;
        case CMD_ACU_MODE_SELECT:
            return AC_CMD_RECEIVER;
        case CMD_AUTO_TRACKING_WAIT:
            return SF_CMD_REDEIVER;
        default:
            return COMMAND_PATH_FAIL;
    }
}


void tcpWriteCallback(int iFd, short nEvent, void* pvData)
{
    (void)nEvent;
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    unsigned char auchWriteBuffer[2048];
    int iWriteSize;
    iWriteSize = evbuffer_get_length(pstIoChannel->pstWriteBuffer);
    if (iWriteSize == 0) {
        event_del(pstIoChannel->pstWriteEvent);
        return;
    }    
    iWriteSize = evbuffer_remove(pstIoChannel->pstWriteBuffer, auchWriteBuffer, iWriteSize);
    MSG_ID stMsgId = { TCP_SVR_ID, TCP_CLN_ID };
    repackageResponse(auchWriteBuffer, &stMsgId, iWriteSize);
    iWriteSize = write(pstIoChannel->iFd, auchWriteBuffer, iWriteSize);
    if (iWriteSize <= 0) {
        perror("write");
        return;
    }
    fprintf(stderr,"\n");
    fprintf(stderr,"### %s():%d Write Size:%d ###\n", __func__,__LINE__, iWriteSize);
    for(int i=1; i<=iWriteSize; i++){
        if(i&16 == 0)
            fprintf(stderr,"\n");
        fprintf(stderr,"%02x ", auchWriteBuffer[i-1]);
    }  
    fprintf(stderr,"\n");
    if (evbuffer_get_length(pstIoChannel->pstWriteBuffer) == 0)
        event_del(pstIoChannel->pstWriteEvent);
}


static void tcpIoChannelHandleEvent(int iFd, short nEvent, void* pvData)
{
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    EVENT_ENGINE* pstEventEngine = pstIoChannel->pstEventEngine;
    IO_EVENT_TYPE eEventType = pstIoChannel->ePendingLogicEvent;

    unsigned char auchRecvBuffer[2048];
    unsigned short unCmd = 0;
    FRAME_ERR eErr;
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
                int iOffset = findFrameHeader(auchRecvBuffer, iCopyLen);
                if (iOffset > 0) {
                    /* 앞부분 garbage 제거 */
                    evbuffer_drain(pstIoChannel->pstReadBuffer, iOffset);
                    fprintf(stderr,"[TCP-SVR] resync: drop %d bytes, retry decode\n", iOffset);
                } else if (iOffset == -2) {
                    /* STX half-match: 데이터 더 수신 */
                    evbuffer_drain(pstIoChannel->pstReadBuffer, iCopyLen-1);
                    fprintf(stderr,"[TCP-SVR] STX half match, wait more data\n");
                } else {
                    /* STX 자체가 없음 → 전부 드랍 */
                    evbuffer_drain(pstIoChannel->pstReadBuffer, iCopyLen);
                    fprintf(stderr, "[TCP-SVR] no STX, drop all\n");
                }
                continue;
            }
            /* === CMD 먼저 추출 (가벼운 파싱) === */
            int iFrameSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_REQUEST);
            if (iCopyLen < iFrameSize)
                break;
            
            /* === 프레임 하나 소비 === */
            evbuffer_drain(pstIoChannel->pstReadBuffer, iFrameSize);
            /* === 처리 경로 결정 === */
            COMMAND_PATH eCommandPath = decideProcessingPath(unCmd);
            if (eCommandPath == COMMAND_PATH_NONE) {
                /* === 명령 처리 === */
                unsigned char auchCmdResult[128];
                unsigned char auchResult[128];
                int iResultSize;
                eErr = cmdDispatch(auchRecvBuffer, iCopyLen, auchCmdResult);
                if (eErr != FRAME_OK)
                    continue;
                MSG_ID stMsgId = { TCP_SVR_ID, TCP_CLN_ID };
                eErr = createCmdResponse(unCmd, auchCmdResult, &stMsgId, auchResult);
                iResultSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_RESPONSE);
                evbuffer_add(pstIoChannel->pstWriteBuffer, auchResult, iResultSize);
                event_add(pstIoChannel->pstWriteEvent, NULL);
            } else if (eCommandPath == SF_CMD_REDEIVER || eCommandPath == AC_CMD_RECEIVER || 
                eCommandPath == (SF_CMD_REDEIVER|AC_CMD_RECEIVER)) {
                /* === IPC 전달 (Fan-out 진입점) === */
                pstEventEngine->uiRequestSeq++;
                fprintf(stderr,"### %s():%d IPC Forwarding CMD:0x%04x Path:%d Copy Size:%d ###\n", __func__, __LINE__, unCmd, eCommandPath, iFrameSize);
                evbuffer_add(pstIoChannel->pstRequestBuffer, &eCommandPath, sizeof(int));
                evbuffer_add(pstIoChannel->pstRequestBuffer, auchRecvBuffer, iFrameSize);
                event_active(pstIoChannel->pstRequestEvent, 0, 0);
            }
        }
        break;

    case IO_EVT_CHANNEL_CLOSED:
    case IO_EVT_ERROR:
        printf("[TCP-SVR] channel closed fd=%d\n", pstIoChannel->iFd);
        event_active(pstIoChannel->pstShutdownEvent, 0, 0);
        break;

    default:
        break;
    }
    pstIoChannel->ePendingLogicEvent = IO_EVENT_NONE;
}

void udsWriteCallback(int iFd, short nEvent, void* pvData)
{
    (void)nEvent;
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    EVENT_ENGINE* pstEventEngine = pstIoChannel->pstEventEngine;
    unsigned char auchWriteBuffer[2048];
    int iWriteSize;
    iWriteSize = evbuffer_get_length(pstIoChannel->pstWriteBuffer);
    if (iWriteSize == 0) {
        event_del(pstIoChannel->pstWriteEvent);
        return;
    }    
    iWriteSize = evbuffer_remove(pstIoChannel->pstWriteBuffer, auchWriteBuffer, iWriteSize);
    //Request ID에 따른 매칭이 필요하기 때문에 Request Context생성시 사용된 Request ID를 UDS클라이언트에 전송 필요.
    memcpy(auchWriteBuffer+iWriteSize, &pstEventEngine->uiRequestSeq, sizeof(pstEventEngine->uiRequestSeq));
    iWriteSize = write(pstIoChannel->iFd, auchWriteBuffer, iWriteSize+sizeof(pstEventEngine->uiRequestSeq));
    if (evbuffer_get_length(pstIoChannel->pstWriteBuffer) == 0)
        event_del(pstIoChannel->pstWriteEvent);
}

static void udsIoChannelHandleEvent(int iFd, short nEvent, void* pvData)
{
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    IO_EVENT_TYPE eEventType = pstIoChannel->ePendingLogicEvent;
    EVENT_ENGINE* pstEventEngine = pstIoChannel->pstEventEngine;
    unsigned char auchRecvBuffer[UDS_MAX_BUFFER_SIZE];
    unsigned short unCmd = 0;
    FRAME_ERR eErr;
    int iRecvLen;
    
    switch (eEventType) {

    case IO_EVT_RX_DATA:
        iRecvLen = evbuffer_get_length(pstIoChannel->pstReadBuffer);
        fprintf(stderr,"### %s():%d Recv Size is %d ###\n", __func__, __LINE__, iRecvLen);
        if (iRecvLen < sizeof(FRAME_HEADER))
            break;
        
        memset(auchRecvBuffer, 0x00, sizeof(auchRecvBuffer));
        int iCopyLen = evbuffer_copyout(pstIoChannel->pstReadBuffer, auchRecvBuffer, iRecvLen);
        eErr = frameDecode(auchRecvBuffer, iCopyLen, FRAME_TYPE_RESPONSE, &unCmd);
        if (eErr != FRAME_OK) {
            fprintf(stderr, "[UDS1Server] frameDecode ERR: %s\n", frameErrToStr(eErr));
            int iOffset = findFrameHeader(auchRecvBuffer, iCopyLen);
            if (iOffset > 0) {
                /* 앞부분 garbage 제거 */
                evbuffer_drain(pstIoChannel->pstReadBuffer, iOffset);
                fprintf(stderr,"[UDS1Server] resync: drop %d bytes, retry decode\n", iOffset);
            } else if (iOffset == -2) {
                /* STX half-match: 데이터 더 수신 */
                evbuffer_drain(pstIoChannel->pstReadBuffer, iCopyLen-1);
                fprintf(stderr,"[UDS1Server] STX half match, wait more data\n");
            } else {
                /* STX 자체가 없음 → 전부 드랍 */
                evbuffer_drain(pstIoChannel->pstReadBuffer, iCopyLen);
                fprintf(stderr, "[UDS1Server] no STX, drop all\n");
            }
        }
        int iFrameSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_RESPONSE);
        evbuffer_drain(pstIoChannel->pstReadBuffer, iFrameSize + sizeof(unsigned int));        
        int iResultSize;

        // eErr = cmdDispatch(auchRecvBuffer, iCopyLen, auchCmdResult);
        // fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
        // if (eErr != FRAME_OK){
        //     fprintf(stderr,"### %s():%d %s ###\n",__func__,__LINE__, frameErrToStr(eErr));
        // }
        // evbuffer_remove(pstIoChannel->pstReadBuffer, &iReqId, sizeof(unsigned int));
        if(unCmd == CMD_ID_INFO){
            pstIoChannel->iWorkerId = (int)getIdInfo(auchRecvBuffer+sizeof(FRAME_HEADER));
            fprintf(stderr,"ID is %d\n", pstIoChannel->iWorkerId);
        }else{
            fprintf(stderr,"Request id is %d\n", pstEventEngine->uiRequestSeq);
            eventEngineHandleWorkerResponse(pstIoChannel->pstEventEngine, pstIoChannel,
                pstEventEngine->uiRequestSeq, auchRecvBuffer, iFrameSize);
        }
        break;

    case IO_EVT_CHANNEL_CLOSED:
    case IO_EVT_ERROR:
        printf("[UDS-SVR] channel closed fd=%d\n", pstIoChannel->iFd);
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
    IO_CHANNEL* pstIoChannel;
    EVENT_ENGINE* pstEventEngine = (EVENT_ENGINE *)pvArg;
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
            pstIoChannel = eventSourceCreateWithBev(pstEventEngine, iClientSock,
                    TYPE_TCP_SVR, ROLE_REQUESTER,
                    NULL, tcpWriteCallback, tcpIoChannelHandleEvent);
            pstIoChannel->iWorkerId = TC_TCP_CMD_RECEIVER;
            pstIoChannel->chFdCloseSet = FD_OPENED;
        } else if (stSockAddrStorage.ss_family == AF_UNIX) {
            fprintf(stderr, "[UDS-SVR] New client FD=%d\n", iClientSock);
            pstIoChannel = eventSourceCreateWithBev(pstEventEngine, iClientSock,
                    TYPE_UDS_SVR, ROLE_WORKER,
                    NULL, udsWriteCallback, udsIoChannelHandleEvent);
            pstIoChannel->chFdCloseSet = FD_OPENED;
            pstIoChannel->iWorkerId = TC_UDS_CMD_CTRL;
            REQ_ID stReqId;
            MSG_ID stMsgId = { TC_UDS_CMD_CTRL,  SF_CMD_REDEIVER|AC_CMD_RECEIVER};
            unsigned char auSendBuf[64];            
            stReqId.chTmp = 0x01;        
            if(createCmdRequest(CMD_ID_INFO, &stMsgId, &stReqId, auSendBuf) == FRAME_OK){
                evbuffer_add(pstIoChannel->pstWriteBuffer, auSendBuf, getFrameSizeWithCmd(CMD_ID_INFO, FRAME_TYPE_REQUEST));
                event_add(pstIoChannel->pstWriteEvent, NULL);
            }
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
    eventEngineInit(&stEventEngine, 2);

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