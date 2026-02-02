#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>

#include "eventEngine.h"
#include "netTcp.h"
#include "netUds.h"
#include "cmdRegistry.h"
#include "netCore.h"
#include "icdCommand.h"

static void recvKeyboard(int iFd, short nEvent, void* pvData)
{
    (void)iFd;
    (void)nEvent;
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    EVENT_ENGINE* pstEventEngine = pstIoChannel->pstEventEngine;
    IO_EVENT_TYPE eEventType = pstIoChannel->ePendingLogicEvent;

    char achRecvBuffer[2048];
    char achTcpSendBuffer[128];
    char achTcpRespBuffer[64];
    unsigned short unCmd = 0;
    FRAME_ERR eErr;

    MSG_ID stMsgId = { KEYBOARD_RECEIVER, SF_SENSOR_RECEIVER };
    createCmdResponse(CDM_KEYBOARD_DATA, achTcpRespBuffer, &stMsgId, achTcpSendBuffer);
    switch (eEventType) {

    case IO_EVT_RX_DATA:
        while (1) {
            unsigned int uiRecvLen = evbuffer_get_length(pstIoChannel->pstReadBuffer);
            /* 최소 헤더도 안 왔으면 중단 */
            if (uiRecvLen < (int)sizeof(FRAME_HEADER))
                break;
            memset(achRecvBuffer, 0x00, sizeof(achRecvBuffer));
            int iCopyLen = evbuffer_copyout(pstIoChannel->pstReadBuffer, achRecvBuffer, uiRecvLen);
            eErr = frameDecode(achRecvBuffer, iCopyLen, FRAME_TYPE_REQUEST, &unCmd);
            if (eErr != FRAME_OK) {
                fprintf(stderr, "[KEYBOARD-RECEIVER] frameDecode ERR: %s\n", frameErrToStr(eErr));
                int iOffset = findFrameHeader(achRecvBuffer, iCopyLen);
                if (iOffset > 0) {
                    /* 앞부분 garbage 제거 */
                    evbuffer_drain(pstIoChannel->pstReadBuffer, iOffset);
                    fprintf(stderr,"[KEYBOARD-RECEIVER] resync: drop %d bytes, retry decode\n", iOffset);
                } else if (iOffset == -2) {
                    /* STX half-match: 데이터 더 수신 */
                    evbuffer_drain(pstIoChannel->pstReadBuffer, iCopyLen-1);
                    fprintf(stderr,"[KEYBOARD-RECEIVER] STX half match, wait more data\n");
                } else {
                    /* STX 자체가 없음 → 전부 드랍 */
                    evbuffer_drain(pstIoChannel->pstReadBuffer, iCopyLen);
                    fprintf(stderr, "[KEYBOARD-RECEIVER] no STX, drop all\n");
                }
                continue;
            }
            /* === CMD 먼저 추출 (가벼운 파싱) === */
            int iFrameSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_REQUEST);
            if (iCopyLen < iFrameSize)
                break;
            /* === 프레임 하나 소비 === */
            evbuffer_drain(pstIoChannel->pstReadBuffer, iFrameSize);
            unsigned char auchKeyboardData[128];
            eErr = cmdDispatch(achRecvBuffer, iCopyLen, auchKeyboardData);
            switch(unCmd){
                case CDM_KEYBOARD_DATA: {
                    IO_CHANNEL* pstKeyboardIo = ioFindChannelByWorkerId(pstIoChannel->pstEventEngine, KEYBOARD_SENDER);
                    if (ioIsChannelAlive(pstKeyboardIo)) {
                        unsigned char auchSendBuf[UDS_MAX_BUFFER_SIZE];
                        MSG_ID stMsgId = { KEYBOARD_RECEIVER, SF_SENSOR_RECEIVER };
                        createCmdResponse(CDM_KEYBOARD_DATA, auchKeyboardData, &stMsgId, auchSendBuf);
                        int iResultSize = getFrameSizeWithCmd(CDM_KEYBOARD_DATA, FRAME_TYPE_RESPONSE);
                        evbuffer_add(pstKeyboardIo->pstWriteBuffer, auchSendBuf, iResultSize);
                        event_add(pstKeyboardIo->pstWriteEvent, NULL);
                    }
                    break;                
                }
                default:
                    fprintf(stderr,"Receiver Keyboard Data Error\n");
                    break;
            }
        }
        break;

    case IO_EVT_CHANNEL_CLOSED:
    case IO_EVT_ERROR:
        printf("[KEYBOARD-RECEIVER] channel closed fd=%d\n", pstIoChannel->iFd);
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
                perror("[KEYBOARD-RECEIVER] accept");
            }
            return;
        }

        netSetNonblock(iClientSock);
        if (stSockAddrStorage.ss_family == AF_INET || stSockAddrStorage.ss_family == AF_INET6) {
            fprintf(stderr, "[KEYBOARD-RECEIVER] New client FD=%d\n", iClientSock);
            pstIoChannel = eventSourceCreateWithBev(pstEventEngine, iClientSock,
                    TYPE_TCP_SVR, ROLE_REQUESTER,
                    NULL, NULL, recvKeyboard);
            pstIoChannel->iWorkerId = KEYBOARD_RECEIVER;
            pstIoChannel->chFdCloseSet = FD_OPENED;
        }
    }
}

