#include "sockSession.h"
#include "frame.h"
#include <stdio.h>
#include <stdlib.h>

static void addClient(SOCK_CONTEXT* pstScokCtx, EVENT_CONTEXT* pstEventCtx)
{
    pstScokCtx->pstNextSockCtx = pstEventCtx->pstSockCtx;
    pstEventCtx->pstSockCtx = pstScokCtx;
}

static void removeClient(SOCK_CONTEXT* pstScokCtx, EVENT_CONTEXT* pstEventCtx)
{
    SOCK_CONTEXT **pstRemoveScokCtx = &pstEventCtx->pstSockCtx;
    while(*pstRemoveScokCtx){
        if(*pstRemoveScokCtx == pstScokCtx){
            *pstRemoveScokCtx = pstScokCtx->pstNextSockCtx;
            return;
        }
        pstRemoveScokCtx = &(*pstRemoveScokCtx)->pstNextSockCtx;
    }
}

static void closeClient(SOCK_CONTEXT *pstSockCtx) {
    if (!pstSockCtx)
        return;

    if (pstSockCtx->pstBufferEvent) {
        bufferevent_free(pstSockCtx->pstBufferEvent);
        pstSockCtx->pstBufferEvent = NULL;
    }

    free(pstSockCtx);
}

static void shutdownApp(EVENT_CONTEXT *pstEventCtx) {
    if (!pstEventCtx)
        return;
    // 1) 역할별 이벤트 정리
    if (pstEventCtx->eRole == ROLE_CLIENT) {
        // 클라: STDIN 이벤트를 우리가 만들었으므로 우리가 정리
        if (pstEventCtx->pstEvent) {
            event_del(pstEventCtx->pstEvent);
            event_free(pstEventCtx->pstEvent);
            pstEventCtx->pstEvent = NULL;
        }
        // 클라: 이제 루프 종료
        if (pstEventCtx->pstEventBase){
            event_base_loopexit(pstEventCtx->pstEventBase, NULL);
        }
    }
}

static void closeAllClients(EVENT_CONTEXT *pstEventCtx) {
    SOCK_CONTEXT *pstSockCtx = pstEventCtx->pstSockCtx;
    while (pstSockCtx) {
        SOCK_CONTEXT *pstNextSockCtx = pstSockCtx->pstNextSockCtx;
        closeClient(pstSockCtx);
        pstSockCtx = pstNextSockCtx;
    }
    pstEventCtx->pstSockCtx = NULL;
    pstEventCtx->iClientCount = 0;
}

/* === Accept 콜백 === */
void acceptCb(int iListenFd, short nKindOfEvent, void* pvData) 
{
    (void)nKindOfEvent;
    EVENT_CONTEXT* pstEventCtx = (EVENT_CONTEXT*)pvData;

    for (;;) {
        //모든 주소 체계를 안전하게 담기 위한 "큰 통" 같은 역할
        struct sockaddr_storage ss;
        socklen_t slen = sizeof(ss);
        int iClientSock = accept(iListenFd, (struct sockaddr*)&ss, &slen);
        if (iClientSock < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            perror("accept");
            break;
        }

        evutil_make_socket_nonblocking(iClientSock);
        evutil_make_socket_closeonexec(iClientSock);

        EVENT_CONTEXT* pstEventCtx = (EVENT_CONTEXT*)pvData;
        struct event_base *pstEventBase = pstEventCtx->pstEventBase;
        struct bufferevent *pstBufferEvent = bufferevent_socket_new(pstEventBase, iClientSock, BEV_OPT_CLOSE_ON_FREE);        
        if (!pstBufferEvent) 
            return;

        SOCK_CONTEXT *pstSockCtx = (SOCK_CONTEXT*)calloc(1, sizeof(SOCK_CONTEXT));
        if (!pstSockCtx) { 
            bufferevent_free(pstBufferEvent);
            return; 
        }
        
        pstSockCtx->pstBufferEvent  = (void*)pstBufferEvent;
        pstSockCtx->pstEventCtx     = pstEventCtx;
        pstSockCtx->uchSrcId        = pstEventCtx->uchMyId;
        pstSockCtx->uchDstId        = 0x00;
        pstSockCtx->uchIsRespone    = 0x01;
        pstEventCtx->eRole          = ROLE_SERVER;

        addClient(pstSockCtx, pstEventCtx);
        pstEventCtx->iClientCount++;

        bufferevent_setcb(pstBufferEvent, readCallback, NULL, eventCallback, pstSockCtx);
        bufferevent_enable(pstBufferEvent, EV_READ|EV_WRITE);
        bufferevent_setwatermark(pstBufferEvent, EV_READ, sizeof(FRAME_HEADER), READ_HIGH_WM);

        fprintf(stdout,"Accepted Client (iSockFd=%d)\n", iClientSock);
    }
}

