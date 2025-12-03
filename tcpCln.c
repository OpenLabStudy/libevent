/**
 * @file tcpCln.c
 * @brief Libevent 기반 TCP Client Application Layer
 *
 * 본 모듈은 TCP 기반 서버와 통신하는 클라이언트 애플리케이션 레이어이며,
 * 사용자 입력(STDIN) → 요청 프레임 생성 → 서버 송신 → 응답 프레임 파싱의 흐름을 수행한다.
 *
 * ### 처리 흐름
 * 1. TCP 소켓 생성 및 연결 (`netTcpCreateClient`)
 * 2. bufferevent 등록 및 callback 바인딩
 * 3. STDIN 이벤트 등록 → 사용자 입력 기반 명령 생성
 * 4. 서버 응답 데이터 수신 → `responseFrame()` 호출
 *
 * ### 특징
 * - Non-blocking 비동기 I/O 구조
 * - 입력 명령 기반 Request-Response 프레임 처리
 * - libevent event_base 기반 구조
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include "netTcp.h"
#include "netCore.h"
#include "eventSession.h"
#include "frame.h"
#include "icdCommand.h"

#define SERVER_IP   "127.0.0.1"
#define SERVER_PORT 5000


/* ========================================================================== */
/* Static Function Prototypes                                                 */
/* ========================================================================== */

/**
 * @brief Server → Client 데이터 수신 처리 callback
 *
 * 수신된 RAW 데이터는 프레임 기반 프로토콜에 따라 `responseFrame()`을 통해 분석되며,
 * 처리 결과 로그 출력 및 buffer drain 수행한다.
 *
 * @param pstBufferEvent bufferevent 핸들
 * @param pvData         SOCK_CONTEXT 포인터
 */
static void appReadCb(struct bufferevent* pstBufferEvent, void* pvData);

/**
 * @brief TCP 연결 이벤트 처리 callback
 *
 * 연결 완료(BEV_EVENT_CONNECTED), 종료(BEV_EVENT_EOF), 오류(BEV_EVENT_ERROR)
 * 상태를 처리하며 필요 시 event loop 종료를 수행한다.
 *
 * @param pstBufferEvent bufferevent 핸들
 * @param nEvents        libevent 이벤트 플래그
 * @param pvData         SOCK_CONTEXT 포인터
 */
static void appEventCb(struct bufferevent* pstBufferEvent,
                    short nEvents, void* pvData);

/**
 * @brief STDIN 입력 처리 callback
 *
 * 사용자가 입력한 명령을 기반으로 요청 프레임(REQ)을 생성하여 서버로 전송한다.
 * 입력 가능한 명령:
 * ```
 * keepalive
 * ibit
 * quit | exit
 * ```
 *
 * @param sig     파일 디스크립터(stdin)
 * @param nEvents 이벤트 종류
 * @param pvData  SOCK_CONTEXT 포인터
 */
static void stdinReadCb(evutil_socket_t sig, short nEvents, void* pvData);



/* ========================================================================== */
/* Application-level Read Callback                                            */
/* ========================================================================== */
static void appReadCb(struct bufferevent* pstBufferEvent, void* pvData)
{
    SOCK_CONTEXT* pstSockCtx = (SOCK_CONTEXT*)pvData;
    if (!pstSockCtx)
        return;

    struct evbuffer* pstEvBuffer = bufferevent_get_input(pstBufferEvent);
    size_t tDataLen = evbuffer_get_length(pstEvBuffer);

    fprintf(stderr, "[Client] Received %zu bytes\n", tDataLen);

    unsigned char* puchRecvData = malloc(tDataLen);
    if (!puchRecvData)
        return;

    evbuffer_copyout(pstEvBuffer, puchRecvData, tDataLen);

    MSG_ID stMsgId;
    stMsgId.uchSrcId = pstSockCtx->uchSrcId;
    stMsgId.uchDstId = pstSockCtx->uchDstId;

    responseFrame(puchRecvData, &stMsgId, tDataLen);

    evbuffer_drain(pstEvBuffer, tDataLen);
    free(puchRecvData);
}



