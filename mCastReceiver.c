#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#include <unistd.h>
#include <fcntl.h>

#include <event2/event.h>
#include <event2/util.h>
#include <event2/buffer.h>

#include "frame.h"
#include "sockSession.h"

#define MCAST_IP        "239.255.0.1"
#define MCAST_PORT      5000
#define MCAST_ID        0


/* SIGINT 처리 */
static void signalCb(evutil_socket_t sig, short ev, void* pvData)
{
    (void)sig;
    (void)ev;
    EVENT_CONTEXT* pstEventCtx = (EVENT_CONTEXT*)pvData;
    event_base_loopexit(pstEventCtx->pstEventBase, NULL);
}

static void mcastEventCallback(struct bufferevent *pstBufferEvent, short nEvents, void *pvData) 
{    
    (void)pvData;
    (void)pstBufferEvent;
    if (nEvents & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
        fprintf(stderr,"### %s():%d ###\n", __func__, __LINE__);
        //todo multicastSockLeave();
    }
}
//todo return 처리 필요
static void mcastReadCallback(struct bufferevent *pstBufferEvent, void *pvData)
{
    (void)pvData;
    struct evbuffer *pstEvBuffer = bufferevent_get_input(pstBufferEvent);
    int iLength = evbuffer_get_length(pstEvBuffer);
    unsigned char *puchData;
    fprintf(stderr,"### %s():%d Recv Data Length is %d\n", __func__, __LINE__, iLength);
    if (iLength > 0) {
        puchData = (unsigned char *)malloc((size_t)iLength+1);
        memset(puchData, 0x0, iLength+1);
        if (!puchData){
            return;//FRAME_ERR_MEMORY_ALLOC_FAIL
        }
        if (evbuffer_remove(pstEvBuffer, puchData, (size_t)iLength) != iLength) {
            free(puchData); 
            return;//EV_REMOVE_SIZE_MISMATCH
        }
        puchData[iLength] = '\0';
        fprintf(stderr,"<<< len=%d msg=\"%s\"\n", iLength, puchData);  
    }    
}

// 멀티캐스트 가입 (uiIpAddr: 네트워크 바이트 오더 s_addr 값 사용)
int multicastSockJoin(int iMulticastSockFd, unsigned int uiIpAddr)
{
    struct ip_mreq stMulticastMreq;
    int iReturnFromSetsockOpt = 0;

    struct in_addr stGroupAddr;  
    struct in_addr stIfaceAddr;  
    stGroupAddr.s_addr  = uiIpAddr;
    stIfaceAddr.s_addr  = htonl(INADDR_ANY);

    fprintf(stderr, "Join Multicast: %s \n", inet_ntoa(stGroupAddr));

    memset(&stMulticastMreq, 0, sizeof(stMulticastMreq));
    stMulticastMreq.imr_multiaddr = stGroupAddr;
    stMulticastMreq.imr_interface = stIfaceAddr;

    iReturnFromSetsockOpt = setsockopt(iMulticastSockFd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                                       &stMulticastMreq, sizeof(stMulticastMreq));
    if (iReturnFromSetsockOpt < 0) {
        fprintf(stderr,"%s():%d fail...[%s]", __func__, __LINE__, strerror(errno));
        return -1;
    }
    return 0;
}

// 멀티캐스트 탈퇴
int multicastSockLeave(int iMulticastSockFd, unsigned int uiIpAddr)
{
    struct ip_mreq stMulticastMreq;

    struct in_addr stGroupAddr; 
    struct in_addr stIfaceAddr; 
    stGroupAddr.s_addr  = uiIpAddr;
    stIfaceAddr.s_addr  = htonl(INADDR_ANY);

    fprintf(stderr,"Leave Multicast: %s\n", inet_ntoa(stGroupAddr));

    memset(&stMulticastMreq, 0, sizeof(stMulticastMreq));
    stMulticastMreq.imr_multiaddr = stGroupAddr;
    stMulticastMreq.imr_interface = stIfaceAddr;

    if (setsockopt(iMulticastSockFd, IPPROTO_IP, IP_DROP_MEMBERSHIP,
                   &stMulticastMreq, sizeof(stMulticastMreq)) < 0) {
        fprintf(stderr,"%s():%d fail...[%s]", __func__, __LINE__, strerror(errno));
        return -1;
    }
    return 0;
}

