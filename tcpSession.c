#include "tcpSession.h"
#include "frame.h"
#include <stdio.h>
#include <stdlib.h>

void tcpListenerCb(struct evconnlistener *listener, evutil_socket_t iSockFd,
                        struct sockaddr *pstSockAddr, int iSockLen, void *pvData)
{
    (void)listener;
    (void)pstSockAddr;
    (void)iSockLen;
    EVENT_CONTEXT* pstEventCtx = (EVENT_CONTEXT*)pvData;
    struct event_base *pstEventBase = pstEventCtx->pstEventBase;
    struct bufferevent *pstBufferEvent = bufferevent_socket_new(pstEventBase, iSockFd, BEV_OPT_CLOSE_ON_FREE);
    if (!pstBufferEvent) 
        return;
    pstEventCtx->pstTcpCtx = (TCP_CONTEXT*)calloc(1, sizeof(TCP_CONTEXT));
    if (!pstEventCtx->pstTcpCtx) { 
        bufferevent_free(pstBufferEvent);
        return; 
    }
    pstEventCtx->pstTcpCtx->uchIsRespone = 0x01;

    struct sockaddr_in *sin = (struct sockaddr_in*)pstSockAddr;
    inet_ntop(pstSockAddr->sa_family, &(sin->sin_addr), pstEventCtx->pstTcpCtx->achTcpIpInfo, sizeof(pstEventCtx->pstTcpCtx->achTcpIpInfo));

    pstEventCtx->pstTcpCtx->pstBufferEvent = (void*)pstBufferEvent;

    bufferevent_setcb(pstBufferEvent, tcpReadCb, NULL, tcpEventCb, pstEventCtx->pstTcpCtx);
    bufferevent_enable(pstBufferEvent, EV_READ|EV_WRITE);
    bufferevent_setwatermark(pstBufferEvent, EV_READ, sizeof(FRAME_HEADER), READ_HIGH_WM);

    printf("Accepted TCP client (iSockFd=%d)\n", iSockFd);
}

/* === Libevent callbacks === */
void tcpReadCb(struct bufferevent *pstBufferEvent, void *pvData)
{
    TCP_CONTEXT* pstTcpCtx = (TCP_CONTEXT*)pvData;    
    struct evbuffer *pstEvBuffer = bufferevent_get_input(pstBufferEvent);
    MSG_ID stMsgId;
    stMsgId.uchSrcId = 0x01;
    stMsgId.uchDstId = 0x00; 
    for (;;) {
        int r = responseFrame(pstEvBuffer, pstTcpCtx->pstBufferEvent, &stMsgId, pstTcpCtx->uchIsRespone);
        if(r == 1){
            break;
        }
        if (r == 0) 
            break;
        if (r < 0) { 
            tcpCloseAndFree(pstTcpCtx);
            return; 
        }        
    }
}

void tcpEventCb(struct bufferevent *bev, short nEvents, void *pvData) 
{
    TCP_CONTEXT* pstTcpCtx = (TCP_CONTEXT*)pvData;
    (void)bev;
    if (nEvents & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
        tcpCloseAndFree(pstTcpCtx);
    }
}

void tcpCloseAndFree(TCP_CONTEXT* pstTcpCtx)
{
    if (!pstTcpCtx)
        return;    
        
    if (pstTcpCtx->pstBufferEvent){
        bufferevent_free(pstTcpCtx->pstBufferEvent);
        pstTcpCtx->pstBufferEvent = NULL;
    }
    free(pstTcpCtx);
    pstTcpCtx = NULL;
}