/* === Libevent callbacks === */
void readCallback(struct bufferevent *pstBufferEvent, void *pvData)
{
    SOCK_CONTEXT *pstSockCtx    = (SOCK_CONTEXT *)pvData;
    struct evbuffer *pstEvBuffer = bufferevent_get_input(pstBufferEvent);
    MSG_ID stMsgId;
    stMsgId.uchSrcId = pstSockCtx->uchSrcId;
    stMsgId.uchDstId = pstSockCtx->uchDstId;
    fprintf(stderr,"MY ID : %d, Dest ID : %d\n",pstSockCtx->uchSrcId, pstSockCtx->uchDstId);
    //TODO 접속한 클라이언트의 ID 저장이 필요
    for (;;) {
        int iRetVal = responseFrame(pstEvBuffer, pstSockCtx->pstBufferEvent, &stMsgId, pstSockCtx->uchIsRespone);
        if(iRetVal == 1){
            break;
        }
        if (iRetVal == 0) 
            break;
        if (iRetVal < 0) { 
            closeAndFree(pvData);
            return; 
        }        
    }
}

void eventCallback(struct bufferevent *pstBufferEvent, short nEvents, void *pvData) 
{    
    (void)pvData;
    (void)pstBufferEvent;
    if (nEvents & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
        closeAndFree(pvData);
    }
}

void closeAndFree(void *pvData)
{
    SOCK_CONTEXT *pstSockCtx = (SOCK_CONTEXT *)pvData;
    if(!pstSockCtx)
        return;
    EVENT_CONTEXT *pstEventCtx = pstSockCtx->pstEventCtx;
    if (pstEventCtx->eRole == ROLE_SERVER) {
        removeClient(pstSockCtx, pstEventCtx);
        if(pstEventCtx->iClientCount > 0)
            pstEventCtx->iClientCount--;
        closeClient(pstSockCtx);
    }else {
        closeClient(pstSockCtx);
        shutdownApp(pstEventCtx);
    }
}

int createTcpUdpServerSocket(char* chAddr, unsigned short unPort, SOCK_TYPE eSockType)
{
    struct sockaddr_in stSocketIn;
    int iSockFd;
    int iReuseAddr = 1;
    memset(&stSocketIn, 0, sizeof(stSocketIn));
    stSocketIn.sin_family = AF_INET;
    stSocketIn.sin_port   = htons(unPort);
    stSocketIn.sin_addr.s_addr = htonl(INADDR_ANY);
    if (inet_pton(AF_INET, chAddr, &stSocketIn.sin_addr) != 1) {
        perror("inet_pton failed");
        return -1;
    }

    if(eSockType == SOCK_TYPE_TCP) {
        iSockFd = socket(AF_INET, SOCK_STREAM, 0);
    }else{
        iSockFd = socket(AF_INET, SOCK_DGRAM, 0);
    }
    if (iSockFd < 0) {
        fprintf(stderr, "socket() failed: %s\n", strerror(errno));
        return -1;
    }

    /* 재시작 시 바인드 오류 방지 */
    if (setsockopt(iSockFd, SOL_SOCKET, SO_REUSEADDR, (void*)&iReuseAddr, sizeof(iReuseAddr)) < 0) {
        fprintf(stderr, "setsockopt(SO_REUSEADDR) failed: %s\n", strerror(errno));
        return -1;
    }

    /* 논블로킹 */
    if (evutil_make_socket_nonblocking(iSockFd) < 0) {
        fprintf(stderr, "evutil_make_socket_nonblocking() failed\n");
        return -1;
    }

    if (bind(iSockFd, (struct sockaddr*)&stSocketIn, (socklen_t)sizeof(stSocketIn)) < 0) {
        fprintf(stderr, "bind() failed: %s\n", strerror(errno));
        return -1;
    }

    if(eSockType == SOCK_TYPE_TCP) {
        /* evconnlistener_new()는 전달된 fd에 대해 listen을 호출하지 않으므로 직접 호출 필요 */
        if (listen(iSockFd, SOMAXCONN) < 0) {
            fprintf(stderr, "listen() failed: %s\n", strerror(errno));
            return -1;
        }
    }   
    return iSockFd;
}

