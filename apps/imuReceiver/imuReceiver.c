#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <termios.h>
#include <stdint.h>

#include "uartConfig.h"
#include "mti670Imu.h"
#include "icdCommand.h"
#include "cmdRegistry.h"
#include "netUds.h"
#include "netCore.h"
#include "ioChannelUtil.h"

/* ============================================================
 * UART read logic event handler
 * ============================================================ */
static void uartReadCallback(int iFd, short nEvent, void* pvData)
{
    (void)iFd; 
    (void)nEvent;

    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    IO_EVENT_TYPE eEventType = pstIoChannel->ePendingLogicEvent;

    /* NOTE: IMU parser는 스트림 상태 유지가 필요 → static OK */
    static MTI670_PARSER_CTX stImuParser;
    static IMU_FORMAT        stImuFormat;

    unsigned char auchRecvBuffer[2048];

    switch (eEventType) {
    case IO_EVT_RX_DATA:
        while (1) {
            unsigned int uiRecvSize = evbuffer_get_length(pstIoChannel->pstReadBuffer);
            if (uiRecvSize == 0)
                break;

            if (uiRecvSize > sizeof(auchRecvBuffer))
                uiRecvSize = sizeof(auchRecvBuffer);

            int uiCopySize = evbuffer_copyout(pstIoChannel->pstReadBuffer, auchRecvBuffer, uiRecvSize);
            if (uiCopySize <= 0)
                break;

            /* 스트림 feed */
            if (mti670Feed(&stImuParser, auchRecvBuffer, uiCopySize, &stImuFormat)) {
                float fRoll  = mtiBeFloat((unsigned char*)stImuFormat.stEulerAngles.chRoll);
                float fPitch = mtiBeFloat((unsigned char*)stImuFormat.stEulerAngles.chPitch);
                float fYaw   = mtiBeFloat((unsigned char*)stImuFormat.stEulerAngles.chYaw);

                fprintf(stderr, "[IMU] R=%.3f P=%.3f Y=%.3f\n", fRoll, fPitch, fYaw);

                /* UDS#2(sensorFusion) 채널로 best-effort 전송 */
                IO_CHANNEL* pstImuTxIo = ioFindChannelByWorkerId(pstIoChannel->pstEventEngine, IMU_RECEIVER);
                if (ioIsChannelAlive(pstImuTxIo)) {
                    unsigned char auchSendBuf[UDS_MAX_BUFFER_SIZE];
                    unsigned char auchImuData[UDS_MAX_BUFFER_SIZE];

                    RES_RPY_DATA *pstImuData = (RES_RPY_DATA *)auchImuData;

                    pstImuData->dRoll  = (double)fRoll;
                    pstImuData->dPitch = (double)fPitch;
                    pstImuData->dYaw   = (double)fYaw;

                    MSG_ID stMsgId = { IMU_RECEIVER, SF_SENSOR_RECEIVER };
                    createCmdResponse(CDM_IMU_DATA, auchImuData, &stMsgId, auchSendBuf);
                    int iResultSize = getFrameSizeWithCmd(CDM_IMU_DATA, FRAME_TYPE_RESPONSE);                    
                    evbuffer_add(pstImuTxIo->pstWriteBuffer, auchSendBuf, iResultSize);
                    event_add(pstImuTxIo->pstWriteEvent, NULL);
                } else {
                    /* sensorFusion 미연결/끊김 → 드롭 */
                    /* fprintf(stderr, "[IMU] UDS_2 not alive, drop\n"); */
                }
            }
            evbuffer_drain(pstIoChannel->pstReadBuffer, uiCopySize);
        }
        break;

    case IO_EVT_CHANNEL_CLOSED:
    case IO_EVT_ERROR:
        fprintf(stderr, "[IMU] UART channel error fd=%d\n", pstIoChannel->iFd);
        event_active(pstIoChannel->pstShutdownEvent, 0, 0);
        break;

    default:
        break;
    }

    pstIoChannel->ePendingLogicEvent = IO_EVENT_NONE;
}
static void applyCommand(unsigned short unCmd, char *pchCmdData, char *pchCmdResult)
{
    (void)pchCmdData;
    switch (unCmd)
    {
    case CMD_ID_INFO:
        ((RES_ID*)pchCmdResult)->chResult = (char)IMU_RECEIVER;
        break; 
    default:
        fprintf(stderr, "[ACU] Unsupported CMD\n");
        break;
    }
}
/* ============================================================
 * UDS command channel logic handler
 * ============================================================ */
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
            fprintf(stderr, "[IMU] frameDecode ERR: %s\n", frameErrToStr(eErr));
            int iOffset = findFrameHeader(achRecvBuffer, iCopyLen);
            if (iOffset > 0) {
                /* 앞부분 garbage 제거 */
                evbuffer_drain(pstIoChannel->pstReadBuffer, iOffset);
                fprintf(stderr,"[IMU] resync: drop %d bytes, retry decode\n", iOffset);
            } else if (iOffset == -2) {
                /* STX half-match: 데이터 더 수신 */
                evbuffer_drain(pstIoChannel->pstReadBuffer, iCopyLen-1);
                fprintf(stderr,"[IMU] STX half match, wait more data\n");
            } else {
                /* STX 자체가 없음 → 전부 드랍 */
                evbuffer_drain(pstIoChannel->pstReadBuffer, iCopyLen);
                fprintf(stderr, "[IMU] no STX, drop all\n");
            }
        }
        int iFrameSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_REQUEST);
        /* === 프레임 소비 === */
        char achCmdData[128];
        char achCmdResult[128];
        char achResult[128];
        unsigned int uiReqId;
        int iResultSize;
        memset(achCmdData, 0x0, sizeof(achCmdData));
        memset(achCmdResult, 0x0, sizeof(achCmdResult));
        memset(achResult, 0x0, sizeof(achResult));
        evbuffer_drain(pstIoChannel->pstReadBuffer, iFrameSize);
        evbuffer_remove(pstIoChannel->pstReadBuffer, &uiReqId, sizeof(unsigned int));
        eErr = cmdDispatch(achRecvBuffer, iCopyLen, achCmdData);
        if (eErr != FRAME_OK){
            fprintf(stderr,"### %s():%d %s ###\n",__func__,__LINE__, frameErrToStr(eErr));
        }            
        applyCommand(unCmd, achCmdData, achCmdResult);
        MSG_ID stMsgId = { IMU_RECEIVER, SF_SENSOR_RECEIVER };
        eErr = createCmdResponse(unCmd, achCmdResult, &stMsgId, achResult);
        iResultSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_RESPONSE);
        evbuffer_add(pstIoChannel->pstWriteBuffer, achResult, iResultSize);
        event_add(pstIoChannel->pstWriteEvent, NULL);
    }
    break;
    default:
        /* TX-only: ignore */
        break;
    }
    pstIoChannel->ePendingLogicEvent = IO_EVENT_NONE;
}

