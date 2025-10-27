#ifndef NET_UTIL_H
#define NET_UTIL_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <arpa/inet.h>

#include <event2/util.h>

typedef enum {
    SOCK_TYPE_NONE = 0,
    SOCK_TYPE_TCP,
    SOCK_TYPE_UDP,
} SOCK_TYPE;

/* 공통 유틸 */
int  makeNonblockClosexec(int fd);
int  setReuseaddr(int fd);

/* TCP/UDP 서버/클라 소켓 */
int  createTcpServer(unsigned short port);
int  createUdpServer(unsigned short port);
int  createTcpClient(const char* ip, unsigned short port);
int  createUdpClient(const char* srv_ip, unsigned short srv_port,
                               unsigned short my_bind_port);

/* UDS 서버/클라 소켓 */
int  createUdsServer(const char* path);
int  createUdsClient(const char* path);

#endif
