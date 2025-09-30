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
#include <event2/util.h>

typedef struct {
    const char* path;                // /dev/tty*, /dev/pts/*
    int fd;                          // 현재 열린 FD (-1이면 닫힘)
    struct event_base* base;
    struct event* ev_read;           // EV_READ | EV_PERSIST
    struct event* ev_sigint;         // SIGINT handler
    struct event* ev_reopen;         // 재연결 타이머
    struct evbuffer* inbuf;          // 입력 누적 버퍼
    int backoff_ms;                  // 재연결 백오프 (초기 200ms, 최대 2000ms)
} uart_ctx_t;

static void schedule_reopen(uart_ctx_t* ctx);

/* --------- UART 설정 유틸 --------- */
static int make_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int set_serial_115200_8N1_raw(int fd) {
    struct termios tio;
    if (tcgetattr(fd, &tio) < 0) {
        perror("tcgetattr");
        return -1;
    }

    cfmakeraw(&tio);
    cfsetispeed(&tio, B115200);
    cfsetospeed(&tio, B115200);

    // 8N1
    tio.c_cflag &= ~PARENB;
    tio.c_cflag &= ~CSTOPB;
    tio.c_cflag &= ~CSIZE;
    tio.c_cflag |= CS8;

    // 로컬/수신 enable + HUPCL off(행업 방지)
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~HUPCL;

    // 비차단 읽기 동작에 맞게 즉시 반환
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tio) < 0) {
        perror("tcsetattr");
        return -1;
    }

    tcflush(fd, TCIFLUSH);
    return 0;
}

static int open_tty(uart_ctx_t* ctx) {
    int fd = open(ctx->path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return -1;
    if (set_serial_115200_8N1_raw(fd) < 0) { close(fd); return -1; }
    if (make_nonblocking(fd) < 0) { close(fd); return -1; }
    ctx->fd = fd;
    return 0;
}

/* --------- 이벤트 콜백들 --------- */
static void read_cb(evutil_socket_t fd, short ev, void* arg) {
    (void)ev;
    uart_ctx_t* ctx = (uart_ctx_t*)arg;

    char tmp[4096];
    for (;;) {
        ssize_t n = read(fd, tmp, sizeof(tmp));
        if (n > 0) {
            evbuffer_add(ctx->inbuf, tmp, (size_t)n);
        } else if (n == 0) {
            // EOF: 반대쪽이 닫힘. 재연결 스케줄.
            fprintf(stderr, "[WARN] EOF (peer closed). scheduling reopen...\n");
            schedule_reopen(ctx);
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // 더 읽을 데이터 없음
            } else if (errno == EINTR) {
                continue; // 신호로 중단, 재시도
            } else {
                // 기타 오류: EIO/ENXIO 등 포함 → 재연결
                fprintf(stderr, "[WARN] read error: %s. scheduling reopen...\n", strerror(errno));
                schedule_reopen(ctx);
                return;
            }
        }
    }

    // 줄 단위로 출력 (LF 기준). 한 줄 미만은 버퍼에 남겨둠
    while (1) {
        size_t n_read = 0;
        char* line = evbuffer_readln(ctx->inbuf, &n_read, EVBUFFER_EOL_LF);
        if (!line) break;
        printf("Received: %s\n", line);
        fflush(stdout);
        free(line);
    }
}

static void sigint_cb(evutil_socket_t sig, short ev, void* arg) {
    (void)sig; (void)ev;
    uart_ctx_t* ctx = (uart_ctx_t*)arg;
    fprintf(stderr, "\n[INFO] SIGINT caught. exiting...\n");
    event_base_loopexit(ctx->base, NULL);
}

static void reopen_cb(evutil_socket_t fd, short ev, void* arg) {
    (void)fd; (void)ev;
    uart_ctx_t* ctx = (uart_ctx_t*)arg;

    // 이미 열려 있으면 읽기 이벤트만 복구
    if (ctx->fd >= 0) {
        if (ctx->ev_read && event_add(ctx->ev_read, NULL) == 0) {
            ctx->backoff_ms = 200; // 성공 → 백오프 리셋
        }
        return;
    }

    if (open_tty(ctx) == 0) {
        fprintf(stderr, "[INFO] Reopened %s. resuming read.\n", ctx->path);
        // 새 FD로 read 이벤트 재바인딩
        if (ctx->ev_read) event_free(ctx->ev_read);
        ctx->ev_read = event_new(ctx->base, ctx->fd, EV_READ | EV_PERSIST, read_cb, ctx);
        event_add(ctx->ev_read, NULL);
        ctx->backoff_ms = 200; // 성공 → 백오프 리셋
    } else {
        // 실패 → 백오프 증가, 타이머 재설정
        if (ctx->backoff_ms < 2000) ctx->backoff_ms *= 2;
        struct timeval tv = { ctx->backoff_ms / 1000, (ctx->backoff_ms % 1000) * 1000 };
        fprintf(stderr, "[INFO] reopen failed (%s). retry in %d ms\n",
                strerror(errno), ctx->backoff_ms);
        evtimer_add(ctx->ev_reopen, &tv);
    }
}

