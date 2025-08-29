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
    struct event_base   *pstEventBase;
    struct bufferevent  *pstBufferEvent;
    struct event        *pstEventSigint;
    int                 iMyId;
} UDS_CLIENT_CTX;

/* === 전방 선언 === */
static void udsReadCb(struct bufferevent*, void*);
static void udsEventCb(struct bufferevent*, short, void*);
static void signalCb(evutil_socket_t, short, void*);

/* === 프레임 송신 === */
static void sendUdsFrame(struct bufferevent* pstBufferEvent, unsigned short unCmd,
                       const MSG_ID* pstMsgId, unsigned char uchSubModule,
                       const void* pvPayload, int iDataLength)
{
    FRAME_HEADER stFrameHeader;
    FRAME_TAIL   stFrameTail;

    stFrameHeader.unStx             = htons(STX_CONST);
    stFrameHeader.iDataLength       = htonl(iDataLength);
    stFrameHeader.stMsgId.uchSrcId  = pstMsgId->uchSrcId;
    stFrameHeader.stMsgId.uchDstId  = pstMsgId->uchDstId;
    stFrameHeader.uchSubModule      = uchSubModule;
    stFrameHeader.unCmd             = htons(unCmd);

    stFrameTail.uchCrc              = proto_crc8_xor((const uint8_t*)pvPayload, (size_t)iDataLength);
    stFrameTail.unEtx               = htons(ETX_CONST);

    bufferevent_write(pstBufferEvent, &stFrameHeader, sizeof(stFrameHeader));
    if (iDataLength > 0 && pvPayload)
        bufferevent_write(pstBufferEvent, pvPayload, iDataLength);
    bufferevent_write(pstBufferEvent, &stFrameTail, sizeof(stFrameTail));
}

/* === 프레임 하나 파싱 및 처리 ===
 * return: 1 consumed, 0 need more, -1 fatal
 */
