/*
 * Framed responder UDS client (Libevent, Unix Domain Socket)
 * - 서버가 먼저 REQ를 보내면, 클라이언트가 RES를 회신
 * - 서버 코드와 동일한 FRAME_HEADER/TAIL, CRC, byte order 사용
 *
 * Build: gcc -Wall -O2 -o udsClient udsClient.c -levent
 * Run  : ./udsClient /tmp/udsCommand.sock
 *
 * 필요 전제: "protocol.h"에서 아래 심볼/타입이 정의
 *   - STX_CONST, ETX_CONST
 *   - FRAME_HEADER { uint16_t unStx; int32_t iLength; MSG_ID stMsgId; char chSubModule; int16_t nCmd; }
 *   - FRAME_TAIL   { char chCrc; uint16_t unEtx; }
 *   - MSG_ID       { uint8_t chSrcId, chDstId; ... }
 *   - CMD_ECHO, CMD_KEEP_ALIVE, CMD_IBIT
 *   - REQ_KEEP_ALIVE, RES_KEEP_ALIVE, REQ_IBIT, RES_IBIT
 *   - uint8_t proto_crc8_xor(const uint8_t* buf, size_t len)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stddef.h> /* offsetof */

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/util.h>

#include "protocol.h"

/* 서버 코드와 일치시키기 */
#define DEFAULT_UDS_PATH "/tmp/udsCommand.sock"
#define READ_HIGH_WM     (1u * 1024u * 1024u)   /* 1MB */
#define MAX_PAYLOAD      (4u * 1024u * 1024u)   /* 4MB */

typedef struct {
    struct event_base   *base;
    struct bufferevent  *bev;
    struct event        *sigint_ev;
    int                 iMyId;
} APP_CTX;

/* === 전방 선언 === */
static void on_read(struct bufferevent*, void*);
static void on_write(struct bufferevent*, void*);
static void on_event(struct bufferevent*, short, void*);
static void on_sigint(evutil_socket_t, short, void*);

/* === 프레임 송신 === */
static void sendUdsFrame(struct bufferevent* bev, int16_t nCmd,
                         const MSG_ID* mid, char submodule,
                         const void* pl, int32_t len)
{
    FRAME_HEADER h;
    FRAME_TAIL   t;

    h.unStx       = htons(STX_CONST);
    h.iLength     = htonl(len);
    h.stMsgId     = *mid;     /* MSG_ID는 1바이트 필드 가정 */
    h.chSubModule = submodule;
    h.nCmd        = htons(nCmd);

    uint8_t crc = 0;
    if (len > 0 && pl)
        crc = proto_crc8_xor((const uint8_t*)pl, (size_t)len);
    else
        crc = proto_crc8_xor((const uint8_t*)"", 0);

    t.chCrc       = crc;
    t.unEtx       = htons(ETX_CONST);

    bufferevent_write(bev, &h, sizeof(h));
    if (len > 0 && pl)
        bufferevent_write(bev, pl, len);
    bufferevent_write(bev, &t, sizeof(t));
}

/* === 프레임 하나 파싱 및 처리 ===
 * return: 1 consumed, 0 need more, -1 fatal
 */
