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


/* ========================================================================== */
/* UART + Event Context Structure                                             */
/* ========================================================================== */

/**
 * @struct SUartCtx
 * @brief UART 디바이스 및 libevent 실행 컨텍스트
 */
typedef struct SUartCtx
{
    const char*             pchDevPath;          /**< /dev/ttyUSB? */
    int                     iFd;                 /**< UART File descriptor */

    struct event_base*      pstEventBase;        /**< Event loop */
    struct bufferevent*     pstBev;              /**< UART bufferevent */

    struct event*           pstEventSigInt;      /**< SIGINT event */
    struct event*           pstEventReconnect;   /**< Reopen retry timer */

    int                     iBackoffMsec;        /**< Retry interval (exp backoff) */

    SGpsDataInfo            stGpsInfo;           /**< GPS 파서 상태 */
} SUartCtx;


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


/**
 * @brief UART를 Non-blocking 모드로 전환한다
 *
 * @param iFd  UART file descriptor
 * @return 0 성공 / -1 실패
 */
static int setNonBlocking(int iFd)
{
    int iFlags = fcntl(iFd, F_GETFL, 0);
    if (iFlags < 0) return -1;

    return (fcntl(iFd, F_SETFL, iFlags | O_NONBLOCK) < 0) ? -1 : 0;
}


/**
 * @brief UART를 115200 8N1 RAW 모드로 설정한다
 *
 * @param iFd UART FD
 * @return 0 성공 / -1 실패
 */
static int setUartConfigRaw115200(int iFd)
{
    struct termios stAttr;

    if (tcgetattr(iFd, &stAttr) < 0)
        return -1;

    cfmakeraw(&stAttr);
    cfsetispeed(&stAttr, B115200);
    cfsetospeed(&stAttr, B115200);
    stAttr.c_cc[VMIN]  = 1;
    stAttr.c_cc[VTIME] = 0;

    return (tcsetattr(iFd, TCSANOW, &stAttr) == 0) ? 0 : -1;
}


/**
 * @brief UART 장치를 오픈하고 설정까지 수행한다
 *
 * @param pstCtx 실행 컨텍스트
 * @return 0 성공 / -1 실패
 */
static int openUartDevice(SUartCtx* pstCtx)
{
    int iFd = open(pstCtx->pchDevPath, O_RDWR | O_NOCTTY | O_NONBLOCK);

    if (iFd < 0)
        return -1;

    if (setUartConfigRaw115200(iFd) < 0 ||
        setNonBlocking(iFd) < 0)
    {
        close(iFd);
        return -1;
    }

    pstCtx->iFd = iFd;
    return 0;
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


/* ========================================================================== */
/* Cleanup                                                                    */
/* ========================================================================== */

/**
 * @brief UART Context에 연관된 모든 리소스를 정리한다
 */
static void cleanupContext(SUartCtx* pstCtx)
{
    if (!pstCtx) return;

    if (pstCtx->pstBev)
        bufferevent_free(pstCtx->pstBev);

    if (pstCtx->pstEventSigInt)
        event_free(pstCtx->pstEventSigInt);

    if (pstCtx->pstEventReconnect)
        event_free(pstCtx->pstEventReconnect);

    if (pstCtx->iFd >= 0)
        close(pstCtx->iFd);

    if (pstCtx->pstEventBase)
        event_base_free(pstCtx->pstEventBase);
}


/* ========================================================================== */
/* Main Entry                                                                 */
/* ========================================================================== */

/**
 * @brief 프로그램 실행 진입점
 *
 * 사용 예:
 * @code
 *   ./uartRx /dev/ttyUSB0
 * @endcode
 */
int main(int iArgc, char* ppszArgv[])
{
    if (iArgc < 2)
    {
        fprintf(stderr, "Usage: %s /dev/ttyUSB0\n", ppszArgv[0]);
        return EXIT_FAILURE;
    }

    SUartCtx stCtx = {0};
    stCtx.pchDevPath   = ppszArgv[1];
    stCtx.iBackoffMsec = 200;
    stCtx.iFd          = -1;

    stCtx.pstEventBase = event_base_new();
    if (!stCtx.pstEventBase)
    {
        fprintf(stderr, "ERROR: event_base_new failed.\n");
        return EXIT_FAILURE;
    }

    /* 초기 장치열기 */
    if (openUartDevice(&stCtx) == 0)
    {
        printf("[INFO] UART Connected (%s)\n", stCtx.pchDevPath);

        stCtx.pstBev = bufferevent_socket_new(
            stCtx.pstEventBase,
            stCtx.iFd,
            BEV_OPT_CLOSE_ON_FREE);

        bufferevent_setcb(stCtx.pstBev, uartReadCallback, NULL, uartEventCallback, &stCtx);
        bufferevent_enable(stCtx.pstBev, EV_READ);
    }
    else
    {
        fprintf(stderr, "[WARN] UART open failed, waiting reconnect...\n");
    }

    /* 재연결 타이머 */
    stCtx.pstEventReconnect = evtimer_new(stCtx.pstEventBase, reconnectTimerCallback, &stCtx);

    if (stCtx.iFd < 0)
    {
        struct timeval stDelay = { .tv_sec = 0, .tv_usec = 200 * 1000 };
        evtimer_add(stCtx.pstEventReconnect, &stDelay);
    }

    /* SIGINT 처리 이벤트 등록 */
    stCtx.pstEventSigInt = evsignal_new(stCtx.pstEventBase, SIGINT, sigintCallback, &stCtx);
    event_add(stCtx.pstEventSigInt, NULL);

    /* 이벤트 루프 실행 */
    printf("[RUN] Event loop started.\n");
    event_base_dispatch(stCtx.pstEventBase);

    printf("[EXIT] Cleaning up...\n");
    cleanupContext(&stCtx);

    return EXIT_SUCCESS;
}
