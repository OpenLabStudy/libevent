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
#include "ipcUtil.h"

typedef enum
{
    ACU_STATE_IDLE,
    ACU_STATE_WAIT_RESPONSE
} ACU_STATE;

typedef struct
{
    ACU_STATE eState;
    unsigned int uiLastCmd;
} ACU_CTRL_STATE;

static ACU_CTRL_STATE g_stAcuState = {
    .eState = ACU_STATE_IDLE
};


/* ============================================================
 * UART command send function
 * ============================================================ */
static int acuSendUartCommand(IO_CHANNEL *pstIoChannel, const unsigned char *puchFrame, unsigned int uiFrameSize)
{    
    if (!pstIoChannel || !pstIoChannel->pstWriteBuffer || !pstIoChannel->pstWriteEvent) {
        return -1; 
    }

    if (g_stAcuState.eState != ACU_STATE_IDLE)
    {
        fprintf(stderr, "[ACU] busy, ignore command\n");
        return -1;
    }

    evbuffer_add(pstIoChannel->pstWriteBuffer, puchFrame, uiFrameSize);   

    /* write event 활성화 */
    event_active(pstIoChannel->pstWriteEvent, EV_WRITE, 0);

    g_stAcuState.eState = ACU_STATE_WAIT_RESPONSE;
    return 0;
}

/* ============================================================
 * UART read logic event handler
 * ============================================================ */
static void uartReadCallback(int iFd, short nEvent, void *pvData)
{
    (void)iFd;
    (void)nEvent;

    IO_CHANNEL *pstIoChannel = (IO_CHANNEL *)pvData;
    IO_EVENT_TYPE eEventType = pstIoChannel->ePendingLogicEvent;

    /* NOTE: IMU parser는 스트림 상태 유지가 필요 → static OK */

    unsigned char auchRecvBuffer[2048];

    switch (eEventType)
    {
    case IO_EVT_RX_DATA:
        while (1)
        {
            unsigned int uiRecvSize = evbuffer_get_length(pstIoChannel->pstReadBuffer);
            if (uiRecvSize == 0)
                break;

            if (uiRecvSize > sizeof(auchRecvBuffer))
                uiRecvSize = sizeof(auchRecvBuffer);

            int uiCopySize = evbuffer_copyout(pstIoChannel->pstReadBuffer, auchRecvBuffer, uiRecvSize);
            if (uiCopySize <= 0)
                break;
            evbuffer_drain(pstIoChannel->pstReadBuffer, uiCopySize);
        }
        break;

    case IO_EVT_CHANNEL_CLOSED:
    case IO_EVT_ERROR:
        fprintf(stderr, "[ACU] UART channel closed fd=%d\n", pstIoChannel->iFd);
        event_active(pstIoChannel->pstShutdownEvent, 0, 0);
        break;

    default:
        break;
    }

    pstIoChannel->ePendingLogicEvent = IO_EVENT_NONE;
}

/* ============================================================
 * UDS command channel logic handler
 * ============================================================ */
