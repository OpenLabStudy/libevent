/**
 * @file udsCln.c
 * @brief Libevent 기반 UDS(Unix Domain Socket) Client Application
 *
 * Unix Domain Socket 기반으로 서버와 통신하며, 사용자 입력(STDIN)에 따라
 * 요청(Request Frame)을 만들어 서버에 전송하고 응답(Response Frame)을 수신하여
 * 파싱하고 결과를 출력한다.
 *
 * ### 특징
 * - Local IPC 최적화: TCP 대비 낮은 latency
 * - 파일 기반 소켓(`/tmp/uds1.sock`)
 * - 비동기 이벤트 기반 통신(libevent)
 *
 * ### 동작 흐름
 * 1. stdinReadCb() → 사용자 입력 기반 요청 생성
 * 2. appReadCb() → 수신된 프레임 파싱 및 결과 출력
 * 3. appEventCb() → 연결/종료/오류 처리
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include "netUds.h"
#include "netCore.h"
#include "eventSession.h"
#include "frame.h"
#include "icdCommand.h"


/* ========================================================================== */
/* Static Function Prototypes                                                 */
/* ========================================================================== */

/**
 * @brief 서버로부터 수신된 데이터 처리 콜백
 *
 * bufferevent 입력 버퍼의 데이터를 responseFrame()을 이용해 파싱한다.
 *
 * @param pstBufferEvent bufferevent 핸들
 * @param pvData         SOCK_CONTEXT 포인터
 */
static void appReadCb(struct bufferevent* pstBufferEvent, void* pvData);

/**
 * @brief bufferevent 이벤트 처리 콜백
 *
 * 서버 연결/해제/오류 등의 상태를 처리하며 필요시 이벤트 루프를 종료한다.
 *
 * @param pstBufferEvent bufferevent 객체
 * @param nEvents        BEV_EVENT_* 플래그
 * @param pvData         SOCK_CONTEXT 포인터
 */
static void appEventCb(struct bufferevent* pstBufferEvent, short nEvents, void* pvData);

/**
 * @brief 사용자 STDIN 입력 콜백
 *
 * 입력 명령(keepalive / ibit)을 기반으로 요청 프레임을 만들어 서버로 전송하며,
 * quit 입력 시 Client의 이벤트 루프가 종료된다.
 *
 * @param sig     file descriptor(STDIN)
 * @param nEvents 이벤트 플래그
 * @param pvData  SOCK_CONTEXT 포인터
 */
static void stdinReadCb(evutil_socket_t sig, short nEvents, void* pvData);



/* ========================================================================== */
/* Read Callback Implementation                                               */
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

    fprintf(stderr, "[UDS Client] Received %zu bytes\n", tDataLen);

    unsigned char* puchRecvData = malloc(tDataLen);
    if (!puchRecvData)
        return;

    evbuffer_copyout(pstEvBuffer, puchRecvData, tDataLen);

    MSG_ID stMsgId = {
        .uchSrcId = pstSockCtx->uchSrcId,
        .uchDstId = pstSockCtx->uchDstId
    };

    responseFrame(puchRecvData, &stMsgId, tDataLen);

    evbuffer_drain(pstEvBuffer, tDataLen);
    free(puchRecvData);
}



