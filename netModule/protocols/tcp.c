#include "tcp.h"
#include "../core/frame.h"
#include "../core/icdCommand.h"
#include "../core/netUtil.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* ================================================================
 * 서버 전용 함수
 * ================================================================ */
/**
 * @brief TCP 컨텍스트 초기화
 */

void tcpSvrInit(TCP_SERVER_CTX* pstTcpCtx, struct event_base* pstEventBase,
    unsigned char uchMyId, NET_MODE eMode)
{
    netBaseInit(&pstTcpCtx->stNetBase, pstEventBase, uchMyId, eMode);
    pstTcpCtx->pstListener = NULL;
}

 /**
 * @brief 클라이언트 접속 콜백
 */
static void tcpAcceptCb(struct evconnlistener* listener, evutil_socket_t fd,
    struct sockaddr* addr, int socklen, void* pvData)
{
    (void)socklen;
    (void)listener;
    TCP_SERVER_CTX* pstTcpCtx = (TCP_SERVER_CTX*)pvData;

    // ==== 클라이언트 정보 추출 ====
    char client_ip[INET_ADDRSTRLEN];
    struct sockaddr_in* client_addr = (struct sockaddr_in*)addr;
    inet_ntop(AF_INET, &client_addr->sin_addr, client_ip, sizeof(client_ip));
    int iClientPort = ntohs(client_addr->sin_port);
    // ===============================

    SESSION_CTX* pstSession = calloc(1, sizeof(*pstSession));
    pstSession->pstCoreCtx = &pstTcpCtx->stNetBase.stCoreCtx;
    pstSession->pstBufferEvent = bufferevent_socket_new(
            pstTcpCtx->stNetBase.stCoreCtx.pstEventBase, 
            fd, 
            BEV_OPT_CLOSE_ON_FREE);

    bufferevent_setcb(pstSession->pstBufferEvent,
            sessionReadCallback, NULL, sessionEventCallback, pstSession);
    bufferevent_enable(pstSession->pstBufferEvent, EV_READ | EV_WRITE);
    sessionAdd(pstSession, &pstTcpCtx->stNetBase.stCoreCtx);
    pstTcpCtx->stNetBase.stCoreCtx.iClientSock = fd;

    printf("[TCP SERVER] Client connected: fd=%d, ip=%s, port=%d (total=%d)\n",
    fd, client_ip, iClientPort, pstTcpCtx->stNetBase.stCoreCtx.iClientCount);
}

/**
 * @brief TCP 서버 시작
 */
int tcpServerStart(TCP_SERVER_CTX *pstTcpCtx, unsigned short unPort)
{
    pstTcpCtx->stNetBase.iSockFd = createTcpServer(unPort);
    if (pstTcpCtx->stNetBase.iSockFd < 0)
        return -1;

    if (!pstTcpCtx->stNetBase.stCoreCtx.pstEventBase) {
        fprintf(stderr, "11[ERROR] Failed to create event_base: %s\n", strerror(errno));
        return -1;
    }
    pstTcpCtx->pstListener = evconnlistener_new(
        pstTcpCtx->stNetBase.stCoreCtx.pstEventBase,
        tcpAcceptCb,
        pstTcpCtx,
        LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE,
        -1,
        pstTcpCtx->stNetBase.iSockFd);

    if (!pstTcpCtx->pstListener){
        close(pstTcpCtx->stNetBase.iSockFd);
        return -1;
    }

    printf("[TCP SERVER] Listening on port %d\n", unPort);
    return 0;
}



/**
 * @brief TCP 종료 및 리소스 해제
 */
