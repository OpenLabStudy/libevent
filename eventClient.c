/*
 * Framed client for ECHO/KEEP_ALIVE/IBIT (Libevent)
 * - stdin에서 명령 입력:
 *    echo <text>
 *    keepalive
 *    ibit <n>
 *    quit
 *
 * Build: gcc -Wall -O2 -o eventClient eventClient.c -levent
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/util.h>

#include "protocol.h"  /* 공통 헤더 */

#define DEFAULT_PORT 9995
#define MAX_PAYLOAD  (8u * 1024u * 1024u)

/* ===== Helpers ===== */
static void send_frame(struct bufferevent* bev, int16_t nCmd,
                       const MSG_ID* ids, char submodule,
                       const void* payload, int32_t payload_len)
{
    FRAME_HEADER h;
    FRAME_TAIL   t;

    h.unStx       = htons(STX_CONST);
    h.iLength     = htonl(payload_len);
    h.stMsgId     = *ids;
    h.chSubModule = submodule;
    h.nCmd        = htons(nCmd);

    t.chCrc       = proto_crc8_xor((const uint8_t*)payload, (size_t)payload_len);
    t.unEtx       = htons(ETX_CONST);

    bufferevent_write(bev, &h, sizeof(h));
    if (payload_len > 0 && payload) bufferevent_write(bev, payload, payload_len);
    bufferevent_write(bev, &t, sizeof(t));
}

typedef struct {
    struct event_base  *base;
    struct bufferevent *bev;
    struct event       *stdin_ev;
    MSG_ID              ids;
} ClientCtx;

/* try parse one frame; 1 consumed, 0 need more, -1 fatal */
static int try_consume_one_frame(struct evbuffer* in) {
    if (evbuffer_get_length(in) < sizeof(FRAME_HEADER)) return 0;

    FRAME_HEADER h;
    if (evbuffer_copyout(in, &h, sizeof(h)) != (ssize_t)sizeof(h)) return 0;

    uint16_t stx  = ntohs(h.unStx);
    int32_t  plen = ntohl(h.iLength);
    int16_t  cmd  = ntohs(h.nCmd);
    /* MSG_ID ids = h.stMsgId; char sub = h.chSubModule; // 필요시 사용 */

    if (stx != STX_CONST) { fprintf(stderr,"client: bad STX\n"); return -1; }
    if (plen < 0 || plen > (int32_t)MAX_PAYLOAD) { fprintf(stderr,"client: bad len=%d\n", plen); return -1; }

    size_t need = sizeof(FRAME_HEADER) + (size_t)plen + sizeof(FRAME_TAIL);
    if (evbuffer_get_length(in) < need) return 0;

    evbuffer_drain(in, sizeof(FRAME_HEADER));

    uint8_t* payload = NULL;
    if (plen > 0) {
        payload = (uint8_t*)malloc((size_t)plen);
        if (!payload) return -1;
        if (evbuffer_remove(in, payload, (size_t)plen) != (ssize_t)plen) { free(payload); return -1; }
    }

    FRAME_TAIL t;
    if (evbuffer_remove(in, &t, sizeof(t)) != (ssize_t)sizeof(t)) { free(payload); return -1; }
    if (ntohs(t.unEtx) != ETX_CONST) { fprintf(stderr,"client: bad ETX\n"); free(payload); return -1; }

    uint8_t exp = proto_crc8_xor(payload, (size_t)plen);
    if (exp != (uint8_t)t.chCrc) { fprintf(stderr,"client: CRC mismatch\n"); free(payload); return -1; }

    /* Print */
    printf("client: <RESP cmd=%d len=%d>\n", (int)cmd, (int)plen);
    if (cmd == CMD_ECHO) {
        printf("  ECHO text: \"%.*s\"\n", plen, (const char*)payload);
    } else if (cmd == CMD_KEEP_ALIVE) {
        if (plen == (int)sizeof(RES_KEEP_ALIVE)) {
            RES_KEEP_ALIVE r; memcpy(&r, payload, sizeof(r));
            printf("  KEEP_ALIVE result=%d\n", (int)r.chResult);
        } else {
            printf("  KEEP_ALIVE malformed len=%d\n", (int)plen);
        }
    } else if (cmd == CMD_IBIT) {
        if (plen == (int)sizeof(RES_IBIT)) {
            RES_IBIT r; memcpy(&r, payload, sizeof(r));
            printf("  IBIT total=%d position=%d\n", (int)r.chBitTotResult, (int)r.chPositionResult);
        } else {
            printf("  IBIT malformed len=%d\n", (int)plen);
        }
    } else {
        if (plen > 0) printf("  raw %d bytes\n", (int)plen);
    }

    free(payload);
    return 1;
}

