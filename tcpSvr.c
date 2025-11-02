#include "netModule/protocols/tcp.h"
#include <event2/event.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void signalCallBack(evutil_socket_t sig, short ev, void *pvData)
{
    (void)sig; (void)ev;
    TCP_SERVER_CTX *pstTcpCtx = (TCP_SERVER_CTX *)pvData;
    printf("\n[TCP SERVER] SIGINT caught. Exiting...\n");
    event_base_loopbreak(pstTcpCtx->stNetBase.stCoreCtx.pstEventBase);
}

int main(int argc, char *argv[])
{
    unsigned short unPort = (argc > 1) ? atoi(argv[1]) : 9000;
    struct event_base *pstEventBase = event_base_new();
    if (!pstEventBase) {
        fprintf(stderr, "[ERROR] Failed to create event_base: %s\n", strerror(errno));
        return -1;
    }
    TCP_SERVER_CTX stTcpCtx;
    tcpSvrInit(&stTcpCtx, pstEventBase, 1, TCP_SERVER);
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    if (tcpServerStart(&stTcpCtx, unPort) < 0) {
        fprintf(stderr, "Failed to start TCP server\n");
        return 1;
    }
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);

    /* SIGINT(CTRL+C) 처리 */
    signal(SIGPIPE, SIG_IGN);    
    stTcpCtx.stNetBase.stCoreCtx.pstSignalEvent = evsignal_new(
        stTcpCtx.stNetBase.stCoreCtx.pstEventBase, 
        SIGINT, 
        signalCallBack, 
        &stTcpCtx);
    if (!stTcpCtx.stNetBase.stCoreCtx.pstSignalEvent || 
            event_add(stTcpCtx.stNetBase.stCoreCtx.pstSignalEvent, NULL) < 0) {
        fprintf(stderr, "Could not create/add SIGINT event!\n");
        tcpSvrStop(&stTcpCtx);
        return 1;
    }   

    printf("[TCP SERVER] Running on port %d\n", unPort);
    event_base_dispatch(pstEventBase);

    tcpSvrStop(&stTcpCtx);
    return 0;
}
