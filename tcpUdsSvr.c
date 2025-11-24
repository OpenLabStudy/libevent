/**
 * @file unifiedServer.c
 * @brief 기존 이벤트 기반 accept 시스템을 유지한 통합 TCP + UDS 서버
 */

#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>

#include "netTcp.h"
#include "netUds.h"
#include "bridgeRouter.h"
#include "udsClientTable.h"
#include "requestContext.h"

extern BRIDGE_CONTEXT g_stBridgeCtx;
static EVENT_CONTEXT g_stTcpCtx;
static EVENT_CONTEXT g_stUdsCtx;

/* ==========================================================
 * Read Callback Wrappers → Bridge Router를 사용하도록 수정
 * ========================================================== */

static void tcpReadWrapper(struct bufferevent* bev, void* arg)
{
    bridgeTcpReadCb(bev, arg);
}

static void udsReadWrapper(struct bufferevent* bev, void* arg)
{
    bridgeUdsReadCb(bev, arg);
}

static void tcpEventWrapper(struct bufferevent* bev, short events, void* arg)
{
    bridgeTcpEventCb(bev, events, arg);
}

static void udsEventWrapper(struct bufferevent* bev, short events, void* arg)
{
    bridgeUdsEventCb(bev, events, arg);
}

/* ==========================================================
 * 통합 서버 실행 함수
 * ========================================================== */

int unifiedServerRun(const char* pszUdsPath, int iTcpPort)
{
    struct event_base* pstEvBase = event_base_new();
    if (!pstEvBase) {
        fprintf(stderr, "[ERR] event_base_new failed!\n");
        return -1;
    }

    /* ==========================================
     * Request/UDS Clients Registry 초기화
     * ========================================== */
    static UDS_CLIENT_TABLE stUdsClientTable;
    static REQUEST_CONTEXT   stReqCtx;

    udsClientTableInit(&stUdsClientTable);
    reqCtxInit(&stReqCtx);

    bridgeInit(&g_stBridgeCtx,
               &stUdsClientTable,
               &stReqCtx,
               pstEvBase,
               0x10,      // TCP Src ID
               300);     // timeout ms

    /* ==========================================
     * TCP 서버 초기화
     * ========================================== */
    initEventContext(&g_stTcpCtx, ROLE_SERVER, 1);
    g_stTcpCtx.pstEventBase = pstEvBase;

    g_stTcpCtx.iSockFd = netTcpCreateServer(iTcpPort);
    if (g_stTcpCtx.iSockFd < 0) {
        fprintf(stderr, "[ERR] Failed to open TCP socket\n");
        return -1;
    }

    g_stTcpCtx.stHandler.pfReadCb  = tcpReadWrapper;
    g_stTcpCtx.stHandler.pfEventCb = tcpEventWrapper;

    setupServerAcceptEvent(&g_stTcpCtx);

    printf("[TCP] Listening on port %d\n", iTcpPort);

    /* ==========================================
     * UDS 서버 초기화
     * ========================================== */
    initEventContext(&g_stUdsCtx, ROLE_SERVER, 1);
    g_stUdsCtx.pstEventBase = pstEvBase;

    unlink(pszUdsPath);
    g_stUdsCtx.iSockFd = netUdsCreateServer(pszUdsPath);

    if (g_stUdsCtx.iSockFd < 0) {
        fprintf(stderr, "[ERR] Failed to open UDS socket\n");
        return -1;
    }

    g_stUdsCtx.stHandler.pfReadCb  = udsReadWrapper;
    g_stUdsCtx.stHandler.pfEventCb = udsEventWrapper;

    setupServerAcceptEvent(&g_stUdsCtx);

    printf("[UDS] Listening on %s\n", pszUdsPath);

    /* ==========================================
     * Signal Handler 설정 (CTRL+C graceful exit)
     * ========================================== */
    signal(SIGPIPE, SIG_IGN);

    struct event* pstSignalEvent =
        evsignal_new(pstEvBase, SIGINT,
                     (void (*)(evutil_socket_t, short, void*))event_base_loopbreak,
                     pstEvBase);

    if (pstSignalEvent)
        event_add(pstSignalEvent, NULL);

    /* ==========================================
     * 실행
     * ========================================== */
    printf("\nUnified TCP + UDS Routing Server Running...\n\n");

    event_base_dispatch(pstEvBase);

    return 0;
}

/* ========================================================================== */
/* Standalone Main                                                            */
/* ========================================================================== */

#ifndef GOOGLE_TEST
int main(void)
{
    return unifiedServerRun("/tmp/routing.sock", 5000);
}
#endif