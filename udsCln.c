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
    EVENT_CONTEXT* pstEventCtx = (EVENT_CONTEXT*)pvData;
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
    EVENT_CONTEXT stEventCtx = (EVENT_CONTEXT){0};
    stEventCtx.pstSockCtx = (SOCK_CONTEXT*)calloc(1, sizeof(SOCK_CONTEXT));
    if (!stEventCtx.pstSockCtx) { 
        event_base_free(stEventCtx.pstEventBase);
        return; 
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

    /* UDS 주소 준비 */
    struct sockaddr_un stSocketUn;    
    memset(&stSocketUn, 0, sizeof(stSocketUn));
    stSocketUn.sun_family = AF_UNIX;
    strcpy(stSocketUn.sun_path, UDS_COMMAND_PATH);
    size_t ulSize = strlen(UDS_COMMAND_PATH);
    socklen_t uiSocketLength = (socklen_t)(offsetof(struct sockaddr_un, sun_path)+ulSize+1);

    stEventCtx.pstSockCtx->pstBufferEvent = bufferevent_socket_new(stEventCtx.pstEventBase, -1, BEV_OPT_CLOSE_ON_FREE);
    if (!stEventCtx.pstSockCtx->pstBufferEvent){
        free(stEventCtx.pstSockCtx);
        event_base_free(stEventCtx.pstEventBase);
        return 1;
    }
    
    bufferevent_setcb(stEventCtx.pstSockCtx->pstBufferEvent, readCallback, NULL, eventCallback, &stEventCtx);
    bufferevent_enable(stEventCtx.pstSockCtx->pstBufferEvent, EV_READ|EV_WRITE);
    bufferevent_setwatermark(stEventCtx.pstSockCtx->pstBufferEvent, EV_READ, sizeof(FRAME_HEADER), READ_HIGH_WM);    
    if (bufferevent_socket_connect(stEventCtx.pstSockCtx->pstBufferEvent, (struct sockaddr*)&stSocketUn, sizeof(stSocketUn)) < 0) {
        fprintf(stderr, "Connect failed: %s\n", strerror(errno));
        bufferevent_free(stEventCtx.pstSockCtx->pstBufferEvent);
        event_base_free(stEventCtx.pstEventBase);
        return 1;
    }

    /* STDIN Event 처리 */
    stEventCtx.pstEvent = event_new(stEventCtx.pstEventBase, fileno(stdin), EV_READ|EV_PERSIST, stdInCb, &stEventCtx);
    if (!stEventCtx.pstEvent || event_add(stEventCtx.pstEvent, NULL) < 0) {
        fprintf(stderr, "Could not add StdIn event\n");
        if (stEventCtx.pstSockCtx->pstBufferEvent) 
            bufferevent_free(stEventCtx.pstSockCtx->pstBufferEvent);
        event_base_free(stEventCtx.pstEventBase);
        return 1;
    }
    fprintf(stderr,"client: connecting to %s ...\n", UDS_COMMAND_PATH);
    event_base_dispatch(stEventCtx.pstEventBase);
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);

    if (stEventCtx.pstEvent) 
        event_free(stEventCtx.pstEvent);
    
    if (stEventCtx.pstEventBase) 
        event_base_free(stEventCtx.pstEventBase);

    printf("done\n");
    return 0;
 }
 