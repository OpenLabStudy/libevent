#include "netModule/protocols/uds.h"
#include "netModule/core/frame.h"
#include "netModule/core/icdCommand.h"
#include <event2/event.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#if 0
static void signalCallBack(evutil_socket_t sig, short ev, void *pvData)
{
    (void)sig; (void)ev;
    UDS_CLIENT_CTX *pstUdsClnCtx = (UDS_CLIENT_CTX *)pvData;
    printf("\n[UDS CLIENT] SIGINT caught. Exiting...\n");
    event_base_loopbreak(pstUdsClnCtx->stNetBase.stCoreCtx.pstEventBase);
}
#endif

static void stdInCallBack(evutil_socket_t sig, short nEvents, void* pvData)
{
    (void)sig;
    (void)nEvents;
    UDS_CLIENT_CTX *pstUdsClnCtx = (UDS_CLIENT_CTX *)pvData;
    MSG_ID stMsgId;
    char achStdInData[1024];
    if (!fgets(achStdInData, sizeof(achStdInData), stdin)) {
        event_base_loopexit(pstUdsClnCtx->stNetBase.stCoreCtx.pstEventBase, NULL);
        return;
    }
    stMsgId.uchSrcId = pstUdsClnCtx->stNetBase.uchMyId;
    stMsgId.uchDstId = 1;
    achStdInData[strcspn(achStdInData, "\n")] = '\0';
    if (strcmp(achStdInData, "keepalive") == 0) {
        printf("client: sent KEEP_ALIVE\n");       
        requestFrame(pstUdsClnCtx->pstBufferEvent, &stMsgId, CMD_KEEP_ALIVE);        
    } else if (strcmp(achStdInData, "ibit") == 0) {        
        printf("client: sent IBIT\n");
        requestFrame(pstUdsClnCtx->pstBufferEvent, &stMsgId, CMD_IBIT);        
    } else if (!strcmp(achStdInData, "quit") || !strcmp(achStdInData, "exit")) {
        event_base_loopexit(pstUdsClnCtx->stNetBase.stCoreCtx.pstEventBase, NULL);
    } else {
        printf("usage:\n  echo <text>\n  keepalive\n  ibit <n>\n  quit\n");
    }
}

int main(int argc, char *argv[])
{
    const char *pchPath = (argc > 1) ? argv[1] : "/tmp/uds_server.sock";
    struct event_base *pstEventBase = event_base_new();

    UDS_CLIENT_CTX stUdsClnCtx;
    udsClnInit(&stUdsClnCtx, pstEventBase, 31, UDS_CLIENT);

    if (udsClientStart(&stUdsClnCtx, pchPath) < 0) {
        fprintf(stderr, "Failed to connect UDS server\n");
        return 1;
    }
    #if 0
    /* SIGINT(CTRL+C) 처리 */
    signal(SIGPIPE, SIG_IGN);    
    stUdsClnCtx.stNetBase.stCoreCtx.pstSignalEvent = evsignal_new(
        stUdsClnCtx.stNetBase.stCoreCtx.pstEventBase, 
        SIGINT, 
        signalCallBack, 
        &stUdsClnCtx);
    if (!stUdsClnCtx.stNetBase.stCoreCtx.pstSignalEvent || 
            event_add(stUdsClnCtx.stNetBase.stCoreCtx.pstSignalEvent, NULL) < 0) {
        fprintf(stderr, "Could not create/add SIGINT event!\n");
        udsClnStop(&stUdsClnCtx);
        return 1;
    }
    #endif
    /* STDIN Event 처리 */
    signal(SIGPIPE, SIG_IGN);
    stUdsClnCtx.stNetBase.stCoreCtx.pstSignalEvent = event_new(
        stUdsClnCtx.stNetBase.stCoreCtx.pstEventBase, 
        fileno(stdin), 
        EV_READ|EV_PERSIST, 
        stdInCallBack, 
        &stUdsClnCtx);
    if (!stUdsClnCtx.stNetBase.stCoreCtx.pstSignalEvent || 
            event_add(stUdsClnCtx.stNetBase.stCoreCtx.pstSignalEvent, NULL) < 0) {
        fprintf(stderr, "Could not add StdIn event\n");
        if (stUdsClnCtx.stNetBase.stCoreCtx.pstSignalEvent) 
            event_free(stUdsClnCtx.stNetBase.stCoreCtx.pstSignalEvent);
        
        udsClnStop(&stUdsClnCtx);
        return 1;
    }

    printf("[UDS CLIENT] Connected to %s\n", pchPath);
    printf("[UDS CLIENT] Type messages and press Enter.\n");

    event_base_dispatch(pstEventBase);

    event_free(stUdsClnCtx.stNetBase.stCoreCtx.pstSignalEvent);
    udsClnStop(&stUdsClnCtx);
    return 0;
}
