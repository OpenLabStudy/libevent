/**
 * @file tcpUdsSvr.c
 * @brief 전역변수 없이 동작하는 TCP↔UDS 브리지 서버
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <signal.h>
 #include <errno.h>
 #include <unistd.h>
 
 #include <event2/event.h>
 #include <event2/util.h>
 
 #include "netTcp.h"
 #include "netUds.h"
 #include "netCore.h"
 #include "eventEngine.h"
 #include "eventSource.h"
 
 
 #define TCP_PORT 5000
 #define UDS_PATH "/tmp/bridge_uds.sock"
 
 
 /* ------------------------------------------------------------ */
 /* 서버 전체 컨텍스트 (전역 제거)                               */
 /* ------------------------------------------------------------ */
 typedef struct _SVR_CONTEXT
 {
     BASE_CONTEXT    base;
     DISPATCHER      disp;
 
     int             tcpListenFd;
     int             udsListenFd;
 
     struct event*   tcpAcceptEvt;
     struct event*   udsAcceptEvt;
     struct event*   signalEvt;
 } SVR_CONTEXT;
 
 
 /* ------------------------------------------------------------ */
 /* Forward declarations                                          */
 /* ------------------------------------------------------------ */
 static void tcpReadCb(EVENT_SOURCE*, const unsigned char*, int);
 static void udsReadCb(EVENT_SOURCE*, const unsigned char*, int);
 static void srcEventCb(EVENT_SOURCE*, short);
 
 static void tcpAcceptCb(evutil_socket_t, short, void*);
 static void udsAcceptCb(evutil_socket_t, short, void*);
 static void signalCb(evutil_socket_t, short, void*);
 
 
 /* ------------------------------------------------------------ */
 /* FD Accept 공통 처리                                           */
 /* ------------------------------------------------------------ */
 static void acceptCommon(SVR_CONTEXT* ctx,
                          int listenFd,
                          SRC_TYPE type,
                          SRC_ROLE role,
                          void (*pfRead)(EVENT_SOURCE*, const unsigned char*, int))
 {
     while (1) {
         struct sockaddr_storage ss;
         socklen_t slen = sizeof(ss);
 
         int cfd = accept(listenFd, (struct sockaddr*)&ss, &slen);
         if (cfd < 0) {
             if (errno == EAGAIN || errno == EWOULDBLOCK)
                 return;
             perror("accept");
             return;
         }
 
         evutil_make_socket_nonblocking(cfd);
         evutil_make_socket_closeonexec(cfd);
 
         EVENT_SOURCE* src = eventSourceCreateWithBev(
             &ctx->disp,
             cfd,
             type,
             role,
             pfRead,
             srcEventCb
         );
 
         if (!src) {
             fprintf(stderr, "[Svr] eventSourceCreateWithBev() failed\n");
             close(cfd);
         } else {
             fprintf(stderr, "[Svr] new %s client accepted (fd=%d)\n",
                     (type == SRC_TYPE_TCP_CLIENT) ? "TCP" : "UDS",
                     cfd);
         }
     }
 }
 
 
 /* ------------------------------------------------------------ */
 /* TCP Requester 읽기 콜백                                       */
 /* ------------------------------------------------------------ */
 static void tcpReadCb(EVENT_SOURCE* src,
                       const unsigned char* buf, int len)
 {
     fprintf(stderr, "[Svr] TCP recv %d bytes\n", len);
     dispatcherHandleRequest(src->pstDispatcher, src, buf, len);
 }
 
 /* ------------------------------------------------------------ */
 /* UDS Worker 읽기 콜백                                          */
 /* ------------------------------------------------------------ */
 static void udsReadCb(EVENT_SOURCE* src,
                       const unsigned char* buf, int len)
 {
     fprintf(stderr, "[Svr] UDS recv %d bytes\n", len);
     dispatcherHandleWorkerResponse(src->pstDispatcher, src, buf, len);
 }
 
 /* ------------------------------------------------------------ */
 /* 개별 소켓 이벤트 콜백                                         */
 /* ------------------------------------------------------------ */
 static void srcEventCb(EVENT_SOURCE* src, short events)
 {
     if (events & BEV_EVENT_EOF)
         fprintf(stderr, "[Svr] connection closed\n");
     if (events & BEV_EVENT_ERROR)
         fprintf(stderr, "[Svr] error: %s\n",
                 evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
 }
 
 
 /* ------------------------------------------------------------ */
 /* Accept 이벤트 콜백                                             */
 /* ------------------------------------------------------------ */
 static void tcpAcceptCb(evutil_socket_t fd, short ev, void* arg)
 {
     (void)ev;
     SVR_CONTEXT* ctx = (SVR_CONTEXT*)arg;
     acceptCommon(ctx, ctx->tcpListenFd,
                  SRC_TYPE_TCP_CLIENT, SRC_ROLE_REQUESTER, tcpReadCb);
 }
 
 static void udsAcceptCb(evutil_socket_t fd, short ev, void* arg)
 {
     (void)ev;
     SVR_CONTEXT* ctx = (SVR_CONTEXT*)arg;
     acceptCommon(ctx, ctx->udsListenFd,
                  SRC_TYPE_UDS_CLIENT, SRC_ROLE_WORKER, udsReadCb);
 }
 
 
 /* ------------------------------------------------------------ */
 /* SIGINT 이벤트 콜백                                            */
 /* ------------------------------------------------------------ */
 static void signalCb(evutil_socket_t sig, short ev, void* arg)
 {
     (void)ev;
     SVR_CONTEXT* ctx = (SVR_CONTEXT*)arg;
 
     fprintf(stderr, "\n[Svr] SIGINT received → stopping loop\n");
 
     event_base_loopexit(ctx->base.pstEventBase, NULL);
 }
 
 
 /* ------------------------------------------------------------ */
 /* main                                                          */
 /* ------------------------------------------------------------ */
 int main(void)
 {
     SVR_CONTEXT ctx;
     memset(&ctx, 0, sizeof(ctx));
 
     /* Base 초기화 */
     baseContextInit(&ctx.base, 0x01);
     ctx.base.pstEventBase = event_base_new();
 
     /* Dispatcher 초기화 */
     dispatcherInit(&ctx.disp, &ctx.base);
 
     /* TCP Listen */
     ctx.tcpListenFd = netTcpCreateServer(TCP_PORT);
     if (ctx.tcpListenFd < 0) {
         fprintf(stderr, "TCP listen fail\n");
         return -1;
     }
     evutil_make_socket_nonblocking(ctx.tcpListenFd);
 
     /* UDS Listen */
     ctx.udsListenFd = netUdsCreateServer(UDS_PATH);
     if (ctx.udsListenFd < 0) {
         fprintf(stderr, "UDS listen fail\n");
         return -1;
     }
     evutil_make_socket_nonblocking(ctx.udsListenFd);
 
     /* Accept 이벤트 등록 */
     ctx.tcpAcceptEvt = event_new(ctx.base.pstEventBase,
                                  ctx.tcpListenFd,
                                  EV_READ | EV_PERSIST,
                                  tcpAcceptCb, &ctx);
     event_add(ctx.tcpAcceptEvt, NULL);
 
     ctx.udsAcceptEvt = event_new(ctx.base.pstEventBase,
                                  ctx.udsListenFd,
                                  EV_READ | EV_PERSIST,
                                  udsAcceptCb, &ctx);
     event_add(ctx.udsAcceptEvt, NULL);
 
     /* SIGINT 처리 */
     ctx.signalEvt = evsignal_new(ctx.base.pstEventBase,
                                  SIGINT,
                                  signalCb, &ctx);
     event_add(ctx.signalEvt, NULL);
 
 
     fprintf(stderr,
             "[Svr] Bridge Server started ✔\n"
             "      TCP : %d\n"
             "      UDS : %s\n",
             TCP_PORT, UDS_PATH);
 
     /* 이벤트 루프 */
     event_base_dispatch(ctx.base.pstEventBase);
 
 
     /* 종료 처리 */
     dispatcherCleanup(&ctx.disp);
 
     if (ctx.tcpAcceptEvt) event_free(ctx.tcpAcceptEvt);
     if (ctx.udsAcceptEvt) event_free(ctx.udsAcceptEvt);
     if (ctx.signalEvt)    event_free(ctx.signalEvt);
 
     if (ctx.base.pstEventBase)
         event_base_free(ctx.base.pstEventBase);
 
     if (ctx.tcpListenFd >= 0) close(ctx.tcpListenFd);
     if (ctx.udsListenFd >= 0) close(ctx.udsListenFd);
 
     return 0;
 }
 