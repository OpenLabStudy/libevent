/*
 * Framed responder UDS client (Libevent, Unix Domain Socket)
 * - 서버가 먼저 REQ를 보내면, 클라이언트가 RES를 회신
 * - 서버 코드와 동일한 FRAME_HEADER/TAIL, CRC, byte order 사용
 *
 * Build: gcc -Wall -O2 -o udsClient udsClient.c -levent
 * Run  : ./udsClient /tmp/udsCommand.sock
 *
 * 필요 전제: "protocol.h"에서 아래 심볼/타입이 정의
 *   - STX_CONST, ETX_CONST
 *   - FRAME_HEADER { uint16_t unStx; int32_t iLength; MSG_ID stMsgId; char chSubModule; int16_t nCmd; }
 *   - FRAME_TAIL   { char chCrc; uint16_t unEtx; }
 *   - MSG_ID       { uint8_t chSrcId, chDstId; ... }
 *   - CMD_ECHO, CMD_KEEP_ALIVE, CMD_IBIT
 *   - REQ_KEEP_ALIVE, RES_KEEP_ALIVE, REQ_IBIT, RES_IBIT
 *   - uint8_t proto_crc8_xor(const uint8_t* buf, size_t len)
 */

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
        event_base_loopexit(pstEventCtx->pstEventBase, NULL);
    } else {
        printf("usage:\n  echo <text>\n  keepalive\n  ibit <n>\n  quit\n");
    }
 }
 
 /* === main === */
 int main(int argc, char** argv)
 {
    const char *chHost = "127.0.0.1";
    EVENT_CONTEXT stEventCtx = (EVENT_CONTEXT){0};
    
    signal(SIGPIPE, SIG_IGN);
    stEventCtx.pstEventBase = event_base_new();
    if (!stEventCtx.pstEventBase) {
        fprintf(stderr, "Could not initialize libevent!\n");
        return 1;
    }

    /* TCP 주소 준비 */
    struct sockaddr_in stSocketIn;
    memset(&stSocketIn,0,sizeof(stSocketIn));
    stSocketIn.sin_family = AF_INET;
    stSocketIn.sin_port   = htons((unsigned short)DEFAULT_PORT);
    if (inet_pton(AF_INET, chHost, &stSocketIn.sin_addr) != 1) {
        fprintf(stderr,"Bad host\n");
        event_base_free(stEventCtx.pstEventBase);        
        return 1;
    }

    stEventCtx.pstSockCtx = (SOCK_CONTEXT*)calloc(1, sizeof(SOCK_CONTEXT));
    if (!stEventCtx.pstSockCtx) { 
        event_base_free(stEventCtx.pstEventBase);
        return; 
    }
    stEventCtx.pstSockCtx->uchIsRespone = 0x00;

    stEventCtx.pstSockCtx->pstBufferEvent = bufferevent_socket_new(stEventCtx.pstEventBase, -1, BEV_OPT_CLOSE_ON_FREE);
    if (!stEventCtx.pstSockCtx->pstBufferEvent){
        free(stEventCtx.pstSockCtx);
        event_base_free(stEventCtx.pstEventBase);
        return 1;
    }
    
    bufferevent_setcb(stEventCtx.pstSockCtx->pstBufferEvent, tcpReadCb, NULL, tcpEventCb, &stEventCtx);
    bufferevent_enable(stEventCtx.pstSockCtx->pstBufferEvent, EV_READ|EV_WRITE);
    bufferevent_setwatermark(stEventCtx.pstSockCtx->pstBufferEvent, EV_READ, sizeof(FRAME_HEADER), READ_HIGH_WM);    
    if (bufferevent_socket_connect(stEventCtx.pstSockCtx->pstBufferEvent, (struct sockaddr*)&stSocketIn, sizeof(stSocketIn)) < 0) {
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
    fprintf(stderr,"client: connecting to %s:%u ...\n", chHost, DEFAULT_PORT);
    event_base_dispatch(stEventCtx.pstEventBase);

    if (stEventCtx.pstEvent) 
        event_free(stEventCtx.pstEvent);

    if (stEventCtx.pstSockCtx->pstBufferEvent)
        bufferevent_free(stEventCtx.pstSockCtx->pstBufferEvent);

    if (stEventCtx.pstEventBase) 
        event_base_free(stEventCtx.pstEventBase);

    printf("done\n");
    return 0;
 }
 