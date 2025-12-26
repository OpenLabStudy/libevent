/**
 * @file uartRx.c
 * @brief libevent 기반 UART 자동 재연결 + Hemisphere R632 GNSS ($BIN) 파서
 *
 * 기능 요약:
 * - UART를 비동기로 읽어 GPS Binary 프레임을 수신
 * - evbuffer로 수신한 데이터에서 GNSS Frame 파싱 (R632Feed)
 * - UART 연결이 끊어지면 자동 재연결 (exponential backoff)
 * - SIGINT 시 안전 종료
 *
 * 빌드예:
 * gcc -O2 -Wall -Wextra -o uartRx uartRx.c -levent -lm
 *
 * @author 
 */

#define _GNU_SOURCE

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
/* Libevent includes                                                          */
/* ========================================================================== */
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

/* ========================================================================== */
/* Project includes                                                           */
/* ========================================================================== */
#include "r632Gps.h"
#include "eventSource.h"
#include "eventEngine.h"
 #include "netUds.h"
 #include "netCore.h"
 #include "frame.h"
 #include "udsFrame.h"
/* ========================================================================== */
/* UART Configuration                                                         */
/* ========================================================================== */
typedef struct {
    const char          *pchDevPath;
    int                 iFd;
    struct event_base   *pstEventBase;
    struct event        *pstEventSigint;
    struct event        *pstEventReopen;
    struct bufferevent  *pstBev;
    int                 iBackoffMsec;
    int                 iBaudrate;
} UART_CTX;

int uartMakeNonblocking(int iFd)
{
    int iFlags = fcntl(iFd, F_GETFL, 0);
    if (iFlags < 0) 
        return -1;
    return fcntl(iFd, F_SETFL, iFlags | O_NONBLOCK);
}

/**
 * @brief UART 속성 설정 함수
 * @param iFd 파일 디스크립터
 * @param baudrate 원하는 Baudrate (예: 9600, 115200, 230400 등)
 * @return 0 성공, -1 실패
 */
int uartSetRaw(int iFd, int baudrate)
{
    struct termios stTermios;
    speed_t speed;

    //Baudrate 매핑
    switch (baudrate) {
        case 9600: speed = B9600; break;
        case 19200: speed = B19200; break;
        case 38400: speed = B38400; break;
        case 57600: speed = B57600; break;
        case 115200: speed = B115200; break;
#ifdef B230400
        case 230400: speed = B230400; break;
#endif
#ifdef B460800
        case 460800: speed = B460800; break;
#endif
        default:
            fprintf(stderr, "Unsupported baudrate: %d\n", baudrate);
            return -1;
    }

    if (tcgetattr(iFd, &stTermios) < 0)
        return -1;

    cfmakeraw(&stTermios);
    cfsetispeed(&stTermios, speed);
    cfsetospeed(&stTermios, speed);

    stTermios.c_cflag &= ~PARENB;   // No parity
    stTermios.c_cflag &= ~CSTOPB;   // 1 stop bit
    stTermios.c_cflag &= ~CSIZE;
    stTermios.c_cflag |= CS8 | CLOCAL | CREAD; // 8 data bits, enable RX
    stTermios.c_cflag &= ~HUPCL;    // No hang-up on close

    stTermios.c_cc[VMIN]  = 1;
    stTermios.c_cc[VTIME] = 0;

    if (tcsetattr(iFd, TCSANOW, &stTermios) < 0)
        return -1;

    tcflush(iFd, TCIFLUSH);
    return 0;
}

/**
 * @brief UART 열기 함수
 * @param ctx UART context
 * @return 0 성공, -1 실패
 */
