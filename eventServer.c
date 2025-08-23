/*
 * Framed ECHO server with deferred periodic IBIT (Libevent)
 * - 프레임: FRAME_HEADER + payload + FRAME_TAIL
 * - ECHO: 요청 payload 그대로 응답
 * - KEEP_ALIVE: REQ_KEEP_ALIVE -> RES_KEEP_ALIVE
 * - IBIT 요청: REQ_IBIT -> RES_IBIT
 * - 주기 IBIT 푸시: 다른 명령 처리 중이 아니면 즉시, 바쁘면 처리 직후 즉시
 *
 * Build: gcc -Wall -O2 -o eventServer eventServer.c -levent
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <event2/event.h>
#include <event2/listener.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/util.h>

#include "protocol.h"  /* 공통 프로토콜 정의만 포함 */

#define DEFAULT_PORT 9995
#define READ_HIGH_WM (4u * 1024u * 1024u)   /* 4MB */
#define MAX_PAYLOAD  (8u * 1024u * 1024u)   /* 8MB */

/* === Per-connection context === */
typedef struct ConnCtx {
    struct bufferevent *bev;
    struct event       *bit_timer;     /* 주기 BIT 타이머 (EV_PERSIST) */
    char                peer_ip[INET6_ADDRSTRLEN];
    uint16_t            peer_port;

    /* BIT 스케줄 제어 */
    int                 busy;          /* 명령 처리 중이면 1 */
    int                 bit_pending;   /* 바쁠 때 BIT 요청이 들어오면 1로 보류 */
} ConnCtx;

/* Forward decls */
static void listener_cb(struct evconnlistener*, evutil_socket_t, struct sockaddr*, int, void*);
static void conn_readcb(struct bufferevent*, void*);
static void conn_writecb(struct bufferevent*, void*);
static void conn_eventcb(struct bufferevent*, short, void*);
static void signal_cb(evutil_socket_t, short, void*);
static void bit_timer_cb(evutil_socket_t, short, void*);
static void run_deferred_bit_now(ConnCtx* ctx);
static void close_and_free(ConnCtx* ctx);

/* Helpers */
static uint8_t crc8_xor(const uint8_t* p, size_t n) {
    uint8_t c=0; for(size_t i=0;i<n;i++) c ^= p[i]; return c;
}
static void send_frame(struct bufferevent* bev, int16_t nCmd,
                       const MSG_ID* ids, char submodule,
                       const void* payload, int32_t payload_len)
{
    FRAME_HEADER h;
    FRAME_TAIL   t;

    h.unStx       = htons(STX_CONST);
    h.iLength     = htonl(payload_len);
    h.stMsgId     = *ids;            /* 1-byte fields */
    h.chSubModule = submodule;
    h.nCmd        = htons(nCmd);

    t.chCrc       = proto_crc8_xor((const uint8_t*)payload, (size_t)payload_len);
    t.unEtx       = htons(ETX_CONST);

    bufferevent_write(bev, &h, sizeof(h));
    if (payload_len > 0 && payload) bufferevent_write(bev, payload, payload_len);
    bufferevent_write(bev, &t, sizeof(t));
}

/* Handlers */
static void handle_echo(ConnCtx* ctx, const MSG_ID* ids, const uint8_t* p, int32_t n) {
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    send_frame(ctx->bev, CMD_ECHO, ids, 0, p, n);
}
static void handle_keepalive(ConnCtx* ctx, const MSG_ID* ids, const REQ_KEEP_ALIVE* req) {
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    (void)req;
    RES_KEEP_ALIVE res = { .chResult = 1 };
    send_frame(ctx->bev, CMD_KEEP_ALIVE, ids, 0, &res, (int32_t)sizeof(res));
}
static void handle_ibit(ConnCtx* ctx, const MSG_ID* ids, const REQ_IBIT* req) {
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    (void)req; /* 여기선 받은 값과 무관하게 정상 응답 */
    RES_IBIT res = { .chBitTotResult = 1, .chPositionResult = 0 };
    send_frame(ctx->bev, CMD_IBIT, ids, 0, &res, (int32_t)sizeof(res));
}