/* ========================================================================== */
/* Application-level Event Callback                                           */
/* ========================================================================== */
static void appEventCb(struct bufferevent* pstBufferEvent,
    short nEvents, void* pvData)
{
    (void)pstBufferEvent;

    SOCK_CONTEXT *pstSockCtx = (SOCK_CONTEXT *)pvData;
    EVENT_CONTEXT* pstEventCtx = pstSockCtx->pstEventCtx;

    if (nEvents & BEV_EVENT_CONNECTED)
        fprintf(stderr,"[Client] Connected to server.\n");

    if (nEvents & BEV_EVENT_EOF) {
        fprintf(stderr,"[Client] Server closed connection.\n");
        event_base_loopexit(pstEventCtx->pstEventBase, NULL);
    }

    if (nEvents & BEV_EVENT_ERROR) {
        fprintf(stderr,"[Client] Error: %s\n",
        evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
        event_base_loopexit(pstEventCtx->pstEventBase, NULL);
    }
}



/* ========================================================================== */
/* STDIN → Request Frame builder                                             */
/* ========================================================================== */
static void stdinReadCb(evutil_socket_t sig, short nEvents, void* pvData)
{
    (void)sig;
    (void)nEvents;

    SOCK_CONTEXT *pstSockCtx = (SOCK_CONTEXT *)pvData;
    EVENT_CONTEXT* pstEventCtx = pstSockCtx->pstEventCtx;

    char achInput[1024];
    unsigned char auSendBuf[1024];
    int iSendLen = 0;
    FRAME_ERR eErr;

    if (!fgets(achInput, sizeof(achInput), stdin)) {
        event_base_loopexit(pstEventCtx->pstEventBase, NULL);
        return;
    }

    achInput[strcspn(achInput, "\n")] = '\0';

    MSG_ID stMsgId = { pstSockCtx->uchSrcId, TCP_SVR_ID };

    if (!strcmp(achInput, "keepalive")) {
        fprintf(stderr,"[Client] REQ_KEEP_ALIVE\n");
        eErr = makeReqFrame(CMD_KEEP_ALIVE, &stMsgId, auSendBuf, &iSendLen);

    } else if (!strcmp(achInput, "ibit")) {
        fprintf(stderr,"[Client] REQ_IBIT\n");
        eErr = makeReqFrame(CMD_IBIT, &stMsgId, auSendBuf, &iSendLen);

    } else if (!strcmp(achInput, "quit") || !strcmp(achInput, "exit")) {
        event_base_loopexit(pstEventCtx->pstEventBase, NULL);
        return;

    } else {
        fprintf(stderr, "Available commands:\n  keepalive\n  ibit\n  quit\n");
        return;
    }

    if (eErr == FRAME_OK && iSendLen > 0) {
        bufferevent_write(pstSockCtx->pstBufferEvent, auSendBuf, (size_t)iSendLen);
    }
}



/* ========================================================================== */
/* Main Entry Point                                                           */
/* ========================================================================== */
int run(void)
{
    EVENT_CONTEXT stEventCtx;
    initEventContext(&stEventCtx, ROLE_TCP_CLIENT, TCP_CLN_ID);

    SOCK_CONTEXT* pstSockCtx = calloc(1, sizeof(SOCK_CONTEXT));
    if (!pstSockCtx) {
        perror("calloc");
        return EXIT_FAILURE;
    }
    initSocketContext(pstSockCtx, &stEventCtx, RESPONSE_ENABLED);

    stEventCtx.iSockFd = netTcpCreateClient(SERVER_IP, SERVER_PORT);
    if (stEventCtx.iSockFd < 0) {
        fprintf(stderr, "[Client] Failed to create TCP client socket\n");
        return EXIT_FAILURE;
    }

    stEventCtx.pstEventBase = event_base_new();
    if (!stEventCtx.pstEventBase) {
        fprintf(stderr, "[Client] event_base_new() failed\n");
        return EXIT_FAILURE;
    }

    pstSockCtx->pstBufferEvent =
        bufferevent_socket_new(stEventCtx.pstEventBase,
                                stEventCtx.iSockFd,
                                BEV_OPT_CLOSE_ON_FREE);
    if (!pstSockCtx->pstBufferEvent)
        return EXIT_FAILURE;

    bufferevent_setcb(
        pstSockCtx->pstBufferEvent,
        appReadCb,
        NULL,
        appEventCb,
        pstSockCtx
    );
    bufferevent_enable(pstSockCtx->pstBufferEvent, EV_READ | EV_WRITE);

    stEventCtx.pstEvent = event_new(stEventCtx.pstEventBase,
                                    fileno(stdin),
                                    EV_READ | EV_PERSIST,
                                    stdinReadCb,
                                    pstSockCtx);
    event_add(stEventCtx.pstEvent, NULL);

    fprintf(stderr, "[Client] Connecting to %s:%d ...\n", SERVER_IP, SERVER_PORT);

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