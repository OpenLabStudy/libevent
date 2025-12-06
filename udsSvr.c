/**
 * @file udsSvr.c
 * @brief Libevent 기반 UDS(UNIX Domain Socket) 서버 Application Layer
 *
 * TCP 서버(tcpSvr.c)와 완전히 동일한 구조를 사용하되,
 * 소켓 생성만 AF_UNIX 기반(netUdsCreateServer)으로 변경한 버전이다.
 *
 * - EVENT_CONTEXT + SOCK_CONTEXT 구조 사용
 * - acceptCb, readCallbackWrapper, eventCallbackWrapper는 eventSession.c 사용
 * - appReadCb / appEventCb 는 Application 레이어 콜백
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include "eventSession.h"
#include "frame.h"
#include "icdCommand.h"
#include "netUds.h"
#include "netCore.h"

/* ========================================================================== */
/* Static Function Prototypes                                                 */
/* ========================================================================== */

/**
 * @brief Application-level Read Callback (UDS 서버)
 *
 * UDS 클라이언트로부터 수신된 데이터를 프레임 단위로 파싱한 뒤,
 * requestFrame() → commandHandler() → makeResFrame() 처리 흐름을 수행한다.
 *
 * @param pstBufferEvent bufferevent 핸들
 * @param pvData         SOCK_CONTEXT*
 */
static void appReadCb(struct bufferevent* pstBufferEvent, void* pvData);

/**
 * @brief Application-level Event Callback (UDS 서버)
 *
 * EOF/ERROR 등의 상태를 로그로 남긴다.
 * 실제 세션 해제는 eventSession 모듈의 closeAndFree() 에서 수행.
 *
 * @param pstBufferEvent bufferevent 핸들
 * @param nEvents        이벤트 플래그
 * @param pvData         SOCK_CONTEXT*
 */
static void appEventCb(struct bufferevent* pstBufferEvent,
                       short nEvents,
                       void* pvData);

/**
 * @brief SIGINT(CTRL+C) 처리 콜백
 *
 * @param sig   시그널 번호
 * @param ev    이벤트 플래그
 * @param pvData EVENT_CONTEXT*
 */
static void signalCb(evutil_socket_t sig, short ev, void* pvData);



/* ========================================================================== */
/* Application-Level Read Processing (UDS Server)                             */
/* ========================================================================== */

static void appReadCb(struct bufferevent* pstBufferEvent, void* pvData)
{
    SOCK_CONTEXT* pstSockCtx = (SOCK_CONTEXT*)pvData;
    if (!pstSockCtx || !pstSockCtx->pstBaseCtx)
        return;

    struct evbuffer* pstInputBuf = bufferevent_get_input(pstBufferEvent);

    MSG_ID          stMsgId;
    unsigned char   auRecvBuf[2048];
    unsigned char   auCmdResult[1024];
    unsigned char   auSendBuf[2048];
    unsigned short  unCmd      = 0;
    FRAME_ERR       eErr;
    int             iSendLen   = 0;

    while (1) {
        size_t tRecvLen = evbuffer_get_length(pstInputBuf);
        if (tRecvLen < FRAME_HEADER_MIN_SIZE)
            break;

        if (tRecvLen > sizeof(auRecvBuf))
            tRecvLen = sizeof(auRecvBuf);

        int iCopyLen = evbuffer_copyout(pstInputBuf, auRecvBuf, tRecvLen);
        if (iCopyLen <= 0)
            break;

        int iFrameSize = getFrameSize(auRecvBuf);
        if (iFrameSize <= 0) {
            /* 프레임 헤더 손상 → 1바이트씩 버리며 재동기화 */
            evbuffer_drain(pstInputBuf, 1);
            continue;
        }

        if (iCopyLen < iFrameSize)
            break;
        
        evbuffer_drain(pstInputBuf, iFrameSize);

        stMsgId.uchSrcId = pstSockCtx->uchSrcId;
        stMsgId.uchDstId = pstSockCtx->uchDstId;

        /* Step 1: 요청 프레임 파싱 */
        eErr = requestFrame(auRecvBuf, &stMsgId, iFrameSize, &unCmd);
        if (eErr != FRAME_OK || unCmd == 0xFFFF) {
            fprintf(stderr, "[UDS-Server] requestFrame() failed, err=%d\n", eErr);
            continue;
        }

        /* Step 2: 명령 처리 */
        eErr = commandHandler(auRecvBuf, &stMsgId,
                              iFrameSize, auCmdResult, &iSendLen);
        if (eErr != FRAME_OK || iSendLen <= 0) {
            continue;
        }

        /* Step 3: 응답 프레임 생성 */
        eErr = makeResFrame(unCmd, &stMsgId, auCmdResult, auSendBuf);
        if (eErr == FRAME_OK && iSendLen > 0) {            
            /* UDS 클라이언트로 응답 전송 */
            fprintf(stderr, "[UDS-Server] Send CMD=%04X, size=%d\n", unCmd, iSendLen);
            if (bufferevent_write(pstBufferEvent, auSendBuf, iSendLen) < 0) {
                fprintf(stderr, "[UDS-Server] bufferevent_write() failed\n");
                break;
            }
        }
    }
}



