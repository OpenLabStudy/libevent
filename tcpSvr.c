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
 * - Frame/Protocol Layer (`frame.*`)
 * 간의 계층 분리를 유지하면서도 코드 가독성과 테스트 용이성을 확보한다.
 */

#include "tcpSvr.h"
#include "eventSession.h"
#include "netTcp.h"
#include "frame.h"

#include <event2/buffer.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>


/* ========================================================================== */
/* Application-Level Callback Prototypes                                      */
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
 * @param nEvents        이벤트 비트 플래그
 * @param pvData         SOCK_CONTEXT 포인터
 */
static void appEventCb(struct bufferevent* pstBufferEvent,
                    short nEvents,
                    void* pvData);

/**
 * @brief SIGINT(CTRL+C) 처리 콜백
 *
 * @param sig UNIX 시그널 번호
 * @param ev  이벤트 플래그
 * @param pvData BASE_CONTEXT 포인터
 */
static void signalCb(evutil_socket_t sig, short ev, void* pvData);


/* ========================================================================== */
/* Application-Level Read Processing                                          */
/* ========================================================================== */

static void appReadCb(struct bufferevent* pstBufferEvent, void* pvData)
{
    SOCK_CONTEXT* pstSockCtx = (SOCK_CONTEXT*)pvData;
    if (!pstSockCtx || !pstSockCtx->pstBaseCtx)
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

        int iCopyLen   = evbuffer_copyout(pstInputBuffer, auRecvBuf, tRecvLen);
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
            fprintf(stderr, "[TCP-Server] requestFrame() failed, err=%d\n", eErr);
            continue;
        }

        /* === 명령 처리 === */
        eErr = commandHandler(auRecvBuf, &stMsgId, iFrameSize, auCmdResult, &iSendLen);
        if (eErr != FRAME_OK || iSendLen <= 0)
            continue;

        /* === 응답 프레임 생성 === */
        eErr = makeResFrame(unCmd, &stMsgId, auCmdResult, auSendBuf);
        if (eErr == FRAME_OK && iSendLen > 0) {            
            /* UDS 클라이언트로 응답 전송 */
            fprintf(stderr, "[TCP-Server] Send CMD=%04X, size=%d\n", unCmd, iSendLen);
            if (bufferevent_write(pstBufferEvent, auSendBuf, iSendLen) < 0) {
                fprintf(stderr, "[TCP-Server] bufferevent_write() failed\n");
                break;
            }
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
        fprintf(stderr, "[TCP-Server] Client disconnected\n");
    } else if (nEvents & BEV_EVENT_ERROR) {
        fprintf(stderr, "[TCP-Server] Client socket error\n");
    }

    /* 실제 close/free 는 eventSession 의 eventCallbackWrapper 에서 수행 */
    (void)pstSockCtx;
}


/* ========================================================================== */
/* Signal Handling                                                            */
/* ========================================================================== */

static void signalCb(evutil_socket_t sig, short ev, void* pvData)
{
    (void)sig;
    (void)ev;

    BASE_CONTEXT* pstBaseCtx = (BASE_CONTEXT*)pvData;
    if (sig == SIGINT) {
        fprintf(stderr, "\n[TCP-Server] SIGINT received. Stopping event loop...\n");
        if (pstBaseCtx && pstBaseCtx->pstEventBase)
            event_base_loopexit(pstBaseCtx->pstEventBase, NULL);
    }
}


/* ========================================================================== */
/* Server Main Entry                                                          */
/* ========================================================================== */
int run(void)
{
    BASE_CONTEXT   stBaseCtx;
    SERVER_CONTEXT stServerCtx;

    baseContextInit(&stBaseCtx, 1);
    serverContextInit(&stServerCtx, &stBaseCtx, ROLE_TCP_SERVER);

    stServerCtx.iListenFd = netTcpCreateServer(SERVER_PORT);
    if (stServerCtx.iListenFd < 0) {
        fprintf(stderr, "[TCP-Server] Failed to create TCP server socket\n");
        return EXIT_FAILURE;
    }

    stBaseCtx.pstEventBase = event_base_new();
    if (!stBaseCtx.pstEventBase) {
        fprintf(stderr, "[TCP-Server] event_base_new() failed\n");
        close(stServerCtx.iListenFd);
        return EXIT_FAILURE;
    }

    /* Application Handler 주입 */
    stBaseCtx.stHandler.pfReadCb  = appReadCb;
    stBaseCtx.stHandler.pfWriteCb = NULL;
    stBaseCtx.stHandler.pfEventCb = appEventCb;

    /* SIGINT 처리 */
    signal(SIGPIPE, SIG_IGN);
    stBaseCtx.pstSignalEvent = evsignal_new(stBaseCtx.pstEventBase,
                                            SIGINT,
                                            signalCb,
                                            &stBaseCtx);
    if (!stBaseCtx.pstSignalEvent ||
        event_add(stBaseCtx.pstSignalEvent, NULL) < 0) {
        fprintf(stderr, "[TCP-Server] Could not create/add SIGINT event\n");
        close(stServerCtx.iListenFd);
        return EXIT_FAILURE;
    }

    setupServerAcceptEvent(&stServerCtx);
    fprintf(stderr, "[TCP-Server] TCP Server Listening on port %d\n", SERVER_PORT);

    /* 이벤트 루프 진입 */
    event_base_dispatch(stBaseCtx.pstEventBase);

    /* === 종료 처리 === */
    /* 모든 클라이언트 세션 해제 */
    SOCK_CONTEXT* pstCur = stServerCtx.pstClientList;
    while (pstCur) {
        SOCK_CONTEXT* pstNext = pstCur->pstNextSockCtx;
        closeAndFree(pstCur);
        pstCur = pstNext;
    }
    stServerCtx.pstClientList = NULL;
    stServerCtx.iClientCount  = 0;

    /* accept 이벤트 해제 */
    if (stServerCtx.pstAcceptEvent) {
        event_free(stServerCtx.pstAcceptEvent);
        stServerCtx.pstAcceptEvent = NULL;
    }

    /* signal 이벤트 해제 */
    if (stBaseCtx.pstSignalEvent) {
        event_free(stBaseCtx.pstSignalEvent);
        stBaseCtx.pstSignalEvent = NULL;
    }

    /* listen 소켓 닫기 */
    if (stServerCtx.iListenFd >= 0) {
        netClose(stServerCtx.iListenFd);
        stServerCtx.iListenFd = -1;
    }

    /* event_base 해제 */
    if (stBaseCtx.pstEventBase) {
        event_base_free(stBaseCtx.pstEventBase);
        stBaseCtx.pstEventBase = NULL;
    }

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
 