static int try_consume_one_frame(struct evbuffer* in, APP_CTX* pstAppCtx)
{
    if (evbuffer_get_length(in) < sizeof(FRAME_HEADER))
        return 0;

    FRAME_HEADER fh;
    if (evbuffer_copyout(in, &fh, sizeof(fh)) != (ssize_t)sizeof(fh))
        return 0;

    uint16_t stx  = ntohs(fh.unStx);
    int32_t  plen = ntohl(fh.iLength);
    int16_t  cmd  = ntohs(fh.nCmd);
    MSG_ID   ids  = fh.stMsgId;

    if (stx != STX_CONST || plen < 0 || plen > (int32_t)MAX_PAYLOAD)
        return -1;

    size_t need = sizeof(FRAME_HEADER) + (size_t)plen + sizeof(FRAME_TAIL);
    if (evbuffer_get_length(in) < need)
        return 0;

    /* HEADER 소비 */
    evbuffer_drain(in, sizeof(FRAME_HEADER));

    /* PAYLOAD */
    uint8_t* payload = NULL;
    if (plen > 0) {
        payload = (uint8_t*)malloc((size_t)plen);
        if (!payload) return -1;
        if (evbuffer_remove(in, payload, (size_t)plen) != (ssize_t)plen) {
            free(payload); return -1;
        }
    }

    /* TAIL */
    FRAME_TAIL ft;
    if (evbuffer_remove(in, &ft, sizeof(ft)) != (ssize_t)sizeof(ft)) {
        free(payload); return -1;
    }

    uint8_t calc_crc = (plen > 0) ? proto_crc8_xor(payload, (size_t)plen)
                                  : proto_crc8_xor((const uint8_t*)"", 0);
    if (ntohs(ft.unEtx) != ETX_CONST || calc_crc != (uint8_t)ft.chCrc) {
        fprintf(stderr, "[CLIENT] CRC/ETX mismatch\n");
        free(payload);
        return -1;
    }

    /* === 요청 처리: 서버가 보낸 REQ에 대해 RES 회신 === */
    /* 응답 MSG_ID는 통상 src/dst를 스왑하는 편이 자연스럽다 */
    MSG_ID res_ids = { .chSrcId = ids.chDstId, .chDstId = ids.chSrcId };

    switch (cmd) {
    case CMD_REQ_ID: {
        /* 요청 payload 검사 (옵션) */
        if ((int32_t)sizeof(REQ_ID) == plen) {
            const REQ_ID* req = (const REQ_ID*)payload;
            (void)req; /* 필요 시 사용 */
        }
        RES_ID res = {0};
        /* 예: 0=OK 로 가정 */
        res.chResult = pstAppCtx->iMyId;
        sendUdsFrame(pstAppCtx->bev, CMD_REQ_ID, &res_ids, 0, &res, (int32_t)sizeof(res));
        fprintf(stderr, "[CLIENT] RES ID sent\n");
        break;
    }
    case CMD_KEEP_ALIVE: {
        /* 요청 payload 검사 (옵션) */
        if ((int32_t)sizeof(REQ_KEEP_ALIVE) == plen) {
            const REQ_KEEP_ALIVE* req = (const REQ_KEEP_ALIVE*)payload;
            (void)req; /* 필요 시 사용 */
        }
        RES_KEEP_ALIVE res = {0};
        /* 예: 0=OK 로 가정 */
        res.chResult = 0;
        sendUdsFrame(pstAppCtx->bev, CMD_KEEP_ALIVE, &res_ids, 0, &res, (int32_t)sizeof(res));
        fprintf(stderr, "[CLIENT] RES KEEP_ALIVE sent\n");
        break;
    }
    case CMD_IBIT: {
        if ((int32_t)sizeof(REQ_IBIT) == plen) {
            const REQ_IBIT* req = (const REQ_IBIT*)payload;
            (void)req; /* 필요 시 세부옵션 참고 */
        }
        RES_IBIT res = {0};
        /* 예: 0=OK 가정, 위치 결과도 0(정상) */
        res.chBitTotResult   = 0;
        res.chPositionResult = 0;
        sendUdsFrame(pstAppCtx->bev, CMD_IBIT, &res_ids, 0, &res, (int32_t)sizeof(res));
        fprintf(stderr, "[CLIENT] RES IBIT sent\n");
        break;
    }
    default:
        /* 알 수 없는 요청 → 빈 응답이나 무시 (정책에 따라) */
        fprintf(stderr, "[CLIENT] Unknown REQ cmd=%d len=%d (ignored)\n", cmd, plen);
        break;
    }

    free(payload);
    return 1;
}