int uartOpen(UART_CTX* pstUartCtx)
{
    int iFd = open(pstUartCtx->pchDevPath, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (iFd < 0)
        return -1;
        
    if (uartSetRaw(iFd, pstUartCtx->iBaudrate) < 0) {
        close(iFd);
        return -1;
    }

    if (uartMakeNonblocking(iFd) < 0) {
        close(iFd);
        return -1;
    }
    pstUartCtx->iFd = iFd;
    return 0;
}

void uartClose(UART_CTX* pstUartCtx)
{
    if (pstUartCtx->iFd >= 0) {
        close(pstUartCtx->iFd);
        pstUartCtx->iFd = -1;
    }
}

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
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    IO_EVENT_TYPE eEventType = pstIoChannel->ePendingLogicEvent;
    SGpsDataInfo            stGpsInfo;
    switch (eEventType) {
        case IO_EVT_RX_DATA:
            while (1) {
                size_t tRecvLen = evbuffer_get_length(pstIoChannel->pstReadBuffer);
                if (tRecvLen == 0)
                    break;

                unsigned char* puchBuf = evbuffer_pullup(pstIoChannel->pstReadBuffer, tRecvLen);
                if (!puchBuf)
                    break;

                if (R632Feed(puchBuf, (int)tRecvLen, &stGpsInfo))
                {
                    printf("\n===== R632 GNSS FRAME RECEIVED =====\n");
                    printf("Time  : %s\n", stGpsInfo.m_szTime);
                    printf("Lat   : %.8lf\n", stGpsInfo.m_stMsg3.m_dLatitude);
                    printf("Lon   : %.8lf\n", stGpsInfo.m_stMsg3.m_dLongitude);
                    printf("Alt   : %.3f m\n", stGpsInfo.m_stMsg3.m_fHeight);
                    printf("Sat   : %u\n", stGpsInfo.m_stMsg3.m_wNumSatsUsed);
                }
                evbuffer_drain(pstIoChannel->pstReadBuffer, tRecvLen);
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
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    IO_EVENT_TYPE eEventType = pstIoChannel->ePendingLogicEvent;
    unsigned char auchRecvBuffer[2048];
    unsigned char uchResult[sizeof(IPC_FRAME)];
    IPC_FRAME *pstIpcFrame = (IPC_FRAME *)uchResult;    

    unsigned short unCmd = 0;
    FRAME_ERR eErr;
    
    switch (eEventType) {
    case IO_EVT_RX_DATA:
        while (1) {
            unsigned int uiRequestId;
            unsigned char *auchPayload=NULL;
            unsigned int uiPayloadLen;
            fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
            unsigned int uiRecvLen = evbuffer_get_length(pstIoChannel->pstReadBuffer);
            /* 최소 헤더도 안 왔으면 중단 */
            if (uiRecvLen < sizeof(FRAME_HEADER))
                break;

            memset(auchRecvBuffer, 0x00, sizeof(auchRecvBuffer));
            int iCopyLen = evbuffer_copyout(pstIoChannel->pstReadBuffer, auchRecvBuffer, uiRecvLen);            
            udsFrameDecode(auchRecvBuffer, iCopyLen, &uiRequestId,
                &auchPayload, &uiPayloadLen);
                /*추후 evbuffer에 삭제 크기 알 필요 있음*/

            eErr = frameDecode(auchPayload, uiPayloadLen, FRAME_TYPE_REQUEST, &unCmd);
            if (eErr != FRAME_OK) {
                fprintf(stderr, "[TCP-SVR] frameDecode ERR: %s\n", frameErrToStr(eErr));
                evbuffer_drain(pstIoChannel->pstReadBuffer, 1);
                continue;
            }
            fprintf(stderr,"\n### %s():%d###\n",__func__,__LINE__);

            /* === CMD 먼저 추출 (가벼운 파싱) === */
            getCmdFromFrame(auchPayload, uiPayloadLen, &unCmd);
            int iFrameSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_REQUEST);
            if (iCopyLen < iFrameSize)
                break;
fprintf(stderr,"\n### %s():%d###\n",__func__,__LINE__);
            /* === 프레임 하나 소비 === */
            evbuffer_drain(pstIoChannel->pstReadBuffer, iCopyLen);
            unsigned char uchaSendBuf[UDS_MAX_SIZE];
            unsigned char auchResult[UDS_MAX_SIZE];
            unsigned int uiSendSize;
            int iResultSize;
            fprintf(stderr,"\n### %s():%d###\n",__func__,__LINE__);
            /* === 명령 처리 === */
            eErr = commandHandler(auchPayload, auchResult, &iResultSize);
            if (eErr != FRAME_OK || iResultSize <= 0)
                continue;
            makeResponseFrame();    

            fprintf(stderr,"\n### %s():%d###\n",__func__,__LINE__);
            
            udsFrameBuildRequest(uiRequestId, auchResult, iResultSize, 
                uchaSendBuf, sizeof(uchaSendBuf), &uiSendSize);

            evbuffer_add(pstIoChannel->pstWriteBuffer, uchaSendBuf, uiSendSize);
            event_add(pstIoChannel->pstWriteEvent, NULL);
        }        
        break;

    case IO_EVT_CHANNEL_CLOSED:
        printf("[UDS-SVR] channel closed fd=%d\n", pstIoChannel->iFd);
        event_active(pstIoChannel->pstShutdownEvent, 0, 0);
        break;

    case IO_EVT_ERROR:
        printf("[UDS-SVR] channel error fd=%d\n", pstIoChannel->iFd);
        event_active(pstIoChannel->pstShutdownEvent, 0, 0);
        break;

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

    fprintf(stderr,"\n[UDP-SVR] SIGINT → shutdown\n");
    if(pstEventEngine->pstEventBase)
        event_base_loopexit(pstEventEngine->pstEventBase, NULL);
}


/* ========================================================================== */
/* Main Entry                                                                 */
/* ========================================================================== */

int run(int iId, char* pchUartPath)
{    
    EVENT_ENGINE   stEventEngine;
    struct event*   pstSignalEvent;
    struct event*   pstEventAccept;
    UART_CTX stUartCtx = {0};
    unsigned char uchMyId = 0x00;    
    if(iId == 1)
        uchMyId = UDS_1_CLN1_ID;
    else if(iId == 2)
        uchMyId = UDS_1_CLN2_ID;
    else if(iId == 3)
        uchMyId = UDS_1_CLN3_ID;
    else if(iId == 4)
        uchMyId = UDS_1_CLN4_ID;

    stEventEngine.pstEventBase = event_base_new();
    if (!stEventEngine.pstEventBase) {
        fprintf(stderr, "[UDS-Client] event_base_new() failed\n");
        return EXIT_FAILURE;
    }
    eventEngineInit(&stEventEngine);

    stUartCtx.pchDevPath = pchUartPath;
    stUartCtx.iBaudrate = 115200;
    stUartCtx.iFd = -1;
    stUartCtx.iBackoffMsec = 200;
    /* 초기 장치열기 */
    uartOpen(&stUartCtx);
    eventSourceCreateWithBev(&stEventEngine, stUartCtx.iFd,
        TYPE_UART, ROLE_REQUESTER,
        NULL, NULL, uartReadCallback
    );

    int iClientSock = netUdsCreateClient(UDS_1_PATH);
    if (iClientSock < 0) {
        fprintf(stderr, "[UDS-CLI] Failed to create UDS client socket\n");
        return EXIT_FAILURE;
    }
    printf("[UDS-CLI] Connecting to %s\n", UDS_1_PATH);
    netSetNonblock(iClientSock);
    eventSourceCreateWithBev(&stEventEngine, iClientSock,
        TYPE_UDS_CLI, ROLE_WORKER,
        NULL, udsWriteCallback, ioChannelHandleEvent
    );


    /* SIGINT 처리 등록 */
    pstSignalEvent = evsignal_new(stEventEngine.pstEventBase,
        SIGINT, signalCb, &stEventEngine);
    event_add(pstSignalEvent, NULL);

    /* 이벤트 루프 시작 */
    event_base_dispatch(stEventEngine.pstEventBase);
    if(pstSignalEvent){
        event_del(pstSignalEvent);
        event_free(pstSignalEvent);
        pstSignalEvent =  NULL;
    }

    if(pstEventAccept){
        event_del(pstEventAccept);
        event_free(pstEventAccept);
        pstEventAccept =  NULL;
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
    if (argc < 3)
    {
        fprintf(stderr, "Usage: %s /dev/ttyUSB0 UDS_ID\n", argv[0]);
        return EXIT_FAILURE;
    }
    run(atoi(argv[2]), argv[1]);
}
#endif