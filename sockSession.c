#include "sockSession.h"
#include "frame.h"
#include <stdio.h>
#include <stdlib.h>

void listenerCb(struct evconnlistener *listener, evutil_socket_t iSockFd,
                        struct sockaddr *pstSockAddr, int iSockLen, void *pvData)
{
    (void)listener;
    (void)pstSockAddr;
    (void)iSockLen;
    EVENT_CONTEXT* pstEventCtx = (EVENT_CONTEXT*)pvData;
    struct event_base *pstEventBase = pstEventCtx->pstEventBase;
    struct bufferevent *pstBufferEvent = bufferevent_socket_new(pstEventBase, iSockFd, BEV_OPT_CLOSE_ON_FREE);
    if (!pstBufferEvent) 
        return;
    pstEventCtx->pstSockCtx = (SOCK_CONTEXT*)calloc(1, sizeof(SOCK_CONTEXT));
    if (!pstEventCtx->pstSockCtx) { 
        bufferevent_free(pstBufferEvent);
        return; 
    }
    pstEventCtx->pstSockCtx->uchIsRespone = 0x01;
    pstEventCtx->eRole = ROLE_SERVER;

    struct sockaddr_in *sin = (struct sockaddr_in*)pstSockAddr;
    inet_ntop(pstSockAddr->sa_family, &(sin->sin_addr), pstEventCtx->pstSockCtx->achSockAddr, sizeof(pstEventCtx->pstSockCtx->achSockAddr));

    pstEventCtx->pstSockCtx->pstBufferEvent = (void*)pstBufferEvent;

    bufferevent_setcb(pstBufferEvent, readCallback, NULL, eventCallback, pstEventCtx);
    bufferevent_enable(pstBufferEvent, EV_READ|EV_WRITE);
    bufferevent_setwatermark(pstBufferEvent, EV_READ, sizeof(FRAME_HEADER), READ_HIGH_WM);

    printf("Accepted TCP client (iSockFd=%d)\n", iSockFd);
}

/* === Libevent callbacks === */
void readCallback(struct bufferevent *pstBufferEvent, void *pvData)
{
    EVENT_CONTEXT *pstEventCtx = (EVENT_CONTEXT *)pvData;
    struct evbuffer *pstEvBuffer = bufferevent_get_input(pstBufferEvent);
    MSG_ID stMsgId;
    stMsgId.uchSrcId = pstEventCtx->pstSockCtx->uchSrcId;
    stMsgId.uchDstId = pstEventCtx->pstSockCtx->uchDstId;
    for (;;) {
        int r = responseFrame(pstEvBuffer, pstEventCtx->pstSockCtx->pstBufferEvent, &stMsgId, pstEventCtx->pstSockCtx->uchIsRespone);
        if(r == 1){
            break;
        }
        if (r == 0) 
            break;
        if (r < 0) { 
            closeAndFree(pvData);
            return; 
        }        
    }
}

void eventCallback(struct bufferevent *pstBufferEvent, short nEvents, void *pvData) 
{
    EVENT_CONTEXT *pstEventCtx = (EVENT_CONTEXT *)pvData;
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    (void)pstBufferEvent;
    if (nEvents & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
        fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
        closeAndFree(pvData);
    }
}

static void closeLink(EVENT_CONTEXT *pstEventCtx) {
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    if (!pstEventCtx)
        return;
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    if (pstEventCtx->pstSockCtx) {
        fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
        if (pstEventCtx->pstSockCtx->pstBufferEvent) {
            bufferevent_free(pstEventCtx->pstSockCtx->pstBufferEvent);
            pstEventCtx->pstSockCtx->pstBufferEvent = NULL;
        }
        free(pstEventCtx->pstSockCtx);
        pstEventCtx->pstSockCtx = NULL;
    }
}

static void shutdownApp(EVENT_CONTEXT *pstEventCtx) {
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    if (!pstEventCtx)
        return;
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    // 1) 역할별 이벤트 정리
    if (pstEventCtx->eRole == ROLE_CLIENT) {
        fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
        // 클라: STDIN 이벤트를 우리가 만들었으므로 우리가 정리
        if (pstEventCtx->pstEvent) {
            fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
            event_del(pstEventCtx->pstEvent);
            event_free(pstEventCtx->pstEvent);
            pstEventCtx->pstEvent = NULL;
        }
        // 클라: 이제 루프 종료
        fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
        if (pstEventCtx->pstEventBase){
            fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
            event_base_loopexit(pstEventCtx->pstEventBase, NULL);
        }
    }
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
}

