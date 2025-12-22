/**
 * @file udpCln.c
 * @brief Libevent 기반 UDP Client Application (bufferevent 사용 버전)
 *
 * UDP 기반으로 서버와 비동기 통신을 수행하며 STDIN 입력을 통해
 * 요청(Request Frame)을 생성하고 서버 응답(Response Frame)을 처리한다.
 *
 * ### 변경 사항
 * - 기존: event_new() + read()/write()
 * - 변경: bufferevent_socket_new() + evbuffer 기반 Read/Write
 * - EVENT_CONTEXT + SOCK_CONTEXT 구조 재사용 (TCP Client와 동일 스타일)
 *
 * ### 동작 순서
 * 1. stdinReadCb(): 사용자 입력 → 요청 프레임 생성 → bufferevent_write()
 * 2. appReadCb(): 서버 응답 수신 → responseFrame()
 * 3. event_base_dispatch(): 이벤트 루프 기반 동작 유지
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include "eventEngine.h"
#include "icdCommand.h"
#include "udpSvr.h"



static void ioChannelHandleEvent(int iFd, short nEvent, void* pvData)
{
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    IO_EVENT_TYPE eEventType = pstIoChannel->ePendingLogicEvent;
    unsigned char auchRecvBuffer[2048];
    
    switch (eEventType) {
    case IO_EVT_RX_DATA:
        /* protocol / packet 처리 */        
        while (1) {
            size_t tRecvLen = evbuffer_get_length(pstIoChannel->pstReadBuffer);
            if (tRecvLen < FRAME_HEADER_MIN_SIZE)
                break;

            if (tRecvLen > sizeof(auchRecvBuffer))
                tRecvLen = sizeof(auchRecvBuffer);

            int iCopyLen = evbuffer_copyout(pstIoChannel->pstReadBuffer, auchRecvBuffer, tRecvLen);
            MSG_ID stMsgId = { UDP_CLN_ID, UDP_SVR_ID };
            responseFrame(auchRecvBuffer, &stMsgId, iCopyLen);

            evbuffer_drain(pstIoChannel->pstReadBuffer, iCopyLen);
        }
        break;

    case IO_EVT_CHANNEL_CLOSED:
        printf("[TCP-CLI] channel closed fd=%d\n", pstIoChannel->iFd);
        event_active(pstIoChannel->pstShutdownEvent, 0, 0);
        break;

    case IO_EVT_ERROR:
        printf("[TCP-CLI] channel error fd=%d\n", pstIoChannel->iFd);
        event_active(pstIoChannel->pstShutdownEvent, 0, 0);
        break;

    default:
        break;
    }
    pstIoChannel->ePendingLogicEvent = IO_EVENT_NONE;
}

/* ============================================================
* stdin 이벤트 콜백
* ============================================================ */
static void stdinReadCb(int iFd, short nEvents, void* pvData)
{
    (void)nEvents;

    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;

    char achInput[1024];
    unsigned char auSendBuf[1024];
    int iSendLen = 0;
    FRAME_ERR eErr;

    if (!fgets(achInput, sizeof(achInput), stdin)) {
        pstIoChannel->ePendingLogicEvent = IO_EVT_CHANNEL_CLOSED;
        event_active(pstIoChannel->pstLogicEvent, 0, 0);
        return;
    }

    achInput[strcspn(achInput, "\n")] = '\0';
    MSG_ID stMsgId = { UDP_CLN_ID, UDP_SVR_ID };

    if (!strcmp(achInput, "keepalive")) {
        fprintf(stderr,"[TCP-CLI] REQ_KEEP_ALIVE\n");
        eErr = makeReqFrame(CMD_KEEP_ALIVE, &stMsgId, auSendBuf, &iSendLen);

    } else if (!strcmp(achInput, "ibit")) {
        fprintf(stderr,"[TCP-CLI] REQ_IBIT\n");
        eErr = makeReqFrame(CMD_IBIT, &stMsgId, auSendBuf, &iSendLen);

    } else if (!strcmp(achInput, "quit") || !strcmp(achInput, "exit")) {
        pstIoChannel->ePendingLogicEvent = IO_EVT_CHANNEL_CLOSED;
        event_active(pstIoChannel->pstShutdownEvent, 0, 0);
        return;
    } else {
        fprintf(stderr, "Available commands:\n  keepalive\n  ibit\n  quit\n");
        return;
    }

    if (eErr == FRAME_OK && iSendLen > 0) {
        evbuffer_add(pstIoChannel->pstWriteBuffer, auSendBuf, iSendLen);
        event_add(pstIoChannel->pstWriteEvent, NULL); 
    } 
}

/* ============================================================
* SIGINT 콜백
* ============================================================ */
static void signalCb(evutil_socket_t sig, short events, void* pvArg)
{
    EVENT_ENGINE* pstEventEngine = (EVENT_ENGINE *)pvArg;

    fprintf(stderr,"\n[TCP-CLI] SIGINT → shutdown\n");
    if(pstEventEngine->pstEventBase)
        event_base_loopexit(pstEventEngine->pstEventBase, NULL);
}

/* ========================================================================== */
/* Entry Point                                                                */
/* ========================================================================== */
int run(void)
{
    EVENT_ENGINE   stEventEngine;
    stEventEngine.pstEventBase = event_base_new();
    if (!stEventEngine.pstEventBase) {
        printf("[TCP-CLI] event_base_new failed\n");
        return -1;
    }
    eventEngineInit(&stEventEngine);

    /* === 1) UDP 클라이언트 소켓 생성 (connect()까지 수행) === */
    int iSockFd = netUdpCreateClient(SERVER_IP, 
        UDP_SERVER_PORT, UDP_CLIENT_PORT);
    if (iSockFd < 0) {
        fprintf(stderr, "[UDP Client] UDP socket create failed\n");
        return EXIT_FAILURE;
    }

    netSetNonblock(iSockFd);

    eventSourceCreateWithBev(
        &stEventEngine,
        iSockFd,
        TYPE_UDP,
        ROLE_WORKER,
        ioChannelHandleEvent
    );

    /* ------------------- */
    /* stdin 이벤트 등록   */
    /* ------------------- */
    struct event* evStdin = event_new(
        stEventEngine.pstEventBase,
        STDIN_FILENO,
        EV_READ | EV_PERSIST,
        stdinReadCb,
        stEventEngine.pstIoChannelList);
    if (!evStdin) {
        printf("[UDP-CLI] evStdin create failed\n");
        event_base_free(stEventEngine.pstEventBase);
        return -1;
    }
    event_add(evStdin, NULL);

    struct event   *pstSignalEvent;
    /* SIGINT 처리 등록 */
    pstSignalEvent = evsignal_new(stEventEngine.pstEventBase, 
        SIGINT, signalCb, &stEventEngine);
    event_add(pstSignalEvent, NULL);

    fprintf(stderr, "[UDP Client] Running %s:%d (local port %d)\n",
            SERVER_IP, UDP_SERVER_PORT, UDP_CLIENT_PORT);

    event_base_dispatch(stEventEngine.pstEventBase);

    if(evStdin){
        event_del(evStdin);
        event_free(evStdin);
        evStdin =  NULL;
    }
    if(pstSignalEvent){
        event_del(pstSignalEvent);
        event_free(pstSignalEvent);
        pstSignalEvent =  NULL;
    }
    
    eventEngineCleanup(&stEventEngine);
    event_base_free(stEventEngine.pstEventBase);

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
