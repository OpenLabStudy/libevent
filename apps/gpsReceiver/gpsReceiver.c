/* ========================================================================== */
/* System includes                                                           */
/* ========================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <termios.h>
#include <stdint.h>

/* ========================================================================== */
/* Project includes                                                           */
/* ========================================================================== */
#include "uartConfig.h"
#include "r632Gps.h"
#include "icdCommand.h"
#include "cmdRegistry.h"
#include "netUds.h"
#include "netCore.h"
#include "ioChannelUtil.h"
#include "runtime.h"
#include "udsClientRuntime.h"

/**
 * @brief UART로부터 데이터가 수신될 때 호출되는 Libevent read callback
 *
 * - bufferevent 입력 버퍼(evbuffer)에서 데이터를 가져온다.
 * - GPS 파서(R632Feed)에 데이터를 전달하여 유효 프레임 검사.
 * - 파싱 성공 시 시간/좌표 출력.
 *
 * @param pstBev    bufferevent 객체
 * @param pvCtx     사용자 정의 컨텍스트 (SUartCtx*)
 */
static void uartReadCallback(int iFd, short nEvent, void* pvData)
{
    (void)iFd; (void)nEvent;

    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    IO_EVENT_TYPE eEventType = pstIoChannel->ePendingLogicEvent;
    SGpsDataInfo            stGpsInfo;
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

                if (R632Feed(auchRecvBuffer, uiRecvSize, &stGpsInfo)) {
                    printf("\n===== R632 GNSS FRAME RECEIVED =====\n");
                    printf("Time  : %s\n", stGpsInfo.m_szTime);
                    printf("Lat   : %.8lf\n", stGpsInfo.m_stMsg3.m_dLatitude);
                    printf("Lon   : %.8lf\n", stGpsInfo.m_stMsg3.m_dLongitude);
                    printf("Alt   : %.3f m\n", stGpsInfo.m_stMsg3.m_fHeight);
                    printf("Head  : %.3f\n", stGpsInfo.m_stMsg3.m_fHeading);
                
                    // UDS#2의 클라이언트를 찾기
                    IO_CHANNEL* pstGpsTxIo = ioFindChannelByWorkerId(pstIoChannel->pstEventEngine, GPS_SND_TO_SF);
                    if (ioIsChannelAlive(pstGpsTxIo)) {
                        unsigned char auchSendBuf[UDS_MAX_BUFFER_SIZE];
                        unsigned char auchGpsData[UDS_MAX_BUFFER_SIZE];
                        /* === Payload 구성 === */
                        RES_GPS_DATA *pstGpsData    = (RES_GPS_DATA *)auchGpsData;
                        pstGpsData->fAltitude       = stGpsInfo.m_stMsg3.m_fHeight;
                        pstGpsData->fHeading        = stGpsInfo.m_stMsg3.m_fHeading;
                        pstGpsData->dLatitude       = stGpsInfo.m_stMsg3.m_dLatitude;
                        pstGpsData->dLongitude      = stGpsInfo.m_stMsg3.m_dLongitude;

                        MSG_ID stMsgId = { GPS_SND_TO_SF, SF_RCV_SENSOR_DATA };
                        createCmdResponse(CDM_GPS_DATA, auchGpsData, &stMsgId, auchSendBuf);
                        int iResultSize = getFrameSizeWithCmd(CDM_GPS_DATA, FRAME_TYPE_RESPONSE);
                        evbuffer_add(pstGpsTxIo->pstWriteBuffer, auchSendBuf, iResultSize);
                        event_add(pstGpsTxIo->pstWriteEvent, NULL);
                    }                    
                }else{

                }
                evbuffer_drain(pstIoChannel->pstReadBuffer, uiCopySize);
            }        
        break;
    
        case IO_EVT_CHANNEL_CLOSED:
        case IO_EVT_ERROR:
            printf("[GPS] channel error fd=%d\n", pstIoChannel->iFd);
            event_active(pstIoChannel->pstShutdownEvent, 0, 0);
            break;
    
        default:
            break;
    }
    pstIoChannel->ePendingLogicEvent = IO_EVENT_NONE;
}

/* ========================================================================== */
/* Main Entry                                                                 */
/* ========================================================================== */
int run(char* pchUartPath)
{    
    ioIgnoreSigpipeOnce();
    EVENT_ENGINE    stEventEngine;
    UART_CTX        stUartCtx = {
        .pchDevPath     = pchUartPath,
        .iBaudrate      = 115200,
        .iFd            = -1,
        .iBackoffMsec   = 200
    };

    UDS_CLIENT_RUNTIME_CFG stUdsClnRuntimeCfg = {
        .chWorkerId     = (char)GPS_SND_TO_SF,
        .chDstWorkerId  = (char)SF_RCV_SENSOR_DATA,
        .pchUdsPath     = UDS_2_PATH,
        .eRole          = ROLE_REQUESTER,
        .eType          = TYPE_UDS_CLI,
        .pchTag         = "GPS_SND_TO_SF",
    };

    stEventEngine.pstEventBase = event_base_new();
    if (!stEventEngine.pstEventBase) {
        fprintf(stderr, "[GPS-RECEIVER] event_base_new() failed\n");
        return EXIT_FAILURE;
    }
    eventEngineInit(&stEventEngine, 0);

    /* UART open */
    if (uartOpen(&stUartCtx) < 0) {
        fprintf(stderr, "[GPS-RECEIVER] uartOpen failed: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    IO_CHANNEL *pstIoChannel = eventSourceCreateWithBev(&stEventEngine, stUartCtx.iFd,
        TYPE_UART, ROLE_REQUESTER, NULL, NULL, uartReadCallback);
    pstIoChannel->chWorkerId = (char)GPS_RCV_UART;

    UDS_CLIENT_RUNTIME *pstUdsClnRuntime = udsClientRuntimeCreate(&stEventEngine, &stUdsClnRuntimeCfg, NULL, NULL);
    APP_SIGNAL_HANDLE *pstSigHandle = appSignalCreate(&stEventEngine, "GPS-RECEIVER");

    /* 이벤트 루프 시작 */
    event_base_dispatch(stEventEngine.pstEventBase);
    udsClientRuntimeDestroy(&pstUdsClnRuntime);
    appSignalDestroy(&pstSigHandle);
    /* 종료 처리 */
    eventEngineCleanup(&stEventEngine);
    event_base_free(stEventEngine.pstEventBase);

    fprintf(stderr,"[GPS-RECEIVER] Terminated.\n");
    return EXIT_SUCCESS;
}

#ifndef GOOGLE_TEST
int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s /dev/ttyUSB0\n", argv[0]);
        return EXIT_FAILURE;
    }
    run(argv[1]);
}
#endif
