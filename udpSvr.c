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
#include "udpSvr.h" 

static void ioChannelHandleEvent(int iFd, short nEvent, void* pvData)
{
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    IO_EVENT_TYPE eEventType = pstIoChannel->ePendingLogicEvent;
    unsigned char auchRecvBuffer[2048];    
    unsigned char auchCmdResult[1000];
    unsigned char auchSendBuf[1024];
    unsigned short unCmd = 0;
    FRAME_ERR eErr;
    int iSendLen = 0;
    
    switch (eEventType) {
    case IO_EVT_RX_DATA:
        while (1) {
            size_t tRecvLen = evbuffer_get_length(pstIoChannel->pstReadBuffer);
            if (tRecvLen < FRAME_HEADER_MIN_SIZE)
                break;

            if (tRecvLen > sizeof(auchRecvBuffer))
                tRecvLen = sizeof(auchRecvBuffer);

            int iCopyLen = evbuffer_copyout(pstIoChannel->pstReadBuffer, auchRecvBuffer, tRecvLen);
            int iFrameSize = getFrameSize(auchRecvBuffer);
            if (iFrameSize <= 0) {
                evbuffer_drain(pstIoChannel->pstReadBuffer, 1);
                continue;
            }

            if (iCopyLen < iFrameSize)
                break;

            evbuffer_drain(pstIoChannel->pstReadBuffer, iFrameSize);
            MSG_ID stMsgId = { UDP_SVR_ID, UDP_CLN_ID };

            /* === 헤더 및 명령 추출 === */
            eErr = requestFrame(auchRecvBuffer, &stMsgId, iFrameSize, &unCmd);
            if (eErr != FRAME_OK) {
                fprintf(stderr, "[UDP-SVR] requestFrame ERR: %s\n", frameErrToStr(eErr));
                continue;
            }

            /* === 명령 처리 === */
            eErr = commandHandler(auchRecvBuffer, &stMsgId, iFrameSize, auchCmdResult, &iSendLen);
            if (eErr != FRAME_OK || iSendLen <= 0)
                continue;

            /* === 응답 프레임 생성 === */
            eErr = makeResFrame(unCmd, &stMsgId, auchCmdResult, auchSendBuf);
            if (eErr != FRAME_OK)
                continue;

            fprintf(stderr, "[UDP-SVR] Send CMD=%04X, size=%d\n", unCmd, iSendLen);
            evbuffer_add(pstIoChannel->pstWriteBuffer, auchSendBuf, iSendLen);
            event_add(pstIoChannel->pstWriteEvent, NULL); 
        }        
        break;

    case IO_EVT_CHANNEL_CLOSED:
        printf("[UDP-SVR] channel closed fd=%d\n", pstIoChannel->iFd);
        event_active(pstIoChannel->pstShutdownEvent, 0, 0);
        break;

    case IO_EVT_ERROR:
        printf("[UDP-SVR] channel error fd=%d\n", pstIoChannel->iFd);
        event_active(pstIoChannel->pstShutdownEvent, 0, 0);
        break;

    default:
        break;
    }
    pstIoChannel->ePendingLogicEvent = IO_EVENT_NONE;
}

/* ============================================================
* SIGINT 콜백
* ============================================================ */
static void signalCb(evutil_socket_t sig, short events, void* pvArg)
{
    EVENT_ENGINE* pstEventEngine = (EVENT_ENGINE *)pvArg;

    fprintf(stderr,"\n[UDP-SVR] SIGINT → shutdown\n");
    if(pstEventEngine->pstEventBase)
        event_base_loopexit(pstEventEngine->pstEventBase, NULL);
}


/* ========================================================================== */
/* Entry Point                                                                */
/* ========================================================================== */
int run(void)
{
    EVENT_ENGINE   stEventEngine;
    struct event*   pstSignalEvent;
    struct event*   pstEventAccept;
    stEventEngine.pstEventBase = event_base_new();
    if (!stEventEngine.pstEventBase) {
        fprintf(stderr, "[UDP-SVR] event_base_new() failed\n");
        return EXIT_FAILURE;
    }

    /* Dispatcher 초기화 */
    eventEngineInit(&stEventEngine);
    int iSockFd = netUdpCreateServer(UDP_SERVER_PORT,
        CLIENT_IP, UDP_CLIENT_PORT);
    if (iSockFd < 0) {
        fprintf(stderr, "UDP Server socket creation failed.\n");
        return EXIT_FAILURE;
    }

    netSetNonblock(iSockFd);
    eventSourceCreateWithBev(
        &stEventEngine,
        iSockFd,
        TYPE_UDP,
        ROLE_REQUESTER,
        ioChannelHandleEvent
    );

    /* SIGINT 처리 등록 */
    pstSignalEvent = evsignal_new(stEventEngine.pstEventBase,
        SIGINT, signalCb, &stEventEngine);
    event_add(pstSignalEvent, NULL);

    printf("[UDP-SVR] Listening on %s:%d -> client %s:%d\n",
           SERVER_IP, UDP_SERVER_PORT,
           CLIENT_IP, UDP_CLIENT_PORT);

    /* 이벤트 루프 시작 */
    event_base_dispatch(stEventEngine.pstEventBase);
    if(pstSignalEvent){
        event_del(pstSignalEvent);
        event_free(pstSignalEvent);
        pstSignalEvent =  NULL;
    }

    if(pstEventAccept){
        event_del(pstEventAccept);
        event_free(pstEventAccept);
        pstEventAccept =  NULL;
    }

    /* 종료 처리 */
    eventEngineCleanup(&stEventEngine);    
    event_base_free(stEventEngine.pstEventBase);

    fprintf(stderr,"[UDP-SVR] Terminated.\n");
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