/* Try to consume exactly one frame; return 1 consumed, 0 need more, -1 fatal */
static int try_consume_one_frame(struct evbuffer* in, ConnCtx* ctx) {
    if (evbuffer_get_length(in) < sizeof(FRAME_HEADER)) return 0;

    FRAME_HEADER h;
    if (evbuffer_copyout(in, &h, sizeof(h)) != (ssize_t)sizeof(h)) return 0;

    uint16_t stx  = ntohs(h.unStx);
    int32_t  plen = ntohl(h.iLength);
    int16_t  cmd  = ntohs(h.nCmd);
    MSG_ID   ids  = h.stMsgId;
    /* char sub = h.chSubModule; // 필요시 사용 */

    if (stx != STX_CONST) {
        fprintf(stderr,"[%s:%u] Bad STX\n", ctx->peer_ip, ctx->peer_port);
        return -1;
    }
    if (plen < 0 || plen > (int32_t)MAX_PAYLOAD) {
        fprintf(stderr,"[%s:%u] Bad length=%d\n", ctx->peer_ip, ctx->peer_port, plen);
        return -1;
    }

    size_t need = sizeof(FRAME_HEADER) + (size_t)plen + sizeof(FRAME_TAIL);
    if (evbuffer_get_length(in) < need) return 0;

    /* pop header */
    evbuffer_drain(in, sizeof(FRAME_HEADER));

    uint8_t* payload = NULL;
    if (plen > 0) {
        payload = (uint8_t*)malloc((size_t)plen);
        if (!payload) return -1;
        if (evbuffer_remove(in, payload, (size_t)plen) != (ssize_t)plen) {
            free(payload); return -1;
        }
    }

    FRAME_TAIL t;
    if (evbuffer_remove(in, &t, sizeof(t)) != (ssize_t)sizeof(t)) { free(payload); return -1; }
    if (ntohs(t.unEtx) != ETX_CONST) { free(payload); return -1; }

    uint8_t exp = proto_crc8_xor(payload, (size_t)plen);
    if (exp != (uint8_t)t.chCrc) { free(payload); return -1; }

    /* === 명령 처리 중 표시 === */
    ctx->busy = 1;

    /* Dispatch */
    switch (cmd) {
        case CMD_ECHO:
            handle_echo(ctx, &ids, payload, plen);
            break;
        case CMD_KEEP_ALIVE: {
            if (plen == (int32_t)sizeof(REQ_KEEP_ALIVE)) {
                REQ_KEEP_ALIVE req; memcpy(&req, payload, sizeof(req));
                handle_keepalive(ctx, &ids, &req);
            } else {
                /* 잘못된 길이: 빈 응답 */
                send_frame(ctx->bev, CMD_KEEP_ALIVE, &ids, 0, NULL, 0);
            }
            break;
        }
        case CMD_IBIT: {
            if (plen == (int32_t)sizeof(REQ_IBIT)) {
                REQ_IBIT req; memcpy(&req, payload, sizeof(req));
                handle_ibit(ctx, &ids, &req);
            } else {
                send_frame(ctx->bev, CMD_IBIT, &ids, 0, NULL, 0);
            }
            break;
        }
        default:
            /* 알 수 없는 명령: 빈 payload 응답 */
            send_frame(ctx->bev, cmd, &ids, 0, NULL, 0);
            break;
    }

    /* === 명령 처리 종료 === */
    ctx->busy = 0;

    free(payload);

    /* 명령 처리 직후, 보류된 BIT가 있으면 즉시 실행 */
    if (ctx->bit_pending) {
        ctx->bit_pending = 0;
        run_deferred_bit_now(ctx);
    }

    return 1;
}

/* === Read: 완전한 프레임 단위로 파싱/디스패치 === */
static void conn_readcb(struct bufferevent *bev, void *user_data)
{
    ConnCtx* ctx = (ConnCtx*)user_data;
    struct evbuffer *in = bufferevent_get_input(bev);

    for (;;) {
        int r = try_consume_one_frame(in, ctx);
        if (r == 0) break;
        if (r < 0) { close_and_free(ctx); return; }
    }
}

/* === Write: 현재 별도 동작 없음 === */
static void conn_writecb(struct bufferevent *bev, void *user_data)
{
    (void)bev; (void)user_data;
}

/* === Event: EOF/ERROR 처리 === */
static void conn_eventcb(struct bufferevent *bev, short events, void *user_data)
{
    ConnCtx* ctx = (ConnCtx*)user_data;
    (void)bev;
    if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
        close_and_free(ctx);
    }
}

/* === SIGINT: 즉시 루프 종료 === */
static void signal_cb(evutil_socket_t sig, short events, void *user_data)
{
    (void)sig; (void)events;
    struct event_base *base = (struct event_base*)user_data;
    event_base_loopexit(base, NULL);
}

/* === BIT 타이머 콜백 ===
 * - 바쁘면 보류만, 아니면 즉시 IBIT 푸시 전송
 */
