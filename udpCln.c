/**
 * @file udpCln.c
 * @brief Libevent 기반 UDP Client Application (bufferevent 사용 버전)
 *
 * UDP 기반으로 서버와 비동기 통신을 수행하며 STDIN 입력을 통해
 * 요청(Request Frame)을 생성하고 서버 응답(Response Frame)을 처리한다.
 *
 * ### 변경 사항
 * - 기존: event_new() + read()/write()
 * - 변경: bufferevent_socket_new() + evbuffer 기반 Read/Write
 * - EVENT_CONTEXT + SOCK_CONTEXT 구조 재사용 (TCP Client와 동일 스타일)
 *
 * ### 동작 순서
 * 1. stdinReadCb(): 사용자 입력 → 요청 프레임 생성 → bufferevent_write()
 * 2. appReadCb(): 서버 응답 수신 → responseFrame()
 * 3. event_base_dispatch(): 이벤트 루프 기반 동작 유지
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include "netUdp.h"
#include "netCore.h"
#include "eventSession.h"
#include "frame.h"
#include "icdCommand.h"

#define SERVER_IP           "127.0.0.1"
#define UDP_SERVER_PORT     5001
#define UDP_CLIENT_PORT     5002

/* ========================================================================== */
/* Static Function Prototypes                                                 */
/* ========================================================================== */

static void appReadCb(struct bufferevent* pstBufferEvent, void* pvData);
static void appEventCb(struct bufferevent* pstBufferEvent,
                       short nEvents, void* pvData);
static void stdinReadCb(evutil_socket_t fd, short nEvents, void* pvData);



/* ========================================================================== */
/* Application Read Callback (Server → Client 데이터 수신)                    */
/* ========================================================================== */
static void appReadCb(struct bufferevent* pstBufferEvent, void* pvData)
{
    SOCK_CONTEXT* pstSockCtx = (SOCK_CONTEXT*)pvData;
    if (!pstSockCtx)
        return;

    struct evbuffer* pstEvBuffer = bufferevent_get_input(pstBufferEvent);
    size_t tDataLen = evbuffer_get_length(pstEvBuffer);

    if (tDataLen == 0)
        return;

    unsigned char* puchRecvData = (unsigned char*)malloc(tDataLen);
    if (!puchRecvData)
        return;

    evbuffer_copyout(pstEvBuffer, puchRecvData, tDataLen);

    fprintf(stderr, "[UDP Client] Received %zu bytes\n", tDataLen);

    MSG_ID stMsgId;
    stMsgId.uchSrcId = pstSockCtx->uchSrcId;
    stMsgId.uchDstId = pstSockCtx->uchDstId;

    responseFrame(puchRecvData, &stMsgId, tDataLen);

    evbuffer_drain(pstEvBuffer, tDataLen);
    free(puchRecvData);
}



