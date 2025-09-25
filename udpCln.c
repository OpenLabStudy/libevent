#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#include "frame.h"
#include "sockSession.h"
#include "icdCommand.h"
 
 static void stdInCb(evutil_socket_t sig, short nEvents, void* pvData)
 {
    (void)sig;
    (void)nEvents;
    SOCK_CONTEXT *pstSockCtx = (SOCK_CONTEXT *)pvData;
    EVENT_CONTEXT* pstEventCtx = pstSockCtx->pstEventCtx;
    MSG_ID stMsgId;
    char achStdInData[1024];
    if (!fgets(achStdInData, sizeof(achStdInData), stdin)) {
        event_base_loopexit(pstEventCtx->pstEventBase, NULL);
        return;
    }
    stMsgId.uchSrcId = pstEventCtx->pstSockCtx->uchSrcId;
    stMsgId.uchDstId = pstEventCtx->pstSockCtx->uchDstId;
    achStdInData[strcspn(achStdInData, "\n")] = '\0';
    if (strcmp(achStdInData, "keepalive") == 0) {
        printf("client: sent KEEP_ALIVE\n");
        requestFrame(pstEventCtx->pstSockCtx->pstBufferEvent, &stMsgId, CMD_KEEP_ALIVE);        
    } else if (strcmp(achStdInData, "ibit") == 0) {        
        printf("client: sent IBIT\n");
        requestFrame(pstEventCtx->pstSockCtx->pstBufferEvent, &stMsgId, CMD_IBIT);        
    } else if (!strcmp(achStdInData, "quit") || !strcmp(achStdInData, "exit")) {
        event_base_loopexit(pstEventCtx->pstEventBase, NULL);
    } else {
        printf("usage:\n  echo <text>\n  keepalive\n  ibit <n>\n  quit\n");
    }
 }
 
 /* === main === */
 int main(int argc, char** argv)
 {
    EVENT_CONTEXT stEventCtx = (EVENT_CONTEXT){0};
    stEventCtx.eRole = ROLE_CLIENT;
    stEventCtx.iClientCount = 0;
    
    stEventCtx.pstEventBase = event_base_new();
    if (!stEventCtx.pstEventBase) {
        fprintf(stderr, "Could not initialize libevent!\n");
        return 1;
    }

    stEventCtx.pstSockCtx = (SOCK_CONTEXT*)calloc(1, sizeof(SOCK_CONTEXT));
    if (!stEventCtx.pstSockCtx) { 
        event_base_free(stEventCtx.pstEventBase);
        return 1;  
    }
    stEventCtx.pstSockCtx->pstEventCtx = &stEventCtx;
    signal(SIGPIPE, SIG_IGN);    
    // Todo iListenFd 변수 이름 재정의 필요
    stEventCtx.iListenFd = createTcpUdpClientSocket("127.0.0.1", UDP_SERVER_PORT, SOCK_TYPE_UDP);    
    stEventCtx.pstSockCtx->pstBufferEvent = bufferevent_socket_new(stEventCtx.pstEventBase, stEventCtx.iListenFd, BEV_OPT_CLOSE_ON_FREE);
    if (!stEventCtx.pstSockCtx->pstBufferEvent){
        free(stEventCtx.pstSockCtx);
        event_base_free(stEventCtx.pstEventBase);
        return 1;
    }
    
    bufferevent_setcb(stEventCtx.pstSockCtx->pstBufferEvent, readCallback, NULL, eventCallback, stEventCtx.pstSockCtx);
    bufferevent_enable(stEventCtx.pstSockCtx->pstBufferEvent, EV_READ|EV_WRITE);
    bufferevent_setwatermark(stEventCtx.pstSockCtx->pstBufferEvent, EV_READ, sizeof(FRAME_HEADER), READ_HIGH_WM);    

    /* STDIN Event 처리 */
    stEventCtx.pstEvent = event_new(stEventCtx.pstEventBase, fileno(stdin), EV_READ|EV_PERSIST, stdInCb, stEventCtx.pstSockCtx);
    if (!stEventCtx.pstEvent || event_add(stEventCtx.pstEvent, NULL) < 0) {
        fprintf(stderr, "Could not add StdIn event\n");
        if (stEventCtx.pstSockCtx->pstBufferEvent) 
            bufferevent_free(stEventCtx.pstSockCtx->pstBufferEvent);
        event_base_free(stEventCtx.pstEventBase);
        return 1;
    }
    fprintf(stderr,"client: connecting to %s:%u ...\n", stEventCtx.pstSockCtx->achSockAddr, stEventCtx.pstSockCtx->unPort);
    event_base_dispatch(stEventCtx.pstEventBase);

    if (stEventCtx.pstEvent) 
        event_free(stEventCtx.pstEvent);

    if (stEventCtx.pstEventBase) 
        event_base_free(stEventCtx.pstEventBase);

    printf("done\n");
    return 0;
 }
 
