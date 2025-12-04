/**
 * @file eventSession.c
 * @brief Libevent 기반 Session 관리 구현부
 *
 * - 서버 모드(TCP 우선)를 중심으로 BASE/.SERVER/SOCK Context를 분리하여 관리
 * - bufferevent 기반 비동기 Read/Write 이벤트 처리
 * - Accept 기반 다중 접속 처리(Server mode)
 * - Callback Wrapper를 통한 사용자 정의 콜백 호출 구조
 *
 * @see eventSession.h
 */

 #include "eventSession.h"
 #include "netCore.h"
 
 #include <stdlib.h>
 #include <string.h>
 #include <stdio.h>
 #include <errno.h>
 
 /* ========================================================================== */
 /* Internal Helper Prototypes                                                 */
 /* ========================================================================== */
 
 /**
  * @brief SOCK_CONTEXT를 서버 클라이언트 리스트에 추가
  *
  * @param pstSockCtx   대상 SOCK_CONTEXT
  * @param pstServerCtx 소속 SERVER_CONTEXT
  */
 static void addClient(SOCK_CONTEXT* pstSockCtx, SERVER_CONTEXT* pstServerCtx);
 
 /**
  * @brief SOCK_CONTEXT를 서버 클라이언트 리스트에서 제거
  *
  * @param pstSockCtx   제거할 SOCK_CONTEXT
  * @param pstServerCtx 소속 SERVER_CONTEXT
  */
 static void removeClient(SOCK_CONTEXT* pstSockCtx, SERVER_CONTEXT* pstServerCtx);
 
 /**
  * @brief Application Handler를 호출하기 위한 Read Callback Wrapper
  *
  * @param pstBufferEvent bufferevent 핸들
  * @param pvData         SOCK_CONTEXT*
  */
 static void readCallbackWrapper(struct bufferevent* pstBufferEvent, void* pvData);
 
 /**
  * @brief Application Handler를 호출하기 위한 Event Callback Wrapper
  *
  * @param pstBufferEvent bufferevent 핸들
  * @param nEvents        이벤트 플래그
  * @param pvData         SOCK_CONTEXT*
  */
 static void eventCallbackWrapper(struct bufferevent* pstBufferEvent,
                                  short nEvents,
                                  void* pvData);
 
 /**
  * @brief 서버 Listen FD에 대한 accept 이벤트 콜백
  *
  * @param iListenFd      Listen 소켓 FD
  * @param nKindOfEvent   이벤트 타입
  * @param pvData         SERVER_CONTEXT*
  */
 static void acceptCb(evutil_socket_t iListenFd, short nKindOfEvent, void* pvData);
 
 
 /* ========================================================================== */
 /* Internal Linked-list Utilities                                             */
 /* ========================================================================== */
 
 static void addClient(SOCK_CONTEXT* pstSockCtx, SERVER_CONTEXT* pstServerCtx)
 {
     if (!pstSockCtx || !pstServerCtx)
         return;
 
     pstSockCtx->pstNextSockCtx = pstServerCtx->pstClientList;
     pstServerCtx->pstClientList = pstSockCtx;
 }
 
 static void removeClient(SOCK_CONTEXT* pstSockCtx, SERVER_CONTEXT* pstServerCtx)
 {
     if (!pstSockCtx || !pstServerCtx)
         return;
 
     SOCK_CONTEXT** ppCur = &pstServerCtx->pstClientList;
 
     while (*ppCur) {
         if (*ppCur == pstSockCtx) {
             *ppCur = pstSockCtx->pstNextSockCtx;
             pstSockCtx->pstNextSockCtx = NULL;
             return;
         }
         ppCur = &(*ppCur)->pstNextSockCtx;
     }
 }
 
 
 /* ========================================================================== */
 /* PUBLIC API Implementations                                                 */
 /* ========================================================================== */
 
 void baseContextInit(BASE_CONTEXT* pstBaseCtx,
                      unsigned char uchMyId)
 {
     if (!pstBaseCtx)
         return;
 
     pstBaseCtx->pstEventBase   = NULL;
     pstBaseCtx->pstEvent       = NULL;
     pstBaseCtx->pstSignalEvent = NULL;
     pstBaseCtx->pvUserCtx      = NULL;
     pstBaseCtx->uchMyId        = uchMyId;
 
     memset(&pstBaseCtx->stHandler, 0, sizeof(pstBaseCtx->stHandler));
 }
 
 void serverContextInit(SERVER_CONTEXT* pstServerCtx,
                        BASE_CONTEXT* pstBaseCtx,
                        APP_ROLE eAppRole)
 {
     if (!pstServerCtx)
         return;
 
     pstServerCtx->eRole          = eAppRole;
     pstServerCtx->iListenFd      = -1;
     pstServerCtx->pstAcceptEvent = NULL;
     pstServerCtx->pstClientList  = NULL;
     pstServerCtx->iClientCount   = 0;
     pstServerCtx->pstBaseCtx     = pstBaseCtx;
 }
 
 void initSocketContext(SOCK_CONTEXT* pstSockCtx,
                        SERVER_CONTEXT* pstServerCtx,
                        unsigned char uchIsResponse)
 {
     if (!pstSockCtx)
         return;
 
     BASE_CONTEXT* pstBaseCtx = pstServerCtx ? pstServerCtx->pstBaseCtx : NULL;
 
     pstSockCtx->pstBufferEvent = NULL;
     pstSockCtx->pstBaseCtx     = pstBaseCtx;
     pstSockCtx->pstServerCtx   = pstServerCtx;
 
     pstSockCtx->unCmd          = 0;
     pstSockCtx->iDataLength    = 0;
 
     pstSockCtx->uchSrcId       = pstBaseCtx ? pstBaseCtx->uchMyId : 0;
     pstSockCtx->uchDstId       = 0;
     pstSockCtx->uchIsResponse  = uchIsResponse;
 
     pstSockCtx->pstNextSockCtx = NULL;
     pstSockCtx->pvUserCtx      = NULL;
 }
 
 
 /**
  * @brief 클라이언트 모드 종료 처리 (stdin 이벤트 제거 + base loop exit)
  *
  * 현재 서버 중심 구조에서는 직접 사용하지 않지만,
  * 향후 클라이언트 모드 확장을 위해 남겨둔다.
  */
 void shutdownApp(BASE_CONTEXT* pstBaseCtx)
 {
     if (!pstBaseCtx)
         return;
 
     if (pstBaseCtx->pstEvent) {
         event_del(pstBaseCtx->pstEvent);
         event_free(pstBaseCtx->pstEvent);
         pstBaseCtx->pstEvent = NULL;
     }
 
     if (pstBaseCtx->pstEventBase) {
         struct timeval stDelay = {0, 100000}; /* 100ms 후 종료 */
         event_base_loopexit(pstBaseCtx->pstEventBase, &stDelay);
     }
 }
 
 
 /**
  * @brief 서버 accept 이벤트 등록 및 활성화
  */
 void setupServerAcceptEvent(SERVER_CONTEXT* pstServerCtx)
 {
     if (!pstServerCtx || !pstServerCtx->pstBaseCtx)
         return;
 
     BASE_CONTEXT* pstBaseCtx = pstServerCtx->pstBaseCtx;
 
     if (!pstBaseCtx->pstEventBase || pstServerCtx->iListenFd < 0) {
         fprintf(stderr, "[ERROR] setupServerAcceptEvent(): invalid SERVER_CONTEXT\n");
         return;
     }
 
     pstServerCtx->pstAcceptEvent = event_new(
         pstBaseCtx->pstEventBase,
         pstServerCtx->iListenFd,
         EV_READ | EV_PERSIST,
         acceptCb,
         pstServerCtx);
 
     if (!pstServerCtx->pstAcceptEvent) {
         fprintf(stderr, "event_new(accept) failed\n");
         return;
     }
 
     if (event_add(pstServerCtx->pstAcceptEvent, NULL) < 0) {
         fprintf(stderr, "event_add(accept) failed\n");
         event_free(pstServerCtx->pstAcceptEvent);
         pstServerCtx->pstAcceptEvent = NULL;
     }
 }
 
 
 /**
  * @brief Session 종료 및 메모리 해제 (Server/Client 공용)
  */
 void closeAndFree(SOCK_CONTEXT* pstSockCtx)
 {
     if (!pstSockCtx)
         return;
 
     BASE_CONTEXT*   pstBaseCtx   = pstSockCtx->pstBaseCtx;
     SERVER_CONTEXT* pstServerCtx = pstSockCtx->pstServerCtx;
 
     if (pstServerCtx && (pstServerCtx->eRole & ROLE_SERVER) == ROLE_SERVER) {
         /* 서버 소속 클라이언트인 경우: 리스트에서 제거 */
         removeClient(pstSockCtx, pstServerCtx);
         if (pstServerCtx->iClientCount > 0)
             pstServerCtx->iClientCount--;
     } else if (pstBaseCtx) {
         /* 클라이언트 모드 확장용: base 종료 처리 */
         shutdownApp(pstBaseCtx);
     }
 
     if (pstSockCtx->pstBufferEvent) {
         bufferevent_disable(pstSockCtx->pstBufferEvent, EV_READ | EV_WRITE);
         bufferevent_free(pstSockCtx->pstBufferEvent);
         pstSockCtx->pstBufferEvent = NULL;
     }
 
     free(pstSockCtx);
 }
 
 
 /* ========================================================================== */
 /* Callback Wrapper Implementations                                           */
 /* ========================================================================== */
 
 static void readCallbackWrapper(struct bufferevent* pstBufferEvent, void* pvData)
 {
     SOCK_CONTEXT* pstSockCtx = (SOCK_CONTEXT*)pvData;
     if (!pstSockCtx || !pstSockCtx->pstBaseCtx)
         return;
 
     BASE_CONTEXT* pstBaseCtx = pstSockCtx->pstBaseCtx;
 
     if (pstBaseCtx->stHandler.pfReadCb) {
         pstBaseCtx->stHandler.pfReadCb(pstBufferEvent, pstSockCtx);
     }
 
     /* 디버깅용 로그 */
     printf("[DEBUG] readCallbackWrapper(): Data Received (Dst:%d)\n",
            pstSockCtx->uchDstId);
 }
 
 static void eventCallbackWrapper(struct bufferevent* pstBufferEvent,
                                  short nEvents,
                                  void* pvData)
 {
     SOCK_CONTEXT* pstSockCtx = (SOCK_CONTEXT*)pvData;
 
     /* Application 레벨 Event Callback 우선 호출 */
     if (pstSockCtx && pstSockCtx->pstBaseCtx &&
         pstSockCtx->pstBaseCtx->stHandler.pfEventCb) {
         pstSockCtx->pstBaseCtx->stHandler.pfEventCb(pstBufferEvent, nEvents, pvData);
     }
 
     if (nEvents & BEV_EVENT_EOF) {
         printf("[INFO] Connection closed\n");
     } else if (nEvents & BEV_EVENT_ERROR) {
         printf("[ERROR] Connection error\n");
     }
 
     closeAndFree(pstSockCtx);
 }
 
 
 /* ========================================================================== */
 /* Accept Callback                                                            */
 /* ========================================================================== */
 
 static void acceptCb(evutil_socket_t iListenFd, short nKindOfEvent, void* pvData)
 {
     (void)nKindOfEvent;
 
     SERVER_CONTEXT* pstServerCtx = (SERVER_CONTEXT*)pvData;
     if (!pstServerCtx || !pstServerCtx->pstBaseCtx)
         return;
 
     BASE_CONTEXT* pstBaseCtx = pstServerCtx->pstBaseCtx;
 
     for (;;) {
         struct sockaddr_storage stAddr;
         socklen_t tLen = sizeof(stAddr);
 
         int iClientSock = accept((int)iListenFd, (struct sockaddr*)&stAddr, &tLen);
 
         if (iClientSock < 0) {
             if (errno == EAGAIN || errno == EWOULDBLOCK)
                 break;
             perror("accept");
             break;
         }
 
         evutil_make_socket_nonblocking(iClientSock);
         evutil_make_socket_closeonexec(iClientSock);
 
         struct bufferevent* pstBufferEvent =
             bufferevent_socket_new(pstBaseCtx->pstEventBase,
                                    iClientSock,
                                    BEV_OPT_CLOSE_ON_FREE);
 
         if (!pstBufferEvent) {
             fprintf(stderr, "bufferevent_socket_new failed\n");
             netClose(iClientSock);
             return;
         }
 
         SOCK_CONTEXT* pstSockCtx = calloc(1, sizeof(SOCK_CONTEXT));
         if (!pstSockCtx) {
             fprintf(stderr, "calloc(SOCK_CONTEXT) failed\n");
             netClose(iClientSock);
             return;
         }
 
         initSocketContext(pstSockCtx, pstServerCtx, RESPONSE_ENABLED);
         pstSockCtx->pstBufferEvent = pstBufferEvent;
 
         addClient(pstSockCtx, pstServerCtx);
         pstServerCtx->iClientCount++;
 
         bufferevent_setcb(pstBufferEvent,
                           pstBaseCtx->stHandler.pfReadCb ?
                               pstBaseCtx->stHandler.pfReadCb : readCallbackWrapper,
                           pstBaseCtx->stHandler.pfWriteCb,
                           pstBaseCtx->stHandler.pfEventCb ?
                               pstBaseCtx->stHandler.pfEventCb : eventCallbackWrapper,
                           pstSockCtx);
 
         bufferevent_enable(pstBufferEvent, EV_READ | EV_WRITE);
 
         printf("[INFO] Client accepted (fd=%d, client count is %d)\n",
                iClientSock, pstServerCtx->iClientCount);
     }
 }
 