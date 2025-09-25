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

static void mcastReadCallback(struct bufferevent *pstBufferEvent, void *pvData)
{
    SOCK_CONTEXT *pstSockCtx    = (SOCK_CONTEXT *)pvData;
    struct evbuffer *pstEvBuffer = bufferevent_get_input(pstBufferEvent);
    int iLength = evbuffer_get_length(pstEvBuffer);
    char achData[512];
    fprintf(stderr,"### %s():%d Recv Data Length is %d\n", __func__, __LINE__, iLength);
    
    if (iLength <= 0){
        return 0;
    }

    if (evbuffer_copyout(pstEvBuffer, achData, iLength) != iLength){
        return 0;
    }
    achData[iLength] = '\0';
    fprintf(stderr,"<<< len=%zd msg=\"%s\"\n", iLength, achData);  
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
    EVENT_CONTEXT stEventCtx = (EVENT_CONTEXT){0};

    // SIGINT 핸들러 등록
    signal(SIGPIPE, SIG_IGN);
    stEventCtx.pstEventBase   = event_base_new();
    if (!stEventCtx.pstEventBase) {
        fprintf(stderr, "Could not initialize libevent!\n");
        return 1;
    }
    stEventCtx.iClientCount = 0;
    stEventCtx.pstSockCtx = (SOCK_CONTEXT*)calloc(1, sizeof(SOCK_CONTEXT));
    if (!stEventCtx.pstSockCtx) { 
        event_base_free(stEventCtx.pstEventBase);
        return; 
    }
    stEventCtx.pstSockCtx->pstEventCtx = &stEventCtx;
    stEventCtx.pstSockCtx->pstNextSockCtx = NULL;
    stEventCtx.pstSockCtx->uchDstId = UDS1_SERVER_ID;
    stEventCtx.pstSockCtx->uchIsRespone = 0x00;
    stEventCtx.pstSockCtx->unPort = 0;
    stEventCtx.eRole = ROLE_CLIENT;

    stEventCtx.iListenFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (stEventCtx.iListenFd < 0) {
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
    if (setsockopt(stEventCtx.iListenFd, SOL_SOCKET, SO_REUSEADDR, &iReuseAddr, sizeof(iReuseAddr)) < 0) {
        fprintf(stderr,"setsockopt(SO_REUSEADDR) fail...[%s]", strerror(errno));
        return -1;
    }

    // 바인드(INADDR_ANY 권장: 커널이 멀티캐스트 수신 처리)
    struct sockaddr_in stMcastBindAddr;
    memset(&stMcastBindAddr, 0, sizeof(stMcastBindAddr));
    stMcastBindAddr.sin_family      = AF_INET;
    stMcastBindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    stMcastBindAddr.sin_port        = htons((uint16_t)MCAST_PORT);

    if (bind(stEventCtx.iListenFd, (struct sockaddr *)&stMcastBindAddr, sizeof(struct sockaddr_in)) < 0) {
        fprintf(stderr,"%s():%d fail...[%s]", __func__, __LINE__, strerror(errno));
        close(stEventCtx.iListenFd);
        return -1;
    }

    // 인터페이스 지정하여 그룹 가입(미지정시 ANY)
    if (multicastSockJoin(stEventCtx.iListenFd, stGroupAddr.s_addr) < 0) {
        close(stEventCtx.iListenFd);
        return EXIT_FAILURE;
    }

    /* ====== 옵션: 수신 버퍼 키우기 (대량 트래픽 시 유용) ====== */
    int iRecvBufferSize = 1*1024;
    if (setsockopt(stEventCtx.iListenFd, SOL_SOCKET, SO_RCVBUF, &iRecvBufferSize, sizeof(iRecvBufferSize)) < 0) {
        fprintf(stderr,"setsockopt(SO_RCVBUF=%d) warn...[%s]", iRecvBufferSize, strerror(errno));
    }
    fprintf(stderr,"[MCAST-RECV] group=%s port=%u\n", MCAST_IP, (unsigned)MCAST_PORT);
    
    evutil_make_socket_nonblocking(stEventCtx.iListenFd);
    evutil_make_socket_closeonexec(stEventCtx.iListenFd);
    fprintf(stdout,"### %s():%d ###\n",__func__,__LINE__);
    stEventCtx.pstSockCtx->pstBufferEvent = bufferevent_socket_new(stEventCtx.pstEventBase, stEventCtx.iListenFd, BEV_OPT_CLOSE_ON_FREE);
    if (!stEventCtx.pstSockCtx->pstBufferEvent){
        free(stEventCtx.pstSockCtx);
        event_base_free(stEventCtx.pstEventBase);
        return 1;
    }
    fprintf(stdout,"### %s():%d ###\n",__func__,__LINE__);    
    bufferevent_setcb(stEventCtx.pstSockCtx->pstBufferEvent, mcastReadCallback, NULL, mcastEventCallback, stEventCtx.pstSockCtx);
    bufferevent_enable(stEventCtx.pstSockCtx->pstBufferEvent, EV_READ|EV_WRITE);
    bufferevent_setwatermark(stEventCtx.pstSockCtx->pstBufferEvent, EV_READ, sizeof(FRAME_HEADER), READ_HIGH_WM);

    stEventCtx.uchMyId = UDS1_SERVER_ID;
    stEventCtx.iClientCount = 0;
    /* SIGINT(CTRL+C) 처리 */
    stEventCtx.pstEvent = evsignal_new(stEventCtx.pstEventBase, SIGINT, signalCb, &stEventCtx);
    if (!stEventCtx.pstEvent || event_add(stEventCtx.pstEvent, NULL) < 0) {
        fprintf(stderr, "Could not create/add SIGINT event!\n");
        event_free(stEventCtx.pstAcceptEvent);
        evutil_closesocket(stEventCtx.iListenFd);
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