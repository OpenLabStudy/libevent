/**
 * @file udpSvr.c
 * @brief Libevent 기반 UDP 서버 Application Layer
 *
 * UDP 기반 데이터그램 통신을 수행하며, 수신된 RAW 데이터를 프레임 기반 프로토콜로
 * 파싱하여 처리하고 필요 시 응답 프레임을 전송한다.
 *
 * ### UDP 통신 특징
 * - TCP와 달리 연결(Connection) 개념이 없으며 데이터그램 단위로 전송됨
 * - 수신 데이터는 순차적 보장이 없으며 패킷 경계가 유지됨
 * - bufferevent 대신 event_new() + recv/read 방식 사용
 *
 * ### 처리 흐름
 * 1. 이벤트 기반 UDP 소켓 생성 (`netUdpCreateServer`)
 * 2. EV_READ 조건 발생 시 `appReadCb()` 호출
 * 3. 수신 데이터 프레임 파싱 → `requestFrame()` → `commandHandler()`
 * 4. 응답 프레임 생성 및 즉시 write() 수행
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <event2/event.h>
#include <event2/buffer.h>

#include "udpSvr.h"

/* ========================================================================== */
/* Static Function Prototypes                                                 */
/* ========================================================================== */

/**
 * @brief UDP 수신 이벤트 콜백
 *
 * UDP 소켓에서 수신된 데이터를 읽어 프레임 단위로 파싱하고 명령 처리 후
 * 필요 시 응답 프레임을 동일 소켓 fd를 통해 전송한다.
 *
 * @param iSockFd  UDP 소켓 File descriptor
 * @param events   Event flags
 * @param pvData   SOCK_CONTEXT 포인터
 */
static void appReadCb(evutil_socket_t iSockFd, short events, void* pvData);

/**
 * @brief SIGINT 처리 콜백
 *
 * CTRL+C 입력 시 event loop 종료 및 소켓 정리를 수행한다.
 *
 * @param sig   시그널 번호
 * @param ev    이벤트 플래그
 * @param pvData EVENT_CONTEXT 포인터
 */
static void signalCb(evutil_socket_t sig, short ev, void* pvData);



/* ========================================================================== */
/* UDP Read Callback                                                          */
/* ========================================================================== */
static void appReadCb(evutil_socket_t iSockFd, short events, void* pvData)
{
    (void)events;

    SOCK_CONTEXT* pstSockCtx = (SOCK_CONTEXT*)pvData;
    if (!pstSockCtx || !pstSockCtx->pstEventCtx)
        return;

    unsigned char auRecvBuf[2048] = {0};
    unsigned char auSendBuf[1024];
    unsigned char auCmdResult[1000];

    MSG_ID stMsgId;
    unsigned short unCmd = 0;
    FRAME_ERR eErr;
    int iRecvSize = read(iSockFd, auRecvBuf, sizeof(auRecvBuf));
    int iOffset = 0;
    int iSendSize = 0;

    if (iRecvSize <= 0)
        return;

    while (iOffset + FRAME_HEADER_MIN_SIZE <= iRecvSize) {

        int iFrameSize = getFrameSize(auRecvBuf + iOffset);
        if (iFrameSize <= 0) {
            iOffset++;
            continue;
        }
        if (iOffset + iFrameSize > iRecvSize)
            break;

        stMsgId.uchSrcId = pstSockCtx->uchSrcId;
        stMsgId.uchDstId = pstSockCtx->uchDstId;

        /* Step 1: 요청 프레임 분석 */
        eErr = requestFrame(auRecvBuf + iOffset, &stMsgId, iFrameSize, &unCmd);

        if (eErr == FRAME_OK && unCmd != 0xFFFF) {

            /* Step 2: 명령 처리 */
            eErr = commandHandler(auRecvBuf + iOffset, &stMsgId,
                                iFrameSize, auCmdResult, &iSendSize);

            if (eErr == FRAME_OK && iSendSize > 0) {

                /* Step 3: 응답 프레임 생성 */
                eErr = makeResFrame(unCmd, &stMsgId, auCmdResult, auSendBuf);

                if (eErr == FRAME_OK) {
                    fprintf(stderr,"[UDP] Send Response Cmd=%04X, Size=%d\n", unCmd, iSendSize);
                int iWritten = write(iSockFd, auSendBuf, iSendSize);
                if (iWritten < 0) {
                    perror("[UDP] write() failed");
                } else if (iWritten != iSendSize) {
                    fprintf(stderr, "[UDP] Partial write: %d/%d bytes sent\n", iWritten, iSendSize);
                }
                }
            }
        }

        fprintf(stderr, "[UDP] Frame Result: %s\n", frameErrToStr(eErr));
        iOffset += iFrameSize;
    }
}



/* ========================================================================== */
/* Signal Handling                                                            */
/* ========================================================================== */
static void signalCb(evutil_socket_t sig, short ev, void* pvData)
{
    (void)sig;
    (void)ev;

    EVENT_CONTEXT* pstEventCtx = (EVENT_CONTEXT*)pvData;

    fprintf(stderr, "[SIGNAL] SIGINT received → Stopping server.\n");

    if (pstEventCtx && pstEventCtx->pstEventBase)
        event_base_loopexit(pstEventCtx->pstEventBase, NULL);
}



/* ========================================================================== */
/* Entry Point                                                                */
/* ========================================================================== */
int run(void)
{
    EVENT_CONTEXT stEventCtx;
    initEventContext(&stEventCtx, ROLE_SERVER, 1);

    stEventCtx.pstSockCtx = calloc(1, sizeof(SOCK_CONTEXT));
    if (!stEventCtx.pstSockCtx) {
        fprintf(stderr, "SOCK_CONTEXT allocation failed.\n");
        return EXIT_FAILURE;
    }

    initSocketContext(stEventCtx.pstSockCtx, &stEventCtx, RESPONSE_ENABLED);

    stEventCtx.iSockFd = netUdpCreateServer(UDP_SERVER_PORT, CLIENT_IP, UDP_CLIENT_PORT);
    if (stEventCtx.iSockFd < 0) {
        fprintf(stderr, "UDP Server socket creation failed.\n");
        return EXIT_FAILURE;
    }

    stEventCtx.pstEventBase = event_base_new();
    if (!stEventCtx.pstEventBase) {
        fprintf(stderr, "event_base_new() failed.\n");
        netClose(stEventCtx.iSockFd);
        return EXIT_FAILURE;
    }

    stEventCtx.pstEvent = event_new(stEventCtx.pstEventBase,
                                    stEventCtx.iSockFd,
                                    EV_READ | EV_PERSIST,
                                    appReadCb,
                                    stEventCtx.pstSockCtx);

    event_add(stEventCtx.pstEvent, NULL);

    signal(SIGPIPE, SIG_IGN);

    stEventCtx.pstSignalEvent = evsignal_new(stEventCtx.pstEventBase, SIGINT,
                                            signalCb, &stEventCtx);
    event_add(stEventCtx.pstSignalEvent, NULL);

    printf("[UDP Server] Listening on %s:%d\n", CLIENT_IP, UDP_SERVER_PORT);

    event_base_dispatch(stEventCtx.pstEventBase);

    closeAndFree(stEventCtx.pstSockCtx);

    return EXIT_SUCCESS;
}

/* === main === */
#ifndef GOOGLE_TEST
int main(int argc, char** argv)
{
    return run();
}
#endif