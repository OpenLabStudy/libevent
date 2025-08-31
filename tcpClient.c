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
 
 /* === 프레임 송신 === */
 static void sendTcpFrame(struct bufferevent* pstBufferEvent, unsigned short unCmd,
                        const MSG_ID* pstMsgId, unsigned char uchSubModule,
                        const void* pvPayload, int iDataLength)
 {
     FRAME_HEADER stFrameHeader;
     FRAME_TAIL   stFrameTail;
 
     stFrameHeader.unStx             = htons(STX_CONST);
     stFrameHeader.iDataLength       = htonl(iDataLength);
     stFrameHeader.stMsgId.uchSrcId  = pstMsgId->uchSrcId;
     stFrameHeader.stMsgId.uchDstId  = pstMsgId->uchDstId;
     stFrameHeader.uchSubModule      = uchSubModule;
     stFrameHeader.unCmd             = htons(unCmd);
 
     stFrameTail.uchCrc              = proto_crc8_xor((const uint8_t*)pvPayload, (size_t)iDataLength);
     stFrameTail.unEtx               = htons(ETX_CONST);
 
     bufferevent_write(pstBufferEvent, &stFrameHeader, sizeof(stFrameHeader));
     if (iDataLength > 0 && pvPayload)
         bufferevent_write(pstBufferEvent, pvPayload, iDataLength);
     bufferevent_write(pstBufferEvent, &stFrameTail, sizeof(stFrameTail));
 }
 
 /* === 프레임 하나 파싱 및 처리 ===
  * return: 1 consumed, 0 need more, -1 fatal
  */
 static int try_consume_one_frame(struct evbuffer* pstEventBuffer, TCP_CLIENT_CTX* pstTcpCtx)
 {
     if (evbuffer_get_length(pstEventBuffer) < sizeof(FRAME_HEADER))
         return 0;
 
     FRAME_HEADER stFrameHeader;
     if (evbuffer_copyout(pstEventBuffer, &stFrameHeader, sizeof(stFrameHeader)) != (ssize_t)sizeof(stFrameHeader))
         return 0;
 
     unsigned short  unStx   = ntohs(stFrameHeader.unStx);
     int iDataLength = ntohl(stFrameHeader.iDataLength);
     unsigned short  unCmd  = ntohs(stFrameHeader.unCmd);
     MSG_ID stMsgId  = stFrameHeader.stMsgId;
 
     if (unStx != STX_CONST || iDataLength < 0 || iDataLength > (int32_t)MAX_PAYLOAD)
         return -1;
 
     int iNeedSize = sizeof(FRAME_HEADER) + (size_t)iDataLength + sizeof(FRAME_TAIL);
     if (evbuffer_get_length(pstEventBuffer) < iNeedSize)
         return 0;
 
     /* HEADER 소비 */
     evbuffer_drain(pstEventBuffer, sizeof(FRAME_HEADER));
 
     /* PAYLOAD */
     unsigned char* puchPayload = NULL;
     if (iDataLength > 0) {
         puchPayload = (uint8_t*)malloc((size_t)iDataLength);
         if (!puchPayload) 
             return -1;
         if (evbuffer_remove(pstEventBuffer, puchPayload, (size_t)iDataLength) != (ssize_t)iDataLength) {
             free(puchPayload);
             return -1;
         }
     }
 
     /* TAIL */
     FRAME_TAIL stFrameTail;
     if (evbuffer_remove(pstEventBuffer, &stFrameTail, sizeof(stFrameTail)) != (ssize_t)sizeof(stFrameTail)) {
         free(puchPayload);
         return -1;
     }
 
     unsigned char uchCrc = (iDataLength > 0) ? proto_crc8_xor(puchPayload, (size_t)iDataLength) : proto_crc8_xor((const uint8_t*)"", 0);
     if (ntohs(stFrameTail.unEtx) != ETX_CONST || uchCrc != (uint8_t)stFrameTail.uchCrc) {
         fprintf(stderr, "[CLIENT] CRC/ETX mismatch\n");
         free(puchPayload);
         return -1;
     }
 
     /* === 요청 처리: 서버가 보낸 REQ에 대해 RES 회신 === */
     /* 응답 MSG_ID는 통상 src/dst를 스왑하는 편이 자연스럽다 */
     MSG_ID stResMsgId = { .uchSrcId = pstTcpCtx->uchMyId, .uchDstId = stMsgId.uchSrcId };
 
     switch (unCmd) {
     case CMD_REQ_ID: {
         /* 요청 payload 검사 (옵션) */
         if ((int)sizeof(REQ_ID) == iDataLength) {
             const REQ_ID* pstReqId = (const REQ_ID*)puchPayload;
             (void)pstReqId; /* 필요 시 사용 */
         }
         RES_ID stResId = {0};
         /* 예: 0=OK 로 가정 */
         stResId.chResult = pstTcpCtx->uchMyId;
         sendTcpFrame(pstTcpCtx->pstBufferEvent, CMD_REQ_ID, &stResMsgId, 0, &stResId, (int32_t)sizeof(stResId));
         fprintf(stderr, "[CLIENT] RES ID sent\n");
         break;
     }
     case CMD_KEEP_ALIVE: {
         /* 요청 payload 검사 (옵션) */
         if ((int32_t)sizeof(REQ_KEEP_ALIVE) == iDataLength) {
             const REQ_KEEP_ALIVE* pstReqKeepAlive = (const REQ_KEEP_ALIVE*)puchPayload;
             (void)pstReqKeepAlive; /* 필요 시 사용 */
         }
         RES_KEEP_ALIVE stResKeepAlive = {0};
         /* 예: 0=OK 로 가정 */
         stResKeepAlive.chResult = 0;
         sendTcpFrame(pstTcpCtx->pstBufferEvent, CMD_KEEP_ALIVE, &stResMsgId, 0, &stResKeepAlive, (int32_t)sizeof(stResKeepAlive));
         fprintf(stderr, "[CLIENT] RES KEEP_ALIVE sent\n");
         break;
     }
     case CMD_IBIT: {
         if ((int32_t)sizeof(REQ_IBIT) == iDataLength) {
             const REQ_IBIT* pstReqIbit = (const REQ_IBIT*)puchPayload;
             (void)pstReqIbit; /* 필요 시 세부옵션 참고 */
         }
         RES_IBIT stResIbit = {0};
         /* 예: 0=OK 가정, 위치 결과도 0(정상) */
         stResIbit.chBitTotResult   = 0;
         stResIbit.chPositionResult = 0;
         sendTcpFrame(pstTcpCtx->pstBufferEvent, CMD_IBIT, &stResMsgId, 0, &stResIbit, (int32_t)sizeof(stResIbit));
         fprintf(stderr, "[CLIENT] RES IBIT sent\n");
         break;
     }
     default:
         /* 알 수 없는 요청 → 빈 응답이나 무시 (정책에 따라) */
         fprintf(stderr, "[CLIENT] Unknown REQ cmd=%d len=%d (ignored)\n", unCmd, iDataLength);
         break;
     }
 
     free(puchPayload);
     return 1;
 }
 
 /* === Libevent 콜백 === */
 static void tcpReadCb(struct bufferevent* pstBufferEvent, void* pvData)
 {
     (void)pstBufferEvent;
     TCP_CLIENT_CTX* pstTcpCtx = (TCP_CLIENT_CTX*)pvData;
     struct evbuffer* pstEvBuffer = bufferevent_get_input(pstTcpCtx->pstBufferEvent);
 
     for (;;) {
         int r = try_consume_one_frame(pstEvBuffer, pstTcpCtx);
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
        REQ_KEEP_ALIVE stReqKeepAlive = { .chTmp = 0 };
        sendTcpFrame(pstTcpCtx->pstBufferEvent, CMD_KEEP_ALIVE, &stMsgId, 0, &stReqKeepAlive, (int32_t)sizeof(stReqKeepAlive));
        printf("client: sent KEEP_ALIVE\n");
    } else if (strcmp(achStdInData, "ibit") == 0) {        
        int v = atoi(achStdInData+5);
        REQ_IBIT stReqIbit = { .chIbit = (char)v };
        sendTcpFrame(pstTcpCtx->pstBufferEvent, CMD_IBIT, &stMsgId, 0, &stReqIbit, (int32_t)sizeof(stReqIbit));
        printf("client: sent IBIT(%d)\n", v);
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
 