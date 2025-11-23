/**
 * @file udsSvr.c
 * @brief Libevent 기반 UDS(Unix Domain Socket) 서버 Application
 *
 * Unix Domain Socket 기반으로 클라이언트 요청(Request Frame)을 수신하고
 * 프레임을 파싱하여 처리(commandHandler) 후 응답(Response Frame)을 작성하여 전송한다.
 *
 * ### 특징
 * - TCP 대비 낮은 오버헤드(Local IPC 최적화)
 * - 파일 경로 기반 소켓 바인딩(`/tmp/uds1.sock`)
 * - Libevent 기반 비동기 이벤트 모델
 *
 * ### 주요 처리 구조
 * - `appReadCb()`  : 수신 데이터→프레임 파싱→응답 구성
 * - `appEventCb()` : 연결 상태/오류 로깅
 * - `signalCb()`   : SIGINT 종료 처리
 * - `main()`       : 이벤트 루프 및 서버 초기화
 */

#include <stdio.h>
#include <stdlib.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <signal.h>

#include "udsSvr.h"


/* ========================================================================== */
/* Static Function Prototypes                                                 */
/* ========================================================================== */

/**
 * @brief 서버 측 recv() 비동기 처리 루틴
 *
 * - Libevent bufferevent input buffer에서 데이터를 수신
 * - Frame 구조가 아닐 경우 헤더 sync 정렬 처리
 * - 정상 프레임이면 parsing → command handler → response frame 생성 및 송신
 *
 * @param pstBufferEvent libevent bufferevent 객체
 * @param pvData         SOCK_CONTEXT 포인터
 */
static void appReadCb(struct bufferevent* pstBufferEvent, void* pvData);

/**
 * @brief bufferevent 상태 이벤트 콜백
 *
 * EOF, ERROR 등의 이벤트 발생 시 로그를 기록한다.
 * close 동작은 eventSession의 관리 함수에서 처리됨.
 *
 * @param pstBufferEvent bufferevent 객체
 * @param nEvents        BEV_EVENT_* 상태 값
 * @param pvData         SOCK_CONTEXT 포인터
 */
static void appEventCb(struct bufferevent* pstBufferEvent,
                    short nEvents,
                    void* pvData);

/**
 * @brief SIGINT 이벤트 처리
 *
 * CTRL+C 입력 시 이벤트 루프 종료 및 UDS 파일 제거
 *
 * @param sig     발생한 signal 번호
 * @param ev      이벤트 플래그
 * @param pvData  EVENT_CONTEXT 포인터
 */
static void signalCb(evutil_socket_t sig, short ev, void* pvData);



/* ========================================================================== */
/* Read Callback Implementation                                               */
/* ========================================================================== */
static void appReadCb(struct bufferevent* pstBufferEvent, void* pvData)
{
    SOCK_CONTEXT* pstSockCtx = (SOCK_CONTEXT*)pvData;
    if (!pstSockCtx || !pstSockCtx->pstEventCtx)
        return;

    struct evbuffer* pstEvBuffer = bufferevent_get_input(pstBufferEvent);

    unsigned char auchRecvData[2048];
    unsigned char auchCmdResult[1000];
    unsigned char auchSendData[1024];
    unsigned short unCmd = 0;
    FRAME_ERR eErr = FRAME_OK;
    int iSendSize = 0;

    while (1) {
        size_t tAvailable = evbuffer_get_length(pstEvBuffer);
        if (tAvailable < FRAME_HEADER_MIN_SIZE)
            break;

        if (tAvailable > sizeof(auchRecvData))
            tAvailable = sizeof(auchRecvData);

        evbuffer_copyout(pstEvBuffer, auchRecvData, tAvailable);

        int iFrameSize = getFrameSize(auchRecvData);

        if (iFrameSize <= 0) {
            evbuffer_drain(pstEvBuffer, 1);
            continue;
        }

        if (tAvailable < (size_t)iFrameSize)
            break;

        evbuffer_drain(pstEvBuffer, iFrameSize);

        MSG_ID stMsgId = {
            .uchSrcId = pstSockCtx->uchSrcId,
            .uchDstId = pstSockCtx->uchDstId
        };

        eErr = requestFrame(auchRecvData, &stMsgId, iFrameSize, &unCmd);

        if (eErr == FRAME_OK && unCmd != 0xFFFF) {
            eErr = commandHandler(auchRecvData, &stMsgId, iFrameSize, auchCmdResult, &iSendSize);

            if (eErr == FRAME_OK && iSendSize > 0) {
                eErr = makeResFrame(unCmd, &stMsgId, auchCmdResult, auchSendData);

                if (eErr == FRAME_OK) {
                    if (bufferevent_write(pstBufferEvent, auchSendData, iSendSize) < 0) {
                        fprintf(stderr, "[UDS Server] bufferevent_write() failed\n");
                    } else {
                        fprintf(stderr,
                                "[UDS] CMD=0x%04X TX=%d bytes\n",
                                unCmd, iSendSize);
                    }
                }
            }
        }

        fprintf(stderr, "[FRAME_STATUS] %s\n", frameErrToStr(eErr));
    }
}