static void commandEventCb(int iFd, short nEvent, void *pvData)
{
    (void)iFd;
    (void)nEvent;
    IO_CHANNEL *pstIoChannel = (IO_CHANNEL *)pvData;
    IO_EVENT_TYPE eEventType = pstIoChannel->ePendingLogicEvent;

    unsigned char auchRecvBuffer[UDS_MAX_BUFFER_SIZE];
    unsigned short unCmd = 0;
    FRAME_ERR eErr;

    switch (eEventType)
    {
    case IO_EVT_CHANNEL_CLOSED:
    case IO_EVT_ERROR:
        ioMarkChannelDead(pstIoChannel, pstIoChannel->ePendingLogicEvent);
        event_active(pstIoChannel->pstShutdownEvent, 0, 0);
        break;
    case IO_EVT_RX_DATA:
        while (1)
        {
            int iRecvLen = evbuffer_get_length(pstIoChannel->pstReadBuffer);            
            /* 최소 헤더도 없으면 중단 */
            if (iRecvLen < sizeof(FRAME_HEADER))
                break;
            fprintf(stderr, "Recv Size is %d\n", iRecvLen);

            memset(auchRecvBuffer, 0x00, sizeof(auchRecvBuffer));
            int iCopyLen = evbuffer_copyout(pstIoChannel->pstReadBuffer,
                                            auchRecvBuffer, iRecvLen);
            /* frameDecode에 대한 처리가 완전한지 확인 필요*/
            eErr = frameDecode(auchRecvBuffer, iCopyLen, FRAME_TYPE_REQUEST, &unCmd);
            if (eErr != FRAME_OK)
            {
                fprintf(stderr, "[UDS-SVR] frameDecode ERR: %s\n", frameErrToStr(eErr));
                int iOffset = findFrameHeader(auchRecvBuffer, iCopyLen);
                if (iOffset > 0)
                {
                    /* 앞부분 garbage 제거 */
                    evbuffer_drain(pstIoChannel->pstReadBuffer, iOffset);
                    fprintf(stderr, "[UDS-SVR] resync: drop %d bytes, retry decode\n", iOffset);
                }
                else if (iOffset == -2)
                {
                    /* STX half-match: 데이터 더 수신 */
                    evbuffer_drain(pstIoChannel->pstReadBuffer, iCopyLen - 1);
                    fprintf(stderr, "[UDS-SVR] STX half match, wait more data\n");
                }
                else
                {
                    /* STX 자체가 없음 → 전부 드랍 */
                    evbuffer_drain(pstIoChannel->pstReadBuffer, iCopyLen);
                    fprintf(stderr, "[UDS-SVR] no STX, drop all\n");
                }
                continue;
            }
            int iFrameSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_REQUEST);
            /* === 프레임 소비 === */
            evbuffer_drain(pstIoChannel->pstReadBuffer, iFrameSize + sizeof(unsigned int));
            unsigned int uiReqId;
            memcpy(&uiReqId, auchRecvBuffer + iFrameSize, sizeof(unsigned int));
            unsigned char uchaSendBuf[UDS_MAX_BUFFER_SIZE];
            unsigned char auchResult[UDS_MAX_BUFFER_SIZE];
            int iResultSize;
            RES_ID *pstResId;
            REQ_POSITIONER_AZ_EL_SET *pstReqPositionerAzElSet;
            RES_POSITIONER_AZ_EL_SET *pstResPositionerAzElSet;
            REQ_POSITIONER_DEG_SEND *pstReqPositionerDegSend;
            RES_POSITIONER_DEG_SEND *pstResPositionerDegSend;
            REQ_ACU_MODE* pstReqAcuMode;
            RES_ACU_MODE* pstResAcuMode;
            REQ_AZ_EL_OFFSET_SET* pstReqAzElOffsetSet;
            RES_AZ_EL_OFFSET_SET* pstResAzElOffsetSet;
            switch (unCmd) {        
                case CMD_ID_INFO:
                    pstResId = (RES_ID *)(auchResult);
                    pstResId->chResult = (char)pstIoChannel->iWorkerId;
                    break;
                case CMD_IBIT:
                    break;

                case CMD_RBIT:
                    break;

                case CMD_CBIT:
                    break;

                case CMD_POSITIONER_AZ_EL_SET:
                    pstReqPositionerAzElSet = (REQ_POSITIONER_AZ_EL_SET *)(auchRecvBuffer + sizeof(FRAME_HEADER));
                    fprintf(stderr,"Azimuth: %s, Elevation: %s\n", pstReqPositionerAzElSet->chAzimuthDeg,
                            pstReqPositionerAzElSet->chElevationDeg);
                    pstResPositionerAzElSet = (RES_POSITIONER_AZ_EL_SET *)(auchResult);
                    pstResPositionerAzElSet->chResult = 0x01;
                    break;

                case CMD_POSITIONER_DEG_SEND:
                    pstReqPositionerDegSend = (REQ_POSITIONER_DEG_SEND *)(auchRecvBuffer + sizeof(FRAME_HEADER));
                    fprintf(stderr,"Positioner DEG Send On/Off: %d\n", pstReqPositionerDegSend->chSendOnOff);   
                    pstResPositionerDegSend = (RES_POSITIONER_DEG_SEND *)(auchResult);
                    pstResPositionerDegSend->chResult = 0x01;
                    break;

                case CMD_ACU_MODE_SELECT:
                    pstReqAcuMode = (REQ_ACU_MODE *)(auchRecvBuffer + sizeof(FRAME_HEADER));
                    fprintf(stderr,"ACU Mode Select: %d\n", pstReqAcuMode->chAcuMode);
                    pstResAcuMode = (RES_ACU_MODE *)(auchResult);
                    pstResAcuMode->chResult = 0x01;
                    break;

                case CMD_AZ_EL_OFFSET_SET:
                    pstReqAzElOffsetSet = (REQ_AZ_EL_OFFSET_SET *)(auchRecvBuffer + sizeof(FRAME_HEADER));
                    fprintf(stderr,"AZ Offset: %d, EL Offset: %d\n", pstReqAzElOffsetSet->iAzOffset,
                            pstReqAzElOffsetSet->iElOffset);
                    pstResAzElOffsetSet = (RES_AZ_EL_OFFSET_SET *)(auchResult);
                    pstResAzElOffsetSet->chResult = 0x1;
                    break;                    

                default:
                    break;  
            }
            MSG_ID stMsgId;
            ipcBuildMsgIdFromWorker(pstIoChannel->iWorkerId, &stMsgId);
            makeResponseFrame(unCmd, &stMsgId, auchResult, uchaSendBuf);
            unsigned int uiFrameSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_RESPONSE);
            fprintf(stderr,"===== RESPONSE DATA =====\n");
            for(int i=0; i<uiFrameSize; i++){
                fprintf(stderr, "%02X ",uchaSendBuf[i]);
            }
            memcpy(uchaSendBuf+uiFrameSize, &uiReqId, sizeof(unsigned int)); 

            evbuffer_add(pstIoChannel->pstWriteBuffer, uchaSendBuf, uiFrameSize+sizeof(unsigned int));
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
static void signalCb(evutil_socket_t sig, short events, void *pvArg)
{
    (void)sig;
    (void)events;
    EVENT_ENGINE *pstEventEngine = (EVENT_ENGINE *)pvArg;

    fprintf(stderr, "\n[ACU] SIGINT → shutdown\n");
    if (pstEventEngine->pstEventBase)
        event_base_loopexit(pstEventEngine->pstEventBase, NULL);
}

static void uds1ReconnectCb(evutil_socket_t fd, short nEvent, void *pvArg)
{
    (void)fd;
    (void)nEvent;

    EVENT_ENGINE *pstEventEngine = (EVENT_ENGINE *)pvArg;
    /* 이미 살아있으면 재접속 불필요 */
    IO_CHANNEL *pstImuTxIo = ioFindChannelByWorkerId(pstEventEngine, UDS_1_ACU_CONTROLLER);

    if (ioIsChannelAlive(pstImuTxIo))
        return;

    int iSock = netUdsCreateClient(UDS_1_PATH);
    if (iSock < 0)
    {
        fprintf(stderr, "[UDS#1] reconnect failed, retry later\n");
        return; /* 타이머는 계속 살아있음 */
    }

    fprintf(stderr, "[UDS#1] reconnected!\n");
    IO_CHANNEL *pstNewIo = eventSourceCreateWithBev(pstEventEngine, iSock,
                                                    TYPE_UDS_CLI, ROLE_REQUESTER,
                                                    NULL, NULL, commandEventCb);
if (!pstNewIo) {
        close(iSock);
        return;
    }
    pstNewIo->iWorkerId = UDS_1_ACU_CONTROLLER;
}

// static void uds3ReconnectCb(evutil_socket_t fd, short nEvent, void *pvArg)
// {
//     (void)fd;
//     (void)nEvent;

//     EVENT_ENGINE *pstEventEngine = (EVENT_ENGINE *)pvArg;
//     /* 이미 살아있으면 재접속 불필요 */
//     IO_CHANNEL *pstImuTxIo = ioFindChannelByWorkerId(pstEventEngine, UDS_3_ACU_CONTROLLER);

//     if (ioIsChannelAlive(pstImuTxIo))
//         return;

//     int iSock = netUdsCreateClient(UDS_3_PATH);
//     if (iSock < 0) {
//         fprintf(stderr, "[UDS#3] reconnect failed, retry later\n");
//         return; /* 타이머는 계속 살아있음 */
//     }

//     fprintf(stderr, "[UDS#3] reconnected!\n");
//     IO_CHANNEL *pstNewIo = eventSourceCreateWithBev(pstEventEngine, iSock,
//             TYPE_UDS_CLI, ROLE_REQUESTER,
//             NULL, NULL, ioChannelHandleEvent);

//     pstNewIo->iWorkerId = UDS_3_ACU_CONTROLLER;

//     /* worker register */
//     ipcSendWorkerRegister(pstNewIo, WORKER_IMU);
// }

/* ============================================================
 * Main
 * ============================================================ */
int run(char *pchUartPath)
{
    // 이미 끊어진 소켓에 write() 했을 때 프로세스가 즉사(SIGPIPE)하는 것을 막는다.
    ioIgnoreSigpipeOnce();

    EVENT_ENGINE stEventEngine;
    struct event *pstSignalEvent = NULL;
    struct event *pstUdsRetryEvent = NULL;
#if 0
    UART_CTX stUartCtx = {
        .pchDevPath     = pchUartPath,
        .iBaudrate      = 115200,
        .iFd            = -1,
        .iBackoffMsec   = 200
    };
#endif
    struct timeval stRertyTimeOut = {1, 0};

    stEventEngine.pstEventBase = event_base_new();
    if (!stEventEngine.pstEventBase)
    {
        fprintf(stderr, "[ACU] event_base_new() failed\n");
        return EXIT_FAILURE;
    }
    eventEngineInit(&stEventEngine);
#if 0
    /* UART open */
    if (uartOpen(&stUartCtx) < 0) {
        fprintf(stderr, "[ACU] uartOpen failed: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    eventSourceCreateWithBev(&stEventEngine, stUartCtx.iFd,
        TYPE_UART, ROLE_REQUESTER, NULL, NULL, uartReadCallback);
#endif
    pstUdsRetryEvent = event_new(stEventEngine.pstEventBase,
                                 -1, EV_PERSIST | EV_TIMEOUT,
                                 uds1ReconnectCb, &stEventEngine);
    event_add(pstUdsRetryEvent, &stRertyTimeOut);

    pstSignalEvent = evsignal_new(stEventEngine.pstEventBase, SIGINT, signalCb, &stEventEngine);
    event_add(pstSignalEvent, NULL);

    event_base_dispatch(stEventEngine.pstEventBase);

    if (pstUdsRetryEvent)
    {
        event_del(pstUdsRetryEvent);
        event_free(pstUdsRetryEvent);
        pstUdsRetryEvent = NULL;
    }

    if (pstSignalEvent)
    {
        event_del(pstSignalEvent);
        event_free(pstSignalEvent);
        pstSignalEvent = NULL;
    }

    eventEngineCleanup(&stEventEngine);
    event_base_free(stEventEngine.pstEventBase);

    fprintf(stderr, "[ACU] Terminated.\n");
    return EXIT_SUCCESS;
}

#ifndef GOOGLE_TEST
int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s /dev/ttyUSB0\n", argv[0]);
        return EXIT_FAILURE;
    }
    return run(argv[1]);
}
#endif