static void schedule_reopen(uart_ctx_t* ctx) {
    // 읽기 이벤트 끄고 FD 닫기
    if (ctx->ev_read) event_del(ctx->ev_read);
    if (ctx->fd >= 0) { close(ctx->fd); ctx->fd = -1; }

    // 타이머 스케줄
    struct timeval tv = { ctx->backoff_ms / 1000, (ctx->backoff_ms % 1000) * 1000 };
    evtimer_add(ctx->ev_reopen, &tv);
}

/* --------- 자원 정리 --------- */
static void cleanup(uart_ctx_t* ctx) {
    if (!ctx) return;
    if (ctx->ev_read)   { event_free(ctx->ev_read);   ctx->ev_read = NULL; }
    if (ctx->ev_sigint) { event_free(ctx->ev_sigint); ctx->ev_sigint = NULL; }
    if (ctx->ev_reopen) { event_free(ctx->ev_reopen); ctx->ev_reopen = NULL; }
    if (ctx->inbuf)     { evbuffer_free(ctx->inbuf);  ctx->inbuf = NULL; }
    if (ctx->fd >= 0)   { close(ctx->fd);             ctx->fd = -1; }
    if (ctx->base)      { event_base_free(ctx->base); ctx->base = NULL; }
}

/* --------- main --------- */
int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "사용법: %s /dev/pts/X\n", argv[0]);
        return 1;
    }

    uart_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.path = argv[1];
    ctx.fd = -1;
    ctx.backoff_ms = 200; // 시작 백오프 200ms

    ctx.base = event_base_new();
    if (!ctx.base) {
        fprintf(stderr, "event_base_new failed\n");
        return 1;
    }

    ctx.inbuf = evbuffer_new();
    if (!ctx.inbuf) {
        fprintf(stderr, "evbuffer_new failed\n");
        cleanup(&ctx);
        return 1;
    }

    // 최초 오픈 시도
    if (open_tty(&ctx) == 0) {
        fprintf(stderr, "[INFO] Listening on %s at 115200 8N1 (raw). Press Ctrl+C to exit.\n", ctx.path);
        ctx.ev_read = event_new(ctx.base, ctx.fd, EV_READ | EV_PERSIST, read_cb, &ctx);
        if (!ctx.ev_read || event_add(ctx.ev_read, NULL) < 0) {
            fprintf(stderr, "failed to add read event\n");
            cleanup(&ctx);
            return 1;
        }
    } else {
        fprintf(stderr, "[WARN] initial open failed (%s). will retry...\n", strerror(errno));
    }

    // 재연결 타이머 이벤트 (처음 실패했더라도 여기서 주기 재시도)
    ctx.ev_reopen = evtimer_new(ctx.base, reopen_cb, &ctx);
    if (!ctx.ev_reopen) {
        fprintf(stderr, "failed to create reopen timer\n");
        cleanup(&ctx);
        return 1;
    }
    if (ctx.fd < 0) {
        // 첫 시도 실패한 경우 타이머 즉시 가동
        struct timeval tv = { ctx.backoff_ms / 1000, (ctx.backoff_ms % 1000) * 1000 };
        evtimer_add(ctx.ev_reopen, &tv);
    }

    // SIGINT 핸들러
    ctx.ev_sigint = evsignal_new(ctx.base, SIGINT, sigint_cb, &ctx);
    if (!ctx.ev_sigint || event_add(ctx.ev_sigint, NULL) < 0) {
        fprintf(stderr, "failed to set SIGINT handler\n");
        cleanup(&ctx);
        return 1;
    }

    event_base_dispatch(ctx.base);
    cleanup(&ctx);
    return 0;
}