/* Callbacks */
static void on_read(struct bufferevent *bev, void *arg) {
    (void)bev; (void)arg;
    struct evbuffer *in = bufferevent_get_input(bev);
    for (;;) {
        int r = try_consume_one_frame(in);
        if (r == 0) break;
        if (r < 0) { break; }
    }
}
static void on_event(struct bufferevent *bev, short events, void *arg) {
    ClientCtx* ctx = (ClientCtx*)arg;
    if (events & BEV_EVENT_CONNECTED) {
        printf("client: connected. Type commands:\n");
        printf("  echo <text>\n");
        printf("  keepalive\n");
        printf("  ibit <n>\n");
        printf("  quit\n");
    }
    if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
        printf("client: connection closed\n");
        event_base_loopexit(ctx->base, NULL);
    }
}
static void on_stdin(evutil_socket_t fd, short what, void *arg) {
    (void)fd; (void)what;
    ClientCtx* ctx = (ClientCtx*)arg;

    char line[1024];
    if (!fgets(line, sizeof(line), stdin)) {
        event_base_loopexit(ctx->base, NULL);
        return;
    }
    line[strcspn(line, "\n")] = '\0';

    if (strcmp(line, "echo") == 0) {
        const char* text = line+5;
        send_frame(ctx->bev, CMD_ECHO, &ctx->ids, 0, text, (int32_t)strlen(text));
        printf("client: sent ECHO(\"%s\")\n", text);
    } else if (strcmp(line, "keepalive") == 0) {
        REQ_KEEP_ALIVE req = { .chTmp = 0 };
        send_frame(ctx->bev, CMD_KEEP_ALIVE, &ctx->ids, 0, &req, (int32_t)sizeof(req));
        printf("client: sent KEEP_ALIVE\n");
    } else if (strcmp(line, "ibit") == 0) {        
        int v = atoi(line+5);
        REQ_IBIT req = { .chIbit = (char)v };
        send_frame(ctx->bev, CMD_IBIT, &ctx->ids, 0, &req, (int32_t)sizeof(req));
        printf("client: sent IBIT(%d)\n", v);
    } else if (!strcmp(line, "quit") || !strcmp(line, "exit")) {
        event_base_loopexit(ctx->base, NULL);
    } else {
        printf("usage:\n  echo <text>\n  keepalive\n  ibit <n>\n  quit\n");
    }
}

int main(int argc, char **argv)
{
    const char *host = (argc > 1) ? argv[1] : "127.0.0.1";
    uint16_t port    = (argc > 2) ? (uint16_t)atoi(argv[2]) : DEFAULT_PORT;
    if (port == 0 || port > 65535) { fprintf(stderr,"Bad port\n"); return 1; }

    signal(SIGPIPE, SIG_IGN);

    ClientCtx *ctx = (ClientCtx*)calloc(1, sizeof(*ctx));
    ctx->base = event_base_new();
    if (!ctx->base) { fprintf(stderr,"event_base_new failed\n"); free(ctx); return 1; }

    ctx->ids.chSrcId = 1;  /* 예시 ID */
    ctx->ids.chDstId = 1;

    /* Connect (IPv4) */
    struct sockaddr_in sin; memset(&sin,0,sizeof(sin));
    sin.sin_family = AF_INET; sin.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &sin.sin_addr) != 1) {
        fprintf(stderr,"Bad host\n");
        event_base_free(ctx->base); free(ctx); return 1;
    }

    ctx->bev = bufferevent_socket_new(ctx->base, -1, BEV_OPT_CLOSE_ON_FREE);
    bufferevent_setcb(ctx->bev, on_read, NULL, on_event, ctx);
    bufferevent_enable(ctx->bev, EV_READ|EV_WRITE);

    if (bufferevent_socket_connect(ctx->bev, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
        fprintf(stderr,"connect failed\n");
        bufferevent_free(ctx->bev); event_base_free(ctx->base); free(ctx); return 1;
    }

    /* stdin event */
    ctx->stdin_ev = event_new(ctx->base, fileno(stdin), EV_READ|EV_PERSIST, on_stdin, ctx);
    event_add(ctx->stdin_ev, NULL);

    printf("client: connecting to %s:%u ...\n", host, port);
    event_base_dispatch(ctx->base);

    if (ctx->stdin_ev) event_free(ctx->stdin_ev);
    if (ctx->bev) bufferevent_free(ctx->bev);
    if (ctx->base) event_base_free(ctx->base);
    free(ctx);
    return 0;
}
