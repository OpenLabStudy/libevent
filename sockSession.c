#include "sockSession.h"
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
    pstEventCtx->pstSockCtx = (SOCK_CONTEXT*)calloc(1, sizeof(SOCK_CONTEXT));
    if (!pstEventCtx->pstSockCtx) { 
        bufferevent_free(pstBufferEvent);
        return; 
    }
    pstEventCtx->pstSockCtx->uchIsRespone = 0x01;

    struct sockaddr_in *sin = (struct sockaddr_in*)pstSockAddr;
    inet_ntop(pstSockAddr->sa_family, &(sin->sin_addr), pstEventCtx->pstSockCtx->achSockAddr, sizeof(pstEventCtx->pstSockCtx->achSockAddr));

    pstEventCtx->pstSockCtx->pstBufferEvent = (void*)pstBufferEvent;

    bufferevent_setcb(pstBufferEvent, tcpReadCb, NULL, tcpEventCb, pstEventCtx);
    bufferevent_enable(pstBufferEvent, EV_READ|EV_WRITE);
    bufferevent_setwatermark(pstBufferEvent, EV_READ, sizeof(FRAME_HEADER), READ_HIGH_WM);

    printf("Accepted TCP client (iSockFd=%d)\n", iSockFd);
}

/* === Libevent callbacks === */
void tcpReadCb(struct bufferevent *pstBufferEvent, void *pvData)
{
    EVENT_CONTEXT *pstEventCtx = (EVENT_CONTEXT *)pvData;
    struct evbuffer *pstEvBuffer = bufferevent_get_input(pstBufferEvent);
    MSG_ID stMsgId;
    stMsgId.uchSrcId = pstEventCtx->pstSockCtx->uchSrcId;
    stMsgId.uchDstId = pstEventCtx->pstSockCtx->uchDstId;
    for (;;) {
        int r = responseFrame(pstEvBuffer, pstEventCtx->pstSockCtx->pstBufferEvent, &stMsgId, pstEventCtx->pstSockCtx->uchIsRespone);
        if(r == 1){
            break;
        }
        if (r == 0) 
            break;
        if (r < 0) { 
            tcpCloseAndFree(pvData);
            return; 
        }        
    }
}

void tcpEventCb(struct bufferevent *bev, short nEvents, void *pvData) 
{
    EVENT_CONTEXT *pstEventCtx = (EVENT_CONTEXT *)pvData;
    (void)bev;
    if (nEvents & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
        tcpCloseAndFree(pstEventCtx->pstSockCtx);
    }
}

void tcpCloseAndFree(void *pvData)
{
    EVENT_CONTEXT *pstEventCtx = (EVENT_CONTEXT *)pvData;
    if (!pstEventCtx->pstSockCtx)
        return;
        
    if (pstEventCtx->pstSockCtx->pstBufferEvent){
        bufferevent_free(pstEventCtx->pstSockCtx->pstBufferEvent);
        pstEventCtx->pstSockCtx->pstBufferEvent = NULL;
    }
    free(pstEventCtx->pstSockCtx);
    pstEventCtx->pstSockCtx = NULL;
}