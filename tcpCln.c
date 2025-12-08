/**
 * @file tcpCln.c
 * @brief EVENT_SOURCE + netTcp 기반 TCP Client (NO GLOBAL VARIABLES, stdin 이벤트 기반)
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <unistd.h>
 
 #include <event2/event.h>
 
 #include "eventSource.h"
 #include "dispatcher.h"
 #include "eventSession.h"
 #include "netTcp.h"
 #include "netCore.h"
 
 /* ============================================================
  * 서버 → 클라이언트 수신 콜백
  * ============================================================ */
 static void srvReadCb(EVENT_SOURCE* src,
                       const unsigned char* buf,
                       int len)
 {
     printf("[CLI] Recv (%d bytes): ", len);
     fwrite(buf, 1, len, stdout);
     printf("\n");
 }
 
 /* ============================================================
  * 서버 이벤트 콜백 (EOF / ERROR)
  * ============================================================ */
 static void srvEventCb(EVENT_SOURCE* src, short ev)
 {
     if (ev & BEV_EVENT_EOF)
         printf("[CLI] Server closed connection.\n");
     else if (ev & BEV_EVENT_ERROR)
         printf("[CLI] Socket error.\n");
 
     eventSourceDestroy(src);
 }
 
 /* ============================================================
  * stdin 이벤트 콜백
  * ============================================================ */
 static void stdinReadCb(evutil_socket_t fd, short what, void* arg)
 {
     (void)what;
     EVENT_SOURCE* src = (EVENT_SOURCE*)arg;
     char buf[256];
 
     ssize_t n = read(fd, buf, sizeof(buf));
     if (n > 0) {
         // 입력한 내용을 서버로 전송
         // 개행 포함해서 그대로 보냄
         // 필요하면 '\n' 처리 여기서 해도 됨
         fprintf(stderr, "[CLI] stdin %zd bytes → send\n", n);
         bufferevent_write(src->pstBev, buf, (int)n);
     } else if (n == 0) {
         // stdin EOF (Ctrl+D 등)
         printf("[CLI] stdin EOF. (no more input)\n");
         // 여기서 바로 loopexit 할지, 서버 종료까지 대기할지 선택 가능
         // 일단은 아무것도 안 하고 리턴만
     } else {
         perror("[CLI] read(stdin)");
     }
 }
 
 /* ============================================================
  * main()
  * ============================================================ */
 int main()
 {
     /* ------------------- */
     /* BASE_CONTEXT 생성   */
     /* ------------------- */
     BASE_CONTEXT stBaseCtx;
     baseContextInit(&stBaseCtx, 0x55);
 
     stBaseCtx.pstEventBase = event_base_new();
     if (!stBaseCtx.pstEventBase) {
         printf("[CLI] event_base_new failed\n");
         return -1;
     }
 
     /* ------------------- */
     /* TCP 연결            */
     /* ------------------- */
     int iFd = netTcpCreateClient("127.0.0.1", 5000);
     if (iFd < 0) {
         perror("netTcpCreateClient");
         event_base_free(stBaseCtx.pstEventBase);
         return -1;
     }
 
     printf("[CLI] Connecting to 127.0.0.1:5000...\n");
 
     /* ------------------- */
     /* EVENT_SOURCE 생성   */
     /* ------------------- */
     EVENT_SOURCE* src = eventSourceCreateBevStandalone(
         stBaseCtx.pstEventBase,
         iFd,
         SRC_TYPE_TCP_CLIENT,
         srvReadCb,
         srvEventCb);
 
     if (!src) {
         printf("[CLI] eventSourceCreateBevStandalone failed\n");
         netClose(iFd);
         event_base_free(stBaseCtx.pstEventBase);
         return -1;
     }
 
     /* ------------------- */
     /* stdin 이벤트 등록   */
     /* ------------------- */
     struct event* evStdin = event_new(
         stBaseCtx.pstEventBase,
         STDIN_FILENO,
         EV_READ | EV_PERSIST,
         stdinReadCb,
         src);  // arg로 EVENT_SOURCE 전달
 
     if (!evStdin) {
         printf("[CLI] evStdin create failed\n");
         eventSourceDestroy(src);
         event_base_free(stBaseCtx.pstEventBase);
         return -1;
     }
 
     event_add(evStdin, NULL);
 
     /* ------------------- */
     /* 이벤트 루프 실행    */
     /* ------------------- */
     event_base_dispatch(stBaseCtx.pstEventBase);
 
     /* clean-up */
     event_free(evStdin);
     baseContextCleanup(&stBaseCtx);
     event_base_free(stBaseCtx.pstEventBase);
 
     return 0;
 }
 