/**
 * @file uart_event_reopen.c
 * @brief libevent 기반 UART 자동 재연결 + Hemisphere R632 GNSS 이진 프레임 파서
 *
 * 본 프로그램은 UART 포트를 비동기(non-blocking)로 읽으며,
 * Hemisphere R632 GNSS 수신기로부터 들어오는 $BIN 이진 데이터를 파싱한다.
 * 장치가 끊기면 자동으로 재연결을 수행하며, CRC 검증 및 GPS 시간 변환까지 포함한다.
 *
 * 빌드:
 * @code
 * gcc -O2 -Wall -Wextra -o uart_event_reopen uart_event_reopen.c -levent -lm
 * @endcode
 */

 #define _GNU_SOURCE

 /* ===================== System / C headers ===================== */
 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <errno.h>
 #include <fcntl.h>
 #include <signal.h>
 #include <unistd.h>
 #include <termios.h>
 #include <math.h>
 #include <time.h>
 #include <stdint.h>
 
 /* ===================== libevent headers ===================== */
 #include <event2/event.h>
 #include <event2/buffer.h>
 #include <event2/bufferevent.h>
 

  #include "r632Gps.h"
 
 

/* ================================================================
 * 3) UART 및 libevent 컨텍스트/유틸
 * ================================================================ */

/**
 * @struct SUartCtx
 * @brief UART 장치 및 이벤트 기반 입출력 컨텍스트
 */
typedef struct
{
    const char*             m_pchDevPath;        /**< 장치 경로 (/dev/ttyUSB0 등) */
    int                     m_iFd;               /**< UART 파일 디스크립터 */
    struct event_base*      m_pstEventBase;      /**< libevent 이벤트 루프 */
    struct event*           m_pstEventSigint;    /**< SIGINT 핸들러 이벤트 */
    struct event*           m_pstEventReopen;    /**< 재연결 타이머 이벤트 */
    struct bufferevent*     m_pstBev;            /**< UART 버퍼 이벤트 */
    int                     m_iBackoffMsec;      /**< 재시도 간격 (백오프, ms) */
    SGpsDataInfo            m_stGpsDataInfo;     /**< GNSS 파서 상태 */
} SUartCtx;

/**
 * @brief 파일 디스크립터를 논블록 모드로 설정
 *
 * @param iFd  대상 FD
 * @return     0: 성공, -1: 실패
 */
static int MakeNonBlocking(int iFd)
{
    int iFlags = fcntl(iFd, F_GETFL, 0);
    if (iFlags < 0) {
        return -1;
    }

    if (fcntl(iFd, F_SETFL, iFlags | O_NONBLOCK) < 0) {
        return -1;
    }

    return 0;
}

/**
 * @brief UART를 115200 8N1 Raw 모드로 설정
 *
 * @param iFd  UART FD
 * @return     0: 성공, -1: 실패
 */
static int SetSerial115200_8N1_Raw(int iFd)
{
    struct termios stTermios;

    if (tcgetattr(iFd, &stTermios) < 0) {
        perror("tcgetattr");
        return -1;
    }

    cfmakeraw(&stTermios);
    cfsetispeed(&stTermios, B115200);
    cfsetospeed(&stTermios, B115200);

    stTermios.c_cflag |= (CLOCAL | CREAD);
    stTermios.c_cflag &= ~HUPCL;
    stTermios.c_cflag &= ~PARENB;
    stTermios.c_cflag &= ~CSTOPB;
    stTermios.c_cflag &= ~CSIZE;
    stTermios.c_cflag |= CS8;

    /* 0-바이트 read 회피: VMIN=1 */
    stTermios.c_cc[VMIN]  = 1;
    stTermios.c_cc[VTIME] = 0;

    if (tcsetattr(iFd, TCSANOW, &stTermios) < 0) {
        perror("tcsetattr");
        return -1;
    }

    tcflush(iFd, TCIFLUSH);
    return 0;
}

/**
 * @brief UART 장치 열기
 *
 * @param p_pstUartCtx  컨텍스트 (경로 입력)
 * @return              0: 성공, -1: 실패
 */
