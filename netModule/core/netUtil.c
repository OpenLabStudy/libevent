#include "netUtil.h"
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stddef.h>

int makeNonblockClosexec(int iFd) 
{
    if (evutil_make_socket_nonblocking(iFd) < 0)
        return -1;

    evutil_make_socket_closeonexec(iFd);
    return 0;
}

int setReuseaddr(int iFd)
{
    int iYes = 1;
    return setsockopt(iFd, SOL_SOCKET, SO_REUSEADDR, &iYes, sizeof(iYes));
}

/* TCP SERVER */
int createTcpServer(unsigned short unPort)
{
    int iFd = socket(AF_INET, SOCK_STREAM, 0);
    if (iFd < 0)
        return -1;

    setReuseaddr(iFd);
    makeNonblockClosexec(iFd);

    struct sockaddr_in stSockAddr;
    memset(&stSockAddr, 0, sizeof(stSockAddr));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    stSockAddr.sin_port = htons(unPort);

    if (bind(iFd, (struct sockaddr*)&stSockAddr, sizeof(stSockAddr)) < 0) {
        close(iFd);
        return -1; 
    }

    if (listen(iFd, SOMAXCONN) < 0) {
        close(iFd);
        return -1;
    }
    return iFd;
}

/* UDP SERVER (recvfrom) */
int createUdpServer(unsigned short unPort)
{
    int iFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (iFd < 0)
        return -1;

    setReuseaddr(iFd);
    makeNonblockClosexec(iFd);

    struct sockaddr_in stSockAddr;
    memset(&stSockAddr, 0, sizeof(stSockAddr));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    stSockAddr.sin_port = htons(unPort);

    if (bind(iFd, (struct sockaddr*)&stSockAddr, sizeof(stSockAddr)) < 0) {
        close(iFd);
        return -1; 
    }

    return iFd;
}

/* TCP CLIENT */
int createTcpClient(const char* pchIp, unsigned short unPort) 
{
    int iFd = socket(AF_INET, SOCK_STREAM, 0);
    if (iFd < 0)
    return -1;

    setReuseaddr(iFd);
    makeNonblockClosexec(iFd);

    struct sockaddr_in stSockAddr;
    memset(&stSockAddr, 0, sizeof(stSockAddr));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(unPort);
    if (inet_pton(AF_INET, pchIp, &stSockAddr.sin_addr) != 1) {
        close(iFd);
        return -1;
    }

    if (connect(iFd, (struct sockaddr*)&stSockAddr, sizeof(stSockAddr)) < 0 && errno != EINPROGRESS) {
        close(iFd);
        return -1;
    }

    return iFd;
}

/* UDP CLIENT (connect 사용 또는 바인드 후 목적지 고정) */
int createUdpClient(const char* pchSrvIp, unsigned short unSvrPort,
                              unsigned short unBindPort)
{
    struct sockaddr_in stSockAddr;
    int iFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (iFd < 0)
        return -1;

    setReuseaddr(iFd);
    makeNonblockClosexec(iFd);

    if (unBindPort) {        
        memset(&stSockAddr, 0, sizeof(stSockAddr));
        stSockAddr.sin_family = AF_INET;
        stSockAddr.sin_addr.s_addr = htonl(INADDR_ANY);
        stSockAddr.sin_port = htons(unBindPort);
        if (bind(iFd, (struct sockaddr*)&stSockAddr, sizeof(stSockAddr)) < 0) {
            close(iFd);
            return -1;
        }
    }

    memset(&stSockAddr, 0, sizeof(stSockAddr));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(unSvrPort);
    if (inet_pton(AF_INET, pchSrvIp, &stSockAddr.sin_addr) != 1) {
        close(iFd);
        return -1;
    }

    if (connect(iFd, (struct sockaddr*)&stSockAddr, sizeof(stSockAddr)) < 0 && errno != EINPROGRESS) {
        close(iFd);
        return -1;
    }

    return iFd;
}

/* UDS SERVER */
int createUdsServer(const char* pchPath)
{
    unlink(pchPath);
    int iFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (iFd < 0)
        return -1;

    makeNonblockClosexec(iFd);

    struct sockaddr_un stSockAddr; 
    memset(&stSockAddr, 0, sizeof(stSockAddr));
    stSockAddr.sun_family = AF_UNIX;
    size_t ulSize = strlen(pchPath);
    if (ulSize >= sizeof(stSockAddr.sun_path)) {
        close(iFd);
        return -1; 
    }

    memcpy(stSockAddr.sun_path, pchPath, ulSize+1);
    socklen_t uiLen = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + ulSize + 1);

    if (bind(iFd, (struct sockaddr*)&stSockAddr, uiLen) < 0) {
        close(iFd);
        unlink(pchPath);
        return -1;
    }

    if (listen(iFd, SOMAXCONN) < 0) {
        close(iFd);
        unlink(pchPath);
        return -1;
    }

    return iFd;
}

/* UDS CLIENT */
int createUdsClient(const char* pchPath)
{
    int iFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (iFd < 0)
        return -1;

    makeNonblockClosexec(iFd);

    struct sockaddr_un stSockAddr;
    memset(&stSockAddr, 0, sizeof(stSockAddr));
    stSockAddr.sun_family = AF_UNIX;
    size_t ulSize = strlen(pchPath);
    if (ulSize >= sizeof(stSockAddr.sun_path)) {
        close(iFd);
        return -1;
    }

    memcpy(stSockAddr.sun_path, pchPath, ulSize+1);
    socklen_t uiLen = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + ulSize + 1);

    if (connect(iFd, (struct sockaddr*)&stSockAddr, uiLen) < 0 && errno != EINPROGRESS){
        close(iFd);
        return -1;
    }

    return iFd;
}
