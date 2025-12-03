/**
 * @file eventSession.c
 * @brief Libevent 기반 TCP Session 관리 구현부
 *
 * 본 소스는 eventSession.h에서 선언된 API를 구현하며, 다음 기능을 포함한다.
 * - 서버/클라이언트 모드에서의 이벤트 루프 관리
 * - bufferevent 기반 비동기 Read/Write 이벤트 처리
 * - Accept 기반 다중 접속 처리(Server mode)
 * - Callback Wrapper를 통한 사용자 정의 콜백 호출 구조
 *
 * @see eventSession.h
 */

#include "eventSession.h"
#include "netCore.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <event2/util.h>

/* ========================================================================== */
/* Internal Function Prototypes (Static)                                      */
/* ========================================================================== */

/**
 * @brief 서버 모드에서 accept 이벤트 발생 시 신규 클라이언트 접속 처리 함수
 *
 * @param iListenFd      리슨 소켓 FD
 * @param nKindOfEvent   EV_READ | EV_PERSIST 플래그
 * @param pvData         EVENT_CONTEXT*
 */
static void acceptCb(evutil_socket_t iListenFd, short nKindOfEvent, void* pvData);

/**
 * @brief bufferevent 기반 Read Wrapper Callback
 *
 * @param pstBufferEvent bufferevent 객체
 * @param pvData         SOCK_CONTEXT*
 */
static void readCallbackWrapper(struct bufferevent* pstBufferEvent, void* pvData);

/**
 * @brief bufferevent 기반 Event Wrapper Callback (EOF, ERROR 등)
 *
 * @param pstBufferEvent bufferevent 객체
 * @param nEvents        이벤트 플래그
 * @param pvData         SOCK_CONTEXT*
 */
static void eventCallbackWrapper(struct bufferevent* pstBufferEvent, short nEvents, void* pvData);

/**
 * @brief SOCK_CONTEXT를 서버 클라이언트 리스트에 추가
 *
 * @param pstSockCtx   대상 SOCK_CONTEXT
 * @param pstEventCtx  부모 EVENT_CONTEXT
 */
static void addClient(SOCK_CONTEXT* pstSockCtx, EVENT_CONTEXT* pstEventCtx);

/**
 * @brief SOCK_CONTEXT를 서버 클라이언트 리스트에서 제거
 *
 * @param pstSockCtx   제거할 SOCK_CONTEXT
 * @param pstEventCtx  부모 EVENT_CONTEXT
 */
static void removeClient(SOCK_CONTEXT* pstSockCtx, EVENT_CONTEXT* pstEventCtx);


/* ========================================================================== */
/* Internal Linked-list Utilities                                             */
/* ========================================================================== */

static void addClient(SOCK_CONTEXT* pstSockCtx, EVENT_CONTEXT* pstEventCtx)
{
    pstSockCtx->pstNextSockCtx     = pstEventCtx->pstSockCtx;
    pstEventCtx->pstSockCtx        = pstSockCtx;
}

static void removeClient(SOCK_CONTEXT* pstSockCtx, EVENT_CONTEXT* pstEventCtx)
{
    SOCK_CONTEXT** ppCur = &pstEventCtx->pstSockCtx;

    while (*ppCur) {
        if (*ppCur == pstSockCtx) {
            *ppCur = pstSockCtx->pstNextSockCtx;
            return;
        }
        ppCur = &(*ppCur)->pstNextSockCtx;
    }
}


/* ========================================================================== */
/* Public API                                                                 */
/* ========================================================================== */

void initEventContext(EVENT_CONTEXT* pstEventCtx,
                    APP_ROLE eAppRole,
                    unsigned char uchMyId)
{
    if (!pstEventCtx)
        return;

    pstEventCtx->eRole          = eAppRole;
    pstEventCtx->iSockFd        = -1;
    pstEventCtx->pstEventBase   = NULL;
    pstEventCtx->pstEvent       = NULL;
    pstEventCtx->pstSignalEvent = NULL;
    pstEventCtx->pstAcceptEvent = NULL;
    pstEventCtx->pstSockCtx     = NULL;
    pstEventCtx->pvUserCtx      = NULL;
    pstEventCtx->iClientCount   = 0;
    pstEventCtx->uchMyId        = uchMyId;

    memset(&pstEventCtx->stHandler, 0, sizeof(pstEventCtx->stHandler));
}


void initSocketContext(SOCK_CONTEXT* pstSockCtx,
                    EVENT_CONTEXT* pstEventCtx,
                    unsigned char uchIsResponse)
{
    if (!pstSockCtx)
        return;

    pstSockCtx->pstBufferEvent  = NULL;
    pstSockCtx->pstEventCtx     = pstEventCtx;
    pstSockCtx->unCmd           = 0;
    pstSockCtx->iDataLength     = 0;
    pstSockCtx->uchSrcId        = pstEventCtx->uchMyId;
    pstSockCtx->uchDstId        = 0;
    pstSockCtx->uchIsResponse   = uchIsResponse;
    pstSockCtx->pstNextSockCtx  = NULL;
    pstSockCtx->pvUserCtx       = pstEventCtx->pvUserCtx;
    fprintf(stderr,"event addr %u, sockCtx addr %u, ID:%02x\n", pstEventCtx, pstSockCtx, pstSockCtx->uchSrcId);
}


/**
 * @brief 클라이언트 모드 종료 처리 (stdin 이벤트 제거 + base loop exit)
 */
