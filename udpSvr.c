/**
 * @file udpSvr.c
 * @brief Libevent 기반 UDP 서버 Application Layer (bufferevent 사용 버전)
 *
 * UDP 기반 데이터그램 통신을 수행하며, 수신된 RAW 데이터를 프레임 기반 프로토콜로
 * 파싱하여 처리하고 필요 시 응답 프레임을 전송한다.
 *
 * ### 변경 사항
 * - 기존: event_new() + read()/write()
 * - 변경: bufferevent_socket_new() + evbuffer 기반 Read/Write
 * - EVENT_CONTEXT + SOCK_CONTEXT 구조 재사용 (TCP와 동일 스타일)
 *
 * ### 처리 흐름
 * 1. UDP 서버 소켓 생성 (`netUdpCreateServer`)
 * 2. bufferevent_socket_new()로 FD를 래핑
 * 3. appReadCb()에서 수신 데이터 → requestFrame() → commandHandler()
 * 4. makeResFrame()으로 응답 생성 후 bufferevent_write()로 송신
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include "eventEngine.h"
#include "udpSvr.h"   // SERVER_IP / CLIENT_IP / UDP_SERVER_PORT / UDP_CLIENT_PORT
                      // netUdp.h, netCore.h, eventSession.h, frame.h 포함됨

/* ========================================================================== */
/* Static Function Prototypes                                                 */
/* ========================================================================== */

/**
 * @brief UDP 수신 이벤트 콜백
 *
 * UDP 소켓에서 수신된 데이터를 읽어 프레임 단위로 파싱하고 명령 처리 후
 * 필요 시 응답 프레임을 동일 bufferevent를 통해 전송한다.
 *
 * @param pstBufferEvent bufferevent 핸들
 * @param pvData         SOCK_CONTEXT*
 */
static void appReadCb(struct bufferevent* pstBufferEvent, void* pvData);

/**
 * @brief UDP bufferevent 이벤트 콜백
 *
 * 에러 또는 EOF 등의 이벤트를 처리한다.
 */
static void appEventCb(struct bufferevent* pstBufferEvent,
                       short nEvents, void* pvData);

/**
 * @brief SIGINT 처리 콜백
 *
 * CTRL+C 입력 시 event loop 종료 및 소켓 정리를 수행한다.
 *
 * @param sig    시그널 번호
 * @param events 이벤트 플래그
 * @param pvData EVENT_CONTEXT*
 */
static void signalCb(evutil_socket_t sig, short events, void* pvData);



/* ========================================================================== */
/* UDP Read Callback (bufferevent 기반)                                       */
/* ========================================================================== */
static void appReadCb(struct bufferevent* pstBufferEvent, void* pvData)
{
    SOCK_CONTEXT* pstSockCtx = (SOCK_CONTEXT*)pvData;
    if (!pstSockCtx || !pstSockCtx->pstEventCtx)
        return;

    EVENT_CONTEXT* pstEventCtx = pstSockCtx->pstEventCtx;

    unsigned char auRecvBuf[2048]  = {0};
    unsigned char auSendBuf[1024]  = {0};
    unsigned char auCmdResult[1000];

    MSG_ID        stMsgId;
    unsigned short unCmd      = 0;
    FRAME_ERR     eErr;
    int           iSendSize   = 0;
    int           iOffset     = 0;

    /* === 1) evbuffer에서 데이터 전체 읽기 === */
    struct evbuffer* pstInput = bufferevent_get_input(pstBufferEvent);
    size_t tDataLen = evbuffer_get_length(pstInput);

    if (tDataLen == 0)
        return;

    if (tDataLen > sizeof(auRecvBuf))
        tDataLen = sizeof(auRecvBuf);

    evbuffer_copyout(pstInput, auRecvBuf, tDataLen);

    fprintf(stderr, "[UDP Server] Received %zu bytes\n", tDataLen);

    /* === 2) 프레임 단위 파싱 루프 === */
    while (iOffset + FRAME_HEADER_MIN_SIZE <= (int)tDataLen) {

        int iFrameSize = getFrameSize(auRecvBuf + iOffset);
        if (iFrameSize <= 0) {
            /* 잘못된 헤더 → 한 바이트씩 스킵 */
            iOffset++;
            continue;
        }

        if (iOffset + iFrameSize > (int)tDataLen) {
            /* 프레임이 완전히 도착하지 않은 경우: 다음 Read에서 처리 */
            break;
        }

        stMsgId.uchSrcId = pstSockCtx->uchSrcId;
        stMsgId.uchDstId = pstSockCtx->uchDstId;

        /* Step 1: 요청 프레임 분석 */
        eErr = requestFrame(auRecvBuf + iOffset, &stMsgId,
                            iFrameSize, &unCmd);

        if (eErr == FRAME_OK && unCmd != 0xFFFF) {

            /* Step 2: 명령 처리 */
            eErr = commandHandler(auRecvBuf + iOffset, &stMsgId,
                                  iFrameSize, auCmdResult, &iSendSize);

            if (eErr == FRAME_OK && iSendSize > 0) {

                /* Step 3: 응답 프레임 생성 */
                eErr = makeResFrame(unCmd, &stMsgId,
                                    auCmdResult, auSendBuf);

                if (eErr == FRAME_OK) {
                    fprintf(stderr,
                            "[UDP] Send Response Cmd=0x%04X, Size=%d\n",
                            unCmd, iSendSize);

                    if (bufferevent_write(pstBufferEvent,
                                          auSendBuf,
                                          (size_t)iSendSize) < 0) {
                        perror("[UDP] bufferevent_write() failed");
                    }
                } else {
                    fprintf(stderr,
                            "[UDP] makeResFrame() failed: %s\n",
                            frameErrToStr(eErr));
                }
            }
        } else if (eErr != FRAME_OK) {
            fprintf(stderr,
                    "[UDP] requestFrame() error: %s\n",
                    frameErrToStr(eErr));
        }

        iOffset += iFrameSize;
    }

    /* 소비한 만큼 evbuffer에서 제거 */
    evbuffer_drain(pstInput, tDataLen);
}



