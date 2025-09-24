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

#include "frame.h"
#include "sockSession.h"

#define MCAST_IP        "239.255.0.1"
#define MCAST_PORT      5000


static volatile sig_atomic_t g_stop = 0;
static void on_sigint(int sig) { 
    (void)sig;   // sig 변수를 쓰지 않으면 경고 억제
    g_stop = 1; 
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
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sigint;
    sigaction(SIGINT, &sa, NULL);

    // signal(SIGPIPE, SIG_IGN);
    stEventCtx.pstEventBase   = event_base_new();
    if (!stEventCtx.pstEventBase) {
        fprintf(stderr, "Could not initialize libevent!\n");
        return 1;
    }

    /* Multicast 주소 준비 */
    struct in_addr stGroupAddr = {0};
    if (inet_pton(AF_INET, MCAST_IP, &stGroupAddr) != 1) {
        fprintf(stderr,"Invalid MULTICAST_IP: %s", MCAST_IP);
        return EXIT_FAILURE;
    }
    int iSockFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (iSockFd < 0) {
        fprintf(stderr,"socket() fail...[%s]", strerror(errno));
        return -1;
    }
    int iReuseAddr = 1;
    if (setsockopt(iSockFd, SOL_SOCKET, SO_REUSEADDR, &iReuseAddr, sizeof(iReuseAddr)) < 0) {
        fprintf(stderr,"setsockopt(SO_REUSEADDR) fail...[%s]", strerror(errno));
        return -1;
    }

    // 바인드(INADDR_ANY 권장: 커널이 멀티캐스트 수신 처리)
    struct sockaddr_in stMcastBindAddr;
    memset(&stMcastBindAddr, 0, sizeof(stMcastBindAddr));
    stMcastBindAddr.sin_family      = AF_INET;
    stMcastBindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    stMcastBindAddr.sin_port        = htons((uint16_t)MCAST_PORT);

    if (bind(iSockFd, (struct sockaddr *)&stMcastBindAddr, sizeof(struct sockaddr_in)) < 0) {
        fprintf(stderr,"%s():%d fail...[%s]", __func__, __LINE__, strerror(errno));
        close(iSockFd);
        return -1;
    }

    // 인터페이스 지정하여 그룹 가입(미지정시 ANY)
    if (multicastSockJoin(iSockFd, stGroupAddr.s_addr) < 0) {
        close(iSockFd);
        return EXIT_FAILURE;
    }

    /* ====== 옵션: 수신 버퍼 키우기 (대량 트래픽 시 유용) ====== */
    int iRecvBufferSize = 1*1024;
    if (setsockopt(iSockFd, SOL_SOCKET, SO_RCVBUF, &iRecvBufferSize, sizeof(iRecvBufferSize)) < 0) {
        fprintf(stderr,"setsockopt(SO_RCVBUF=%d) warn...[%s]", iRecvBufferSize, strerror(errno));
    }
    fprintf(stderr,"[MCAST-RECV] group=%s port=%u\n", MCAST_IP, (unsigned)MCAST_PORT);
    fprintf(stderr,"Waiting packets... (Ctrl+C to stop)");
    // 수신 루프
    while (!g_stop) {
        char buf[2048];
        struct sockaddr_in src; socklen_t slen = sizeof(src);
        ssize_t n = recvfrom(iSockFd, buf, sizeof(buf)-1, 0, (struct sockaddr*)&src, &slen);
        if (n < 0) {
            if (errno == EINTR && g_stop) break;
            fprintf(stderr,"recvfrom() fail...[%s]", strerror(errno));
            // 오류 지속 시 탈출할지 여부는 정책에 따라
            continue;
        }
        buf[n] = '\0';

        char src_ip[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &src.sin_addr, src_ip, sizeof(src_ip));
        fprintf(stderr,"<<< %s:%u len=%zd msg=\"%s\"",
                 src_ip, ntohs(src.sin_port), n, buf);
    }

    // 종료: 그룹 탈퇴 → 소켓 닫기
    if (multicastSockLeave(iSockFd, stGroupAddr.s_addr) < 0) {
        fprintf(stderr,"dropMulticastSockOpt() warn");
    }
    close(iSockFd);
    fprintf(stderr,"Stopped.");

#if 0
    struct sockaddr_in stClientSocket;
    memset(&stClientSocket,0,sizeof(stClientSocket));
    stClientSocket.sin_family = AF_INET;
    stClientSocket.sin_port   = htons(UDP_CLIENT_PORT);
    if (inet_pton(AF_INET, "127.0.0.1", &stClientSocket.sin_addr) != 1) {
        fprintf(stderr,"Bad host\n");
        close(stEventCtx.iListenFd);
        event_base_free(stEventCtx.pstEventBase);
        return -1;
    }
    int iConnectRetVal = connect(stEventCtx.iListenFd, (struct sockaddr*)&stClientSocket, sizeof(stClientSocket));
    if (iConnectRetVal < 0 && errno != EINPROGRESS) {
        perror("connect");
        close(stEventCtx.iListenFd);
        event_base_free(stEventCtx.pstEventBase);
        return -1;
    }    
    stEventCtx.uchMyId       = UDS1_SERVER_ID;   /* 필요 시 식별자 재사용 */
    stEventCtx.iClientCount  = 0;                /* UDP는 연결 개념 없지만 통계용으로 사용해도 됨 */
    
    stEventCtx.pstSockCtx = (SOCK_CONTEXT*)calloc(1, sizeof(SOCK_CONTEXT));
    if (!stEventCtx.pstSockCtx) {
        event_base_free(stEventCtx.pstEventBase);
        close(stEventCtx.iListenFd); 
        return -1; 
    }
    stEventCtx.pstSockCtx->pstEventCtx = &stEventCtx;

    stEventCtx.pstSockCtx->pstBufferEvent = bufferevent_socket_new(stEventCtx.pstEventBase, stEventCtx.iListenFd, BEV_OPT_CLOSE_ON_FREE);
    if (!stEventCtx.pstSockCtx->pstBufferEvent) { 
        fprintf(stderr, "bufferevent_socket_new failed\n");
        free(stEventCtx.pstSockCtx);
        close(stEventCtx.iListenFd);          // ★ 추가: fd 닫기
        event_base_free(stEventCtx.pstEventBase);
        return 1;
    }
    bufferevent_setcb(stEventCtx.pstSockCtx->pstBufferEvent, readCallback, NULL, eventCallback, stEventCtx.pstSockCtx);
    bufferevent_enable(stEventCtx.pstSockCtx->pstBufferEvent, EV_READ|EV_WRITE);
    bufferevent_setwatermark(stEventCtx.pstSockCtx->pstBufferEvent, EV_READ, sizeof(FRAME_HEADER), READ_HIGH_WM);  

    /* SIGINT 핸들러 */
    stEventCtx.pstEvent = evsignal_new(stEventCtx.pstEventBase, SIGINT, signalCb, &stEventCtx);
    if (!stEventCtx.pstEvent || event_add(stEventCtx.pstEvent, NULL) < 0) {
        fprintf(stderr, "Could not create/add SIGINT event!\n");
        bufferevent_free(stEventCtx.pstSockCtx->pstBufferEvent);
        free(stEventCtx.pstSockCtx);
        event_base_free(stEventCtx.pstEventBase);
        return 1;
    }

    fprintf(stderr, "UDP Server Start 127.0.0.1:%d\n", UDP_SERVER_PORT);

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
    #endif
    return 0;
}


/* === main === */
#ifndef GOOGLE_TEST
int main(int argc, char** argv)
{
    return run();
}
#endif