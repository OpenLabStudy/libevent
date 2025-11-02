#include "udp.h"
#include "../core/frame.h"
#include "../core/icdCommand.h"
#include "../core/netUtil.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>


static void udpRecvCb(evutil_socket_t fd, short ev, void* arg);
static void udpStdinCb(evutil_socket_t fd, short ev, void* arg);

void udpInit(UDP_CTX* pstUdpCtx, struct event_base* pstEventBase, unsigned char uchMyId, NET_MODE eMode)
{
    memset(pstUdpCtx, 0, sizeof(*pstUdpCtx));
    sessionInitCore(&pstUdpCtx->stCoreCtx, pstEventBase, uchMyId);
    pstUdpCtx->eMode = eMode;
    signal(SIGPIPE, SIG_IGN);
}

int udpServerStart(UDP_CTX* pstUdpCtx, unsigned short unPort)
{
    pstUdpCtx->stNetBase.iSockFd = createUdpServer(unPort);
    if (pstUdpCtx->stNetBase.iSockFd < 0) {
        return -1;
    }

    pstUdpCtx->pstRecvEvent = event_new(pstUdpCtx->stCoreCtx.pstEventBase, pstUdpCtx->iSockFd, EV_READ|EV_PERSIST, udpRecvCb, pstUdpCtx);
    event_add(pstUdpCtx->pstRecvEvent, NULL);
    printf("[UDP SERVER] Listening on port %d\n", unPort);
    return 0;
}

int udpClientStart(UDP_CTX* pstUdpCtx, const char* pchIpAddr, unsigned short unSvrPort, unsigned short unMyPort)
{
    pstUdpCtx->iSockFd = createUdpClient(pchIpAddr, unSvrPort, unMyPort);
    if (pstUdpCtx->iSockFd < 0)
        return -1;

    pstUdpCtx->pstRecvEvent = event_new(pstUdpCtx->stCoreCtx.pstEventBase, \
        pstUdpCtx->iSockFd, EV_READ|EV_PERSIST, udpRecvCb, pstUdpCtx);
    event_add(pstUdpCtx->pstRecvEvent, NULL);
    udpStdinCb(0, 0, pstUdpCtx);  // 초기화

    pstUdpCtx->pstStdinEvent = event_new(pstUdpCtx->stCoreCtx.pstEventBase, \
        fileno(stdin), EV_READ|EV_PERSIST, udpStdinCb, pstUdpCtx);
    event_add(pstUdpCtx->pstStdinEvent, NULL);
    return 0;
}

static void udpRecvCb(evutil_socket_t fd, short nEvent, void* pvData)
{
    UDP_CTX* pstUdpCtx = (UDP_CTX*)pvData;
    char buf[1024];
    struct sockaddr_in src;
    socklen_t len = sizeof(src);
    ssize_t n = recvfrom(fd, buf, sizeof(buf)-1, 0, (struct sockaddr*)&src, &len);
    if (n > 0) {
        buf[n] = 0;
        printf("[UDP RECV] %s\n", buf);
    }
}

static void udpStdinCb(evutil_socket_t fd, short nEvent, void* pvData)
{
    (void)fd; 
    (void)nEvent;
    UDP_CTX* pstUdpCtx = (UDP_CTX*)pvData;
    char line[256];
    if (!fgets(line, sizeof(line), stdin))
        return;

    line[strcspn(line, "\n")] = 0;
    sendto(pstUdpCtx->iSockFd, line, strlen(line), 0, (struct sockaddr*)&pstUdpCtx->stSrvAddr, sizeof(pstUdpCtx->stSrvAddr));
}

void udpStop(UDP_CTX* pstUdpCtx)
{
    if (pstUdpCtx->pstRecvEvent)
        event_free(pstUdpCtx->pstRecvEvent);

    if (pstUdpCtx->pstStdinEvent)
        event_free(pstUdpCtx->pstStdinEvent);

    if (pstUdpCtx->iSockFd >= 0)
        close(pstUdpCtx->iSockFd);
}