/* ========================================================================== */
/* Event Callback Implementation                                               */
/* ========================================================================== */
static void appEventCb(struct bufferevent* pstBufferEvent,
                    short nEvents,
                    void* pvData)
{
    (void)pstBufferEvent;
    SOCK_CONTEXT* pstSockCtx = (SOCK_CONTEXT*)pvData;

    if (nEvents & BEV_EVENT_EOF)
        fprintf(stderr, "[UDS Server] Client disconnected (fd=%d)\n",
                pstSockCtx ? bufferevent_getfd(pstSockCtx->pstBufferEvent) : -1);

    if (nEvents & BEV_EVENT_ERROR)
        fprintf(stderr, "[UDS Server] Connection error\n");
}



/* ========================================================================== */
/* Signal Handler                                                             */
/* ========================================================================== */
static void signalCb(evutil_socket_t sig, short ev, void* pvData)
{
    (void)sig;
    (void)ev;

    EVENT_CONTEXT* pstEventCtx = (EVENT_CONTEXT*)pvData;
    if (pstEventCtx && pstEventCtx->pstEventBase)
        event_base_loopexit(pstEventCtx->pstEventBase, NULL);
}



/* ========================================================================== */
/* Main Entry                                                                 */
/* ========================================================================== */
int run(void)
{
    EVENT_CONTEXT stEventCtx;
    initEventContext(&stEventCtx, ROLE_SERVER, 1);

    unlink("/tmp/uds1.sock");

    stEventCtx.iSockFd = netUdsCreateServer("/tmp/uds1.sock");
    if (stEventCtx.iSockFd < 0) {
        fprintf(stderr, "[ERROR] Failed to create UDS server socket\n");
        return EXIT_FAILURE;
    }

    stEventCtx.pstEventBase = event_base_new();
    if (!stEventCtx.pstEventBase) {
        fprintf(stderr, "event_base_new failed\n");
        netClose(stEventCtx.iSockFd);
        return EXIT_FAILURE;
    }

    stEventCtx.stHandler.pfReadCb  = appReadCb;
    stEventCtx.stHandler.pfWriteCb = NULL;
    stEventCtx.stHandler.pfEventCb = appEventCb;

    setupServerAcceptEvent(&stEventCtx);

    signal(SIGPIPE, SIG_IGN);

    stEventCtx.pstSignalEvent =
        evsignal_new(stEventCtx.pstEventBase, SIGINT, signalCb, &stEventCtx);

    if (!stEventCtx.pstSignalEvent || event_add(stEventCtx.pstSignalEvent, NULL) < 0) {
        fprintf(stderr, "[ERROR] Could not register SIGINT event\n");
        return 1;
    }

    printf("[UDS Server] Listening on /tmp/uds1.sock\n");

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