// gcc -O2 -Wall -Wextra -o uart_event_reopen uart_event_reopen.c -levent
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <termios.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

typedef struct {
    const char*       pchDevPath;        // /dev/tty*, /dev/pts/*
    int               iFd;               // 현재 열린 FD (-1이면 닫힘)
    struct event_base* pstEventBase;
    struct event*     pstEventSigint;    // SIGINT handler
    struct event*     pstEventReopen;    // 재연결 타이머
    struct bufferevent* pstBev;          // <-- bufferevent로 전환
    int               iBackoffMsec;      // 재연결 백오프 (초기 200ms, 최대 2000ms)
} UART_CTX;

static void schedule_reopen(UART_CTX* pstUartCtx);

/* --------- UART 설정 유틸 --------- */
static int make_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) 
        return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int set_serial_115200_8N1_raw(int iFd) {
    struct termios stTermios;
    if (tcgetattr(iFd, &stTermios) < 0) {
        perror("tcgetattr");
        return -1;
    }

    cfmakeraw(&stTermios);
    cfsetispeed(&stTermios, B115200);
    cfsetospeed(&stTermios, B115200);

    // 8N1
    stTermios.c_cflag &= ~PARENB;
    stTermios.c_cflag &= ~CSTOPB;
    stTermios.c_cflag &= ~CSIZE;
    stTermios.c_cflag |= CS8;

    // 로컬/수신 enable + HUPCL off(행업 방지)
    stTermios.c_cflag |= (CLOCAL | CREAD);
    stTermios.c_cflag &= ~HUPCL;

    // 0-바이트 read 회피를 원하면 VMIN=1 권장 (여기서는 1로 설정)
    stTermios.c_cc[VMIN]  = 1;
    stTermios.c_cc[VTIME] = 0;

    if (tcsetattr(iFd, TCSANOW, &stTermios) < 0) {
        perror("tcsetattr");
        return -1;
    }

    tcflush(iFd, TCIFLUSH);
    return 0;
}

