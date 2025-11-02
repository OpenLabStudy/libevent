#include "uds.h"
#include "../core/frame.h"
#include "../core/icdCommand.h"
#include "../core/netUtil.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

void udsSvrInit(UDS_SERVER_CTX* pstUdsSrvCtx, struct event_base* pstEventBase,
    unsigned char uchMyId, NET_MODE eMode)
{
    netBaseInit(&pstUdsSrvCtx->stNetBase, pstEventBase, uchMyId, eMode);
    pstUdsSrvCtx->pstClnConnectEvent = NULL;
}

/* ================================================================
 * 클라이언트 접속 수락 콜백 (accept)
 * ================================================================ */
static void udsAcceptCb(evutil_socket_t fd, short ev, void* pvData)
{
    (void)ev;
    UDS_SERVER_CTX* pstUdsSrvCtx = (UDS_SERVER_CTX*)pvData;

    struct sockaddr_un client_addr;
    socklen_t len = sizeof(client_addr);
    int client_fd = accept(fd, (struct sockaddr*)&client_addr, &len);
    if (client_fd < 0) {
        perror("[UDS SERVER] accept failed");
        return;
    }

    SESSION_CTX* pstSession = calloc(1, sizeof(*pstSession));
    pstSession->pstCoreCtx = &pstUdsSrvCtx->stNetBase.stCoreCtx;
    pstSession->pstBufferEvent = bufferevent_socket_new(
        pstUdsSrvCtx->stNetBase.stCoreCtx.pstEventBase, 
        client_fd, 
        BEV_OPT_CLOSE_ON_FREE);

    bufferevent_setcb(pstSession->pstBufferEvent, 
        sessionReadCallback, NULL, sessionEventCallback, pstSession);
    bufferevent_enable(pstSession->pstBufferEvent, EV_READ | EV_WRITE);

    sessionAdd(pstSession, &pstUdsSrvCtx->stNetBase.stCoreCtx);
    pstUdsSrvCtx->stNetBase.stCoreCtx.iClientSock = client_fd;

    printf("[UDS SERVER] Client connected (fd=%d, total=%d)\n",
           client_fd, pstUdsSrvCtx->stNetBase.stCoreCtx.iClientCount);
}

/* ================================================================
 * 서버 시작 (여러 클라이언트 수락 가능)
 * ================================================================ */
int udsServerStart(UDS_SERVER_CTX *pstUdsSrvCtx, const char *pchPath)
{
    unlink(pchPath);
    pstUdsSrvCtx->stNetBase.iSockFd = createUdsServer(pchPath);
    if (pstUdsSrvCtx->stNetBase.iSockFd < 0) {
        perror("[UDS SERVER] createUdsServer failed");
        return -1;
    }

    // 클라이언트 접속 대기용 이벤트 등록
    pstUdsSrvCtx->pstClnConnectEvent = event_new(
        pstUdsSrvCtx->stNetBase.stCoreCtx.pstEventBase,
        pstUdsSrvCtx->stNetBase.iSockFd,
        EV_READ | EV_PERSIST,
        udsAcceptCb,
        pstUdsSrvCtx);

    event_add(pstUdsSrvCtx->pstClnConnectEvent, NULL);

    printf("[UDS SERVER] Listening on %s\n", pchPath);
    return 0;
}

/* ================================================================
 * 서버 종료 및 정리
 * ================================================================ */
void udsSvrStop(UDS_SERVER_CTX *pstUdsSrvCtx)
{
    CORE_CTX* pstCoreCtx = &pstUdsSrvCtx->stNetBase.stCoreCtx;

    // 세션 정리
    SESSION_CTX* pstSessionCtx = pstCoreCtx->pstSockCtxHead;
    while (pstSessionCtx) {
        SESSION_CTX* pstNextSessionCtx = pstSessionCtx->pstSockCtxNext;
        if (pstSessionCtx->pstBufferEvent) {
            bufferevent_free(pstSessionCtx->pstBufferEvent);
        }
        free(pstSessionCtx);
        pstSessionCtx = pstNextSessionCtx;
    }

    pstCoreCtx->pstSockCtxHead = NULL;
    pstCoreCtx->iClientCount = 0;

    // 이벤트 해제
    if (pstUdsSrvCtx->pstClnConnectEvent) {
        event_free(pstUdsSrvCtx->pstClnConnectEvent);
        pstUdsSrvCtx->pstClnConnectEvent = NULL;
    }

    if (pstUdsSrvCtx->stNetBase.iSockFd >= 0) {
        close(pstUdsSrvCtx->stNetBase.iSockFd);
        pstUdsSrvCtx->stNetBase.iSockFd = -1;
    }

    printf("[UDS SERVER] Stopped and cleaned up.\n");
}



void udsClnInit(UDS_CLIENT_CTX *pstUdsClnCtx, struct event_base *pstEventBase,
    unsigned char uchMyId, NET_MODE eMode)
{
    netBaseInit(&pstUdsClnCtx->stNetBase, pstEventBase, uchMyId, eMode);
    pstUdsClnCtx->pstBufferEvent = NULL;
}

int udsClientStart(UDS_CLIENT_CTX *pstUdsClnCtx, const char *pchPath)
{
    pstUdsClnCtx->stNetBase.iSockFd = createUdsClient(pchPath);
    if (pstUdsClnCtx->stNetBase.iSockFd < 0)
        return -1;

    pstUdsClnCtx->pstBufferEvent = bufferevent_socket_new(
        pstUdsClnCtx->stNetBase.stCoreCtx.pstEventBase, 
        pstUdsClnCtx->stNetBase.iSockFd, 
        BEV_OPT_CLOSE_ON_FREE);
    if (!pstUdsClnCtx->pstBufferEvent) {
        fprintf(stderr, "[TCP CLIENT] bufferevent_socket_new failed\n");
        close(pstUdsClnCtx->stNetBase.iSockFd);
        return -1;
    }

    bufferevent_setcb(pstUdsClnCtx->pstBufferEvent, 
        sessionReadCallback, NULL, sessionEventCallback, NULL);
    bufferevent_enable(pstUdsClnCtx->pstBufferEvent, EV_READ | EV_WRITE);
    return 0;
}


void udsClnStop(UDS_CLIENT_CTX *pstUdsClnCtx)
{
    // 1. 클라이언트 bufferevent 해제
    if (pstUdsClnCtx->pstBufferEvent) {
        bufferevent_disable(pstUdsClnCtx->pstBufferEvent, EV_READ | EV_WRITE);
        bufferevent_free(pstUdsClnCtx->pstBufferEvent);   // fd 자동 close
        pstUdsClnCtx->pstBufferEvent = NULL;
    }

    // 2. event_base 해제
    if (pstUdsClnCtx->stNetBase.stCoreCtx.pstEventBase) {
        event_base_loopbreak(pstUdsClnCtx->stNetBase.stCoreCtx.pstEventBase);
        event_base_free(pstUdsClnCtx->stNetBase.stCoreCtx.pstEventBase);
        pstUdsClnCtx->stNetBase.stCoreCtx.pstEventBase = NULL;
    }

    pstUdsClnCtx->stNetBase.iSockFd = -1;
    printf("[UDS CLIENT] Stopped and cleaned up.\n");
}