void shutdownApp(EVENT_CONTEXT* pstEventCtx)
{
    if (!pstEventCtx)
        return;

    if (pstEventCtx->eRole & ROLE_CLIENT == ROLE_CLIENT) {

        if (pstEventCtx->pstEvent) {
            event_del(pstEventCtx->pstEvent);
            event_free(pstEventCtx->pstEvent);
            pstEventCtx->pstEvent = NULL;
        }

        if (pstEventCtx->pstEventBase) {
            struct timeval stDelay = {0, 100000};
            event_base_loopexit(pstEventCtx->pstEventBase, &stDelay);
        }
    }
}


/**
 * @brief 서버 accept 이벤트 등록 및 활성화
 *
 * @param pstEventCtx 서버 EVENT_CONTEXT
 */
void setupServerAcceptEvent(EVENT_CONTEXT* pstEventCtx)
{
    if (!pstEventCtx || !pstEventCtx->pstEventBase || pstEventCtx->iSockFd < 0) {
        fprintf(stderr, "[ERROR] setupServerAcceptEvent(): invalid EVENT_CONTEXT\n");
        return;
    }

    pstEventCtx->pstAcceptEvent = event_new(
        pstEventCtx->pstEventBase,
        pstEventCtx->iSockFd,
        EV_READ | EV_PERSIST,
        acceptCb,
        pstEventCtx);

    if (!pstEventCtx->pstAcceptEvent) {
        fprintf(stderr, "event_new(accept) failed\n");
        return;
    }

    if (event_add(pstEventCtx->pstAcceptEvent, NULL) < 0) {
        fprintf(stderr, "event_add(accept) failed\n");
        event_free(pstEventCtx->pstAcceptEvent);
        pstEventCtx->pstAcceptEvent = NULL;
    }
}


/**
 * @brief Session 종료 및 메모리 해제 (Server/Client 공용)
 */
void closeAndFree(SOCK_CONTEXT* pstSockCtx)
{
    EVENT_CONTEXT* pstEventCtx = pstSockCtx->pstEventCtx;

    if (!pstEventCtx)
        return;

    if (pstEventCtx->eRole & ROLE_SERVER == ROLE_SERVER) {
        removeClient(pstSockCtx, pstEventCtx);
        pstEventCtx->iClientCount--;
    } else {
        shutdownApp(pstEventCtx);
    }

    if (pstSockCtx->pstBufferEvent) {
        bufferevent_disable(pstSockCtx->pstBufferEvent, EV_READ | EV_WRITE);
        bufferevent_free(pstSockCtx->pstBufferEvent);
        pstSockCtx->pstBufferEvent = NULL;
    }

    if (pstEventCtx->pstEvent) {
        event_del(pstEventCtx->pstEvent);
        event_free(pstEventCtx->pstEvent);
        pstEventCtx->pstEvent = NULL;
    }

    if (pstEventCtx->iSockFd >= 0) {
        netClose(pstEventCtx->iSockFd); /* 지정함수 유지 */
        pstEventCtx->iSockFd = -1;
    }

    if (pstEventCtx->pstEventBase) {
        event_base_free(pstEventCtx->pstEventBase);
        pstEventCtx->pstEventBase = NULL;
    }

    free(pstSockCtx);
}


/* ========================================================================== */
/* Internal Accept Callback                                                   */
/* ========================================================================== */

/**
 * @brief accept 이벤트 처리: 신규 TCP 연결에 대한 bufferevent 생성
 */
static void acceptCb(evutil_socket_t iListenFd, short nKindOfEvent, void* pvData)
{
    (void)nKindOfEvent;
    EVENT_CONTEXT* pstEventCtx = (EVENT_CONTEXT*)pvData;
    if (!pstEventCtx)
        return;

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
            bufferevent_socket_new(pstEventCtx->pstEventBase, iClientSock,
                                BEV_OPT_CLOSE_ON_FREE);

        if (!pstBufferEvent) {
            fprintf(stderr, "bufferevent_socket_new failed\n");
            netClose(iClientSock);
            return;
        }

        SOCK_CONTEXT* pstSockCtx = calloc(1, sizeof(SOCK_CONTEXT));
        initSocketContext(pstSockCtx, pstEventCtx, RESPONSE_ENABLED);
        pstSockCtx->pstBufferEvent = pstBufferEvent;

        addClient(pstSockCtx, pstEventCtx);
        pstEventCtx->iClientCount++;

        bufferevent_setcb(pstBufferEvent,
                        pstEventCtx->stHandler.pfReadCb ?
                        pstEventCtx->stHandler.pfReadCb : readCallbackWrapper,
                        pstEventCtx->stHandler.pfWriteCb,
                        pstEventCtx->stHandler.pfEventCb ?
                        pstEventCtx->stHandler.pfEventCb : eventCallbackWrapper,
                        pstSockCtx);

        bufferevent_enable(pstBufferEvent, EV_READ | EV_WRITE);

        printf("[INFO] Client accepted (fd=%d, client count is %d)\n", iClientSock, pstEventCtx->iClientCount);
    }
}


/* ========================================================================== */
/* Callback Wrappers                                                          */
/* ========================================================================== */

static void readCallbackWrapper(struct bufferevent* pstBufferEvent, void* pvData)
{
    (void)pstBufferEvent;
    SOCK_CONTEXT* pstSockCtx = pvData;
    printf("[DEBUG] readCallbackWrapper(): Data Received (Dst:%d)\n", pstSockCtx->uchDstId);
}

static void eventCallbackWrapper(struct bufferevent* pstBufferEvent, short nEvents, void* pvData)
{
    SOCK_CONTEXT* pstSockCtx = pvData;

    if (nEvents & BEV_EVENT_EOF)
        printf("[INFO] Connection closed\n");

    else if (nEvents & BEV_EVENT_ERROR)
        printf("[ERROR] Connection error\n");

    closeAndFree(pstSockCtx);
}
