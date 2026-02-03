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
#include "runtime.h"
#include "udsClientRuntime.h"

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

                fprintf(stderr, "[IMU_SND_TO_SF] Roll=%.3f Pitch=%.3f Yaw=%.3f\n", fRoll, fPitch, fYaw);
                // fprintf(stderr,">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
                // fprintf(stderr, "[IMU] AccX=%.3f AccY=%.3f AccZ=%.3f\n", mtiSwapFloat(stImuFormat.stAcceleration.fAccX), 
                //         mtiSwapFloat(stImuFormat.stAcceleration.fAccY), mtiSwapFloat(stImuFormat.stAcceleration.fAccZ));
                // fprintf(stderr, "[IMU] DeltaX=%.3f DeltaY=%.3f DeltaZ=%.3f\n", mtiSwapFloat(stImuFormat.stDeltaV.fDeltaX), 
                //         mtiSwapFloat(stImuFormat.stDeltaV.fDeltaY), mtiSwapFloat(stImuFormat.stDeltaV.fDeltaZ));
                // fprintf(stderr, "[IMU] GyrX=%.3f GyrY=%.3f GyrZ=%.3f\n", mtiSwapFloat(stImuFormat.stRateOfTurn.fGyrX), 
                //         mtiSwapFloat(stImuFormat.stRateOfTurn.fGyrY), mtiSwapFloat(stImuFormat.stRateOfTurn.fGyrZ));
                // fprintf(stderr,"<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n");


                /* UDS#2(sensorFusion) 채널로 best-effort 전송 */
                IO_CHANNEL* pstImuTxIo = ioFindChannelByWorkerId(pstIoChannel->pstEventEngine, IMU_SND_TO_SF);
                if (ioIsChannelAlive(pstImuTxIo)) {
                    unsigned char auchSendBuf[UDS_MAX_BUFFER_SIZE];
                    unsigned char auchImuData[UDS_MAX_BUFFER_SIZE];

                    RES_RPY_DATA *pstImuData = (RES_RPY_DATA *)auchImuData;

                    pstImuData->dRoll  = (double)fRoll;
                    pstImuData->dPitch = (double)fPitch;
                    pstImuData->dYaw   = (double)fYaw;

                    MSG_ID stMsgId = { IMU_SND_TO_SF, SF_RCV_SENSOR_DATA };
                    createCmdResponse(CDM_IMU_DATA, auchImuData, &stMsgId, auchSendBuf);
                    int iResultSize = getFrameSizeWithCmd(CDM_IMU_DATA, FRAME_TYPE_RESPONSE);                    
                    evbuffer_add(pstImuTxIo->pstWriteBuffer, auchSendBuf, iResultSize);
                    event_add(pstImuTxIo->pstWriteEvent, NULL);
                } else {
                    /* sensorFusion 미연결/끊김 → 드롭 */
                    /* fprintf(stderr, "[IMU_SND_TO_SF] UDS_2 not alive, drop\n"); */
                }
            }
            evbuffer_drain(pstIoChannel->pstReadBuffer, uiCopySize);
        }
        break;

    case IO_EVT_CHANNEL_CLOSED:
    case IO_EVT_ERROR:
        fprintf(stderr, "[IMU_SND_TO_SF] UART channel error fd=%d\n", pstIoChannel->iFd);
        event_active(pstIoChannel->pstShutdownEvent, 0, 0);
        break;

    default:
        break;
    }

    pstIoChannel->ePendingLogicEvent = IO_EVENT_NONE;
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

    UDS_CLIENT_RUNTIME_CFG stUdsClnRuntimeCfg = {
        .iSelfWorkerId  = IMU_SND_TO_SF,
        .iDstWorkerId   = SF_RCV_SENSOR_DATA,
        .pchUdsPath     = UDS_2_PATH,
        .pchTag         = "IMU_SND_TO_SF"
    };
    

    stEventEngine.pstEventBase = event_base_new();
    if (!stEventEngine.pstEventBase) {
        fprintf(stderr, "[IMU-RECEIVER] event_base_new() failed\n");
        return EXIT_FAILURE;
    }
    eventEngineInit(&stEventEngine, 0);

    /* UART open */
    if (uartOpen(&stUartCtx) < 0) {
        fprintf(stderr, "[IMU-RECEIVER] uartOpen failed: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    IO_CHANNEL *pstIoChannel = eventSourceCreateWithBev(&stEventEngine, stUartCtx.iFd,
        TYPE_UART, ROLE_REQUESTER, NULL, NULL, uartReadCallback);
    pstIoChannel->iWorkerId = IMU_RCV_UART;

    UDS_CLIENT_RUNTIME *pstUdsClnRuntime = udsClientRuntimeCreate(&stEventEngine, &stUdsClnRuntimeCfg, NULL, NULL);
    APP_SIGNAL_HANDLE *pstSigHandle = appSignalCreate(&stEventEngine, "IMU-RECEIVER");

    event_base_dispatch(stEventEngine.pstEventBase);

    udsClientRuntimeDestroy(&pstUdsClnRuntime);
    appSignalDestroy(&pstSigHandle);

    eventEngineCleanup(&stEventEngine);
    event_base_free(stEventEngine.pstEventBase);

    fprintf(stderr, "[IMU-RECEIVER] Terminated.\n");
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
