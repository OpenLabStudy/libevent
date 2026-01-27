/**
 * @file netUds.c
 * @brief UNIX Domain Socket 서버 및 클라이언트 생성 함수 구현부
 *
 * 이 구현체는 파일 기반 IPC를 위한 AF_UNIX 소켓을 제공하며,
 * Linux/POSIX 환경에서 IPC 성능이 TCP 대비 빠르고 오버헤드가 낮다.
 */

#include "netUds.h"
#include "netCore.h"

#include <sys/un.h>
#include <string.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <errno.h>

/* ========================================================================== */
/* Public API Implementation                                                  */
/* ========================================================================== */

int netUdsCreateServer(const char* pszPath)
{
    /* 기존 소켓 파일이 존재할 경우 제거 */
    unlink(pszPath);

    int iFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (iFd < 0)
        return -1;

    netSetNonblock(iFd);
    netSetCloexec(iFd);

    struct sockaddr_un stAddrUn;
    memset(&stAddrUn, 0, sizeof(stAddrUn));
    stAddrUn.sun_family = AF_UNIX;
    strncpy(stAddrUn.sun_path, pszPath, sizeof(stAddrUn.sun_path) - 1);

    if (bind(iFd, (struct sockaddr*)&stAddrUn, sizeof(stAddrUn)) < 0) {
        close(iFd);
        return -1;
    }

    if (listen(iFd, SOMAXCONN) < 0) {
        close(iFd);
        return -1;
    }

    return iFd;
}

int netUdsCreateClient(const char* pszPath)
{
    int iFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (iFd < 0)
        return -1;

    netSetNonblock(iFd);
    netSetCloexec(iFd);

    struct sockaddr_un stAddrUn;
    memset(&stAddrUn, 0, sizeof(stAddrUn));
    stAddrUn.sun_family = AF_UNIX;
    strncpy(stAddrUn.sun_path, pszPath, sizeof(stAddrUn.sun_path) - 1);

    /* Non-blocking connect → EINPROGRESS가 정상 상태일 수 있음 */
    int iRet = connect(iFd, (struct sockaddr*)&stAddrUn, sizeof(stAddrUn));
    if (iRet < 0 && errno != EINPROGRESS) {
        close(iFd);
        return -1;
    }
    return iFd;
}