static int try_consume_one_frame(struct evbuffer* pstEventBuffer, UDS_CLIENT_CTX* pstUdsCtx)
{
    if (evbuffer_get_length(pstEventBuffer) < sizeof(FRAME_HEADER))
        return 0;

    FRAME_HEADER stFrameHeader;
    if (evbuffer_copyout(pstEventBuffer, &stFrameHeader, sizeof(stFrameHeader)) != (ssize_t)sizeof(stFrameHeader))
        return 0;

    unsigned short  unStx   = ntohs(stFrameHeader.unStx);
    int iDataLength = ntohl(stFrameHeader.iDataLength);
    unsigned short  unCmd  = ntohs(stFrameHeader.unCmd);
    MSG_ID stMsgId  = stFrameHeader.stMsgId;

    if (unStx != STX_CONST || iDataLength < 0 || iDataLength > (int32_t)MAX_PAYLOAD)
        return -1;

    int iNeedSize = sizeof(FRAME_HEADER) + (size_t)iDataLength + sizeof(FRAME_TAIL);
    if (evbuffer_get_length(pstEventBuffer) < iNeedSize)
        return 0;

    /* HEADER 소비 */
    evbuffer_drain(pstEventBuffer, sizeof(FRAME_HEADER));

    /* PAYLOAD */
    unsigned char* puchPayload = NULL;
    if (iDataLength > 0) {
        puchPayload = (uint8_t*)malloc((size_t)iDataLength);
        if (!puchPayload) 
            return -1;
        if (evbuffer_remove(pstEventBuffer, puchPayload, (size_t)iDataLength) != (ssize_t)iDataLength) {
            free(puchPayload);
            return -1;
        }
    }

    /* TAIL */
    FRAME_TAIL stFrameTail;
    if (evbuffer_remove(pstEventBuffer, &stFrameTail, sizeof(stFrameTail)) != (ssize_t)sizeof(stFrameTail)) {
        free(puchPayload);
        return -1;
    }

    unsigned char uchCrc = (iDataLength > 0) ? proto_crc8_xor(puchPayload, (size_t)iDataLength) : proto_crc8_xor((const uint8_t*)"", 0);
    if (ntohs(stFrameTail.unEtx) != ETX_CONST || uchCrc != (uint8_t)stFrameTail.uchCrc) {
        fprintf(stderr, "[CLIENT] CRC/ETX mismatch\n");
        free(puchPayload);
        return -1;
    }

    /* === 요청 처리: 서버가 보낸 REQ에 대해 RES 회신 === */
    /* 응답 MSG_ID는 통상 src/dst를 스왑하는 편이 자연스럽다 */
    MSG_ID stResMsgId = { .uchSrcId = pstUdsCtx->iMyId, .uchDstId = stMsgId.uchSrcId };

    switch (unCmd) {
    case CMD_REQ_ID: {
        /* 요청 payload 검사 (옵션) */
        if ((int)sizeof(REQ_ID) == iDataLength) {
            const REQ_ID* pstReqId = (const REQ_ID*)puchPayload;
            (void)pstReqId; /* 필요 시 사용 */
        }
        RES_ID stResId = {0};
        /* 예: 0=OK 로 가정 */
        stResId.chResult = pstUdsCtx->iMyId;
        sendUdsFrame(pstUdsCtx->pstBufferEvent, CMD_REQ_ID, &stResMsgId, 0, &stResId, (int32_t)sizeof(stResId));
        fprintf(stderr, "[CLIENT] RES ID sent\n");
        break;
    }
    case CMD_KEEP_ALIVE: {
        /* 요청 payload 검사 (옵션) */
        if ((int32_t)sizeof(REQ_KEEP_ALIVE) == iDataLength) {
            const REQ_KEEP_ALIVE* pstReqKeepAlive = (const REQ_KEEP_ALIVE*)puchPayload;
            (void)pstReqKeepAlive; /* 필요 시 사용 */
        }
        RES_KEEP_ALIVE stResKeepAlive = {0};
        /* 예: 0=OK 로 가정 */
        stResKeepAlive.chResult = 0;
        sendUdsFrame(pstUdsCtx->pstBufferEvent, CMD_KEEP_ALIVE, &stResMsgId, 0, &stResKeepAlive, (int32_t)sizeof(stResKeepAlive));
        fprintf(stderr, "[CLIENT] RES KEEP_ALIVE sent\n");
        break;
    }
    case CMD_IBIT: {
        if ((int32_t)sizeof(REQ_IBIT) == iDataLength) {
            const REQ_IBIT* pstReqIbit = (const REQ_IBIT*)puchPayload;
            (void)pstReqIbit; /* 필요 시 세부옵션 참고 */
        }
        RES_IBIT stResIbit = {0};
        /* 예: 0=OK 가정, 위치 결과도 0(정상) */
        stResIbit.chBitTotResult   = 0;
        stResIbit.chPositionResult = 0;
        sendUdsFrame(pstUdsCtx->pstBufferEvent, CMD_IBIT, &stResMsgId, 0, &stResIbit, (int32_t)sizeof(stResIbit));
        fprintf(stderr, "[CLIENT] RES IBIT sent\n");
        break;
    }
    default:
        /* 알 수 없는 요청 → 빈 응답이나 무시 (정책에 따라) */
        fprintf(stderr, "[CLIENT] Unknown REQ cmd=%d len=%d (ignored)\n", unCmd, iDataLength);
        break;
    }

    free(puchPayload);
    return 1;
}

/* === Libevent 콜백 === */
static void udsReadCb(struct bufferevent* pstBufferEvent, void* pvData)
{
    (void)pstBufferEvent;
    UDS_CLIENT_CTX* pstUdsCtx = (UDS_CLIENT_CTX*)pvData;
    struct evbuffer* pstEvBuffer = bufferevent_get_input(pstUdsCtx->pstBufferEvent);

    for (;;) {
        int r = try_consume_one_frame(pstEvBuffer, pstUdsCtx);
        if (r == 0) break;
        if (r < 0) {
            fprintf(stderr, "[CLIENT] fatal parse error -> closing\n");
            bufferevent_free(pstUdsCtx->pstBufferEvent);
            pstUdsCtx->pstBufferEvent = NULL;
            event_base_loopexit(pstUdsCtx->pstEventBase, NULL);
            return;
        }
    }
}

