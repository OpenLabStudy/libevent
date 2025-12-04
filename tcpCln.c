/**
 * @file tcpCln.c
 * @brief Libevent 기반 TCP Client Application Layer (리팩토링 버전)
 *
 * BASE_CONTEXT + SOCK_CONTEXT 2레벨 구조 기반
 * - BASE_CONTEXT : event_base, signal, stdin 관리
 * - SOCK_CONTEXT : 서버와의 연결(bufferevent) 관리
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
  #include <signal.h>
 
 #include <event2/event.h>
 #include <event2/buffer.h>
 #include <event2/bufferevent.h>
 
 #include "netTcp.h"
 #include "netCore.h"
 #include "eventSession.h"
 #include "frame.h"
 #include "icdCommand.h"
 
 #define SERVER_IP   "127.0.0.1"
 #define SERVER_PORT 5000
 
 
 /* ========================================================================== */
 /* Static Function Prototypes                                                 */
 /* ========================================================================== */
 static void appReadCb(struct bufferevent* pstBufferEvent, void* pvData);
 static void appEventCb(struct bufferevent* pstBufferEvent, short nEvents, void* pvData);
 static void stdinReadCb(evutil_socket_t fd, short nEvents, void* pvData);
 static void signalCb(evutil_socket_t sig, short events, void* pvData);
 
 
 /* ========================================================================== */
 /* Application Read Callback                                                  */
 /* ========================================================================== */
 static void appReadCb(struct bufferevent* pstBufferEvent, void* pvData)
 {
     SOCK_CONTEXT* pstSockCtx = (SOCK_CONTEXT*)pvData;
     if (!pstSockCtx)
         return;
 
     struct evbuffer* pstEvBuffer = bufferevent_get_input(pstBufferEvent);
     size_t tDataLen = evbuffer_get_length(pstEvBuffer);
 
     fprintf(stderr, "[Client] Received %zu bytes\n", tDataLen);
 
     unsigned char* puchRecvData = malloc(tDataLen);
     if (!puchRecvData)
         return;
 
     evbuffer_copyout(pstEvBuffer, puchRecvData, tDataLen);
 
     MSG_ID stMsgId;
     stMsgId.uchSrcId = pstSockCtx->uchSrcId;
     stMsgId.uchDstId = pstSockCtx->uchDstId;
 
     responseFrame(puchRecvData, &stMsgId, tDataLen);
 
     evbuffer_drain(pstEvBuffer, tDataLen);
     free(puchRecvData);
 }
 
 
 /* ========================================================================== */
 /* Application Event Callback                                                 */
 /* ========================================================================== */
 static void appEventCb(struct bufferevent* pstBufferEvent,
                        short nEvents, void* pvData)
 {
     (void)pstBufferEvent;
 
     SOCK_CONTEXT* pstSockCtx = (SOCK_CONTEXT*)pvData;
     BASE_CONTEXT* pstBaseCtx = pstSockCtx->pstBaseCtx;
 
     if (nEvents & BEV_EVENT_CONNECTED)
         fprintf(stderr, "[Client] Connected to server.\n");
 
     if (nEvents & BEV_EVENT_EOF) {
         fprintf(stderr, "[Client] Server closed connection.\n");
         shutdownApp(pstBaseCtx);
     }
 
     if (nEvents & BEV_EVENT_ERROR) {
         fprintf(stderr, "[Client] Error: %s\n",
                 evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
         shutdownApp(pstBaseCtx);
     }
 }
 
 
 /* ========================================================================== */
 /* STDIN → Request Frame builder                                             */
 /* ========================================================================== */
 static void stdinReadCb(evutil_socket_t fd, short nEvents, void* pvData)
 {
     (void)fd;
     (void)nEvents;
 
     SOCK_CONTEXT* pstSockCtx = (SOCK_CONTEXT*)pvData;
     BASE_CONTEXT* pstBaseCtx = pstSockCtx->pstBaseCtx;
 
     char achInput[1024];
     unsigned char auSendBuf[1024];
     int iSendLen = 0;
     FRAME_ERR eErr;
 
     if (!fgets(achInput, sizeof(achInput), stdin)) {
         shutdownApp(pstBaseCtx);
         return;
     }
 
     achInput[strcspn(achInput, "\n")] = '\0';
 
     MSG_ID stMsgId = { pstSockCtx->uchSrcId, TCP_SVR_ID };
 
     if (!strcmp(achInput, "keepalive")) {
         fprintf(stderr,"[Client] REQ_KEEP_ALIVE\n");
         eErr = makeReqFrame(CMD_KEEP_ALIVE, &stMsgId, auSendBuf, &iSendLen);
 
     } else if (!strcmp(achInput, "ibit")) {
         fprintf(stderr,"[Client] REQ_IBIT\n");
         eErr = makeReqFrame(CMD_IBIT, &stMsgId, auSendBuf, &iSendLen);
 
     } else if (!strcmp(achInput, "quit") || !strcmp(achInput, "exit")) {
         shutdownApp(pstBaseCtx);
         return;
 
     } else {
         fprintf(stderr, "Available commands:\n  keepalive\n  ibit\n  quit\n");
         return;
     }
 
     if (eErr == FRAME_OK && iSendLen > 0)
         bufferevent_write(pstSockCtx->pstBufferEvent, auSendBuf, (size_t)iSendLen);
 }
 
 
 /* ========================================================================== */
 /* SIGINT Handler                                                             */
 /* ========================================================================== */
 static void signalCb(evutil_socket_t sig, short events, void* pvData)
 {
     (void)sig;
     (void)events;
 
     BASE_CONTEXT* pstBaseCtx = (BASE_CONTEXT*)pvData;
     shutdownApp(pstBaseCtx);
 }
 
 
 /* ========================================================================== */
 /* Main Entry                                                                  */
 /* ========================================================================== */
 int run(void)
 {
     BASE_CONTEXT stBaseCtx;
     baseContextInit(&stBaseCtx, TCP_CLN_ID);  // ID 설정
 
     /* ===== STEP 1: 서버 연결 ===== */
     int iSockFd = netTcpCreateClient(SERVER_IP, SERVER_PORT);
     if (iSockFd < 0) {
         fprintf(stderr, "[Client] Failed to create/connect TCP\n");
         return EXIT_FAILURE;
     }
 
     /* ===== STEP 2: event_base 생성 ===== */
     stBaseCtx.pstEventBase = event_base_new();
     if (!stBaseCtx.pstEventBase) {
         fprintf(stderr, "[Client] event_base_new() failed\n");
         return EXIT_FAILURE;
     }
 
     /* ===== STEP 3: SOCK_CONTEXT 생성 ===== */
     SOCK_CONTEXT* pstSockCtx = calloc(1, sizeof(SOCK_CONTEXT));
     if (!pstSockCtx) {
         perror("calloc");
         return EXIT_FAILURE;
     }
     initSocketContext(pstSockCtx, NULL, RESPONSE_ENABLED);
     pstSockCtx->pstBaseCtx = &stBaseCtx;
 
     pstSockCtx->pstBufferEvent =
         bufferevent_socket_new(stBaseCtx.pstEventBase,
                                iSockFd,
                                BEV_OPT_CLOSE_ON_FREE);
 
     if (!pstSockCtx->pstBufferEvent) {
         fprintf(stderr, "[Client] bufferevent_socket_new() failed\n");
         return EXIT_FAILURE;
     }
 
     /* ===== STEP 4: Callback 등록 ===== */
     bufferevent_setcb(pstSockCtx->pstBufferEvent,
                       appReadCb,
                       NULL,
                       appEventCb,
                       pstSockCtx);
 
     bufferevent_enable(pstSockCtx->pstBufferEvent, EV_READ | EV_WRITE);
 
 
     /* ===== STEP 5: STDIN 이벤트 등록 ===== */
     stBaseCtx.pstEvent = event_new(stBaseCtx.pstEventBase,
                                    fileno(stdin),
                                    EV_READ | EV_PERSIST,
                                    stdinReadCb,
                                    pstSockCtx);
     event_add(stBaseCtx.pstEvent, NULL);
 
     /* ===== STEP 6: SIGINT 등록 ===== */
     stBaseCtx.pstSignalEvent = evsignal_new(stBaseCtx.pstEventBase,
                                             SIGINT,
                                             signalCb,
                                             &stBaseCtx);
     event_add(stBaseCtx.pstSignalEvent, NULL);
 
 
     fprintf(stderr, "[Client] Connecting to %s:%d ...\n",
             SERVER_IP, SERVER_PORT);
 
     /* ===== STEP 7: 이벤트 루프 ===== */
     event_base_dispatch(stBaseCtx.pstEventBase);
 
     /* ===== STEP 8: Cleanup ===== */
     closeAndFree(pstSockCtx);
 
     if (stBaseCtx.pstSignalEvent)
         event_free(stBaseCtx.pstSignalEvent);
 
     if (stBaseCtx.pstEvent)
         event_free(stBaseCtx.pstEvent);
 
     if (stBaseCtx.pstEventBase)
         event_base_free(stBaseCtx.pstEventBase);
 
     return EXIT_SUCCESS;
 }
 
 
 #ifndef GOOGLE_TEST
 int main(int argc, char** argv)
 {
     (void)argc; (void)argv;
     return run();
 }
 #endif
 