void tcpSvrStop(TCP_SERVER_CTX* pstTcpCtx)
{
    CORE_CTX* pstCoreCtx = &pstTcpCtx->stNetBase.stCoreCtx;

    // 연결된 모든 세션 종료
    SESSION_CTX* pstSessionCtx = pstCoreCtx->pstSockCtxHead;
    while (pstSessionCtx) {
        SESSION_CTX *pstNextSessionCtx = pstSessionCtx->pstSockCtxNext;
        if (pstSessionCtx->pstBufferEvent) {
            bufferevent_disable(pstSessionCtx->pstBufferEvent, EV_READ | EV_WRITE);
            bufferevent_free(pstSessionCtx->pstBufferEvent); // fd 자동 close
            pstSessionCtx->pstBufferEvent = NULL;
        }
        free(pstSessionCtx);
        pstSessionCtx = pstNextSessionCtx;
    }
    pstCoreCtx->pstSockCtxHead = NULL;
    pstCoreCtx->iClientCount = 0;

    // 리슨 소켓 해제
    if (pstTcpCtx->pstListener) {
        evconnlistener_free(pstTcpCtx->pstListener);   // 리슨 소켓 close
        pstTcpCtx->pstListener = NULL;
    }

    // 등록된 이벤트 해제 (있을 경우)
    if (pstCoreCtx->pstAcceptEvent) {
        event_free(pstCoreCtx->pstAcceptEvent);
        pstCoreCtx->pstAcceptEvent = NULL;
    }
    if (pstCoreCtx->pstSignalEvent) {
        event_free(pstCoreCtx->pstSignalEvent);
        pstCoreCtx->pstSignalEvent = NULL;
    }

    // event_base 해제
    if (pstCoreCtx->pstEventBase) {
        event_base_loopbreak(pstCoreCtx->pstEventBase);
        event_base_free(pstCoreCtx->pstEventBase);
        pstCoreCtx->pstEventBase = NULL;
    }

    // 상태 초기화
    pstTcpCtx->stNetBase.iSockFd = -1;
    pstCoreCtx->iClientSock = -1;

    printf("[TCP SERVER] Stopped and cleaned up.\n");
}


/* ================================================================
 * 클라이언트 전용 함수
 * ================================================================ */

/**
 * @brief TCP 클라이언트 연결
 */

 void tcpClnInit(TCP_CLIENT_CTX* pstTcpCtx, struct event_base* pstEventBase,
    unsigned char uchMyId, NET_MODE eMode)
{
    netBaseInit(&pstTcpCtx->stNetBase, pstEventBase, uchMyId, eMode);
    pstTcpCtx->pstBufferEvent = NULL;
}

int tcpClientConnect(TCP_CLIENT_CTX* pstTcpCtx, const char* pchIp, unsigned short unPort)
{
    int fd = createTcpClient(pchIp, unPort);
    if (fd < 0) {
        fprintf(stderr, "[TCP CLIENT] Failed to connect to %s:%d\n", pchIp, unPort);
        return -1;   // GTest에서 ASSERT_EQ(...)로 확인 가능
    }

    pstTcpCtx->pstBufferEvent = bufferevent_socket_new(
        pstTcpCtx->stNetBase.stCoreCtx.pstEventBase, fd, BEV_OPT_CLOSE_ON_FREE);
    if (!pstTcpCtx->pstBufferEvent) {
        fprintf(stderr, "[TCP CLIENT] bufferevent_socket_new failed\n");
        close(fd);
        return -1;
    }

    bufferevent_setcb(pstTcpCtx->pstBufferEvent, 
        sessionReadCallback, NULL, sessionEventCallback, NULL);
    bufferevent_enable(pstTcpCtx->pstBufferEvent, EV_READ | EV_WRITE);

    // 실제 연결 확인
    struct sockaddr_in sa;
    socklen_t len = sizeof(sa);
    if (getpeername(fd, (struct sockaddr*)&sa, &len) < 0) {
        perror("[TCP CLIENT] getpeername failed");
        bufferevent_free(pstTcpCtx->pstBufferEvent);
        pstTcpCtx->pstBufferEvent = NULL;
        return -1;
    }
    return 0;
}

void tcpClnStop(TCP_CLIENT_CTX* pstTcpCtx)
{
    // 1. 클라이언트 bufferevent 해제
    if (pstTcpCtx->pstBufferEvent) {
        bufferevent_disable(pstTcpCtx->pstBufferEvent, EV_READ | EV_WRITE);
        bufferevent_free(pstTcpCtx->pstBufferEvent);   // fd 자동 close
        pstTcpCtx->pstBufferEvent = NULL;
    }

    // 2. event_base 해제
    if (pstTcpCtx->stNetBase.stCoreCtx.pstEventBase) {
        event_base_loopbreak(pstTcpCtx->stNetBase.stCoreCtx.pstEventBase);
        event_base_free(pstTcpCtx->stNetBase.stCoreCtx.pstEventBase);
        pstTcpCtx->stNetBase.stCoreCtx.pstEventBase = NULL;
    }

    pstTcpCtx->stNetBase.iSockFd = -1;

    printf("[TCP CLIENT] Stopped and cleaned up.\n");
}