static void applyCommand(unsigned short unCmd, char *pchCmdData, char *pchCmdResult)
{
    (void)pchCmdData;
    switch (unCmd)
    {
    case CMD_ID_INFO:
        ((RES_ID*)pchCmdResult)->chResult = (char)KEYBOARD_RECEIVER;
        break; 
    default:
        fprintf(stderr, "[KEYBOARD] Unsupported CMD\n");
        break;
    }
}

static void ioChannelHandleEvent(int iFd, short nEvent, void* pvData)
{
    (void)iFd;
    (void)nEvent;
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    IO_EVENT_TYPE eEventType = pstIoChannel->ePendingLogicEvent;
    FRAME_ERR eErr;
    unsigned short unCmd = 0;
    char achRecvBuffer[UDS_MAX_BUFFER_SIZE];
    switch (eEventType) {
    case IO_EVT_CHANNEL_CLOSED:
    case IO_EVT_ERROR:
        ioMarkChannelDead(pstIoChannel, pstIoChannel->ePendingLogicEvent);
        break;
    case IO_EVT_RX_DATA:
    {
        int iRecvLen = evbuffer_get_length(pstIoChannel->pstReadBuffer);
        fprintf(stderr,"### %s():%d Recv Size is %d ###\n", __func__, __LINE__, iRecvLen);
        if (iRecvLen < (int)sizeof(FRAME_HEADER))
            break;

        memset(achRecvBuffer, 0x00, sizeof(achRecvBuffer));
        int iCopyLen = evbuffer_copyout(pstIoChannel->pstReadBuffer, achRecvBuffer, iRecvLen);
        eErr = frameDecode(achRecvBuffer, iCopyLen, FRAME_TYPE_REQUEST, &unCmd);
        if (eErr != FRAME_OK) {
            fprintf(stderr, "[KEYBOARD] frameDecode ERR: %s\n", frameErrToStr(eErr));
            int iOffset = findFrameHeader(achRecvBuffer, iCopyLen);
            if (iOffset > 0) {
                /* 앞부분 garbage 제거 */
                evbuffer_drain(pstIoChannel->pstReadBuffer, iOffset);
                fprintf(stderr,"[KEYBOARD] resync: drop %d bytes, retry decode\n", iOffset);
            } else if (iOffset == -2) {
                /* STX half-match: 데이터 더 수신 */
                evbuffer_drain(pstIoChannel->pstReadBuffer, iCopyLen-1);
                fprintf(stderr,"[KEYBOARD] STX half match, wait more data\n");
            } else {
                /* STX 자체가 없음 → 전부 드랍 */
                evbuffer_drain(pstIoChannel->pstReadBuffer, iCopyLen);
                fprintf(stderr, "[KEYBOARD] no STX, drop all\n");
            }
        }
        int iFrameSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_REQUEST);
        /* === 프레임 소비 === */
        char achCmdData[128];
        char achCmdResult[128];
        char achResult[128];
        memset(achCmdData, 0x0, sizeof(achCmdData));
        memset(achCmdResult, 0x0, sizeof(achCmdResult));
        memset(achResult, 0x0, sizeof(achResult));
        unsigned int uiReqId;
        int iResultSize;
        evbuffer_drain(pstIoChannel->pstReadBuffer, iFrameSize);
        evbuffer_remove(pstIoChannel->pstReadBuffer, &uiReqId, sizeof(unsigned int));
        eErr = cmdDispatch(achRecvBuffer, iCopyLen, achCmdData);
        if (eErr != FRAME_OK){
            fprintf(stderr,"### %s():%d %s ###\n",__func__,__LINE__, frameErrToStr(eErr));
        }            
        applyCommand(unCmd, achCmdData, achCmdResult);
        MSG_ID stMsgId = { KEYBOARD_RECEIVER, SF_SENSOR_RECEIVER };
        eErr = createCmdResponse(unCmd, achCmdResult, &stMsgId, achResult);
        iResultSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_RESPONSE);
        // sendUdsResponse(pstIoChannel, unCmd, uiReqId, auchResult, iResultSize);
        evbuffer_add(pstIoChannel->pstWriteBuffer, achResult, iResultSize);
        event_add(pstIoChannel->pstWriteEvent, NULL);
    }

    default:
        break;
    }
    pstIoChannel->ePendingLogicEvent = IO_EVENT_NONE;
}

