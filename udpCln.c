/**
 * @file udpCln.c
 * @brief Libevent 기반 UDP Client Application
 *
 * UDP 기반으로 서버와 비동기 통신을 수행하며 STDIN 입력을 통해
 * 요청(Request Frame)을 생성하고 서버 응답(Response Frame)을 처리한다.
 *
 * ### 특징
 * - 연결 기반 프로토콜이 아닌 UDP datagram 처리 방식
 * - libevent EV_READ 기반 이벤트 감지
 * - requestFrame(), responseFrame() 호출 구조를 그대로 유지
 *
 * ### 동작 순서
 * 1. stdinReadCb(): 사용자 입력 → 요청 프레임 생성 → write()
 * 2. appReadCb(): 서버 응답 수신 → responseFrame() 전달
 * 3. event_base_dispatch(): 이벤트 루프 기반 동작 유지
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include "netUdp.h"
#include "netCore.h"
#include "eventSession.h"
#include "frame.h"
#include "icdCommand.h"


/* ========================================================================== */
/* Defines                                                                     */
/* ========================================================================== */

#define SERVER_IP           "127.0.0.1"
#define UDP_SERVER_PORT     5001
#define UDP_CLIENT_PORT     5002


/* ========================================================================== */
/* Static Function Prototypes                                                 */
/* ========================================================================== */

/**
 * @brief UDP 데이터 수신 처리 콜백
 *
 * UDP 소켓에서 datagram을 읽어 responseFrame()으로 처리한다.
 * (UDP 특성상 프레임 단위 분리 처리는 upper-layer(frame.c)에서 수행)
 *
 * @param iSockFd  수신 소켓 FD
 * @param nEvents  EV_READ 플래그
 * @param pvData   SOCK_CONTEXT 포인터
 */
static void appReadCb(evutil_socket_t iSockFd, short nEvents, void* pvData);

/**
 * @brief STDIN 입력 처리 콜백
 *
 * 사용자가 보낸 문자열 기반으로 프로토콜 프레임을 생성하고
 * UDP 소켓을 통해 서버로 전송한다.
 * `"quit"` 입력 시 이벤트 루프 종료.
 *
 * @param sig      STDIN FD
 * @param nEvents  EV_READ 플래그
 * @param pvData   SOCK_CONTEXT 포인터
 */
static void stdinReadCb(evutil_socket_t sig, short nEvents, void* pvData);



/* ========================================================================== */
/* Read Callback Implementation                                               */
/* ========================================================================== */
static void appReadCb(evutil_socket_t iSockFd, short nEvents, void* pvData)
{
    (void)nEvents;

    SOCK_CONTEXT* pstSockCtx = (SOCK_CONTEXT*)pvData;
    if (!pstSockCtx)
        return;

    unsigned char auchRecvData[1024];
    int iRecvSize = read(iSockFd, auchRecvData, sizeof(auchRecvData));

    if (iRecvSize <= 0)
        return;

    fprintf(stderr, "[UDP Client] Received %d bytes\n", iRecvSize);

    MSG_ID stMsgId;
    stMsgId.uchSrcId = pstSockCtx->uchSrcId;
    stMsgId.uchDstId = pstSockCtx->uchDstId;

    responseFrame(auchRecvData, &stMsgId, (size_t)iRecvSize);
}



/* ========================================================================== */
/* STDIN Callback Implementation                                              */
/* ========================================================================== */
static void stdinReadCb(evutil_socket_t sig, short nEvents, void* pvData)
{
    (void)sig;
    (void)nEvents;

    SOCK_CONTEXT* pstSockCtx  = (SOCK_CONTEXT*)pvData;
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
        fprintf(stderr, "[UDP Client] SEND: KEEP_ALIVE\n");
        eErr = makeReqFrame(CMD_KEEP_ALIVE, &stMsgId, auchSendBuf, &iSendSize);

    } else if (strcmp(achStdInBuf, "ibit") == 0) {
        fprintf(stderr, "[UDP Client] SEND: IBIT\n");
        eErr = makeReqFrame(CMD_IBIT, &stMsgId, auchSendBuf, &iSendSize);

    } else if (!strcmp(achStdInBuf, "quit") || !strcmp(achStdInBuf, "exit")) {
        fprintf(stderr, "[UDP Client] Terminating...\n");
        event_base_loopexit(pstEventCtx->pstEventBase, NULL);
        return;

    } else {
        fprintf(stderr,
            "[Usage]\n"
            "  keepalive\n"
            "  ibit\n"
            "  quit\n\n");
        return;
    }

    if (eErr == FRAME_OK && iSendSize > 0) {        
    int iWritten = write(pstEventCtx->iSockFd, auchSendBuf, iSendSize);
    if (iWritten < 0) {
        perror("[UDP] write() failed");
    } else if (iWritten != iSendSize) {
        fprintf(stderr, "[UDP] Partial write: %d/%d bytes sent\n", iWritten, iSendSize);
    }
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
        perror("calloc");
        return EXIT_FAILURE;
    }

    initSocketContext(pstSockCtx, &stEventCtx, RESPONSE_ENABLED);

    struct event* pstStdInEvent = NULL;

    /* UDP 소켓 생성 */
    stEventCtx.iSockFd = netUdpCreateClient(SERVER_IP, UDP_SERVER_PORT, UDP_CLIENT_PORT);
    if (stEventCtx.iSockFd < 0) {
        fprintf(stderr, "UDP socket create failed\n");
        return EXIT_FAILURE;
    }

    /* event_base 생성 */
    stEventCtx.pstEventBase = event_base_new();
    if (!stEventCtx.pstEventBase) {
        fprintf(stderr, "event_base_new() failed\n");
        netClose(stEventCtx.iSockFd);
        return EXIT_FAILURE;
    }

    /* UDP FD 이벤트 등록 */
    stEventCtx.pstEvent = event_new(
        stEventCtx.pstEventBase,
        stEventCtx.iSockFd,
        EV_READ | EV_PERSIST,
        appReadCb,
        pstSockCtx
    );
    event_add(stEventCtx.pstEvent, NULL);

    /* STDIN 이벤트 등록 */
    pstStdInEvent = event_new(
        stEventCtx.pstEventBase,
        fileno(stdin),
        EV_READ | EV_PERSIST,
        stdinReadCb,
        pstSockCtx
    );
    event_add(pstStdInEvent, NULL);

    fprintf(stderr, "[UDP Client] Running %s:%d\n", SERVER_IP, UDP_SERVER_PORT);

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