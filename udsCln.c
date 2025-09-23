#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#include <unistd.h>
#include <stddef.h>     /* offsetof */

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
        closeAndFree((void*)pvData);
        event_base_loopexit(pstEventCtx->pstEventBase, NULL);
    } else {
        printf("usage:\n  echo <text>\n  keepalive\n  ibit <n>\n  quit\n");
    }
 }
 
 /* === main === */
 int main(int argc, char** argv)
 {    
    int iClientId;
    EVENT_CONTEXT stEventCtx = (EVENT_CONTEXT){0};
    stEventCtx.iClientCount = 0;    
    if(argc != 2){        
        fprintf(stderr,"### %s():%d Error argument ###\n",__func__,__LINE__);
        return 0;
    }
    iClientId = atoi(argv[1]);
    stEventCtx.pstSockCtx = (SOCK_CONTEXT*)calloc(1, sizeof(SOCK_CONTEXT));
    if (!stEventCtx.pstSockCtx) { 
        event_base_free(stEventCtx.pstEventBase);
        return; 
    }
    stEventCtx.pstSockCtx->pstEventCtx = &stEventCtx;
    stEventCtx.pstSockCtx->pstNextSockCtx = NULL;
    stEventCtx.pstSockCtx->uchDstId = UDS1_SERVER_ID;
    if(iClientId == 1){
        stEventCtx.pstSockCtx->uchSrcId = UDS1_CLIENT1_ID;
        stEventCtx.uchMyId = UDS1_CLIENT1_ID;
    }else if(iClientId == 2){
        stEventCtx.pstSockCtx->uchSrcId = UDS1_CLIENT2_ID;
        stEventCtx.uchMyId = UDS1_CLIENT2_ID;
    }else if(iClientId == 3){
        stEventCtx.pstSockCtx->uchSrcId = UDS1_CLIENT3_ID;
        stEventCtx.uchMyId = UDS1_CLIENT3_ID;
    }else{
        fprintf(stderr,"### %s():%d Error Client ID ###\n",__func__,__LINE__);
        return 0;
    }

    stEventCtx.pstSockCtx->uchIsRespone = 0x00;
    stEventCtx.pstSockCtx->unPort = 0;
    stEventCtx.eRole = ROLE_CLIENT;

    signal(SIGPIPE, SIG_IGN);
    stEventCtx.pstEventBase = event_base_new();
    if (!stEventCtx.pstEventBase) {
        fprintf(stderr, "Could not initialize libevent!\n");
        return 1;
    }

    int iSockFd = createUdsClientSocket(UDS1_PATH);
    stEventCtx.pstSockCtx->pstBufferEvent = bufferevent_socket_new(stEventCtx.pstEventBase, iSockFd, BEV_OPT_CLOSE_ON_FREE);
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
    fprintf(stderr,"client: connecting to %s ...\n", UDS1_PATH);
    event_base_dispatch(stEventCtx.pstEventBase);
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);

    if (stEventCtx.pstEvent) 
        event_free(stEventCtx.pstEvent);
    
    if (stEventCtx.pstEventBase) 
        event_base_free(stEventCtx.pstEventBase);

    printf("done\n");
    return 0;
 }
 