void closeAndFree(void *pvData)
{
    EVENT_CONTEXT *pstEventCtx = (EVENT_CONTEXT *)pvData;
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    if (pstEventCtx->eRole == ROLE_SERVER) {
        closeLink(pstEventCtx);
    }else {
        closeLink(pstEventCtx);
        shutdownApp(pstEventCtx);
    }
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
}

int createTcpListenSocket(char* chAddr, unsigned short unPort)
{
    struct sockaddr_in stSocketIn;
    int iFd;
    int iReuseAddr = 1;
    memset(&stSocketIn, 0, sizeof(stSocketIn));
    stSocketIn.sin_family = AF_INET;
    stSocketIn.sin_port   = htons(unPort);
    stSocketIn.sin_addr.s_addr = htonl(INADDR_ANY);
    if (inet_pton(AF_INET, chAddr, &stSocketIn.sin_addr) != 1) {
        perror("inet_pton failed");
        return -1;
    }

    iFd = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (iFd < 0) {
        fprintf(stderr, "socket() failed: %s\n", strerror(errno));
        return -1;
    }

    /* 재시작 시 바인드 오류 방지 */
    if (setsockopt(iFd, SOL_SOCKET, SO_REUSEADDR, (void*)&iReuseAddr, sizeof(iReuseAddr)) < 0) {
        fprintf(stderr, "setsockopt(SO_REUSEADDR) failed: %s\n", strerror(errno));
        return -1;
    }

    /* 논블로킹 */
    if (evutil_make_socket_nonblocking(iFd) < 0) {
        fprintf(stderr, "evutil_make_socket_nonblocking() failed\n");
        return -1;
    }

    if (bind(iFd, (struct sockaddr*)&stSocketIn, (socklen_t)sizeof(stSocketIn)) < 0) {
        fprintf(stderr, "bind() failed: %s\n", strerror(errno));
        return -1;
    }

    /* evconnlistener_new()는 전달된 fd에 대해 listen을 호출하지 않으므로 직접 호출 필요 */
    if (listen(iFd, SOMAXCONN) < 0) {
        fprintf(stderr, "listen() failed: %s\n", strerror(errno));
        return -1;
    }
    return iFd;
}

int createUdsListenSocket(char* chAddr)
{
    unlink(chAddr); // ★ 바인드 전에 선삭제
    /* ===== 1) UDS 주소 준비 ===== */
    struct sockaddr_un stSocketUn;
    memset(&stSocketUn, 0, sizeof(stSocketUn));
    stSocketUn.sun_family = AF_UNIX;

    size_t iSize = strlen(chAddr);
    if (iSize >= sizeof(stSocketUn.sun_path)) {
        fprintf(stderr, "UDS path too long: %s\n", chAddr);
        return -1;
    }
    memcpy(stSocketUn.sun_path, chAddr, iSize + 1);
    socklen_t uiSocketLength =
        (socklen_t)(offsetof(struct sockaddr_un, sun_path) + iSize + 1);

    /* ===== 2) 수동 소켓 생성 ===== */
    int iFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (iFd < 0) {
        fprintf(stderr, "socket(AF_UNIX) failed: %s\n", strerror(errno));
        return -1;
    }

    /* 논블로킹 & CLOEXEC 설정 (권장) */
    if (evutil_make_socket_nonblocking(iFd) < 0) {
        fprintf(stderr, "make_socket_nonblocking failed\n");
        evutil_closesocket(iFd);
        return -1;
    }
    evutil_make_socket_closeonexec(iFd);

    /* ===== 3) bind() ===== */
    if (bind(iFd, (struct sockaddr*)&stSocketUn, uiSocketLength) < 0) {
        fprintf(stderr, "bind(%s) failed: %s\n", chAddr, strerror(errno));
        evutil_closesocket(iFd);
        unlink(chAddr);
        return -1;
    }

    /* (선택) 퍼미션 조정: 그룹까지 허용하고 싶다면 예시처럼 */
    /* chmod(chAddr, 0660); */

    /* ===== 4) listen() ===== */
    if (listen(iFd, SOMAXCONN) < 0) {
        fprintf(stderr, "listen() failed: %s\n", strerror(errno));
        evutil_closesocket(iFd);
        unlink(chAddr);
        return -1;
    }
    return iFd;
}