static int OpenTty(SUartCtx* p_pstUartCtx)
{
    int iFd = open(p_pstUartCtx->m_pchDevPath, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (iFd < 0) {
        return -1;
    }

    if (SetSerial115200_8N1_Raw(iFd) < 0 || MakeNonBlocking(iFd) < 0) {
        close(iFd);
        return -1;
    }

    p_pstUartCtx->m_iFd = iFd;
    return 0;
}

/* ================================================================
 * 4) libevent Callbacks (read/event/signal/reopen)
 * ================================================================ */

/**
 * @brief 재연결 스케줄링 (타이머 추가)
 *
 * @param p_pstUartCtx  컨텍스트
 */
static void ScheduleReopen(SUartCtx* p_pstUartCtx)
{
    if (p_pstUartCtx->m_pstBev != NULL) {
        bufferevent_free(p_pstUartCtx->m_pstBev);
        p_pstUartCtx->m_pstBev = NULL;
    }

    if (p_pstUartCtx->m_iFd >= 0) {
        close(p_pstUartCtx->m_iFd);
        p_pstUartCtx->m_iFd = -1;
    }

    struct timeval stTv;
    stTv.tv_sec  = p_pstUartCtx->m_iBackoffMsec / 1000;
    stTv.tv_usec = (p_pstUartCtx->m_iBackoffMsec % 1000) * 1000;

    evtimer_add(p_pstUartCtx->m_pstEventReopen, &stTv);
}

/**
 * @brief 새 FD로 bufferevent를 붙이고 읽기 활성화
 *
 * @param p_pstUartCtx  컨텍스트
 */
static void AttachNewBev(SUartCtx* p_pstUartCtx)
{
    if (p_pstUartCtx->m_pstBev != NULL) {
        bufferevent_free(p_pstUartCtx->m_pstBev);
        p_pstUartCtx->m_pstBev = NULL;
    }

    p_pstUartCtx->m_pstBev = bufferevent_socket_new(
        p_pstUartCtx->m_pstEventBase,
        p_pstUartCtx->m_iFd,
        BEV_OPT_CLOSE_ON_FREE | BEV_OPT_DEFER_CALLBACKS
    );

    if (p_pstUartCtx->m_pstBev == NULL) {
        fprintf(stderr, "bufferevent_socket_new failed\n");
        close(p_pstUartCtx->m_iFd);
        p_pstUartCtx->m_iFd = -1;
        return;
    }

    /* 아래 콜백 선언부는 뒤쪽에 있음 */
    extern void BevReadCb(struct bufferevent*, void*);
    extern void BevEventCb(struct bufferevent*, short, void*);

    bufferevent_setcb(p_pstUartCtx->m_pstBev, BevReadCb, NULL, BevEventCb, p_pstUartCtx);
    bufferevent_enable(p_pstUartCtx->m_pstBev, EV_READ);
}

/**
 * @brief UART 데이터 수신 콜백
 *
 * libevent가 readable 이벤트를 감지하면 호출된다.
 * 내부 evbuffer에서 데이터를 꺼내 파서(R632Feed)에 전달한다.
 *
 * @param p_pstBev  bufferevent
 * @param p_pvCtx   SUartCtx*
 */
void BevReadCb(struct bufferevent* p_pstBev, void* p_pvCtx)
{
    SUartCtx*       p_pstUartCtx = (SUartCtx*)p_pvCtx;
    struct evbuffer* pstIn       = bufferevent_get_input(p_pstBev);
    SGpsDataInfo*    p_pstInfo   = &p_pstUartCtx->m_stGpsDataInfo;

    while (1) {
        size_t stLen = evbuffer_get_length(pstIn);
        if (stLen == 0) {
            break;
        }

        unsigned char* p_pchBuf = evbuffer_pullup(pstIn, stLen);
        if (p_pchBuf == NULL) {
            break;
        }

        if (R632Feed(p_pchBuf, (int)stLen, p_pstInfo)) {
            printf("\nR632 Frame OK:\n");
            printf("  Time : %s\n", p_pstInfo->m_szTime);
            printf("  Lat  : %.8f\n", p_pstInfo->m_stMsg3.m_dLatitude);
            printf("  Lon  : %.8f\n", p_pstInfo->m_stMsg3.m_dLongitude);
            printf("  Hgt  : %.3f\n",  p_pstInfo->m_stMsg3.m_fHeight);
            fflush(stdout);
        }

        evbuffer_drain(pstIn, stLen);
    }
}

/**
 * @brief bufferevent 이벤트 콜백 (에러/EOF 감지 등)
 *
 * @param p_pstBev  bufferevent
 * @param shEvents  이벤트 플래그
 * @param p_pvCtx   SUartCtx*
 */
void BevEventCb(struct bufferevent* p_pstBev, short shEvents, void* p_pvCtx)
{
    (void)p_pstBev;
    SUartCtx* p_pstUartCtx = (SUartCtx*)p_pvCtx;

    if (shEvents & BEV_EVENT_ERROR) {
        fprintf(stderr, "[WARN] bufferevent error: %s\n", strerror(errno));
    }

    if (shEvents & BEV_EVENT_EOF) {
        fprintf(stderr, "[INFO] EOF detected. scheduling reopen...\n");
    }

    if (shEvents & (BEV_EVENT_ERROR | BEV_EVENT_EOF)) {
        ScheduleReopen(p_pstUartCtx);
    }
}

/**
 * @brief SIGINT 신호 콜백
 *
 * Ctrl+C 입력 시 이벤트 루프를 종료한다.
 *
 * @param iSig     시그널 번호
 * @param shEvent  이벤트 플래그
 * @param p_pvCtx  SUartCtx*
 */
static void SigintCb(evutil_socket_t iSig, short shEvent, void* p_pvCtx)
{
    (void)iSig;
    (void)shEvent;

    SUartCtx* p_pstUartCtx = (SUartCtx*)p_pvCtx;
    fprintf(stderr, "\n[INFO] SIGINT caught. exiting...\n");
    event_base_loopexit(p_pstUartCtx->m_pstEventBase, NULL);
}

/**
 * @brief 재연결 타이머 콜백
 *
 * 장치 열기 재시도 → 성공 시 bev 붙이고, 실패 시 백오프 증가 후 재타이머 설정
 *
 * @param iFd      (unused)
 * @param shEvent  (unused)
 * @param p_pvCtx  SUartCtx*
 */
static void ReopenCb(evutil_socket_t iFd, short shEvent, void* p_pvCtx)
{
    (void)iFd;
    (void)shEvent;

    SUartCtx* p_pstUartCtx = (SUartCtx*)p_pvCtx;

    if (p_pstUartCtx->m_iFd >= 0 && p_pstUartCtx->m_pstBev != NULL) {
        /* 이미 열려 있으면 읽기만 보장 */
        bufferevent_enable(p_pstUartCtx->m_pstBev, EV_READ);
        p_pstUartCtx->m_iBackoffMsec = 200;
        return;
    }

    if (OpenTty(p_pstUartCtx) == 0) {
        fprintf(stderr, "[INFO] Reopened %s. resuming read.\n", \
            p_pstUartCtx->m_pchDevPath);
        AttachNewBev(p_pstUartCtx);
        p_pstUartCtx->m_iBackoffMsec = 200;
    } else {
        if (p_pstUartCtx->m_iBackoffMsec < 2000)
        {
            p_pstUartCtx->m_iBackoffMsec *= 2;
        }

        struct timeval stTv;
        stTv.tv_sec  = p_pstUartCtx->m_iBackoffMsec / 1000;
        stTv.tv_usec = (p_pstUartCtx->m_iBackoffMsec % 1000) * 1000;

        fprintf(stderr, "[INFO] reopen failed (%s). retry in %d ms\n",
                strerror(errno), p_pstUartCtx->m_iBackoffMsec);

        evtimer_add(p_pstUartCtx->m_pstEventReopen, &stTv);
    }
}

/* ================================================================
 * 5) 정리(Cleanup) & main()
 * ================================================================ */

/**
 * @brief 리소스 정리
 *
 * @param p_pstUartCtx  컨텍스트
 */
static void Cleanup(SUartCtx* p_pstUartCtx)
{
    if (p_pstUartCtx == NULL) {
        return;
    }

    if (p_pstUartCtx->m_pstBev != NULL) {
        bufferevent_free(p_pstUartCtx->m_pstBev);
        p_pstUartCtx->m_pstBev = NULL;
    }

    if (p_pstUartCtx->m_pstEventSigint != NULL) {
        event_free(p_pstUartCtx->m_pstEventSigint);
        p_pstUartCtx->m_pstEventSigint = NULL;
    }

    if (p_pstUartCtx->m_pstEventReopen != NULL) {
        event_free(p_pstUartCtx->m_pstEventReopen);
        p_pstUartCtx->m_pstEventReopen = NULL;
    }

    if (p_pstUartCtx->m_iFd >= 0) {
        close(p_pstUartCtx->m_iFd);
        p_pstUartCtx->m_iFd = -1;
    }

    if (p_pstUartCtx->m_pstEventBase != NULL) {
        event_base_free(p_pstUartCtx->m_pstEventBase);
        p_pstUartCtx->m_pstEventBase = NULL;
    }
}

/**
 * @brief 프로그램 진입점
 *
 * 사용법:
 * @code
 *   ./uart_event_reopen /dev/ttyUSB0
 *   ./uart_event_reopen /dev/pts/3
 * @endcode
 */
int main(int iArgc, char* pp_szArgv[])
{
    if (iArgc < 2) {
        fprintf(stderr, "Usage: %s /dev/ttyUSB0\n", pp_szArgv[0]);
        return 1;
    }

    SUartCtx stUartCtx;
    memset(&stUartCtx, 0, sizeof(stUartCtx));

    stUartCtx.m_pchDevPath   = pp_szArgv[1];
    stUartCtx.m_iFd          = -1;
    stUartCtx.m_iBackoffMsec = 200;

    stUartCtx.m_pstEventBase = event_base_new();
    if (stUartCtx.m_pstEventBase == NULL) {
        fprintf(stderr, "event_base_new failed\n");
        return 1;
    }

    /* 최초 오픈 시도 */
    if (OpenTty(&stUartCtx) == 0) {
        fprintf(stderr, "[INFO] Listening on %s at 115200 8N1 (raw). Press Ctrl+C to exit.\n",
                stUartCtx.m_pchDevPath);

        AttachNewBev(&stUartCtx);
        if (stUartCtx.m_pstBev == NULL) {
            Cleanup(&stUartCtx);
            return 1;
        }
    } else {
        fprintf(stderr, "[WARN] initial open failed (%s). will retry...\n", strerror(errno));
    }

    /* 재연결 타이머 이벤트 생성 */
    stUartCtx.m_pstEventReopen = evtimer_new(stUartCtx.m_pstEventBase, ReopenCb, &stUartCtx);
    if (stUartCtx.m_pstEventReopen == NULL) {
        fprintf(stderr, "failed to create reopen timer\n");
        Cleanup(&stUartCtx);
        return 1;
    }

    if (stUartCtx.m_iFd < 0) {
        struct timeval stTv;
        stTv.tv_sec  = stUartCtx.m_iBackoffMsec / 1000;
        stTv.tv_usec = (stUartCtx.m_iBackoffMsec % 1000) * 1000;
        evtimer_add(stUartCtx.m_pstEventReopen, &stTv);
    }

    /* SIGINT 핸들러 */
    stUartCtx.m_pstEventSigint = evsignal_new(stUartCtx.m_pstEventBase, SIGINT, SigintCb, &stUartCtx);
    if (stUartCtx.m_pstEventSigint == NULL || \
        event_add(stUartCtx.m_pstEventSigint, NULL) < 0) {
        fprintf(stderr, "failed to set SIGINT handler\n");
        Cleanup(&stUartCtx);
        return 1;
    }

    /* 이벤트 루프 */
    event_base_dispatch(stUartCtx.m_pstEventBase);

    /* 정리 */
    Cleanup(&stUartCtx);
    return 0;
}

 