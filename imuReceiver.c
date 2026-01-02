#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <termios.h>
#include <stdint.h>

#include "mti670Imu.h"
#include "eventEngine.h"
#include "icdCommand.h"
#include "netUds.h"
#include "netCore.h"
#include "eventSource.h"
#include "frame.h"
#include "uartConfig.h"

/* ============================================================
 * UART read logic event handler
 * ============================================================ */
static void uartReadCallback(int iFd, short nEvent, void* pvData)
{
    (void)iFd; (void)nEvent;

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
                IO_CHANNEL* pstImuTxIo =
                    ioFindChannelByWorkerId(pstIoChannel->pstEventEngine, UDS_2_CLN2_ID);

                if (ioIsChannelAlive(pstImuTxIo)) {
                    unsigned char auchSendBuf[UDS_MAX_SIZE];
                    RES_RPY_DATA stImuData;

                    stImuData.dRoll  = (double)fRoll;
                    stImuData.dPitch = (double)fPitch;
                    stImuData.dYaw   = (double)fYaw;

                    MSG_ID stMsgId;
                    ipcBuildMsgIdFromWorker(pstIoChannel->iWorkerId, &stMsgId);
                    if (makeResponseFrame(CDM_IMU_DATA, &stMsgId, &stImuData, auchSendBuf) == FRAME_OK) {
                        size_t sz = (size_t)getFrameSizeWithCmd(CDM_IMU_DATA, FRAME_TYPE_RESPONSE);
                        evbuffer_add(pstImuTxIo->pstWriteBuffer, auchSendBuf, sz);
                        event_add(pstImuTxIo->pstWriteEvent, NULL);
                    }
                } else {
                    /* sensorFusion 미연결/끊김 → 드롭 */
                    /* fprintf(stderr, "[IMU] UDS_2 not alive, drop\n"); */
                }
            }
            evbuffer_drain(pstIoChannel->pstReadBuffer, uiCopySize);
        }
        break;

    case IO_EVT_CHANNEL_CLOSED:
        fprintf(stderr, "[IMU] UART channel closed fd=%d\n", pstIoChannel->iFd);
        event_active(pstIoChannel->pstShutdownEvent, 0, 0);
        break;

    case IO_EVT_ERROR:
        fprintf(stderr, "[IMU] UART channel error fd=%d\n", pstIoChannel->iFd);
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
static void ioChannelHandleEvent(int iFd, short nEvent, void* pvData)
{
    (void)iFd;
    (void)nEvent;
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    IO_EVENT_TYPE eEventType = pstIoChannel->ePendingLogicEvent;

    switch (eEventType) {
    case IO_EVT_CHANNEL_CLOSED:
    case IO_EVT_ERROR:
        ioMarkChannelDead(pstIoChannel, pstIoChannel->ePendingLogicEvent);
        event_active(pstIoChannel->pstShutdownEvent, 0, 0);
        break;
    case IO_EVT_RX_DATA:
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
    (void)sig; (void)events;
    EVENT_ENGINE* pstEventEngine = (EVENT_ENGINE *)pvArg;

    fprintf(stderr, "\n[IMU-RX] SIGINT → shutdown\n");
    if (pstEventEngine->pstEventBase)
        event_base_loopexit(pstEventEngine->pstEventBase, NULL);
}

static void uds2ReconnectCb(evutil_socket_t fd, short nEvent, void *pvArg)
{
    (void)fd;
    (void)nEvent;
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);

    EVENT_ENGINE *pstEventEngine = (EVENT_ENGINE *)pvArg;
    /* 이미 살아있으면 재접속 불필요 */
    IO_CHANNEL *pstImuTxIo = ioFindChannelByWorkerId(pstEventEngine, UDS_2_CLN2_ID);

    if (ioIsChannelAlive(pstImuTxIo))
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

    pstNewIo->iWorkerId = UDS_2_CLN2_ID;

    /* worker register */
    ipcSendWorkerRegister(pstNewIo, WORKER_IMU);
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
    eventEngineInit(&stEventEngine);

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
