#ifndef NET_CONTEXT_H
#define NET_CONTEXT_H

#include "../core/netCore.h"
#include "commonSession.h"
#include <event2/listener.h>

typedef enum {
    NET_MODE_SERVER,
    NET_MODE_CLIENT
} NET_MODE;

/**
 * @brief 공통 네트워크 컨텍스트 구조체
 * 
 * TCP / UDP / UDS 모듈이 공통으로 사용
 */
typedef struct {
    CORE_CTX stCoreCtx;             // 공용 이벤트 기반 구조체
    NET_MODE eMode;                 // 서버/클라이언트 구분

    int iSockFd;                    // 소켓 FD (TCP/UDP/UDS 공용)
    struct evconnlistener* pstListener; // TCP 서버용
    struct bufferevent*    pstBev;      // TCP 클라이언트용
    struct event*          pstRecvEvent; // UDP/UDS 수신용
    struct event*          pstStdinEvent; // 클라이언트 표준입력용
} NET_CTX;

#endif
