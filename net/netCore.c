/**
 * @file netCore.c
 * @brief 네트워크 File Descriptor 설정 및 소켓 옵션 유틸리티 구현부
 *
 * 이 파일은 BSD/POSIX 소켓 환경에서 FD 설정과 자원 관리 기능을 제공한다.
 */

#include "netCore.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <errno.h>

/* ========================================================================== */
/* Public API Implementation                                                  */
/* ========================================================================== */

int netSetNonblock(int iFd)
{
    int iFlags = fcntl(iFd, F_GETFL, 0);
    if (iFlags < 0)
        return -1;

    if (fcntl(iFd, F_SETFL, iFlags | O_NONBLOCK) < 0)
        return -1;

    return 0;
}

int netSetReuseAddr(int iFd)
{
    int iReuse = 1;
    return setsockopt(iFd, SOL_SOCKET, SO_REUSEADDR,
                    &iReuse, sizeof(iReuse));
}

int netSetCloexec(int iFd)
{
    int iFlags = fcntl(iFd, F_GETFD, 0);
    if (iFlags < 0)
        return -1;

    return fcntl(iFd, F_SETFD, iFlags | FD_CLOEXEC);
}

int netClose(int iFd)
{
    if (iFd >= 0)
        return close(iFd);

    return 0;
}
 