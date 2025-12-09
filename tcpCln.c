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
 #include "netTcp.h"
 #include "netCore.h"
 
 /* ============================================================
  * 서버 → 클라이언트 수신 콜백
  * ============================================================ */
static void readCallback(struct bufferevent* pstBufferEvent, void* pvData)
{
    unsigned char auchRecvBuffer[2048];
    struct evbuffer* pstInputBuffer = bufferevent_get_input(pstBufferEvent);
    while (1) {
        size_t tRecvLen = evbuffer_get_length(pstInputBuffer);
        fprintf(stderr,"### %s():%d %zu###\n",__func__,__LINE__, tRecvLen);        
        if (tRecvLen <= 0)
            break;
            
        int iCopyLen   = evbuffer_copyout(pstInputBuffer, auchRecvBuffer, tRecvLen);
        evbuffer_drain(pstInputBuffer, iCopyLen);
        fprintf(stderr,"### %s():%d read %s###\n",__func__,__LINE__,auchRecvBuffer);
    }
}
 
 /* ============================================================
  * 서버 이벤트 콜백 (EOF / ERROR)
  * ============================================================ */
static void eventCallback(struct bufferevent* pstBufferEvent,
    short nEvents, void* pvData)
{
    EVENT_SOURCE* pstEventSrc = (EVENT_SOURCE *)pvData;
    (void)pstBufferEvent;

    if (nEvents & BEV_EVENT_EOF) {
        fprintf(stderr, "[TCP-Client] Server disconnected\n");
    } else if (nEvents & BEV_EVENT_ERROR) {
        fprintf(stderr, "[TCP-Client] Client socket error\n");
    }

    /* 실제 close/free 는 eventSession 의 eventCallbackWrapper 에서 수행 */
    eventSourceDestroy(pstEventSrc);
    /* 이벤트 루프 종료 지시 */
    
    if (pstEventSrc->pstDispatcher->pstBaseCtx->pstEventBase)
        event_base_loopexit(pstEventSrc->pstDispatcher->pstBaseCtx->pstEventBase, NULL);
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
         fprintf(stderr, "[CLI] stdin %zd bytes → send\n", n);
         bufferevent_write(src->pstBufferEvent, buf, (int)n);
     } else if (n == 0) {
         printf("[CLI] stdin EOF. (no more input)\n");
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
     EVENT_SOURCE* pstEventSrc = eventSourceCreateBevStandalone(
         stBaseCtx.pstEventBase,
         iFd,
         SRC_TYPE_TCP_CLIENT,
         readCallback,
         eventCallback);
 
     if (!pstEventSrc) {
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
         pstEventSrc);  // arg로 EVENT_SOURCE 전달
 
     if (!evStdin) {
         printf("[CLI] evStdin create failed\n");
         eventSourceDestroy(pstEventSrc);
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
 