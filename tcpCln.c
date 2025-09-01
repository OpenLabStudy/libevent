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
 
 #include <sys/types.h>
 #include <sys/socket.h>
 #include <sys/un.h>
 #include <unistd.h>
 #include <stddef.h> /* offsetof */
 
 #include <event2/event.h>
 #include <event2/buffer.h>
 #include <event2/bufferevent.h>
 #include <event2/util.h>
 
 #include "protocol.h"
 
 /* 서버 코드와 일치시키기 */
 #define DEFAULT_PORT       9995
 #define READ_HIGH_WM       (1u * 1024u * 1024u)   /* 1MB */
 #define MAX_PAYLOAD        (4u * 1024u * 1024u)   /* 4MB */
 
 typedef struct {
    struct event_base   *pstEventBase;
    struct bufferevent  *pstBufferEvent;
    struct event        *pstEventStdIn;
    unsigned char       uchMyId;
 } TCP_CLIENT_CTX;
 
 /* === 전방 선언 === */
 static void tcpReadCb(struct bufferevent*, void*);
 static void tcpEventCb(struct bufferevent*, short, void*);
 static void stdInCb(evutil_socket_t, short, void*);
 
 
 /* === Libevent 콜백 === */
 static void tcpReadCb(struct bufferevent* pstBufferEvent, void* pvData)
 {
     (void)pstBufferEvent;
     TCP_CLIENT_CTX* pstTcpCtx = (TCP_CLIENT_CTX*)pvData;
     struct evbuffer* pstEvBuffer = bufferevent_get_input(pstTcpCtx->pstBufferEvent);
     MSG_ID stMsgId;
    stMsgId.uchSrcId = pstTcpCtx->uchMyId;
    stMsgId.uchDstId = 0x00;
    fprintf(stderr,"### %s():%d ###", __func__, __LINE__);
     for (;;) {
         int r = responseFrame(pstEvBuffer, pstTcpCtx->pstBufferEvent, &stMsgId, 0x00);
         if (r == 0) break;
         if (r < 0) {
             fprintf(stderr, "[CLIENT] fatal parse error -> closing\n");
             bufferevent_free(pstTcpCtx->pstBufferEvent);
             pstTcpCtx->pstBufferEvent = NULL;
             event_base_loopexit(pstTcpCtx->pstEventBase, NULL);
             return;
         }
     }
 }


 
 static void tcpEventCb(struct bufferevent* pstBufferEvent, short nEvents, void* pvData)
 {
     TCP_CLIENT_CTX* pstTcpCtx = (TCP_CLIENT_CTX*)pvData;
     if (nEvents & BEV_EVENT_CONNECTED) {
         fprintf(stderr, "[CLIENT] connected\n");
         fprintf(stderr, "keepalive\n");
         fprintf(stderr, "ibit\n");
         fprintf(stderr, "quit\n");
     }

     if (nEvents & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
         fprintf(stderr, "[CLIENT] disconnected\n");
         if (pstTcpCtx->pstBufferEvent) { 
             bufferevent_free(pstTcpCtx->pstBufferEvent); 
             pstTcpCtx->pstBufferEvent = NULL; 
         }
         event_base_loopexit(pstTcpCtx->pstEventBase, NULL);
     }
 }
 
 static void stdInCb(evutil_socket_t sig, short nEvents, void* pvData)
 {
    (void)sig;
    (void)nEvents;    
    TCP_CLIENT_CTX* pstTcpCtx = (TCP_CLIENT_CTX*)pvData;
    MSG_ID stMsgId = {.uchSrcId = pstTcpCtx->uchMyId, .uchDstId = 0x0};
    char achStdInData[1024];
    if (!fgets(achStdInData, sizeof(achStdInData), stdin)) {
        event_base_loopexit(pstTcpCtx->pstEventBase, NULL);
        return;
    }
    achStdInData[strcspn(achStdInData, "\n")] = '\0';
    if (strcmp(achStdInData, "keepalive") == 0) {
        requestFrame(pstTcpCtx->pstBufferEvent, &stMsgId, CMD_KEEP_ALIVE);
        printf("client: sent KEEP_ALIVE\n");
    } else if (strcmp(achStdInData, "ibit") == 0) {        
        requestFrame(pstTcpCtx->pstBufferEvent, &stMsgId, CMD_IBIT);
        printf("client: sent IBIT\n");
    } else if (!strcmp(achStdInData, "quit") || !strcmp(achStdInData, "exit")) {
        event_base_loopexit(pstTcpCtx->pstEventBase, NULL);
    } else {
        printf("usage:\n  echo <text>\n  keepalive\n  ibit <n>\n  quit\n");
    }
 }
 
 /* === main === */
 int main(int argc, char** argv)
 {
    const char *pchHostAddr = (argc > 1) ? argv[1] : "127.0.0.1";
    unsigned short unPort = (argc > 2) ? (unsigned short)atoi(argv[2]) : DEFAULT_PORT;
    TCP_CLIENT_CTX stTcpCtx;
    memset(&stTcpCtx, 0, sizeof(stTcpCtx));
    stTcpCtx.uchMyId = 0x08;
    

    signal(SIGPIPE, SIG_IGN);
    stTcpCtx.pstEventBase = event_base_new();
    if (!stTcpCtx.pstEventBase) {
        fprintf(stderr, "Could not init libevent\n");
        return 1;
    }

    /* 소켓 주소 준비 */
    struct sockaddr_in stSocketIn;
    stSocketIn.sin_family = AF_INET;
    stSocketIn.sin_port = htons(unPort);
    if (inet_pton(AF_INET, pchHostAddr, &stSocketIn.sin_addr) != 1) {
        fprintf(stderr,"Bad host\n");
        event_base_free(stTcpCtx.pstEventBase);
        return 1;
    }

    /* bufferevent + connect */
    stTcpCtx.pstBufferEvent = bufferevent_socket_new(stTcpCtx.pstEventBase, -1, BEV_OPT_CLOSE_ON_FREE);
    if (!stTcpCtx.pstBufferEvent) {
        fprintf(stderr, "Could not create bufferevent\n");
        event_base_free(stTcpCtx.pstEventBase);
        return 1;
    }

    bufferevent_setcb(stTcpCtx.pstBufferEvent, tcpReadCb, NULL, tcpEventCb, &stTcpCtx);
    bufferevent_enable(stTcpCtx.pstBufferEvent, EV_READ | EV_WRITE);
    bufferevent_setwatermark(stTcpCtx.pstBufferEvent, EV_READ, 0, READ_HIGH_WM);

    if (bufferevent_socket_connect(stTcpCtx.pstBufferEvent, (struct sockaddr*)&stSocketIn, sizeof(stSocketIn)) < 0) {
        fprintf(stderr, "Connect failed: %s\n", strerror(errno));
        bufferevent_free(stTcpCtx.pstBufferEvent);
        event_base_free(stTcpCtx.pstEventBase);
        return 1;
    }

    /* STDIN Event 처리 */
    stTcpCtx.pstEventStdIn = event_new(stTcpCtx.pstEventBase, fileno(stdin), EV_READ|EV_PERSIST, stdInCb, &stTcpCtx);
    if (!stTcpCtx.pstEventStdIn || event_add(stTcpCtx.pstEventStdIn, NULL) < 0) {
        fprintf(stderr, "Could not add StdIn event\n");
        if (stTcpCtx.pstBufferEvent) 
            bufferevent_free(stTcpCtx.pstBufferEvent);
        event_base_free(stTcpCtx.pstEventBase);
        return 1;
    }
    fprintf(stderr,"client: connecting to %s:%u ...\n", pchHostAddr, unPort);

    event_base_dispatch(stTcpCtx.pstEventBase);

    if (stTcpCtx.pstEventStdIn) 
        event_free(stTcpCtx.pstEventStdIn);

    if (stTcpCtx.pstBufferEvent) 
        bufferevent_free(stTcpCtx.pstBufferEvent);

    if (stTcpCtx.pstEventBase) 
        event_base_free(stTcpCtx.pstEventBase);

    printf("done\n");
    return 0;
 }
 