/* ========================================================================== */
/* Application-Level Event Callback (UDS Server)                              */
/* ========================================================================== */

static void appEventCb(struct bufferevent* pstBufferEvent,
                       short nEvents,
                       void* pvData)
{
    (void)pstBufferEvent;
    SOCK_CONTEXT* pstSockCtx = (SOCK_CONTEXT*)pvData;

    if (nEvents & BEV_EVENT_EOF) {
        fprintf(stderr, "[UDS-Server] Client disconnected (bev=%p)\n",
                (void*)pstBufferEvent);
    }
    if (nEvents & BEV_EVENT_ERROR) {
        fprintf(stderr, "[UDS-Server] Connection error\n");
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
        fprintf(stderr, "\n[UDS-Server] SIGINT received. Stopping event loop...\n");
        if (pstBaseCtx && pstBaseCtx->pstEventBase)
            event_base_loopexit(pstBaseCtx->pstEventBase, NULL);
    }
}



/* ========================================================================== */
/* Main Entry Point                                                           */
/* ========================================================================== */

int run(void)
{
    BASE_CONTEXT stBaseCtx;
    SERVER_CONTEXT stServerCtx;

    baseContextInit(&stBaseCtx, UDS_1_SVR_ID);
    serverContextInit(&stServerCtx, &stBaseCtx, ROLE_UDS_SERVER);

    stServerCtx.iListenFd = netUdsCreateServer(UDS_1_PATH);
    if (stServerCtx.iListenFd < 0) {
        fprintf(stderr, "[UDS-Server] netUdsCreateServer() failed\n");
        return EXIT_FAILURE;
    }

    /* 3) event_base 생성 */
    stBaseCtx.pstEventBase = event_base_new();
    if (!stBaseCtx.pstEventBase) {
        fprintf(stderr, "[UDS-Server] event_base_new() failed\n");
        close(stServerCtx.iListenFd);
        return EXIT_FAILURE;
    }

    /* 4) Application-level 콜백 등록 */
    stBaseCtx.stHandler.pfReadCb  = appReadCb;
    stBaseCtx.stHandler.pfWriteCb = NULL;
    stBaseCtx.stHandler.pfEventCb = appEventCb;

    /* 5) SIGINT 처리 이벤트 등록 */
    stBaseCtx.pstSignalEvent = evsignal_new(
        stBaseCtx.pstEventBase,
        SIGINT,
        signalCb,
        &stBaseCtx
    );
    if (!stBaseCtx.pstSignalEvent ||
        event_add(stBaseCtx.pstSignalEvent, NULL) < 0) {
        fprintf(stderr, "[UDS-Server] evsignal_new()/event_add() failed\n");
        close(stServerCtx.iListenFd);
        return EXIT_FAILURE;
    }

    /* 6) Accept 이벤트 등록 (eventSession.c 제공 함수) */
    setupServerAcceptEvent(&stServerCtx);
    fprintf(stderr, "[UDS-Server] Listening at %s\n", UDS_1_PATH);

    /* 7) 이벤트 루프 진입 */
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
    (void)argc; (void)argv;
    return run();
}
#endif