static void uds2ReconnectCb(evutil_socket_t fd, short nEvent, void *pvArg)
{
    (void)fd;
    (void)nEvent;
    EVENT_ENGINE *pstEventEngine = (EVENT_ENGINE *)pvArg;
    /* 이미 살아있으면 재접속 불필요 */
    IO_CHANNEL *pstIoChannel = ioFindChannelByWorkerId(pstEventEngine, GPS_RECEIVER);

    if (ioIsChannelAlive(pstIoChannel))
        return;

    int iSock = netUdsCreateClient(UDS_2_PATH);
    if (iSock < 0) {
        fprintf(stderr, "[KEYBOARD] reconnect failed, retry later\n");
        return; /* 타이머는 계속 살아있음 */
    }

    fprintf(stderr, "[KEYBOARD] reconnected!\n");
    netSetNonblock(iSock);

    IO_CHANNEL *pstNewIo = eventSourceCreateWithBev(pstEventEngine, iSock,
            TYPE_UDS_CLI, ROLE_REQUESTER,
            NULL, NULL, ioChannelHandleEvent);
    if (!pstNewIo) {
        close(iSock);
        return;
    }      
    pstNewIo->iWorkerId = KEYBOARD_SENDER;
    pstNewIo->chFdCloseSet =  FD_OPENED;
}

/* ============================================================
* SIGINT 콜백
* ============================================================ */
static void signalCb(evutil_socket_t sig, short events, void* pvArg)
{
    (void)sig;
    (void)events;
    EVENT_ENGINE* pstEventEngine = (EVENT_ENGINE *)pvArg;

    fprintf(stderr,"\n[KEYBOARD-RECEIVER] SIGINT → shutdown\n");
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
    int iTcpListenFd;
    struct event* pstTcpEventAccept;
    struct event*   pstUdsRetryEvent = NULL;
    struct timeval stRertyTimeOut = {1, 0};

    stEventEngine.pstEventBase = event_base_new();
    if (!stEventEngine.pstEventBase) {
        fprintf(stderr,"event_base_new failed\n");
        return -1;
    }
    eventEngineInit(&stEventEngine, 2);

    /* TCP Listen 소켓 생성 */
    iTcpListenFd = netTcpCreateServer(KEYBOARD_RECEIVER);
    if (iTcpListenFd < 0) {
        perror("netTcpCreateServer");
        return -1;
    }

    /* Accept 이벤트 등록 */
    pstTcpEventAccept = event_new(
            stEventEngine.pstEventBase, iTcpListenFd, 
            EV_READ | EV_PERSIST, acceptCb, &stEventEngine);
    event_add(pstTcpEventAccept, NULL);


    pstUdsRetryEvent = event_new(stEventEngine.pstEventBase,
                  -1, EV_PERSIST | EV_TIMEOUT,
                  uds2ReconnectCb, &stEventEngine);
    event_add(pstUdsRetryEvent, &stRertyTimeOut);


    /* SIGINT 처리 등록 */
    pstSignalEvent = evsignal_new(stEventEngine.pstEventBase,
        SIGINT, signalCb, &stEventEngine);
    event_add(pstSignalEvent, NULL);

    fprintf(stderr,"[KEYBOARD-RECEIVER] Listening on port %d\n", KEYBOARD_RECEIVER);

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
    
    /* 종료 처리 */
    eventEngineCleanup(&stEventEngine);
    event_base_free(stEventEngine.pstEventBase);

    fprintf(stderr,"[KEYBOARD-RECEIVER] Terminated.\n");
    return 0;
}

/* === main === */
#ifndef GOOGLE_TEST
int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    return run();
}
#endif