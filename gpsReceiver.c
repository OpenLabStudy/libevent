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
#include "dispatcher.h"

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

    BASE_CONTEXT*       pstBaseCtx;     /**< 공용 BASE_CONTEXT */
} SUartCtx;


/* ========================================================================== */
/* Static Function Prototypes                                                 */
/* ========================================================================== */
static void appReadCb(struct bufferevent* pstBufferEvent, void* pvData);
static void appEventCb(struct bufferevent* pstBufferEvent,
                       short nEvents, void* pvData);
static void stdinReadCb(evutil_socket_t sig, short nEvents, void* pvData);
static void signalCb(evutil_socket_t sig, short ev, void* pvData);

/* ========================================================================== */
/* Application-level Read Callback (UDS Client)                               */
/* ========================================================================== */
static void appReadCb(struct bufferevent* pstBufferEvent, void* pvData)
{
    unsigned short unCmd = 0;
    unsigned char auCmdResult[1000];
    unsigned char auSendBuf[1024];
    int iSendLen = 0;
    FRAME_ERR eErr;
    MSG_ID stMsgId;
    SOCK_CONTEXT* pstSockCtx = (SOCK_CONTEXT*)pvData;
    if (!pstSockCtx)
        return;

    struct evbuffer* pstEvBuffer = bufferevent_get_input(pstBufferEvent);
    size_t tDataLen = evbuffer_get_length(pstEvBuffer);

    fprintf(stderr, "[UDS-Client] Received %zu bytes\n", tDataLen);

    if (!tDataLen)
        return;

    unsigned char* puchRecvData = malloc(tDataLen);
    if (!puchRecvData)
        return;

    evbuffer_copyout(pstEvBuffer, puchRecvData, tDataLen);
    
    stMsgId.uchSrcId = pstSockCtx->uchSrcId;
    stMsgId.uchDstId = getSrcId(puchRecvData);

    fprintf(stderr, "[UDS Client] %02x %02x\n", pstSockCtx->uchSrcId, pstSockCtx->uchDstId);
    int iFrameSize = getFrameSize(puchRecvData);

    /* === 헤더 및 명령 추출 === */
    eErr = requestFrame(puchRecvData, &stMsgId, iFrameSize, &unCmd);
    if (eErr != FRAME_OK) {
        fprintf(stderr, "[APP] requestFrame ERR: %s\n", frameErrToStr(eErr));
    }

    /* === 명령 처리 === */
    eErr = commandHandler(puchRecvData, &stMsgId, iFrameSize, auCmdResult, &iSendLen);

    /* === 응답 프레임 생성 === */
    eErr = makeResFrame(unCmd, &stMsgId, auCmdResult, auSendBuf);

    fprintf(stderr, "[APP] Send CMD=%04X, size=%d\n", unCmd, iSendLen);
    if (bufferevent_write(pstBufferEvent, auSendBuf, iSendLen) < 0) {
        fprintf(stderr, "[APP] bufferevent_write() failed\n");
    }

    evbuffer_drain(pstEvBuffer, tDataLen);
    free(puchRecvData);    
}