/* ========================================================================== */
/* UDP Event Callback                                                         */
/* ========================================================================== */
static void appEventCb(struct bufferevent* pstBufferEvent,
                       short nEvents, void* pvData)
{
    (void)pstBufferEvent;

    SOCK_CONTEXT*  pstSockCtx  = (SOCK_CONTEXT*)pvData;
    EVENT_CONTEXT* pstEventCtx = pstSockCtx ? pstSockCtx->pstEventCtx : NULL;

    if (nEvents & BEV_EVENT_ERROR) {
        fprintf(stderr, "[UDP Server] BEV_EVENT_ERROR: %s\n",
                evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
    }
    if (nEvents & BEV_EVENT_EOF) {
        fprintf(stderr, "[UDP Server] BEV_EVENT_EOF\n");
    }
    if (nEvents & (BEV_EVENT_ERROR | BEV_EVENT_EOF)) {
        if (pstEventCtx && pstEventCtx->pstEventBase)
            event_base_loopexit(pstEventCtx->pstEventBase, NULL);
    }
}



/* ========================================================================== */
/* SIGINT Callback                                                            */
/* ========================================================================== */
static void signalCb(evutil_socket_t sig, short events, void* pvData)
{
    (void)events;

    EVENT_CONTEXT* pstEventCtx = (EVENT_CONTEXT*)pvData;

    fprintf(stderr, "\n[UDP Server] Caught signal %d. Shutting down...\n", (int)sig);

    if (pstEventCtx && pstEventCtx->pstEventBase)
        event_base_loopexit(pstEventCtx->pstEventBase, NULL);
}



/* ========================================================================== */
/* Entry Point                                                                */
/* ========================================================================== */
int run(void)
{
    BASE_CONTEXT   stBaseCtx;
    SERVER_CONTEXT stServerCtx;
    baseContextInit(&stBaseCtx, 1);
    serverContextInit(&stServerCtx, &stBaseCtx, ROLE_UDP_SERVER);
    stServerCtx.iSockFd = netUdpCreateServer(UDP_SERVER_PORT,
                                            CLIENT_IP,
                                            UDP_CLIENT_PORT);




    /* SOCK_CONTEXT 할당 */
    stEventCtx.pstSockCtx = (SOCK_CONTEXT*)calloc(1, sizeof(SOCK_CONTEXT));
    if (!stEventCtx.pstSockCtx) {
        fprintf(stderr, "SOCK_CONTEXT allocation failed.\n");
        return EXIT_FAILURE;
    }

    initSocketContext(stEventCtx.pstSockCtx, &stEventCtx, RESPONSE_ENABLED);

    /* === 1) UDP 서버 소켓 생성 === */
    
    if (stEventCtx.iSockFd < 0) {
        fprintf(stderr, "UDP Server socket creation failed.\n");
        free(stEventCtx.pstSockCtx);
        return EXIT_FAILURE;
    }

    /* === 2) event_base 생성 === */
    stEventCtx.pstEventBase = event_base_new();
    if (!stEventCtx.pstEventBase) {
        fprintf(stderr, "event_base_new() failed.\n");
        netClose(stEventCtx.iSockFd);
        free(stEventCtx.pstSockCtx);
        return EXIT_FAILURE;
    }

    /* === 3) UDP FD를 bufferevent로 래핑 === */
    stEventCtx.pstSockCtx->pstBufferEvent =
        bufferevent_socket_new(stEventCtx.pstEventBase,
                               stEventCtx.iSockFd,
                               BEV_OPT_CLOSE_ON_FREE);
    if (!stEventCtx.pstSockCtx->pstBufferEvent) {
        fprintf(stderr, "bufferevent_socket_new() failed.\n");
        event_base_free(stEventCtx.pstEventBase);
        netClose(stEventCtx.iSockFd);
        free(stEventCtx.pstSockCtx);
        return EXIT_FAILURE;
    }

    bufferevent_setcb(stEventCtx.pstSockCtx->pstBufferEvent,
                      appReadCb,
                      NULL,
                      appEventCb,
                      stEventCtx.pstSockCtx);

    bufferevent_enable(stEventCtx.pstSockCtx->pstBufferEvent,
                       EV_READ | EV_WRITE);

    /* === 4) SIGINT 이벤트 등록 === */
    signal(SIGPIPE, SIG_IGN);

    stEventCtx.pstSignalEvent =
        evsignal_new(stEventCtx.pstEventBase,
                     SIGINT,
                     signalCb,
                     &stEventCtx);
    if (stEventCtx.pstSignalEvent)
        event_add(stEventCtx.pstSignalEvent, NULL);

    printf("[UDP Server] Listening on %s:%d -> client %s:%d\n",
           SERVER_IP, UDP_SERVER_PORT,
           CLIENT_IP, UDP_CLIENT_PORT);

    /* === 5) 이벤트 루프 진입 === */
    event_base_dispatch(stEventCtx.pstEventBase);

    /* === 6) 자원 해제 === */
    closeAndFree(stEventCtx.pstSockCtx);

    if (stEventCtx.pstSignalEvent)
        event_free(stEventCtx.pstSignalEvent);

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
