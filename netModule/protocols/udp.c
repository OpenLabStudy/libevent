#include "udp.h"
#include "../core/frame.h"
#include "../core/icdCommand.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

static void udpRecvCb(evutil_socket_t fd, short ev, void* arg);
static void udpStdinCb(evutil_socket_t fd, short ev, void* arg);

void udpInit(UDP_CTX* C, struct event_base* base, unsigned char id, NET_MODE mode)
{
    memset(C, 0, sizeof(*C));
    sessionInitCore(&C->stCoreCtx, base, id);
    C->eMode = mode;
    signal(SIGPIPE, SIG_IGN);
}

int udpServerStart(UDP_CTX* C, unsigned short port)
{
    C->iSockFd = createUdpServer(port);
    if (C->iSockFd < 0) return -1;

    C->pstRecvEvent = event_new(C->stCoreCtx.pstEventBase, C->iSockFd, EV_READ|EV_PERSIST, udpRecvCb, C);
    event_add(C->pstRecvEvent, NULL);
    printf("[UDP SERVER] Listening on port %d\n", port);
    return 0;
}

int udpClientStart(UDP_CTX* C, const char* ip, unsigned short srvPort, unsigned short myPort)
{
    C->iSockFd = createUdpClient(ip, srvPort, myPort);
    if (C->iSockFd < 0) return -1;

    C->pstRecvEvent = event_new(C->stCoreCtx.pstEventBase, C->iSockFd, EV_READ|EV_PERSIST, udpRecvCb, C);
    event_add(C->pstRecvEvent, NULL);
    udpStdinCb(0, 0, C);  // 초기화

    C->pstStdinEvent = event_new(C->stCoreCtx.pstEventBase, fileno(stdin), EV_READ|EV_PERSIST, udpStdinCb, C);
    event_add(C->pstStdinEvent, NULL);
    return 0;
}

static void udpRecvCb(evutil_socket_t fd, short ev, void* arg)
{
    UDP_CTX* C = (UDP_CTX*)arg;
    char buf[1024];
    struct sockaddr_in src;
    socklen_t len = sizeof(src);
    ssize_t n = recvfrom(fd, buf, sizeof(buf)-1, 0, (struct sockaddr*)&src, &len);
    if (n > 0) {
        buf[n] = 0;
        printf("[UDP RECV] %s\n", buf);
    }
}

static void udpStdinCb(evutil_socket_t fd, short ev, void* arg)
{
    (void)fd; (void)ev;
    UDP_CTX* C = (UDP_CTX*)arg;
    char line[256];
    if (!fgets(line, sizeof(line), stdin)) return;
    line[strcspn(line, "\n")] = 0;
    sendto(C->iSockFd, line, strlen(line), 0, (struct sockaddr*)&C->stSrvAddr, sizeof(C->stSrvAddr));
}

void udpStop(UDP_CTX* C)
{
    if (C->pstRecvEvent) event_free(C->pstRecvEvent);
    if (C->pstStdinEvent) event_free(C->pstStdinEvent);
    if (C->iSockFd >= 0) close(C->iSockFd);
}
