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

/* ==========================================================
 * Read Callback Wrappers → Bridge Router를 사용하도록 수정
 * ========================================================== */

static void tcpReadWrapper(struct bufferevent* bev, void* pvData)
{
    bridgeTcpReadCb(bev, pvData);
}

static void udsReadWrapper(struct bufferevent* bev, void* pvData)
{    
    bridgeUdsReadCb(bev, pvData);
}

static void tcpEventWrapper(struct bufferevent* bev, short events, void* pvData)
{
    bridgeTcpEventCb(bev, events, pvData);
}

static void udsEventWrapper(struct bufferevent* bev, short events, void* pvData)
{
    bridgeUdsEventCb(bev, events, pvData);
}

/* ==========================================================
 * 통합 서버 실행 함수
 * ========================================================== */

int unifiedServerRun(const char* pszUdsPath, int iTcpPort)
{
    BRIDGE_CONTEXT stBridgeCtx;
    EVENT_CONTEXT stTcpCtx;
    EVENT_CONTEXT stUdsCtx;
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

    bridgeInit(&stBridgeCtx,
               &stUdsClientTable,
               &stReqCtx,
               pstEvBase,
               TCP_SVR_ID,      // TCP Src ID
               300);     // timeout ms

    /* ==========================================
     * TCP 서버 초기화
     * ========================================== */
    initEventContext(&stTcpCtx, ROLE_TCP_SERVER, TCP_SVR_ID);
    stTcpCtx.pstEventBase = pstEvBase;

    stTcpCtx.iSockFd = netTcpCreateServer(iTcpPort);
    if (stTcpCtx.iSockFd < 0) {
        fprintf(stderr, "[ERR] Failed to open TCP socket\n");
        return -1;
    }

    stTcpCtx.stHandler.pfReadCb  = tcpReadWrapper;
    stTcpCtx.stHandler.pfEventCb = tcpEventWrapper;
    stTcpCtx.pvUserCtx = (void *)&stBridgeCtx;

    setupServerAcceptEvent(&stTcpCtx);

    printf("[TCP] Listening on port %d\n", iTcpPort);

    /* ==========================================
     * UDS 서버 초기화
     * ========================================== */
    initEventContext(&stUdsCtx, ROLE_UDS_SERVER, UDS_1_SVR_ID);
    stUdsCtx.pstEventBase = pstEvBase;

    unlink(pszUdsPath);
    stUdsCtx.iSockFd = netUdsCreateServer(pszUdsPath);

    if (stUdsCtx.iSockFd < 0) {
        fprintf(stderr, "[ERR] Failed to open UDS socket\n");
        return -1;
    }

    stUdsCtx.stHandler.pfReadCb  = udsReadWrapper;
    stUdsCtx.stHandler.pfEventCb = udsEventWrapper;
    stUdsCtx.pvUserCtx = (void *)&stBridgeCtx;

    fprintf(stderr,"### %s():%d stUdsCtx addr %u ###\n", __func__,__LINE__, &stUdsCtx);
    setupServerAcceptEvent(&stUdsCtx);

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
    return unifiedServerRun("/tmp/uds1.sock", 5000);
}
#endif