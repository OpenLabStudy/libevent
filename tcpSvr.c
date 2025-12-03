/**
 * @file tcpSvr.c
 * @brief Libevent 기반 TCP 서버의 최상위 Application 레이어
 *
 * 이 파일은 TCP 서버 환경에서 이벤트 기반 I/O 처리를 수행하며,
 * 수신된 RAW 데이터 스트림을 프레임 파서(frame.c)에 전달하여 처리하고
 * 필요한 경우 응답 프레임을 생성하여 클라이언트에게 다시 전송한다.
 *
 * ### 주요 기능
 * - TCP 서버 소켓 생성 (`netTcpCreateServer`)
 * - 수신 데이터 → `requestFrame()` → `commandHandler()` → `makeResFrame()` 흐름 처리
 * - SIGINT 처리 및 이벤트 루프 종료
 *
 * ### 설계 목적
 * - Network Layer (`netTcp.*`)
 * - Event Session Layer (`eventSession.*`)
 * - Protocol & Frame Layer (`frame.*`)
 *
 * 위 세 계층을 연결하는 실제 Application 코드 역할을 수행한다.
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include "tcpSvr.h"

#define SERVER_PORT 5000


/* ========================================================================== */
/* Static Function Prototypes                                                 */
/* ========================================================================== */

/**
 * @brief Application Level Read Callback
 *
 * bufferevent 이벤트 기반으로 TCP 스트림에서 받은 데이터를
 * 프레임 단위로 구분하여 처리하고, 명령어에 따라 응답 프레임을 생성해 전송한다.
 *
 * @param pstBufferEvent libevent bufferevent 객체
 * @param pvData         SOCK_CONTEXT 포인터 (클라이언트별 상태 정보)
 */
static void appReadCb(struct bufferevent* pstBufferEvent, void* pvData);

/**
 * @brief Application Level Event Callback
 *
 * 연결 종료(EVF_EVENT_EOF), 오류(BEV_EVENT_ERROR) 상태를 출력 로그로 남긴다.
 * 세션 종료 처리는 eventSession 모듈이 수행한다.
 *
 * @param pstBufferEvent bufferevent 핸들
 * @param nEvents        libevent 이벤트 플래그
 * @param pvData         SOCK_CONTEXT
 */
static void appEventCb(struct bufferevent* pstBufferEvent,
                    short nEvents,
                    void* pvData);

/**
 * @brief SIGINT(CTRL+C) 처리 콜백
 *
 * @param sig UNIX 시그널 번호
 * @param ev  이벤트 플래그
 * @param pvData EVENT_CONTEXT 포인터
 */
static void signalCb(evutil_socket_t sig, short ev, void* pvData);