int run(void)
{
    EVENT_CONTEXT stEventCtx;
    initEventContext(&stEventCtx, ROLE_CLIENT, MCAST_ID);

    stEventCtx.pstSockCtx = (SOCK_CONTEXT*)calloc(1, sizeof(SOCK_CONTEXT));
    if (!stEventCtx.pstSockCtx) { 
        event_base_free(stEventCtx.pstEventBase);
        return 0;
    }
    initSocketContext(stEventCtx.pstSockCtx, RESPONSE_DISABLED);
    stEventCtx.pstSockCtx->pstEventCtx = &stEventCtx;    
    
    stEventCtx.pstEventBase   = event_base_new();
    if (!stEventCtx.pstEventBase) {
        fprintf(stderr, "Could not initialize libevent!\n");
        return 1;
    }

    stEventCtx.iSockFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (stEventCtx.iSockFd < 0) {
        fprintf(stderr,"socket() fail...[%s]", strerror(errno));
        return -1;
    }
    /* Multicast 주소 준비 */
    struct in_addr stGroupAddr = {0};
    if (inet_pton(AF_INET, MCAST_IP, &stGroupAddr) != 1) {
        fprintf(stderr,"Invalid MULTICAST_IP: %s", MCAST_IP);
        return EXIT_FAILURE;
    }
    
    int iReuseAddr = 1;
    if (setsockopt(stEventCtx.iSockFd, SOL_SOCKET, SO_REUSEADDR, &iReuseAddr, sizeof(iReuseAddr)) < 0) {
        fprintf(stderr,"setsockopt(SO_REUSEADDR) fail...[%s]", strerror(errno));
        return -1;
    }

    // 바인드(커널이 멀티캐스트 수신 처리)
    struct sockaddr_in stMcastBindAddr;
    memset(&stMcastBindAddr, 0, sizeof(stMcastBindAddr));
    stMcastBindAddr.sin_family      = AF_INET;
    stMcastBindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    stMcastBindAddr.sin_port        = htons((uint16_t)MCAST_PORT);
    if (bind(stEventCtx.iSockFd, (struct sockaddr *)&stMcastBindAddr, sizeof(struct sockaddr_in)) < 0) {
        fprintf(stderr,"%s():%d fail...[%s]", __func__, __LINE__, strerror(errno));
        close(stEventCtx.iSockFd);
        return -1;
    }

    // 인터페이스 지정하여 그룹 가입
    if (multicastSockJoin(stEventCtx.iSockFd, stGroupAddr.s_addr) < 0) {
        close(stEventCtx.iSockFd);
        return EXIT_FAILURE;
    }

    /* ====== 옵션: 수신 버퍼 키우기 (대량 트래픽 시 유용) ====== */
    int iRecvBufferSize = 1*1024;
    if (setsockopt(stEventCtx.iSockFd, SOL_SOCKET, SO_RCVBUF, &iRecvBufferSize, sizeof(iRecvBufferSize)) < 0) {
        fprintf(stderr,"setsockopt(SO_RCVBUF=%d) warn...[%s]", iRecvBufferSize, strerror(errno));
    }
    fprintf(stderr,"[MCAST-RECV] group=%s port=%u\n", MCAST_IP, (unsigned)MCAST_PORT);
    
    evutil_make_socket_nonblocking(stEventCtx.iSockFd);
    evutil_make_socket_closeonexec(stEventCtx.iSockFd);
    fprintf(stdout,"### %s():%d ###\n",__func__,__LINE__);
    stEventCtx.pstSockCtx->pstBufferEvent = bufferevent_socket_new(stEventCtx.pstEventBase, stEventCtx.iSockFd, BEV_OPT_CLOSE_ON_FREE);
    if (!stEventCtx.pstSockCtx->pstBufferEvent){
        free(stEventCtx.pstSockCtx);
        event_base_free(stEventCtx.pstEventBase);
        return 1;
    }
    fprintf(stdout,"### %s():%d ###\n",__func__,__LINE__);    
    bufferevent_setcb(stEventCtx.pstSockCtx->pstBufferEvent, mcastReadCallback, NULL, mcastEventCallback, stEventCtx.pstSockCtx);
    bufferevent_enable(stEventCtx.pstSockCtx->pstBufferEvent, EV_READ|EV_WRITE);
    bufferevent_setwatermark(stEventCtx.pstSockCtx->pstBufferEvent, EV_READ, sizeof(FRAME_HEADER), READ_HIGH_WM);

    /* SIGINT(CTRL+C) 처리 */
    signal(SIGPIPE, SIG_IGN);
    stEventCtx.pstEvent = evsignal_new(stEventCtx.pstEventBase, SIGINT, signalCb, &stEventCtx);
    if (!stEventCtx.pstEvent || event_add(stEventCtx.pstEvent, NULL) < 0) {
        fprintf(stderr, "Could not create/add SIGINT event!\n");
        event_free(stEventCtx.pstAcceptEvent);
        evutil_closesocket(stEventCtx.iSockFd);
        event_base_free(stEventCtx.pstEventBase);
        return 1;
    }    
    fprintf(stderr,"Multicast Receiver Start\n");
    fprintf(stderr,"Waiting packets... (Ctrl+C to stop)\n");

    event_base_dispatch(stEventCtx.pstEventBase);

    /* 정리 */
    if (stEventCtx.pstEvent)
        event_free(stEventCtx.pstEvent);    

    if (stEventCtx.pstSockCtx){
        if(stEventCtx.pstSockCtx->pstBufferEvent)
            bufferevent_free(stEventCtx.pstSockCtx->pstBufferEvent);
        free(stEventCtx.pstSockCtx);
        stEventCtx.pstSockCtx = NULL;
    }

    event_base_free(stEventCtx.pstEventBase);
    printf("done\n");
    return 0;
}


/* === main === */
#ifndef GOOGLE_TEST
int main(int argc, char** argv)
{
    return run();
}
#endif