/* ============================================================
 * SIGINT
 * ============================================================ */
static void signalCb(evutil_socket_t sig, short events, void* pvArg)
{
    (void)sig;
    (void)events;
    EVENT_ENGINE* pstEventEngine = (EVENT_ENGINE *)pvArg;

    fprintf(stderr, "\n[IMU-RX] SIGINT → shutdown\n");
    if (pstEventEngine->pstEventBase)
        event_base_loopexit(pstEventEngine->pstEventBase, NULL);
}

static void uds2ReconnectCb(evutil_socket_t fd, short nEvent, void *pvArg)
{
    (void)fd;
    (void)nEvent;
    EVENT_ENGINE *pstEventEngine = (EVENT_ENGINE *)pvArg;
    /* 이미 살아있으면 재접속 불필요 */
    IO_CHANNEL *pstIoChannel = ioFindChannelByWorkerId(pstEventEngine, IMU_RECEIVER);

    if (ioIsChannelAlive(pstIoChannel))
        return;

    int iSock = netUdsCreateClient(UDS_2_PATH);
    if (iSock < 0) {
        fprintf(stderr, "[UDS#2] reconnect failed, retry later\n");
        return; /* 타이머는 계속 살아있음 */
    }

    fprintf(stderr, "[UDS#2] reconnected!\n");
    IO_CHANNEL *pstNewIo = eventSourceCreateWithBev(pstEventEngine, iSock,
            TYPE_UDS_CLI, ROLE_REQUESTER,
            NULL, NULL, ioChannelHandleEvent);
    if (!pstNewIo) {
        close(iSock);
        return;
    }        
    pstNewIo->chFdCloseSet =  FD_OPENED;
    pstNewIo->iWorkerId = IMU_RECEIVER;
}


/* ============================================================
 * Main
 * ============================================================ */
int run(char* pchUartPath)
{    
    //이미 끊어진 소켓에 write() 했을 때 프로세스가 즉사(SIGPIPE)하는 것을 막는다.
    ioIgnoreSigpipeOnce();    

    EVENT_ENGINE    stEventEngine;
    struct event*   pstSignalEvent = NULL;
    struct event*   pstUdsRetryEvent = NULL;
    UART_CTX stUartCtx = {
        .pchDevPath     = pchUartPath,
        .iBaudrate      = 115200,
        .iFd            = -1,
        .iBackoffMsec   = 200
    };
    struct timeval stRertyTimeOut = {1, 0};

    stEventEngine.pstEventBase = event_base_new();
    if (!stEventEngine.pstEventBase) {
        fprintf(stderr, "[IMU-RX] event_base_new() failed\n");
        return EXIT_FAILURE;
    }
    eventEngineInit(&stEventEngine, 0);

    /* UART open */
    if (uartOpen(&stUartCtx) < 0) {
        fprintf(stderr, "[IMU-RX] uartOpen failed: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    eventSourceCreateWithBev(&stEventEngine, stUartCtx.iFd,
        TYPE_UART, ROLE_REQUESTER, NULL, NULL, uartReadCallback);
    
    pstUdsRetryEvent = event_new(stEventEngine.pstEventBase,
                  -1, EV_PERSIST | EV_TIMEOUT,
                  uds2ReconnectCb, &stEventEngine);
    event_add(pstUdsRetryEvent, &stRertyTimeOut);
    
    pstSignalEvent = evsignal_new(stEventEngine.pstEventBase, SIGINT, signalCb, &stEventEngine);
    event_add(pstSignalEvent, NULL);    

    event_base_dispatch(stEventEngine.pstEventBase);

    if (pstUdsRetryEvent) {
        event_del(pstUdsRetryEvent);
        event_free(pstUdsRetryEvent);
        pstUdsRetryEvent = NULL;   
    }

    if (pstSignalEvent) {
        event_del(pstSignalEvent);
        event_free(pstSignalEvent);
        pstSignalEvent = NULL;
    }

    eventEngineCleanup(&stEventEngine);
    event_base_free(stEventEngine.pstEventBase);

    fprintf(stderr, "[IMU-RX] Terminated.\n");
    return EXIT_SUCCESS;
}

#ifndef GOOGLE_TEST
int main(int argc, char* argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s /dev/ttyUSB0\n", argv[0]);
        return EXIT_FAILURE;
    }
    return run(argv[1]);
}
#endif
