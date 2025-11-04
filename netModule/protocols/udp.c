#include "udp.h"
#include "../core/frame.h"
#include "../core/icdCommand.h"
#include "../core/netUtil.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

/* ================================================================
 * 공통 초기화 함수
 * ================================================================ */
void udpInit(UDP_CTX *pstUdpCtx, struct event_base *pstEventBase,
             unsigned char uchMyId, NET_MODE eMode)
{
    netBaseInit(&pstUdpCtx->stNetBase, pstEventBase, uchMyId, eMode);
    pstUdpCtx->pstRecvEvent = NULL;
}


/* === Libevent callbacks === */
void readCallback(struct bufferevent *pstBufferEvent, void *pvData)
{
    UDP_CTX *pstUdpCtx    = (UDP_CTX *)pvData;
    struct evbuffer *pstEvBuffer = bufferevent_get_input(pstBufferEvent);
    MSG_ID stMsgId;
    stMsgId.uchSrcId = pstUdpCtx->stNetBase.uchMyId;
    stMsgId.uchDstId = pstUdpCtx->stNetBase.uchDstId;
    fprintf(stderr,"MY ID : %d, Dest ID : %d\n", 
        pstUdpCtx->stNetBase.uchMyId, pstUdpCtx->stNetBase.uchDstId);
    //TODO 접속한 클라이언트의 ID 저장이 필요
    for (;;) {
        int iRetVal = responseFrame(pstEvBuffer, 
            pstUdpCtx->pstBufferEvent, &stMsgId, pstUdpCtx->stNetBase);
        if(iRetVal == 1){
            break;
        }
        if (iRetVal == 0) 
            break;
        if (iRetVal < 0) { 
            closeAndFree(pvData);
            return; 
        }        
    }
}

void eventCallback(struct bufferevent *pstBufferEvent, short nEvents, void *pvData) 
{    
    (void)pvData;
    (void)pstBufferEvent;
    if (nEvents & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
        closeAndFree(pvData);
    }
}


/* ================================================================
 * UDP 서버 시작
 * ================================================================ */
int udpServerStart(UDP_CTX *pstUdpCtx, uint16_t unPort)
{
    pstUdpCtx->stNetBase.iSockFd = createUdpServer(unPort);
    if (pstUdpCtx->stNetBase.iSockFd < 0) {
        perror("[UDP SERVER] createUdpServer failed");
        return -1;
    }
    
    pstUdpCtx->pstBufferEvent = bufferevent_socket_new(
        pstUdpCtx->stNetBase.stCoreCtx.pstEventBase, 
        pstUdpCtx->stNetBase.iSockFd, 
        BEV_OPT_CLOSE_ON_FREE);
    if (!pstUdpCtx->pstBufferEvent) { 
        fprintf(stderr, "bufferevent_socket_new failed\n");
        return 1;
    }
    bufferevent_setcb(pstUdpCtx->pstBufferEvent, 
        readCallback, 
        NULL, 
        eventCallback, 
        pstUdpCtx);
    bufferevent_enable(pstUdpCtx->pstBufferEvent, EV_READ|EV_WRITE);
    bufferevent_setwatermark(pstUdpCtx->pstBufferEvent, 
        EV_READ, 
        sizeof(FRAME_HEADER), 
        READ_HIGH_WM);  

    printf("[UDP SERVER] Listening on port %d\n", unPort);
    return 0;
}

/* ================================================================
 * UDP 서버 정리
 * ================================================================ */
void udpServerStop(UDP_CTX *pstUdpCtx)
{
    if (pstUdpCtx->pstBufferEvent) {
        event_free(pstUdpCtx->pstBufferEvent);
        pstUdpCtx->pstBufferEvent = NULL;
    }

    if (pstUdpCtx->stNetBase.iSockFd >= 0) {
        close(pstUdpCtx->stNetBase.iSockFd);
        pstUdpCtx->stNetBase.iSockFd = -1;
    }

    printf("[UDP SERVER] Stopped.\n");
}

/* ================================================================
 * UDP 클라이언트 시작
 * ================================================================ */
int udpClientStart(UDP_CTX *pstUdpCtx, uint16_t unBindPort)
{
    pstUdpCtx->stNetBase.iSockFd = createUdpClient(unBindPort);
    if (pstUdpCtx->stNetBase.iSockFd < 0) {
        perror("[UDP CLIENT] createUdpClient failed");
        return -1;
    }

    pstUdpCtx->pstBufferEvent = bufferevent_socket_new(
        pstUdpCtx->stNetBase.stCoreCtx.pstEventBase, 
        pstUdpCtx->stNetBase.iSockFd, 
        BEV_OPT_CLOSE_ON_FREE);
    if (!pstUdpCtx->pstBufferEvent) { 
        fprintf(stderr, "bufferevent_socket_new failed\n");
        return 1;
    }
    bufferevent_setcb(pstUdpCtx->pstBufferEvent, 
        readCallback, 
        NULL, 
        eventCallback, 
        pstUdpCtx);
    bufferevent_enable(pstUdpCtx->pstBufferEvent, EV_READ|EV_WRITE);
    bufferevent_setwatermark(pstUdpCtx->pstBufferEvent, 
        EV_READ, 
        sizeof(FRAME_HEADER), 
        READ_HIGH_WM);  
    printf("[UDP CLIENT] Started (bind port=%d)\n", unBindPort);
    return 0;
}

/* ================================================================
 * UDP 클라이언트 종료
 * ================================================================ */
void udpClientStop(UDP_CTX *pstUdpCtx)
{
    if (pstUdpCtx->pstBufferEvent) {
        event_free(pstUdpCtx->pstBufferEvent);
        pstUdpCtx->pstBufferEvent = NULL;
    }

    if (pstUdpCtx->stNetBase.iSockFd >= 0) {
        close(pstUdpCtx->stNetBase.iSockFd);
        pstUdpCtx->stNetBase.iSockFd = -1;
    }

    printf("[UDP CLIENT] Stopped.\n");
}