/* ========================================================================== */
/* Application-Level Read Processing                                          */
/* ========================================================================== */
static void appReadCb(struct bufferevent* pstBufferEvent, void* pvData)
{
    SOCK_CONTEXT* pstSockCtx = (SOCK_CONTEXT*)pvData;
    if (!pstSockCtx || !pstSockCtx->pstEventCtx)
        return;

    MSG_ID stMsgId;
    unsigned char auRecvBuf[2048];
    unsigned char auCmdResult[1000];
    unsigned char auSendBuf[1024];
    unsigned short unCmd = 0;
    FRAME_ERR eErr;
    int iSendLen = 0;

    struct evbuffer* pstInputBuffer = bufferevent_get_input(pstBufferEvent);

    while (1) {
        size_t tRecvLen = evbuffer_get_length(pstInputBuffer);
        if (tRecvLen < FRAME_HEADER_MIN_SIZE)
            break;

        if (tRecvLen > sizeof(auRecvBuf))
            tRecvLen = sizeof(auRecvBuf);

        int iCopyLen = evbuffer_copyout(pstInputBuffer, auRecvBuf, tRecvLen);
        int iFrameSize = getFrameSize(auRecvBuf);

        if (iFrameSize <= 0) {
            evbuffer_drain(pstInputBuffer, 1);
            continue;
        }

        if (iCopyLen < iFrameSize)
            break;

        evbuffer_drain(pstInputBuffer, iFrameSize);

        stMsgId.uchSrcId = pstSockCtx->uchSrcId;
        stMsgId.uchDstId = pstSockCtx->uchDstId;

        /* === 헤더 및 명령 추출 === */
        eErr = requestFrame(auRecvBuf, &stMsgId, iFrameSize, &unCmd);
        if (eErr != FRAME_OK) {
            fprintf(stderr, "[APP] requestFrame ERR: %s\n", frameErrToStr(eErr));
            continue;
        }

        /* === 명령 처리 === */
        eErr = commandHandler(auRecvBuf, &stMsgId, iFrameSize, auCmdResult, &iSendLen);
        if (eErr != FRAME_OK || iSendLen <= 0)
            continue;

        /* === 응답 프레임 생성 === */
        eErr = makeResFrame(unCmd, &stMsgId, auCmdResult, auSendBuf);
        if (eErr != FRAME_OK)
            continue;

        fprintf(stderr, "[APP] Send CMD=%04X, size=%d\n", unCmd, iSendLen);

        if (bufferevent_write(pstBufferEvent, auSendBuf, iSendLen) < 0) {
            fprintf(stderr, "[APP] bufferevent_write() failed\n");
        }
    }
}



/* ========================================================================== */
/* Application-Level Event Callback                                           */
/* ========================================================================== */
static void appEventCb(struct bufferevent* pstBufferEvent,
                    short nEvents,
                    void* pvData)
{
    (void)pstBufferEvent;
    SOCK_CONTEXT* pstSockCtx = (SOCK_CONTEXT*)pvData;

    if (nEvents & BEV_EVENT_EOF) {
        fprintf(stderr, "[APP] Client disconnected (fd=%d)\n",
                pstSockCtx ? bufferevent_getfd(pstSockCtx->pstBufferEvent) : -1);
    }
    if (nEvents & BEV_EVENT_ERROR) {
        fprintf(stderr, "[APP] Connection error\n");
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

    if (pstEventCtx && pstEventCtx->pstEventBase)
        event_base_loopexit(pstEventCtx->pstEventBase, NULL);
}



/* ========================================================================== */
/* Server Main Entry                                                          */
/* ========================================================================== */
int run(void)
{
    EVENT_CONTEXT stEventCtx;
    initEventContext(&stEventCtx, ROLE_TCP_SERVER, 1);

    stEventCtx.iSockFd = netTcpCreateServer(SERVER_PORT);
    if (stEventCtx.iSockFd < 0) {
        fprintf(stderr, "[MAIN] Failed to create TCP server socket\n");
        return EXIT_FAILURE;
    }

    stEventCtx.pstEventBase = event_base_new();
    if (!stEventCtx.pstEventBase) {
        fprintf(stderr, "[MAIN] event_base_new() failed\n");
        netClose(stEventCtx.iSockFd);
        return EXIT_FAILURE;
    }

    /* APP 콜백 등록 */
    stEventCtx.stHandler.pfReadCb  = appReadCb;
    stEventCtx.stHandler.pfWriteCb = NULL;
    stEventCtx.stHandler.pfEventCb = appEventCb;

    setupServerAcceptEvent(&stEventCtx);

    /* SIGINT 처리 */
    signal(SIGPIPE, SIG_IGN);
    stEventCtx.pstSignalEvent = evsignal_new(stEventCtx.pstEventBase, SIGINT, signalCb, &stEventCtx);
    if (!stEventCtx.pstSignalEvent || event_add(stEventCtx.pstSignalEvent, NULL) < 0) {
        fprintf(stderr, "[MAIN] Could not create/add SIGINT event\n");
        return EXIT_FAILURE;
    }

    fprintf(stderr, "[MAIN] TCP Server Listening on port %d\n", SERVER_PORT);

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
 