#include "uds.h"
#include "../core/frame.h"
#include "../core/icdCommand.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

static void udsRecvCb(evutil_socket_t fd, short ev, void* arg);
static void udsStdinCb(evutil_socket_t fd, short ev, void* arg);

void udsInit(UDS_CTX* C, struct event_base* base, unsigned char id, NET_MODE mode)
{
    memset(C, 0, sizeof(*C));
    sessionInitCore(&C->stCoreCtx, base, id);
    C->eMode = mode;
    signal(SIGPIPE, SIG_IGN);
}

int udsServerStart(UDS_CTX* C, const char* path)
{
    unlink(path);
    C->iSockFd = createUdsServer(path);
    if (C->iSockFd < 0) return -1;

    C->pstRecvEvent = event_new(C->stCoreCtx.pstEventBase, C->iSockFd, EV_READ|EV_PERSIST, udsRecvCb, C);
    event_add(C->pstRecvEvent, NULL);
    printf("[UDS SERVER] Listening on %s\n", path);
    return 0;
}

int udsClientStart(UDS_CTX* C, const char* path)
{
    C->iSockFd = createUdsClient(path);
    if (C->iSockFd < 0) return -1;

    C->pstRecvEvent = event_new(C->stCoreCtx.pstEventBase, C->iSockFd, EV_READ|EV_PERSIST, udsRecvCb, C);
    event_add(C->pstRecvEvent, NULL);

    C->pstStdinEvent = event_new(C->stCoreCtx.pstEventBase, fileno(stdin), EV_READ|EV_PERSIST, udsStdinCb, C);
    event_add(C->pstStdinEvent, NULL);
    return 0;
}

static void udsRecvCb(evutil_socket_t fd, short ev, void* arg)
{
    UDS_CTX* C = (UDS_CTX*)arg;
    char buf[1024];
    ssize_t n = read(fd, buf, sizeof(buf)-1);
    if (n > 0) {
        buf[n] = 0;
        printf("[UDS RECV] %s\n", buf);
    }
}

static void udsStdinCb(evutil_socket_t fd, short ev, void* arg)
{
    (void)fd; (void)ev;
    UDS_CTX* C = (UDS_CTX*)arg;
    char line[256];
    if (!fgets(line, sizeof(line), stdin)) return;
    line[strcspn(line, "\n")] = 0;
    write(C->iSockFd, line, strlen(line));
}

void udsStop(UDS_CTX* C)
{
    if (C->pstRecvEvent) event_free(C->pstRecvEvent);
    if (C->pstStdinEvent) event_free(C->pstStdinEvent);
    if (C->iSockFd >= 0) close(C->iSockFd);
}