int createTcpUdpClientSocket(char* chAddr, unsigned short unPort, SOCK_TYPE eSockType)
{
    int iSockFd;
    if(eSockType == SOCK_TYPE_TCP) {
        iSockFd = socket(AF_INET, SOCK_STREAM, 0);
    }else{
        iSockFd = socket(AF_INET, SOCK_DGRAM, 0);
    }
    if (iSockFd < 0) {
        perror("socket");
        return -1;
    }

    /* 논블로킹 */
    if (evutil_make_socket_nonblocking(iSockFd) < 0) {
        fprintf(stderr, "evutil_make_socket_nonblocking() failed\n");
        return -1;
    }
    if(eSockType == SOCK_TYPE_UDP) {
        struct sockaddr_in stClientSocket;
        memset(&stClientSocket,0,sizeof(stClientSocket));
        stClientSocket.sin_family = AF_INET;
        stClientSocket.sin_port   = htons(UDP_CLIENT_PORT);
        if (inet_pton(AF_INET, "127.0.0.1", &stClientSocket.sin_addr) != 1) {
            fprintf(stderr,"Bad host\n");
            close(iSockFd);
            return -1;
        }
        if (bind(iSockFd, (struct sockaddr*)&stClientSocket, sizeof(stClientSocket)) < 0) {
            perror("bind client"); 
            close(iSockFd); 
            return -1;
        }
    }


    /* 주소 준비 */
    struct sockaddr_in stSocketIn;
    memset(&stSocketIn,0,sizeof(stSocketIn));
    stSocketIn.sin_family = AF_INET;
    stSocketIn.sin_port   = htons(unPort);
    if (inet_pton(AF_INET, chAddr, &stSocketIn.sin_addr) != 1) {
        fprintf(stderr,"Bad host\n");     
        return -1;
    }
    int iConnectRetVal = connect(iSockFd, (struct sockaddr*)&stSocketIn, sizeof(stSocketIn));
    if (iConnectRetVal < 0 && errno != EINPROGRESS) {
        perror("connect");
        close(iSockFd);
        return -1;
    }
    return iSockFd;
}

int createUdsServerSocket(char* chAddr)
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

    /* ===== 4) listen() ===== */
    if (listen(iFd, SOMAXCONN) < 0) {
        fprintf(stderr, "listen() failed: %s\n", strerror(errno));
        evutil_closesocket(iFd);
        unlink(chAddr);
        return -1;
    }
    return iFd;
}

int createUdsClientSocket(char* chAddr)
{
    int iSockFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (iSockFd < 0) {
        perror("socket");
        return -1;
    }

    /* UDS 주소 준비 */
    struct sockaddr_un stSocketUn;    
    memset(&stSocketUn, 0, sizeof(stSocketUn));
    stSocketUn.sun_family = AF_UNIX;
    strcpy(stSocketUn.sun_path, UDS1_PATH);
    size_t ulSize = strlen(UDS1_PATH);
    socklen_t uiSocketLength = (socklen_t)(offsetof(struct sockaddr_un, sun_path)+ulSize+1);

    if (connect(iSockFd, (struct sockaddr*)&stSocketUn, sizeof(stSocketUn)) < 0) {
        perror("connect");
        close(iSockFd);
        return -1;
    }
    return iSockFd;
}