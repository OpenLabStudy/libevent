/**
 * @file netTcp.c
 * @brief TCP 유틸리티 API 구현부
 *
 * 본 파일은 netTcp.h에서 정의된 TCP 서버 및 클라이언트 생성 기능을 구현한다.
 * 내부적으로 netCore 모듈을 활용해 소켓 속성을 설정하고,
 * Non-blocking 기반으로 TCP 연결 또는 수신 소켓을 초기화한다.
 */

#include "netTcp.h"
#include "netCore.h"

#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdio.h>

/* ========================================================================== */
/* Public API Implementation                                                  */
/* ========================================================================== */

int netTcpCreateServer(uint16_t unPort)
{
    int iFd = socket(AF_INET, SOCK_STREAM, 0);
    if (iFd < 0)
        return -1;

    netSetReuseAddr(iFd);
    netSetNonblock(iFd);
    netSetCloexec(iFd);

    struct sockaddr_in stAddr;
    memset(&stAddr, 0, sizeof(stAddr));
    stAddr.sin_family      = AF_INET;
    stAddr.sin_port        = htons(unPort);
    stAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(iFd, (struct sockaddr*)&stAddr, sizeof(stAddr)) < 0) {
        close(iFd);
        return -1;
    }

    if (listen(iFd, SOMAXCONN) < 0) {
        close(iFd);
        return -1;
    }

    return iFd;
}


int netTcpCreateClient(const char* pszIp, uint16_t unPort)
{
    int iFd = socket(AF_INET, SOCK_STREAM, 0);
    if (iFd < 0)
        return -1;

    netSetReuseAddr(iFd);
    netSetNonblock(iFd);
    netSetCloexec(iFd);

    struct sockaddr_in stAddr;
    memset(&stAddr, 0, sizeof(stAddr));
    stAddr.sin_family = AF_INET;
    stAddr.sin_port   = htons(unPort);

    inet_pton(AF_INET, pszIp, &stAddr.sin_addr);

    /* non-blocking connect → EINPROGRESS expected */
    connect(iFd, (struct sockaddr*)&stAddr, sizeof(stAddr));

    return iFd;
}
 