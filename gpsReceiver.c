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
#include "netUds.h"
#include "netCore.h"
#include "eventSource.h"
#include "frame.h"
#include "icdCommand.h"
#include "eventEngine.h"

/* ========================================================================== */
/* UART + Event Context Structure                                             */
/* ========================================================================== */

/**
 * @struct SUartCtx
 * @brief UART 디바이스 및 libevent 실행 컨텍스트
 */
typedef struct SUartCtx
{
    const char*         pchDevPath;          /**< /dev/ttyUSB? */
    int                 iFd;                 /**< UART File descriptor */
    
    struct bufferevent* pstBufferEvent;              /**< UART bufferevent */
    struct event*       pstEventReconnect;   /**< Reopen retry timer */

    int                 iRetryIntervalMsec;        /**< Retry interval (exp backoff) */
} SUartCtx;


/* ========================================================================== */
/* Application-level Read Callback (UDS Client)                               */
/* ========================================================================== */
static void readCallback(struct bufferevent* pstBufferEvent, void* pvData)
{
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    unsigned char* puchRecvData;
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    struct evbuffer* pstInputBuffer = bufferevent_get_input(pstBufferEvent);
    size_t ulDataLen = evbuffer_get_length(pstInputBuffer);
    fprintf(stderr, "[Client] Received %zu bytes\n", ulDataLen);
    puchRecvData = (unsigned char*)malloc(ulDataLen);
    if (!puchRecvData)
        return;

    evbuffer_copyout(pstInputBuffer, puchRecvData, ulDataLen);
    MSG_ID stMsgId = { UDS_1_CLN1_ID, UDS_1_SVR_ID };
    
    responseFrame(puchRecvData, &stMsgId, ulDataLen);

    evbuffer_drain(pstInputBuffer, ulDataLen);
    free(puchRecvData);
}



/* ========================================================================== */
/* Application-level Event Callback (UDS Client)                              */
/* ========================================================================== */
static void eventCallback(struct bufferevent* pstBufferEvent,
    short nEvents, void* pvData)
{
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    (void)pstBufferEvent;

    if (nEvents & BEV_EVENT_EOF) {
        fprintf(stderr, "[TCP-Client] Server disconnected\n");
    } else if (nEvents & BEV_EVENT_ERROR) {
        fprintf(stderr, "[TCP-Client] Client socket error\n");
    }

    /* 실제 close/free 는 eventSession 의 eventCallbackWrapper 에서 수행 */
    // eventSourceDestroy(pstIoChannel);
    /* 이벤트 루프 종료 지시 */
    
    if (pstIoChannel->pstEventBase)
        event_base_loopexit(pstIoChannel->pstEventBase, NULL);
}


/* ========================================================================== */
/* Static Forward Declarations (Hungarian Prefix 적용)                        */
/* ========================================================================== */
static int  openUartDevice(SUartCtx* pstCtx);
static int  setUartConfigRaw115200(int iFd);
static int  setNonBlocking(int iFd);

static void uartReadCallback(struct bufferevent* pstBev, void* pvCtx);
static void uartEventCallback(struct bufferevent* pstBev, short shEvents, void* pvCtx);
static void sigintCallback(evutil_socket_t iSig, short shEvent, void* pvCtx);
static void reconnectTimerCallback(evutil_socket_t iFd, short shEvent, void* pvCtx);

static void cleanupContext(SUartCtx* pstCtx);


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
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    // 원하는 Baudrate을 Context에서 가져오도록 변경
    if (uartSetRaw(iFd, pstUartCtx->iBaudrate) < 0) {
        close(iFd);
        return -1;
    }
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);

    if (uartMakeNonblocking(iFd) < 0) {
        close(iFd);
        return -1;
    }
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);

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
/* ========================================================================== */
/* UART Read / Event Callbacks                                                */
/* ========================================================================== */

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
static void uartReadCallback(struct bufferevent* pstBev, void* pvArg)
{
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);

}


/**
 * @brief UART bufferevent 상태 이벤트 콜백
 *
 * 역할:
 * - UART 장치 제거/끊김 감지
 * - 자동 재연결 스케줄링
 *
 * @param pstBev     bufferevent 객체
 * @param shEvents   libevent 이벤트 플래그
 * @param pvCtx      사용자 컨텍스트
 */
static void uartEventCallback(struct bufferevent* pstBev, short shEvents, void* pvCtx)
{
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    (void)pstBev;
    // SUartCtx* pstCtx = (SUartCtx*)pvCtx;

    // if (shEvents & BEV_EVENT_ERROR)
    //     fprintf(stderr, "[WARN] UART error detected: %s\n", strerror(errno));

    // if (shEvents & BEV_EVENT_EOF)
    //     fprintf(stderr, "[INFO] UART disconnected.\n");

    // if (shEvents & (BEV_EVENT_ERROR | BEV_EVENT_EOF))
    // {
    //     struct timeval stDelay =
    //     {
    //         .tv_sec  = pstCtx->iBackoffMsec / 1000,
    //         .tv_usec = (pstCtx->iBackoffMsec % 1000) * 1000
    //     };

    //     if (pstCtx->iBackoffMsec < 2000)
    //         pstCtx->iBackoffMsec *= 2;

    //     evtimer_add(pstCtx->pstEventReconnect, &stDelay);
    //     bufferevent_free(pstCtx->pstBev);
    //     pstCtx->pstBev = NULL;

    //     if (pstCtx->iFd >= 0)
    //     {
    //         close(pstCtx->iFd);
    //         pstCtx->iFd = -1;
    //     }
    // }
}





/* ========================================================================== */
/* Main Entry                                                                 */
/* ========================================================================== */

int run(int iId, char* pchUartPath)
{    
    EVENT_ENGINE   stEventEngine;
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

    int iClientSock = netUdsCreateClient(UDS_1_PATH);
    if (iClientSock < 0) {
        fprintf(stderr, "[UDS-Client] Failed to create UDS client socket\n");
        return EXIT_FAILURE;
    }

    printf("[CLI] Connecting to %s\n", UDS_1_PATH);
    /* ------------------- */
    /* EVENT_SOURCE 생성   */
    /* ------------------- */
    netSetNonblock(iClientSock);

    eventSourceCreateWithBev(
        &stEventEngine,
        iClientSock,
        SRC_TYPE_UDS,
        SRC_ROLE_WORKER,
        readCallback,
        eventCallback
    );

    UART_CTX stUartCtx = {0};
    stUartCtx.pchDevPath = pchUartPath;
    stUartCtx.iBaudrate = 115200;
    stUartCtx.iFd = -1;
    stUartCtx.iBackoffMsec = 200;

    /* 초기 장치열기 */
    fprintf(stderr,"### %s():%d %s###\n",__func__,__LINE__, stUartCtx.pchDevPath);
    uartOpen(&stUartCtx);
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    eventSourceCreateWithFd(
        &stEventEngine,
        stUartCtx.iFd,
        SRC_TYPE_UART,
        SRC_ROLE_WORKER,
        uartReadCallback,
        uartEventCallback
    );
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);

    /* ------------------- */
    /* 이벤트 루프 실행    */
    /* ------------------- */
    event_base_dispatch(stEventEngine.pstEventBase);

    /* clean-up */
    event_base_free(stEventEngine.pstEventBase);

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