static int open_tty(UART_CTX* pstUartCtx) {
    int iFd = open(pstUartCtx->pchDevPath, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (iFd < 0)
        return -1;

    if (set_serial_115200_8N1_raw(iFd) < 0) {
        close(iFd);
        return -1;
    }
    if (make_nonblocking(iFd) < 0) {
        close(iFd);
        return -1;
    }
    pstUartCtx->iFd = iFd;
    return 0;
}

/* --------- bufferevent 콜백들 --------- */
static void bev_read_cb(struct bufferevent *bev, void *ctx) {
    UART_CTX* pstUartCtx = (UART_CTX*)ctx;
    struct evbuffer* in = bufferevent_get_input(bev);

    // 1) 줄 단위 처리 (LF 기준)
    for (;;) {
        size_t n_read = 0;
        char* line = evbuffer_readln(in, &n_read, EVBUFFER_EOL_LF);
        if (!line) 
            break;
        printf("Received: %s\n", line);
        fflush(stdout);
        free(line);
    }

    // 2) 남은 데이터(개행 없는 부분)가 있으면 모두 드레인해서 그대로 출력
    size_t pending = evbuffer_get_length(in);
    if (pending > 0) {
        unsigned char* buf = (unsigned char*)malloc(pending);
        if (buf) {
            size_t removed = evbuffer_remove(in, buf, pending);
            printf("Received (partial, no LF, %zu bytes): ", removed);
            fwrite(buf, 1, removed, stdout);
            putchar('\n');
            fflush(stdout);
            free(buf);
        }
    }
}

static void bev_event_cb(struct bufferevent *bev, short events, void *ctx) {
    UART_CTX* pstUartCtx = (UART_CTX*)ctx;

    if (events & BEV_EVENT_ERROR) {
        fprintf(stderr, "[WARN] bufferevent error: %s\n", strerror(errno));
    }
    if (events & BEV_EVENT_EOF) {
        fprintf(stderr, "[INFO] EOF detected. scheduling reopen...\n");
    }
    if (events & (BEV_EVENT_ERROR | BEV_EVENT_EOF)) {
        schedule_reopen(pstUartCtx);
    }
}

static void sigint_cb(evutil_socket_t sig, short ev, void* pvData) {
    (void)sig; (void)ev;
    UART_CTX* pstUartCtx = (UART_CTX *)pvData;
    fprintf(stderr, "\n[INFO] SIGINT caught. exiting...\n");
    event_base_loopexit(pstUartCtx->pstEventBase, NULL);
}

static void attach_new_bev(UART_CTX* pstUartCtx) {
    // 기존 bev 정리
    if (pstUartCtx->pstBev) {
        bufferevent_free(pstUartCtx->pstBev);
        pstUartCtx->pstBev = NULL;
    }
    // 새 FD로 bufferevent 생성 (tty fd여도 리눅스에선 poll/epoll로 동작 가능)
    pstUartCtx->pstBev = bufferevent_socket_new(
        pstUartCtx->pstEventBase,
        pstUartCtx->iFd,
        BEV_OPT_CLOSE_ON_FREE | BEV_OPT_DEFER_CALLBACKS
    );
    if (!pstUartCtx->pstBev) {
        fprintf(stderr, "bufferevent_socket_new failed\n");
        close(pstUartCtx->iFd);
        pstUartCtx->iFd = -1;
        return;
    }
    bufferevent_setcb(pstUartCtx->pstBev, bev_read_cb, NULL, bev_event_cb, pstUartCtx);
    bufferevent_enable(pstUartCtx->pstBev, EV_READ); // 필요시 EV_WRITE도
}

static void reopen_cb(evutil_socket_t fd, short ev, void* pvData) {
    (void)fd; (void)ev;
    UART_CTX* pstUartCtx = (UART_CTX *)pvData;

    if (pstUartCtx->iFd >= 0 && pstUartCtx->pstBev) {
        // 이미 열려있다면 콜백만 보장되도록 enable (여기선 noop일 수 있음)
        bufferevent_enable(pstUartCtx->pstBev, EV_READ);
        pstUartCtx->iBackoffMsec = 200;
        return;
    }

    if (open_tty(pstUartCtx) == 0) {
        fprintf(stderr, "[INFO] Reopened %s. resuming read.\n", pstUartCtx->pchDevPath);
        attach_new_bev(pstUartCtx);
        pstUartCtx->iBackoffMsec = 200; // 성공 → 백오프 리셋
    } else {
        if (pstUartCtx->iBackoffMsec < 2000) 
            pstUartCtx->iBackoffMsec *= 2;
        struct timeval tv = { pstUartCtx->iBackoffMsec / 1000, (pstUartCtx->iBackoffMsec % 1000) * 1000 };
        fprintf(stderr, "[INFO] reopen failed (%s). retry in %d ms\n",
                strerror(errno), pstUartCtx->iBackoffMsec);
        evtimer_add(pstUartCtx->pstEventReopen, &tv);
    }
}

static void schedule_reopen(UART_CTX* pstUartCtx)
{
    // bev 정리(자동으로 FD도 닫힘: BEV_OPT_CLOSE_ON_FREE)
    if (pstUartCtx->pstBev) {
        bufferevent_free(pstUartCtx->pstBev);
        pstUartCtx->pstBev = NULL;
    }
    if (pstUartCtx->iFd >= 0) {
        close(pstUartCtx->iFd);
        pstUartCtx->iFd = -1;
    }

    // 타이머 스케줄
    struct timeval tv = { pstUartCtx->iBackoffMsec / 1000, (pstUartCtx->iBackoffMsec % 1000) * 1000 };
    evtimer_add(pstUartCtx->pstEventReopen, &tv);
}

/* --------- 자원 정리 --------- */
static void cleanup(UART_CTX* pstUartCtx)
{
    if (!pstUartCtx) 
        return;

    if (pstUartCtx->pstBev) {
        bufferevent_free(pstUartCtx->pstBev); // FD도 함께 닫힘
        pstUartCtx->pstBev = NULL;
    }
    if (pstUartCtx->pstEventSigint) {
        event_free(pstUartCtx->pstEventSigint);
        pstUartCtx->pstEventSigint = NULL;
    }
    if (pstUartCtx->pstEventReopen) {
        event_free(pstUartCtx->pstEventReopen);
        pstUartCtx->pstEventReopen = NULL;
    }
    if (pstUartCtx->iFd >= 0) {
        close(pstUartCtx->iFd);
        pstUartCtx->iFd = -1;
    }
    if (pstUartCtx->pstEventBase) {
        event_base_free(pstUartCtx->pstEventBase);
        pstUartCtx->pstEventBase = NULL;
    }
}

/* --------- main --------- */
int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "사용법: %s /dev/pts/X\n", argv[0]);
        return 1;
    }
    UART_CTX stUartCtx;
    memset(&stUartCtx, 0, sizeof(stUartCtx));
    stUartCtx.pchDevPath = argv[1];
    stUartCtx.iFd = -1;
    stUartCtx.iBackoffMsec = 200; // 시작 백오프 200ms

    stUartCtx.pstEventBase = event_base_new();
    if (!stUartCtx.pstEventBase) {
        fprintf(stderr, "event_base_new failed\n");
        return 1;
    }

    // 최초 오픈 시도
    if (open_tty(&stUartCtx) == 0) {
        fprintf(stderr, "[INFO] Listening on %s at 115200 8N1 (raw). Press Ctrl+C to exit.\n", stUartCtx.pchDevPath);
        attach_new_bev(&stUartCtx);
        if (!stUartCtx.pstBev) {
            cleanup(&stUartCtx);
            return 1;
        }
    } else {
        fprintf(stderr, "[WARN] initial open failed (%s). will retry...\n", strerror(errno));
    }

    // 재연결 타이머 이벤트
    stUartCtx.pstEventReopen = evtimer_new(stUartCtx.pstEventBase, reopen_cb, &stUartCtx);
    if (!stUartCtx.pstEventReopen) {
        fprintf(stderr, "failed to create reopen timer\n");
        cleanup(&stUartCtx);
        return 1;
    }
    if (stUartCtx.iFd < 0) {
        struct timeval tv = { stUartCtx.iBackoffMsec / 1000, (stUartCtx.iBackoffMsec % 1000) * 1000 };
        evtimer_add(stUartCtx.pstEventReopen, &tv);
    }

    // SIGINT 핸들러
    stUartCtx.pstEventSigint = evsignal_new(stUartCtx.pstEventBase, SIGINT, sigint_cb, &stUartCtx);
    if (!stUartCtx.pstEventSigint || event_add(stUartCtx.pstEventSigint, NULL) < 0) {
        fprintf(stderr, "failed to set SIGINT handler\n");
        cleanup(&stUartCtx);
        return 1;
    }

    event_base_dispatch(stUartCtx.pstEventBase);
    cleanup(&stUartCtx);
    return 0;
}