/* ========================================================================== */
/* Application-level Event Callback (UDS Client)                              */
/* ========================================================================== */
static void appEventCb(struct bufferevent* pstBufferEvent,
                       short nEvents, void* pvData)
{
    (void)pstBufferEvent;
    SOCK_CONTEXT* pstSockCtx = (SOCK_CONTEXT*)pvData;
    BASE_CONTEXT* pstBaseCtx = pstSockCtx ? pstSockCtx->pstBaseCtx : NULL;

    if (nEvents & BEV_EVENT_CONNECTED) {
        fprintf(stderr,"[UDS-Client] Connected to server.\n");
    }

    if (nEvents & BEV_EVENT_EOF) {
        fprintf(stderr,"[UDS-Client] Server closed connection.\n");
        shutdownApp(pstBaseCtx);
    }

    if (nEvents & BEV_EVENT_ERROR) {
        fprintf(stderr,"[UDS-Client] Error: %s\n",
            evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
        shutdownApp(pstBaseCtx);
    }
}


/* ========================================================================== */
/* Signal Handling (UDS Client)                                               */
/* ========================================================================== */
static void signalCb(evutil_socket_t sig, short ev, void* pvData)
{
    (void)ev;
    BASE_CONTEXT* pstBaseCtx = (BASE_CONTEXT*)pvData;

    if (sig == SIGINT) {
        fprintf(stderr, "\n[UDS-Client] SIGINT received. Stopping event loop...\n");
        shutdownApp(pstBaseCtx);
    }
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

    // 원하는 Baudrate을 Context에서 가져오도록 변경
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
static void uartReadCallback(struct bufferevent* pstBev, void* pvCtx)
{
    SUartCtx* pstCtx = (SUartCtx*)pvCtx;
    struct evbuffer* pstInput = bufferevent_get_input(pstBev);

    while (1)
    {
        size_t tLen = evbuffer_get_length(pstInput);
        if (tLen == 0)
            break;

        unsigned char* puchBuf = evbuffer_pullup(pstInput, tLen);
        if (!puchBuf)
            break;

        if (R632Feed(puchBuf, (int)tLen, &pstCtx->stGpsInfo))
        {
            printf("\n===== R632 GNSS FRAME RECEIVED =====\n");
            printf("Time  : %s\n", pstCtx->stGpsInfo.m_szTime);
            printf("Lat   : %.8lf\n", pstCtx->stGpsInfo.m_stMsg3.m_dLatitude);
            printf("Lon   : %.8lf\n", pstCtx->stGpsInfo.m_stMsg3.m_dLongitude);
            printf("Alt   : %.3f m\n", pstCtx->stGpsInfo.m_stMsg3.m_fHeight);
            printf("Sat   : %u\n", pstCtx->stGpsInfo.m_stMsg3.m_wNumSatsUsed);
        }

        evbuffer_drain(pstInput, tLen);
    }
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
    (void)pstBev;
    SUartCtx* pstCtx = (SUartCtx*)pvCtx;

    if (shEvents & BEV_EVENT_ERROR)
        fprintf(stderr, "[WARN] UART error detected: %s\n", strerror(errno));

    if (shEvents & BEV_EVENT_EOF)
        fprintf(stderr, "[INFO] UART disconnected.\n");

    if (shEvents & (BEV_EVENT_ERROR | BEV_EVENT_EOF))
    {
        struct timeval stDelay =
        {
            .tv_sec  = pstCtx->iBackoffMsec / 1000,
            .tv_usec = (pstCtx->iBackoffMsec % 1000) * 1000
        };

        if (pstCtx->iBackoffMsec < 2000)
            pstCtx->iBackoffMsec *= 2;

        evtimer_add(pstCtx->pstEventReconnect, &stDelay);
        bufferevent_free(pstCtx->pstBev);
        pstCtx->pstBev = NULL;

        if (pstCtx->iFd >= 0)
        {
            close(pstCtx->iFd);
            pstCtx->iFd = -1;
        }
    }
}

/**
 * @brief 재연결 이벤트 타이머 콜백
 *
 * UART 장치가 끊어졌을 경우 일정 시간 후 자동 재연결을 수행한다.
 *
 * @param iFd      unused
 * @param shEvent  unused
 * @param pvCtx    SUartCtx*
 */
static void reconnectTimerCallback(evutil_socket_t iFd, short shEvent, void* pvCtx)
{
    (void)iFd;
    (void)shEvent;

    SUartCtx* pstCtx = (SUartCtx*)pvCtx;

    printf("[INFO] Attempting UART reopen: %s ...\n", pstCtx->pchDevPath);

    if (openUartDevice(pstCtx) == 0)
    {
        printf("[OK] UART reconnected.\n");
        pstCtx->iBackoffMsec = 200;

        pstCtx->pstBev = bufferevent_socket_new(
            pstCtx->pstEventBase,
            pstCtx->iFd,
            BEV_OPT_CLOSE_ON_FREE);

        bufferevent_setcb(pstCtx->pstBev, uartReadCallback, NULL, uartEventCallback, pstCtx);
        bufferevent_enable(pstCtx->pstBev, EV_READ);
    }
    else
    {
        fprintf(stderr, "[FAIL] Retry in %d ms...\n", pstCtx->iBackoffMsec);

        struct timeval stDelay =
        {
            .tv_sec  = pstCtx->iBackoffMsec / 1000,
            .tv_usec = (pstCtx->iBackoffMsec % 1000) * 1000
        };

        if (pstCtx->iBackoffMsec < 2000)
            pstCtx->iBackoffMsec *= 2;

        evtimer_add(pstCtx->pstEventReconnect, &stDelay);
    }
}



/**
 * @brief SIGINT (Ctrl+C) 처리 함수
 *
 * 이벤트 루프 종료를 호출한다.
 */
static void sigintCallback(evutil_socket_t iSig, short shEvent, void* pvCtx)
{
    (void)iSig;
    (void)shEvent;

    SUartCtx* pstCtx = (SUartCtx*)pvCtx;
    fprintf(stderr, "\n[INFO] SIGINT received. Stopping event loop...\n");
    event_base_loopexit(pstCtx->pstEventBase, NULL);
}





/* ========================================================================== */
/* Main Entry                                                                 */
/* ========================================================================== */

int run(int iId, char* pchUartPath)
{    
    BASE_CONTEXT stBaseCtx;
    DISPATCHER   stDispatcher;
    unsigned char uchMyId = 0x00;
    if(iId == 1)
        uchMyId = UDS_1_CLN1_ID;
    else if(iId == 2)
        uchMyId = UDS_1_CLN2_ID;
    else if(iId == 3)
        uchMyId = UDS_1_CLN3_ID;
    else if(iId == 4)
        uchMyId = UDS_1_CLN4_ID;
    baseContextInit(&stBaseCtx, uchMyId);
    stBaseCtx.pstEventBase = event_base_new();
    if (!stBaseCtx.pstEventBase) {
        fprintf(stderr, "[UDS-Client] event_base_new() failed\n");
        return EXIT_FAILURE;
    }
    dispatcherInit(&stDispatcher, &stBaseCtx);
    stBaseCtx.pvUserCtx = &stDispatcher;

    int iSockFd = netUdsCreateClient(UDS_1_PATH);
    if (iSockFd < 0) {
        fprintf(stderr, "[UDS-Client] Failed to create UDS client socket\n");
        return EXIT_FAILURE;
    }

    printf("[CLI] Connecting to %s\n", UDS_1_PATH);
    /* ------------------- */
    /* EVENT_SOURCE 생성   */
    /* ------------------- */
    netSetNonblock(iSockFd);

    eventSourceCreateWithBev(
        &stDispatcher,
        iSockFd,
        SRC_TYPE_UDS_CLIENT,
        SRC_ROLE_WORKER,
        readCallback,
        eventCallback
    );

    UART_CTX stUartCtx = {0};
    stUartCtx.pchDevPath = pchUartPath;
    stUartCtx.iFd = -1;
    stUartCtx.iBackoffMsec = 200;

    /* 초기 장치열기 */
    uartOpen(&stUartCtx);

    /* SIGINT 처리 이벤트 등록 */
    struct event  *pstEventSigint;
    pstEventSigint = evsignal_new(
        stBaseCtx.pstEventBase,
        SIGINT,
        signalCb,
        &stBaseCtx
    );
    if (pstEventSigint ||
        event_add(pstEventSigint, NULL) < 0) {
        fprintf(stderr, "[UDS-Client] evsignal_new/event_add failed\n");
        event_base_free(stBaseCtx.pstEventBase);
        netClose(iSockFd);
        return EXIT_FAILURE;
    }

    /* ------------------- */
    /* 이벤트 루프 실행    */
    /* ------------------- */
    event_base_dispatch(stBaseCtx.pstEventBase);

    /* clean-up */
    event_free(pstEventSigint);
    baseContextCleanup(&stBaseCtx);
    event_base_free(stBaseCtx.pstEventBase);

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