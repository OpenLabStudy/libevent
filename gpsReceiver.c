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
                    IO_CHANNEL* pstGpsTxIo = ioFindChannelByWorkerId(pstIoChannel->pstEventEngine, GPS_RECEIVER);
                    if (ioIsChannelAlive(pstGpsTxIo)) {
                        unsigned char auchSendBuf[UDS_MAX_BUFFER_SIZE];
                        unsigned char auchGpsData[UDS_MAX_BUFFER_SIZE];
                        /* === Payload 구성 === */
                        RES_GPS_DATA *pstGpsData    = (RES_GPS_DATA *)auchGpsData;
                        pstGpsData->fAltitude       = stGpsInfo.m_stMsg3.m_fHeight;
                        pstGpsData->fHeading        = stGpsInfo.m_stMsg3.m_fHeading;
                        pstGpsData->dLatitude       = stGpsInfo.m_stMsg3.m_dLatitude;
                        pstGpsData->dLongitude      = stGpsInfo.m_stMsg3.m_dLongitude;

                        MSG_ID stMsgId = { GPS_RECEIVER, SF_SENSOR_RECEIVER };
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

static void applyCommand(unsigned short unCmd, unsigned char *puchCmdData, unsigned char *puchCmdResult)
{
    memset(puchCmdResult, 0, sizeof(puchCmdResult));
    switch (unCmd)
    {
    case CMD_ID_INFO:
        ((RES_ID*)puchCmdResult)->chResult = (char)GPS_RECEIVER;
        break; 
    default:
        fprintf(stderr, "[ACU] Unsupported CMD\n");
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
    unsigned char auchRecvBuffer[UDS_MAX_BUFFER_SIZE];
    switch (eEventType) {
    case IO_EVT_CHANNEL_CLOSED:
    case IO_EVT_ERROR:
        ioMarkChannelDead(pstIoChannel, pstIoChannel->ePendingLogicEvent);
        break;
    case IO_EVT_RX_DATA:
    {
        int iRecvLen = evbuffer_get_length(pstIoChannel->pstReadBuffer);
        fprintf(stderr,"### %s():%d Recv Size is %d ###\n", __func__, __LINE__, iRecvLen);
        if (iRecvLen < sizeof(FRAME_HEADER))
            break;

        memset(auchRecvBuffer, 0x00, sizeof(auchRecvBuffer));
        int iCopyLen = evbuffer_copyout(pstIoChannel->pstReadBuffer, auchRecvBuffer, iRecvLen);
        eErr = frameDecode(auchRecvBuffer, iCopyLen, FRAME_TYPE_REQUEST, &unCmd);
        if (eErr != FRAME_OK) {
            fprintf(stderr, "[GPS] frameDecode ERR: %s\n", frameErrToStr(eErr));
            int iOffset = findFrameHeader(auchRecvBuffer, iCopyLen);
            if (iOffset > 0) {
                /* 앞부분 garbage 제거 */
                evbuffer_drain(pstIoChannel->pstReadBuffer, iOffset);
                fprintf(stderr,"[GPS] resync: drop %d bytes, retry decode\n", iOffset);
            } else if (iOffset == -2) {
                /* STX half-match: 데이터 더 수신 */
                evbuffer_drain(pstIoChannel->pstReadBuffer, iCopyLen-1);
                fprintf(stderr,"[GPS] STX half match, wait more data\n");
            } else {
                /* STX 자체가 없음 → 전부 드랍 */
                evbuffer_drain(pstIoChannel->pstReadBuffer, iCopyLen);
                fprintf(stderr, "[GPS] no STX, drop all\n");
            }
        }
        int iFrameSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_REQUEST);
        /* === 프레임 소비 === */
        unsigned char auchCmdData[128];
        unsigned char auchCmdResult[128];
        unsigned char auchResult[128];
        unsigned int uiReqId;
        int iResultSize;
        evbuffer_drain(pstIoChannel->pstReadBuffer, iFrameSize);
        evbuffer_remove(pstIoChannel->pstReadBuffer, &uiReqId, sizeof(unsigned int));
        eErr = cmdDispatch(auchRecvBuffer, iCopyLen, auchCmdData);
        if (eErr != FRAME_OK){
            fprintf(stderr,"### %s():%d %s ###\n",__func__,__LINE__, frameErrToStr(eErr));
        }            
        applyCommand(unCmd, auchCmdData, auchCmdResult);
        fprintf(stderr,"### %s():%d %02X ###\n",__func__,__LINE__, auchCmdResult[0]);
        MSG_ID stMsgId = { GPS_RECEIVER, SF_SENSOR_RECEIVER };
        eErr = createCmdResponse(unCmd, auchCmdResult, &stMsgId, auchResult);
        iResultSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_RESPONSE);
        fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
        // sendUdsResponse(pstIoChannel, unCmd, uiReqId, auchResult, iResultSize);
        evbuffer_add(pstIoChannel->pstWriteBuffer, auchResult, iResultSize);
        event_add(pstIoChannel->pstWriteEvent, NULL);
    }

    default:
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

    fprintf(stderr,"\n[GPS-RX] SIGINT → shutdown\n");
    if(pstEventEngine->pstEventBase)
        event_base_loopexit(pstEventEngine->pstEventBase, NULL);
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
        fprintf(stderr, "[UDS#2] reconnect failed, retry later\n");
        return; /* 타이머는 계속 살아있음 */
    }

    fprintf(stderr, "[UDS#2] reconnected!\n");
    netSetNonblock(iSock);

    IO_CHANNEL *pstNewIo = eventSourceCreateWithBev(pstEventEngine, iSock,
            TYPE_UDS_CLI, ROLE_REQUESTER,
            NULL, NULL, ioChannelHandleEvent);
    if (!pstNewIo) {
        close(iSock);
        return;
    }      
    pstNewIo->iWorkerId = GPS_RECEIVER;
    pstNewIo->chFdCloseSet =  FD_OPENED;
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
        fprintf(stderr, "[GPS-RX] event_base_new() failed\n");
        return EXIT_FAILURE;
    }
    eventEngineInit(&stEventEngine, 0);

    /* UART open */
    if (uartOpen(&stUartCtx) < 0) {
        fprintf(stderr, "[GPS-RX] uartOpen failed: %s\n", strerror(errno));
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

    fprintf(stderr,"[GPS-RX] Terminated.\n");
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
