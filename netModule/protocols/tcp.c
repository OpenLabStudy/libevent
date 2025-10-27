#include "tcp.h"
#include "../core/frame.h"
#include "../core/icdCommand.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

static void tcpAcceptCb(struct evconnlistener* listener, evutil_socket_t fd,
                        struct sockaddr* addr, int socklen, void* ctx);
static void tcpStdinCb(evutil_socket_t fd, short ev, void* arg);

void tcpInit(NET_CTX* pstTcpCtx, struct event_base* pstBase, unsigned char uchMyId, NET_MODE eMode)
{
    memset(pstTcpCtx, 0, sizeof(*pstTcpCtx));
    sessionInitCore(&pstTcpCtx->stCoreCtx, pstBase, uchMyId);
    pstTcpCtx->eMode = eMode;
    signal(SIGPIPE, SIG_IGN);
}

/* ================================
 * TCP SERVER START
 * ================================ */
int tcpServerStart(NET_CTX* pstTcpCtx, unsigned short unPort)
{
    pstTcpCtx->iSockFd = createTcpServer(unPort);
    if (pstTcpCtx->iSockFd < 0)
        return -1;

    pstTcpCtx->pstListener = evconnlistener_new(
        pstTcpCtx->stCoreCtx.pstEventBase,
        tcpAcceptCb,
        pstTcpCtx,
        LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE,
        -1,
        pstTcpCtx->iSockFd);

    if (!pstTcpCtx->pstListener)
        return -1;

    printf("[TCP SERVER] Listening on port %d\n", unPort);
    return 0;
}

static void tcpAcceptCb(struct evconnlistener* listener, evutil_socket_t fd,
                        struct sockaddr* addr, int socklen, void* ctx)
{
    (void)addr; (void)socklen; (void)listener;
    NET_CTX* pstTcpCtx = (NET_CTX*)ctx;

    SESSION_CTX* pstSession = calloc(1, sizeof(*pstSession));
    pstSession->pstCoreCtx = &pstTcpCtx->stCoreCtx;
    pstSession->pstBufferEvent = bufferevent_socket_new(
        pstTcpCtx->stCoreCtx.pstEventBase, fd, BEV_OPT_CLOSE_ON_FREE);

    bufferevent_setcb(pstSession->pstBufferEvent,
                      sessionReadCallback, NULL, sessionEventCallback, pstSession);
    bufferevent_enable(pstSession->pstBufferEvent, EV_READ | EV_WRITE);
    sessionAdd(pstSession, &pstTcpCtx->stCoreCtx);

    printf("[TCP SERVER] Client connected (total=%d)\n", pstTcpCtx->stCoreCtx.iClientCount);
}

/* ================================
 * TCP CLIENT START
 * ================================ */
int tcpClientConnect(NET_CTX* pstTcpCtx, const char* pchIp, unsigned short unPort)
{
    int fd = createTcpClient(pchIp, unPort);
    if (fd < 0)
        return -1;

    pstTcpCtx->pstBev = bufferevent_socket_new(
        pstTcpCtx->stCoreCtx.pstEventBase, fd, BEV_OPT_CLOSE_ON_FREE);
    if (!pstTcpCtx->pstBev)
        return -1;

    bufferevent_setcb(pstTcpCtx->pstBev, sessionReadCallback, NULL, sessionEventCallback, NULL);
    bufferevent_enable(pstTcpCtx->pstBev, EV_READ | EV_WRITE);
    printf("[TCP CLIENT] Connected to %s:%d\n", pchIp, unPort);
    return 0;
}

void tcpClientAttachStdin(NET_CTX* pstTcpCtx)
{
    pstTcpCtx->pstStdinEvent = event_new(
        pstTcpCtx->stCoreCtx.pstEventBase,
        fileno(stdin),
        EV_READ | EV_PERSIST,
        tcpStdinCb,
        pstTcpCtx);

    if (pstTcpCtx->pstStdinEvent)
        event_add(pstTcpCtx->pstStdinEvent, NULL);
}

static void tcpStdinCb(evutil_socket_t fd, short ev, void* arg)
{
    (void)fd; (void)ev;
    NET_CTX* pstTcpCtx = (NET_CTX*)arg;
    char line[256];
    if (!fgets(line, sizeof(line), stdin))
        return;

    MSG_ID id = { .uchSrcId = pstTcpCtx->stCoreCtx.uchMyId, .uchDstId = 1 };
    if (pstTcpCtx->pstBev)
        writeFrame(pstTcpCtx->pstBev, CMD_KEEP_ALIVE, &id, 0, line, strlen(line));
}

/* ================================
 * STOP / CLEANUP
 * ================================ */
void tcpStop(NET_CTX* pstTcpCtx)
{
    if (pstTcpCtx->pstStdinEvent)
        event_free(pstTcpCtx->pstStdinEvent);
    if (pstTcpCtx->pstBev)
        bufferevent_free(pstTcpCtx->pstBev);
    if (pstTcpCtx->pstListener)
        evconnlistener_free(pstTcpCtx->pstListener);
}
