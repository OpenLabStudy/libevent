#include "netModule/protocols/tcp.h"
#include "netModule/core/frame.h"
#include "netModule/core/icdCommand.h"
#include <event2/event.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if 0
static void signalCallBack(evutil_socket_t sig, short ev, void *pvData)
{
    (void)sig; (void)ev;
    TCP_CLIENT_CTX *pstTcpCtx = (TCP_CLIENT_CTX *)pvData;
    printf("\n[TCP CLIENT] SIGINT caught. Exiting...\n");
    event_base_loopbreak(pstTcpCtx->stNetBase.stCoreCtx.pstEventBase);
}
#endif

static void stdInCallBack(evutil_socket_t sig, short nEvents, void* pvData)
{
    (void)sig;
    (void)nEvents;
    TCP_CLIENT_CTX *pstTcpCtx = (TCP_CLIENT_CTX *)pvData;
    MSG_ID stMsgId;
    char achStdInData[1024];
    if (!fgets(achStdInData, sizeof(achStdInData), stdin)) {
        event_base_loopexit(pstTcpCtx->stNetBase.stCoreCtx.pstEventBase, NULL);
        return;
    }
    stMsgId.uchSrcId = pstTcpCtx->stNetBase.uchMyId;
    stMsgId.uchDstId = 1;
    achStdInData[strcspn(achStdInData, "\n")] = '\0';
    if (strcmp(achStdInData, "keepalive") == 0) {
        printf("client: sent KEEP_ALIVE\n");       
        requestFrame(pstTcpCtx->pstBufferEvent, &stMsgId, CMD_KEEP_ALIVE);        
    } else if (strcmp(achStdInData, "ibit") == 0) {        
        printf("client: sent IBIT\n");
        requestFrame(pstTcpCtx->pstBufferEvent, &stMsgId, CMD_IBIT);        
    } else if (!strcmp(achStdInData, "quit") || !strcmp(achStdInData, "exit")) {
        event_base_loopexit(pstTcpCtx->stNetBase.stCoreCtx.pstEventBase, NULL);
    } else {
        printf("usage:\n  echo <text>\n  keepalive\n  ibit <n>\n  quit\n");
    }
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        printf("Usage: %s <ip> <port>\n", argv[0]);
        return 1;
    }

    const char *pchIp = argv[1];
    unsigned short unPort = atoi(argv[2]);

    struct event_base *pstEventBase = event_base_new();
    TCP_CLIENT_CTX stTcpCtx;
    tcpClnInit(&stTcpCtx, pstEventBase, 2, TCP_CLIENT);

    if (tcpClientConnect(&stTcpCtx, pchIp, unPort) < 0) {
        fprintf(stderr, "Failed to connect TCP server\n");
        return 1;
    }
#if 0
    /* SIGINT(CTRL+C) 처리 */
    signal(SIGPIPE, SIG_IGN);    
    stTcpCtx.stNetBase.stCoreCtx.pstSigIntEvent = evsignal_new(
        stTcpCtx.stNetBase.stCoreCtx.pstEventBase, 
        SIGINT, 
        signalCallBack, 
        &stTcpCtx);
    if (!stTcpCtx.stNetBase.stCoreCtx.pstSigIntEvent || 
            event_add(stTcpCtx.stNetBase.stCoreCtx.pstSigIntEvent, NULL) < 0) {
        fprintf(stderr, "Could not create/add SIGINT event!\n");
        tcpClnStop(&stTcpCtx);
        return 1;
    }
#endif
    /* STDIN Event 처리 */
    signal(SIGPIPE, SIG_IGN);
    stTcpCtx.stNetBase.stCoreCtx.pstSignalEvent = event_new(
        stTcpCtx.stNetBase.stCoreCtx.pstEventBase, 
        fileno(stdin), 
        EV_READ|EV_PERSIST, 
        stdInCallBack, 
        &stTcpCtx);
    if (!stTcpCtx.stNetBase.stCoreCtx.pstSignalEvent || 
            event_add(stTcpCtx.stNetBase.stCoreCtx.pstSignalEvent, NULL) < 0) {
        fprintf(stderr, "Could not add StdIn event\n");
        if (stTcpCtx.stNetBase.stCoreCtx.pstSignalEvent) 
            event_free(stTcpCtx.stNetBase.stCoreCtx.pstSignalEvent);
        
        tcpClnStop(&stTcpCtx);
        return 1;
    }

    printf("[TCP CLIENT] Connected to %s:%d\n", pchIp, unPort);
    printf("[TCP CLIENT] Type message and press Enter.\n");

    event_base_dispatch(pstEventBase);

    tcpClnStop(&stTcpCtx);
    return 0;
}