/* === Libevent 콜백 === */
static void on_read(struct bufferevent* bev, void* arg)
{
    (void)bev;
    APP_CTX* app = (APP_CTX*)arg;
    struct evbuffer* in = bufferevent_get_input(app->bev);

    for (;;) {
        int r = try_consume_one_frame(in, app);
        if (r == 0) break;
        if (r < 0) {
            fprintf(stderr, "[CLIENT] fatal parse error -> closing\n");
            bufferevent_free(app->bev);
            app->bev = NULL;
            event_base_loopexit(app->base, NULL);
            return;
        }
    }
}

static void on_write(struct bufferevent* bev, void* arg)
{
    (void)bev; (void)arg;
    /* 필요 시 사용 */
}

static void on_event(struct bufferevent* bev, short events, void* arg)
{
    APP_CTX* app = (APP_CTX*)arg;
    if (events & BEV_EVENT_CONNECTED) {
        fprintf(stderr, "[CLIENT] connected\n");
        return;
    }
    if (events & BEV_EVENT_ERROR) {
        int err = EVUTIL_SOCKET_ERROR();
        fprintf(stderr, "[CLIENT] BEV error: %s\n",
                evutil_socket_error_to_string(err));
    }
    if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR | BEV_EVENT_TIMEOUT)) {
        fprintf(stderr, "[CLIENT] disconnected\n");
        if (app->bev) { bufferevent_free(app->bev); app->bev = NULL; }
        event_base_loopexit(app->base, NULL);
    }
}

static void on_sigint(evutil_socket_t sig, short ev, void* arg)
{
    (void)sig; (void)ev;
    APP_CTX* app = (APP_CTX*)arg;
    fprintf(stderr, "[CLIENT] SIGINT -> exit loop\n");
    event_base_loopexit(app->base, NULL);
}

/* === main === */
int main(int argc, char** argv)
{
    APP_CTX app;
    int iUdsPathLength = strlen(DEFAULT_UDS_PATH);
    memset(&app, 0, sizeof(app));
    
    app.iMyId = (argc == 2) ? atoi(argv[1]) : 0;
    fprintf(stderr,"### MY ID is %d[%d,%s] \n", app.iMyId, argc, argv[1]);
    signal(SIGPIPE, SIG_IGN);

    app.base = event_base_new();
    if (!app.base) {
        fprintf(stderr, "Could not init libevent\n");
        return 1;
    }

    /* 소켓 주소 준비 */
    struct sockaddr_un sun;
    memset(&sun, 0, sizeof(sun));
    sun.sun_family = AF_UNIX;
    strcpy(sun.sun_path, DEFAULT_UDS_PATH);
    socklen_t slen = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + iUdsPathLength + 1);

    /* bufferevent + connect */
    app.bev = bufferevent_socket_new(app.base, -1, BEV_OPT_CLOSE_ON_FREE);
    if (!app.bev) {
        fprintf(stderr, "Could not create bufferevent\n");
        event_base_free(app.base);
        return 1;
    }

    bufferevent_setcb(app.bev, on_read, on_write, on_event, &app);
    bufferevent_enable(app.bev, EV_READ | EV_WRITE);
    bufferevent_setwatermark(app.bev, EV_READ, 0, READ_HIGH_WM);

    if (bufferevent_socket_connect(app.bev, (struct sockaddr*)&sun, slen) < 0) {
        fprintf(stderr, "Connect failed: %s\n", strerror(errno));
        bufferevent_free(app.bev);
        event_base_free(app.base);
        return 1;
    }

    /* SIGINT 처리 */
    app.sigint_ev = evsignal_new(app.base, SIGINT, on_sigint, &app);
    if (!app.sigint_ev || event_add(app.sigint_ev, NULL) < 0) {
        fprintf(stderr, "Could not add SIGINT event\n");
        if (app.bev) bufferevent_free(app.bev);
        event_base_free(app.base);
        return 1;
    }

    printf("UDS client connecting to %s\n", DEFAULT_UDS_PATH);
    event_base_dispatch(app.base);

    if (app.sigint_ev) event_free(app.sigint_ev);
    if (app.bev) bufferevent_free(app.bev);
    if (app.base) event_base_free(app.base);

    printf("done\n");
    return 0;
}
