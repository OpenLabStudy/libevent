/**
 * @file netUdp.c
 * @brief UDP 소켓 생성 및 설정 유틸리티 구현부
 *
 * 본 모듈은 UDP 서버/클라이언트용 Non-blocking FD 생성과
 * 선택적 connect() 호출을 포함하고 있으며, netCore 모듈과 함께
 * 네트워크 초기화 계층 역할을 수행한다.
 */

#include "netUdp.h"
#include "netCore.h"

#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>

/* ========================================================================== */
/* Public API Implementation                                                  */
/* ========================================================================== */

int netUdpCreateServer(uint16_t unPort,
                    const char* pchClientIp,
                    uint16_t unClientPort)
{
    int iSockFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (iSockFd < 0)
        return -1;

    netSetReuseAddr(iSockFd);
    netSetNonblock(iSockFd);
    netSetCloexec(iSockFd);

    struct sockaddr_in stLocalAddr = {0};
    stLocalAddr.sin_family      = AF_INET;
    stLocalAddr.sin_port        = htons(unPort);
    stLocalAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(iSockFd, (struct sockaddr*)&stLocalAddr,
            sizeof(stLocalAddr)) < 0) {
        close(iSockFd);
        return -1;
    }

    /* Optional peer binding using connect() */
    struct sockaddr_in stRemoteAddr;
    memset(&stRemoteAddr, 0, sizeof(stRemoteAddr));
    stRemoteAddr.sin_family = AF_INET;
    stRemoteAddr.sin_port   = htons(unClientPort);

    if (inet_pton(AF_INET, pchClientIp, &stRemoteAddr.sin_addr) != 1) {
        close(iSockFd);
        return -1;
    }

    /* Non-blocking, no handshake (expected behavior for UDP) */
    if (connect(iSockFd, (struct sockaddr*)&stRemoteAddr,
                sizeof(stRemoteAddr)) < 0) {
        close(iSockFd);
        return -1;
    }

    return iSockFd;
}

int netUdpCreateClient(const char* pszIp,
                    uint16_t unSrvPort,
                    uint16_t unMyPort)
{
    int iSockFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (iSockFd < 0)
        return -1;

    netSetReuseAddr(iSockFd);
    netSetNonblock(iSockFd);
    netSetCloexec(iSockFd);

    /* Optional bind (client owns a fixed port) */
    if (unMyPort != 0) {
        struct sockaddr_in stLocalAddr = {0};
        stLocalAddr.sin_family      = AF_INET;
        stLocalAddr.sin_port        = htons(unMyPort);
        stLocalAddr.sin_addr.s_addr = htonl(INADDR_ANY);
        bind(iSockFd, (struct sockaddr*)&stLocalAddr, sizeof(stLocalAddr));
    }

    struct sockaddr_in stRemoteAddr = {0};
    stRemoteAddr.sin_family = AF_INET;
    stRemoteAddr.sin_port   = htons(unSrvPort);
    inet_pton(AF_INET, pszIp, &stRemoteAddr.sin_addr);

    /* Non-blocking connect() for address association only */
    connect(iSockFd, (struct sockaddr*)&stRemoteAddr,
            sizeof(stRemoteAddr));

    return iSockFd;
}
