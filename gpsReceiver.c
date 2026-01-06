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
#include "ipcUtil.h"

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
                    printf("Sat   : %u\n", stGpsInfo.m_stMsg3.m_wNumSatsUsed);
                }
                // UDS#2의 클라이언트를 찾기
                IO_CHANNEL* pstGpsTxIo = ioFindChannelByWorkerId(pstIoChannel->pstEventEngine, UDS_2_GPS_RECEIVER);
                if (ioIsChannelAlive(pstGpsTxIo)) {
                    unsigned char uchaSendBuf[UDS_MAX_BUFFER_SIZE];
                    /* === Payload 구성 === */
                    RES_LLA_DATA stGpsData;
                    stGpsData.dAltitude = stGpsInfo.m_stMsg3.m_fHeight;
                    stGpsData.dLatitude = stGpsInfo.m_stMsg3.m_dLatitude;
                    stGpsData.dLongitude = stGpsInfo.m_stMsg3.m_dLongitude;

                    /* === Frame 생성 === */
                    MSG_ID stMsgId;
                    ipcBuildMsgIdFromWorker(pstIoChannel->iWorkerId, &stMsgId);
                    makeResponseFrame(CDM_GPS_DATA, &stMsgId, (unsigned char*)&stGpsData, uchaSendBuf);
                    /* === Write buffer에 적재 === */
                    evbuffer_add(pstGpsTxIo->pstWriteBuffer, uchaSendBuf, getFrameSizeWithCmd(CDM_GPS_DATA, FRAME_TYPE_RESPONSE));
                    /* === Write 이벤트 발생 === */
                    event_add(pstGpsTxIo->pstWriteEvent, NULL);
                }
                evbuffer_drain(pstIoChannel->pstReadBuffer, uiCopySize);
            }        
        break;
    
        case IO_EVT_CHANNEL_CLOSED:
            printf("[GPS] channel closed fd=%d\n", pstIoChannel->iFd);
            event_active(pstIoChannel->pstShutdownEvent, 0, 0);
            break;
    
        case IO_EVT_ERROR:
            printf("[GPS] channel error fd=%d\n", pstIoChannel->iFd);
            event_active(pstIoChannel->pstShutdownEvent, 0, 0);
            break;
    
        default:
            break;
    }
    pstIoChannel->ePendingLogicEvent = IO_EVENT_NONE;
}



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
* SIGINT 콜백
* ============================================================ */
static void signalCb(evutil_socket_t sig, short events, void* pvArg)
{
    EVENT_ENGINE* pstEventEngine = (EVENT_ENGINE *)pvArg;

    fprintf(stderr,"\n[UDP-SVR] SIGINT → shutdown\n");
    if(pstEventEngine->pstEventBase)
        event_base_loopexit(pstEventEngine->pstEventBase, NULL);
}

static void uds2ReconnectCb(evutil_socket_t fd, short nEvent, void *pvArg)
{
    (void)fd;
    (void)nEvent;
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);

    EVENT_ENGINE *pstEventEngine = (EVENT_ENGINE *)pvArg;
    /* 이미 살아있으면 재접속 불필요 */
    IO_CHANNEL *pstImuTxIo = ioFindChannelByWorkerId(pstEventEngine, UDS_2_GPS_RECEIVER);

    if (ioIsChannelAlive(pstImuTxIo))
        return;

    int iSock = netUdsCreateClient(UDS_2_PATH);
    if (iSock < 0) {
        fprintf(stderr, "[UDS#2] reconnect failed, retry later\n");
        return; /* 타이머는 계속 살아있음 */
    }

    fprintf(stderr, "[UDS#2] reconnected!\n");
    netSetNonblock(iSock);

    IO_CHANNEL *pstNewIo = eventSourceCreateWithBev(pstEventEngine, iSock,
            TYPE_UDS_CLI, ROLE_REQUESTER,
            NULL, NULL, ioChannelHandleEvent);

    pstNewIo->iWorkerId = UDS_2_GPS_RECEIVER;

    /* worker register */
    ipcSendWorkerRegister(pstNewIo, WORKER_IMU);
}

/* ========================================================================== */
/* Main Entry                                                                 */
/* ========================================================================== */

int run(char* pchUartPath)
{    
    ioIgnoreSigpipeOnce();
    EVENT_ENGINE    stEventEngine;
    struct event*   pstSignalEvent;
    struct event*   pstUdsRetryEvent = NULL;
    UART_CTX        stUartCtx = {
        .pchDevPath     = pchUartPath,
        .iBaudrate      = 115200,
        .iFd            = -1,
        .iBackoffMsec   = 200
    };
    struct timeval stRertyTimeOut = {1, 0};

    stEventEngine.pstEventBase = event_base_new();
    if (!stEventEngine.pstEventBase) {
        fprintf(stderr, "[UDS-Client] event_base_new() failed\n");
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

    /* SIGINT 처리 등록 */
    pstSignalEvent = evsignal_new(stEventEngine.pstEventBase, SIGINT, signalCb, &stEventEngine);
    event_add(pstSignalEvent, NULL);

    /* 이벤트 루프 시작 */
    event_base_dispatch(stEventEngine.pstEventBase);
    if (pstUdsRetryEvent) {
        event_del(pstUdsRetryEvent);
        event_free(pstUdsRetryEvent);
        pstUdsRetryEvent = NULL;   
    }
    if(pstSignalEvent){
        event_del(pstSignalEvent);
        event_free(pstSignalEvent);
        pstSignalEvent =  NULL;
    }
    /* 종료 처리 */
    eventEngineCleanup(&stEventEngine);    
    event_base_free(stEventEngine.pstEventBase);

    fprintf(stderr,"[UDP-SVR] Terminated.\n");
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