static void udsEventCb(struct bufferevent* pstBufferEvent, short nEvents, void* pvData)
{
    UDS_CLIENT_CTX* pstUdsCtx = (UDS_CLIENT_CTX*)pvData;
    if (nEvents & BEV_EVENT_CONNECTED) {
        fprintf(stderr, "[CLIENT] connected\n");
        return;
    }
    if (nEvents & BEV_EVENT_ERROR) {
        int err = EVUTIL_SOCKET_ERROR();
        fprintf(stderr, "[CLIENT] BEV error: %s\n",
                evutil_socket_error_to_string(err));
    }
    if (nEvents & (BEV_EVENT_EOF | BEV_EVENT_ERROR | BEV_EVENT_TIMEOUT)) {
        fprintf(stderr, "[CLIENT] disconnected\n");
        if (pstUdsCtx->pstBufferEvent) { 
            bufferevent_free(pstUdsCtx->pstBufferEvent); 
            pstUdsCtx->pstBufferEvent = NULL; 
        }
        event_base_loopexit(pstUdsCtx->pstEventBase, NULL);
    }
}

static void signalCb(evutil_socket_t sig, short nEvents, void* pvData)
{
    (void)sig;
    (void)nEvents;
    UDS_CLIENT_CTX* pstUdsCtx = (UDS_CLIENT_CTX*)pvData;
    fprintf(stderr, "[CLIENT] SIGINT -> exit loop\n");
    event_base_loopexit(pstUdsCtx->pstEventBase, NULL);
}

/* === main === */
int main(int argc, char** argv)
{
    UDS_CLIENT_CTX stUdsCtx;
    int iUdsPathLength = strlen(DEFAULT_UDS_PATH);
    memset(&stUdsCtx, 0, sizeof(stUdsCtx));
    
    stUdsCtx.iMyId = (argc == 2) ? atoi(argv[1]) : 0;
    fprintf(stderr,"### MY ID is %d[%d,%s] \n", stUdsCtx.iMyId, argc, argv[1]);
    signal(SIGPIPE, SIG_IGN);

    stUdsCtx.pstEventBase = event_base_new();
    if (!stUdsCtx.pstEventBase) {
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
    stUdsCtx.pstBufferEvent = bufferevent_socket_new(stUdsCtx.pstEventBase, -1, BEV_OPT_CLOSE_ON_FREE);
    if (!stUdsCtx.pstBufferEvent) {
        fprintf(stderr, "Could not create bufferevent\n");
        event_base_free(stUdsCtx.pstEventBase);
        return 1;
    }

    bufferevent_setcb(stUdsCtx.pstBufferEvent, udsReadCb, NULL, udsEventCb, &stUdsCtx);
    bufferevent_enable(stUdsCtx.pstBufferEvent, EV_READ | EV_WRITE);
    bufferevent_setwatermark(stUdsCtx.pstBufferEvent, EV_READ, 0, READ_HIGH_WM);

    if (bufferevent_socket_connect(stUdsCtx.pstBufferEvent, (struct sockaddr*)&sun, slen) < 0) {
        fprintf(stderr, "Connect failed: %s\n", strerror(errno));
        bufferevent_free(stUdsCtx.pstBufferEvent);
        event_base_free(stUdsCtx.pstEventBase);
        return 1;
    }

    /* SIGINT 처리 */
    stUdsCtx.pstEventSigint = evsignal_new(stUdsCtx.pstEventBase, SIGINT, signalCb, &stUdsCtx);
    if (!stUdsCtx.pstEventSigint || event_add(stUdsCtx.pstEventSigint, NULL) < 0) {
        fprintf(stderr, "Could not add SIGINT event\n");
        if (stUdsCtx.pstBufferEvent) 
            bufferevent_free(stUdsCtx.pstBufferEvent);
        event_base_free(stUdsCtx.pstEventBase);
        return 1;
    }

    printf("UDS client connecting to %s\n", DEFAULT_UDS_PATH);
    event_base_dispatch(stUdsCtx.pstEventBase);

    if (stUdsCtx.pstEventSigint) 
        event_free(stUdsCtx.pstEventSigint);

    if (stUdsCtx.pstBufferEvent) 
        bufferevent_free(stUdsCtx.pstBufferEvent);

    if (stUdsCtx.pstEventBase) 
        event_base_free(stUdsCtx.pstEventBase);

    printf("done\n");
    return 0;
}