static void bit_timer_cb(evutil_socket_t fd, short events, void *user_data)
{
    (void)fd; (void)events;
    ConnCtx* ctx = (ConnCtx*)user_data;

    if (ctx->busy) {
        ctx->bit_pending = 1;
        return;
    }
    /* 한가하면: 주기 IBIT 푸시 */
    run_deferred_bit_now(ctx);
}

/* === 보류된 BIT를 즉시 실행 === */
static void run_deferred_bit_now(ConnCtx* ctx)
{
    if (ctx->busy) { ctx->bit_pending = 1; return; }

    /* 서버-initiated 푸시: ids는 임의(여기서는 1/1) */
    MSG_ID ids = { .chSrcId = 1, .chDstId = 1 };
    RES_IBIT res = { .chBitTotResult = 1, .chPositionResult = 0 };
    send_frame(ctx->bev, CMD_IBIT, &ids, 0, &res, (int32_t)sizeof(res));
}

/* === Listener: accept → ConnCtx 생성/설정 === */
static void listener_cb(struct evconnlistener *listener, evutil_socket_t fd,
                        struct sockaddr *sa, int socklen, void *user_data)
{
    struct event_base *base = (struct event_base*)user_data;
    (void)listener; (void)socklen;

    struct bufferevent *bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
    if (!bev) { event_base_loopbreak(base); return; }

    ConnCtx* ctx = (ConnCtx*)calloc(1, sizeof(ConnCtx));
    if (!ctx) { bufferevent_free(bev); return; }
    ctx->bev = bev;
    ctx->busy = 0;
    ctx->bit_pending = 0;

    /* 피어 IP/포트 기록 */
    void *addr_ptr = NULL; uint16_t port = 0;
    if (sa->sa_family == AF_INET) {
        struct sockaddr_in *sin = (struct sockaddr_in*)sa;
        addr_ptr = &(sin->sin_addr); port = ntohs(sin->sin_port);
    } else if (sa->sa_family == AF_INET6) {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6*)sa;
        addr_ptr = &(sin6->sin6_addr); port = ntohs(sin6->sin6_port);
    }
    if (addr_ptr) inet_ntop(sa->sa_family, addr_ptr, ctx->peer_ip, sizeof(ctx->peer_ip));
    ctx->peer_port = port;

    /* 콜백/옵션 */
    bufferevent_setcb(bev, conn_readcb, conn_writecb, conn_eventcb, ctx);
    bufferevent_enable(bev, EV_READ|EV_WRITE);
    bufferevent_setwatermark(bev, EV_READ, 0, READ_HIGH_WM);

    /* 주기 IBIT 타이머 (예: 60초) */
    struct timeval interval = {60, 0};
    ctx->bit_timer = event_new(base, -1, EV_PERSIST, bit_timer_cb, ctx);
    if (ctx->bit_timer) event_add(ctx->bit_timer, &interval);

    printf("Accepted %s:%u\n", ctx->peer_ip, ctx->peer_port);
}

/* === Helper: 연결 자원 일괄 정리 === */
static void close_and_free(ConnCtx* ctx)
{
    if (!ctx) return;
    if (ctx->bit_timer) { event_free(ctx->bit_timer); ctx->bit_timer=NULL; }
    if (ctx->bev)       { bufferevent_free(ctx->bev); ctx->bev=NULL; }
    free(ctx);
}

int main(int argc, char** argv)
{
    uint16_t port = (argc > 1) ? (uint16_t)atoi(argv[1]) : DEFAULT_PORT;
    if (port == 0 || port > 65535) { fprintf(stderr,"Bad port\n"); return 1; }

    signal(SIGPIPE, SIG_IGN);

    struct event_base *base = event_base_new();
    if (!base) { fprintf(stderr, "Could not initialize libevent!\n"); return 1; }

    struct sockaddr_in sin; memset(&sin,0,sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port   = htons(port);
    sin.sin_addr.s_addr = htonl(INADDR_ANY);

    struct evconnlistener *listener =
        evconnlistener_new_bind(base, listener_cb, base,
            LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_FREE, -1,
            (struct sockaddr*)&sin, sizeof(sin));

    if (!listener) { fprintf(stderr, "Could not create a listener!\n"); event_base_free(base); return 1; }

    struct event *sigint = evsignal_new(base, SIGINT, signal_cb, base);
    if (!sigint || event_add(sigint, NULL)<0) {
        fprintf(stderr, "Could not create/add SIGINT event!\n");
        evconnlistener_free(listener); event_base_free(base); return 1;
    }

    printf("Framed ECHO+IBIT server listening on 0.0.0.0:%u\n", port);
    event_base_dispatch(base);

    evconnlistener_free(listener);
    event_free(sigint);
    event_base_free(base);
    printf("done\n");
    return 0;
}