/* ========================================================================== */
/* Application Event Callback                                                 */
/* ========================================================================== */
static void appEventCb(struct bufferevent* pstBufferEvent,
                       short nEvents, void* pvData)
{
    (void)pstBufferEvent;

    SOCK_CONTEXT*  pstSockCtx  = (SOCK_CONTEXT*)pvData;
    EVENT_CONTEXT* pstEventCtx = pstSockCtx ? pstSockCtx->pstEventCtx : NULL;

    if (nEvents & BEV_EVENT_CONNECTED) {
        fprintf(stderr, "[UDP Client] Connected (UDP connect())\n");
    }

    if (nEvents & BEV_EVENT_EOF) {
        fprintf(stderr, "[UDP Client] EOF from server.\n");
        if (pstEventCtx && pstEventCtx->pstEventBase)
            event_base_loopexit(pstEventCtx->pstEventBase, NULL);
    }

    if (nEvents & BEV_EVENT_ERROR) {
        fprintf(stderr, "[UDP Client] Error: %s\n",
                evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
        if (pstEventCtx && pstEventCtx->pstEventBase)
            event_base_loopexit(pstEventCtx->pstEventBase, NULL);
    }
}



/* ========================================================================== */
/* STDIN Callback Implementation                                              */
/* ========================================================================== */
static void stdinReadCb(evutil_socket_t fd, short nEvents, void* pvData)
{
    (void)fd;
    (void)nEvents;

    SOCK_CONTEXT*  pstSockCtx   = (SOCK_CONTEXT*)pvData;
    EVENT_CONTEXT* pstEventCtx  = pstSockCtx ? pstSockCtx->pstEventCtx : NULL;

    char          achStdInBuf[1024];
    unsigned char auchSendBuf[1024];
    int           iSendSize = 0;
    FRAME_ERR     eErr;

    if (!fgets(achStdInBuf, sizeof(achStdInBuf), stdin)) {
        if (pstEventCtx && pstEventCtx->pstEventBase)
            event_base_loopexit(pstEventCtx->pstEventBase, NULL);
        return;
    }

    achStdInBuf[strcspn(achStdInBuf, "\n")] = '\0';

    MSG_ID stMsgId = {
        .uchSrcId = pstSockCtx->uchSrcId,
        .uchDstId = pstSockCtx->uchDstId
    };

    if (strcmp(achStdInBuf, "keepalive") == 0) {
        fprintf(stderr, "[UDP Client] SEND: KEEP_ALIVE\n");
        eErr = makeReqFrame(CMD_KEEP_ALIVE, &stMsgId,
                            auchSendBuf, &iSendSize);

    } else if (strcmp(achStdInBuf, "ibit") == 0) {
        fprintf(stderr, "[UDP Client] SEND: IBIT\n");
        eErr = makeReqFrame(CMD_IBIT, &stMsgId,
                            auchSendBuf, &iSendSize);

    } else if (!strcmp(achStdInBuf, "quit") ||
               !strcmp(achStdInBuf, "exit")) {
        fprintf(stderr, "[UDP Client] Terminating...\n");
        if (pstEventCtx && pstEventCtx->pstEventBase)
            event_base_loopexit(pstEventCtx->pstEventBase, NULL);
        return;

    } else {
        fprintf(stderr,
                "[Usage]\n"
                "  keepalive\n"
                "  ibit\n"
                "  quit / exit\n");
        return;
    }

    if (eErr == FRAME_OK && iSendSize > 0) {
        if (bufferevent_write(pstSockCtx->pstBufferEvent,
                              auchSendBuf, (size_t)iSendSize) < 0) {
            fprintf(stderr,
                    "[UDP Client] bufferevent_write() failed\n");
        }
    } else if (eErr != FRAME_OK) {
        fprintf(stderr, "[FRAME ERROR] %s\n", frameErrToStr(eErr));
    }
}



/* ========================================================================== */
/* Entry Point                                                                */
/* ========================================================================== */
int run(void)
{
    EVENT_CONTEXT stEventCtx;
    /* UDP Client: MyId = 2 (예: 서버=1, 클라=2) */
    initEventContext(&stEventCtx, ROLE_UDP_CLIENT, 2);

    SOCK_CONTEXT* pstSockCtx = (SOCK_CONTEXT*)calloc(1, sizeof(SOCK_CONTEXT));
    if (!pstSockCtx) {
        perror("calloc");
        return EXIT_FAILURE;
    }

    initSocketContext(pstSockCtx, &stEventCtx, RESPONSE_ENABLED);

    /* === 1) UDP 클라이언트 소켓 생성 (connect()까지 수행) === */
    stEventCtx.iSockFd = netUdpCreateClient(SERVER_IP,
                                            UDP_SERVER_PORT,
                                            UDP_CLIENT_PORT);
    if (stEventCtx.iSockFd < 0) {
        fprintf(stderr, "[UDP Client] UDP socket create failed\n");
        free(pstSockCtx);
        return EXIT_FAILURE;
    }

    /* === 2) event_base 생성 === */
    stEventCtx.pstEventBase = event_base_new();
    if (!stEventCtx.pstEventBase) {
        fprintf(stderr, "[UDP Client] event_base_new() failed\n");
        netClose(stEventCtx.iSockFd);
        free(pstSockCtx);
        return EXIT_FAILURE;
    }

    /* === 3) UDP FD를 bufferevent로 래핑 === */
    pstSockCtx->pstBufferEvent =
        bufferevent_socket_new(stEventCtx.pstEventBase,
                               stEventCtx.iSockFd,
                               BEV_OPT_CLOSE_ON_FREE);
    if (!pstSockCtx->pstBufferEvent) {
        fprintf(stderr, "[UDP Client] bufferevent_socket_new() failed\n");
        event_base_free(stEventCtx.pstEventBase);
        netClose(stEventCtx.iSockFd);
        free(pstSockCtx);
        return EXIT_FAILURE;
    }

    bufferevent_setcb(pstSockCtx->pstBufferEvent,
                      appReadCb,
                      NULL,
                      appEventCb,
                      pstSockCtx);

    bufferevent_enable(pstSockCtx->pstBufferEvent,
                       EV_READ | EV_WRITE);

    /* === 4) STDIN 이벤트 등록 === */
    stEventCtx.pstEvent = event_new(stEventCtx.pstEventBase,
                                    fileno(stdin),
                                    EV_READ | EV_PERSIST,
                                    stdinReadCb,
                                    pstSockCtx);
    if (stEventCtx.pstEvent)
        event_add(stEventCtx.pstEvent, NULL);

    fprintf(stderr, "[UDP Client] Running %s:%d (local port %d)\n",
            SERVER_IP, UDP_SERVER_PORT, UDP_CLIENT_PORT);

    /* === 5) 이벤트 루프 === */
    event_base_dispatch(stEventCtx.pstEventBase);

    /* === 6) 자원 정리 === */
    closeAndFree(pstSockCtx);

    if (stEventCtx.pstEvent)
        event_free(stEventCtx.pstEvent);
    if (stEventCtx.pstEventBase)
        event_base_free(stEventCtx.pstEventBase);

    return EXIT_SUCCESS;
}

/* === main === */
#ifndef GOOGLE_TEST
int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    return run();
}
#endif