/* ========================================================================== */
/* Event Callback Implementation                                               */
/* ========================================================================== */
static void appEventCb(struct bufferevent* pstBufferEvent,
                    short nEvents,
                    void* pvData)
{
    SOCK_CONTEXT* pstSockCtx = (SOCK_CONTEXT*)pvData;
    EVENT_CONTEXT* pstEventCtx = pstSockCtx->pstEventCtx;

    if (nEvents & BEV_EVENT_CONNECTED) {
        fprintf(stderr, "[UDS Client] Connected to server.\n");
    }

    if (nEvents & BEV_EVENT_EOF) {
        fprintf(stderr, "[UDS Client] Server closed connection.\n");
        event_base_loopexit(pstEventCtx->pstEventBase, NULL);
        return;
    }

    if (nEvents & BEV_EVENT_ERROR) {
        fprintf(stderr, "[UDS Client] Connection Error: %s\n",
                evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
        event_base_loopexit(pstEventCtx->pstEventBase, NULL);
        return;
    }
}



/* ========================================================================== */
/* STDIN Callback Implementation                                              */
/* ========================================================================== */
static void stdinReadCb(evutil_socket_t sig, short nEvents, void* pvData)
{
    (void)sig;
    (void)nEvents;

    SOCK_CONTEXT* pstSockCtx = (SOCK_CONTEXT*)pvData;
    EVENT_CONTEXT* pstEventCtx = pstSockCtx->pstEventCtx;

    char achStdInBuf[1024];
    unsigned char auchSendBuf[1024];
    FRAME_ERR eErr = FRAME_OK;
    int iSendSize = 0;

    if (!fgets(achStdInBuf, sizeof(achStdInBuf), stdin)) {
        event_base_loopexit(pstEventCtx->pstEventBase, NULL);
        return;
    }

    achStdInBuf[strcspn(achStdInBuf, "\n")] = '\0';

    MSG_ID stMsgId = {
        .uchSrcId = pstSockCtx->uchSrcId,
        .uchDstId = pstSockCtx->uchDstId
    };

    if (strcmp(achStdInBuf, "keepalive") == 0) {
        fprintf(stderr, "[UDS Client] SEND: KEEP_ALIVE\n");
        eErr = makeReqFrame(CMD_KEEP_ALIVE, &stMsgId, auchSendBuf, &iSendSize);

    } else if (strcmp(achStdInBuf, "ibit") == 0) {
        fprintf(stderr, "[UDS Client] SEND: IBIT\n");
        eErr = makeReqFrame(CMD_IBIT, &stMsgId, auchSendBuf, &iSendSize);

    } else if (!strcmp(achStdInBuf, "quit") || !strcmp(achStdInBuf, "exit")) {
        event_base_loopexit(pstEventCtx->pstEventBase, NULL);
        return;

    } else {
        fprintf(stderr,
                "[UDS Client] Commands:\n"
                "   keepalive\n"
                "   ibit\n"
                "   quit\n");
        return;
    }

    if (eErr == FRAME_OK && iSendSize > 0) {
        bufferevent_write(pstSockCtx->pstBufferEvent, auchSendBuf, iSendSize);
    } else {
        fprintf(stderr, "[FRAME ERROR] %s\n", frameErrToStr(eErr));
    }
}



/* ========================================================================== */
/* Entry Point                                                                */
/* ========================================================================== */
int run(void)
{
    EVENT_CONTEXT stEventCtx;
    initEventContext(&stEventCtx, ROLE_CLIENT, 2);

    SOCK_CONTEXT* pstSockCtx = calloc(1, sizeof(SOCK_CONTEXT));
    if (!pstSockCtx) {
        perror("calloc failed");
        return EXIT_FAILURE;
    }

    initSocketContext(pstSockCtx, &stEventCtx, RESPONSE_ENABLED);

    stEventCtx.iSockFd = netUdsCreateClient("/tmp/uds1.sock");
    if (stEventCtx.iSockFd < 0) {
        fprintf(stderr, "[UDS Client] Failed to connect socket\n");
        return EXIT_FAILURE;
    }

    stEventCtx.pstEventBase = event_base_new();
    if (!stEventCtx.pstEventBase) {
        fprintf(stderr, "event_base_new failed\n");
        return EXIT_FAILURE;
    }

    pstSockCtx->pstBufferEvent =
        bufferevent_socket_new(stEventCtx.pstEventBase,
                            stEventCtx.iSockFd,
                            BEV_OPT_CLOSE_ON_FREE);

    bufferevent_setcb(pstSockCtx->pstBufferEvent,
                    appReadCb,
                    NULL,
                    appEventCb,
                    pstSockCtx);

    bufferevent_enable(pstSockCtx->pstBufferEvent, EV_READ | EV_WRITE);

    stEventCtx.pstEvent =
        event_new(stEventCtx.pstEventBase, fileno(stdin),
                EV_READ | EV_PERSIST, stdinReadCb, pstSockCtx);

    event_add(stEventCtx.pstEvent, NULL);

    printf("[UDS Client] Connecting to /tmp/uds1.sock...\n");

    event_base_dispatch(stEventCtx.pstEventBase);

    closeAndFree(pstSockCtx);

    return EXIT_SUCCESS;
}

/* === main === */
#ifndef GOOGLE_TEST
int main(int argc, char** argv)
{
    return run();
}
#endif