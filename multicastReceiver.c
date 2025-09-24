/**
 * @file mcast_receiver.c
 * @brief IPv4 UDP 멀티캐스트 수신 예제 (send/recv 기반, libevent 미사용)
 *
 * 사용법:
 *   ./mcast_receiver <MULTICAST_IP> <PORT> [IFACE_IP]
 *
 * 예:
 *   ./mcast_receiver 239.255.0.1 5000
 *   ./mcast_receiver 239.255.0.1 5000 10.0.0.5   // 특정 NIC(수신용 인터페이스)에서 조인
 *
 * 빌드:
 *   gcc -O2 -Wall -Wextra -o mcast_receiver mcast_receiver.c
 */


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#ifndef printLog
#define printLog(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)
#endif

/* ====== 사용자 제공 스타일의 헬퍼들 ====== */

// 소켓 생성
int createMulticastSocket(void)
{
    int iSockFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (iSockFd < 0) {
        printLog("socket() fail...[%s]", strerror(errno));
        return -1;
    }
    return iSockFd;
}

// REUSEADDR/REUSEPORT 설정(여러 프로세스 동시 수신용)
static int enableReuse(int fd)
{
    int on = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
        printLog("setsockopt(SO_REUSEADDR) fail...[%s]", strerror(errno));
        return -1;
    }
#ifdef SO_REUSEPORT
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on)) < 0) {
        // 필수 아님: 일부 OS에서 실패 가능
        printLog("setsockopt(SO_REUSEPORT) warn...[%s]", strerror(errno));
    }
#endif
    return 0;
}

// 바인드 (일반적으로 INADDR_ANY 권장)
int bindMulticastPort(struct sockaddr_in* pstMulticastAddr, int iMulticastSockFd, int iMulticastPort)
{
    memset(pstMulticastAddr, 0, sizeof(*pstMulticastAddr));
    pstMulticastAddr->sin_family      = AF_INET;
    pstMulticastAddr->sin_addr.s_addr = htonl(INADDR_ANY);
    pstMulticastAddr->sin_port        = htons((uint16_t)iMulticastPort);

    if (bind(iMulticastSockFd, (struct sockaddr *)pstMulticastAddr, sizeof(struct sockaddr_in)) < 0) {
        printLog("%s():%d fail...[%s]", __func__, __LINE__, strerror(errno));
        return -1;
    }
    return 0;
}

// 멀티캐스트 가입 (uiIpAddr: 네트워크 바이트 오더 s_addr 값 사용)
int setMulticastSockOpt(int iMulticastSockFd, unsigned int uiIpAddr, unsigned int uiIfaceIpAddr /*0이면 ANY*/)
{
    struct ip_mreq stMulticastMreq;
    int iReturnFromSetsockOpt = 0;

    struct in_addr stGroupAddr;  stGroupAddr.s_addr  = uiIpAddr;
    struct in_addr stIfaceAddr;  stIfaceAddr.s_addr  = (uiIfaceIpAddr != 0) ? uiIfaceIpAddr : htonl(INADDR_ANY);

    printLog("Join Multicast: %s (iface=%s)",
             inet_ntoa(stGroupAddr),
             (uiIfaceIpAddr != 0) ? inet_ntoa(stIfaceAddr) : "ANY");

    memset(&stMulticastMreq, 0, sizeof(stMulticastMreq));
    stMulticastMreq.imr_multiaddr = stGroupAddr;
    stMulticastMreq.imr_interface = stIfaceAddr;

    iReturnFromSetsockOpt = setsockopt(iMulticastSockFd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                                       &stMulticastMreq, sizeof(stMulticastMreq));
    if (iReturnFromSetsockOpt < 0) {
        printLog("%s():%d fail...[%s]", __func__, __LINE__, strerror(errno));
        return -1;
    }
    return 0;
}

// 멀티캐스트 탈퇴
int dropMulticastSockOpt(int iMulticastSockFd, unsigned int uiIpAddr, unsigned int uiIfaceIpAddr /*0이면 ANY*/)
{
    struct ip_mreq stMulticastMreq;

    struct in_addr stGroupAddr; stGroupAddr.s_addr  = uiIpAddr;
    struct in_addr stIfaceAddr; stIfaceAddr.s_addr  = (uiIfaceIpAddr != 0) ? uiIfaceIpAddr : htonl(INADDR_ANY);

    printLog("Leave Multicast: %s (iface=%s)",
             inet_ntoa(stGroupAddr),
             (uiIfaceIpAddr != 0) ? inet_ntoa(stIfaceAddr) : "ANY");

    memset(&stMulticastMreq, 0, sizeof(stMulticastMreq));
    stMulticastMreq.imr_multiaddr = stGroupAddr;
    stMulticastMreq.imr_interface = stIfaceAddr;

    if (setsockopt(iMulticastSockFd, IPPROTO_IP, IP_DROP_MEMBERSHIP,
                   &stMulticastMreq, sizeof(stMulticastMreq)) < 0) {
        printLog("%s():%d fail...[%s]", __func__, __LINE__, strerror(errno));
        return -1;
    }
    return 0;
}

/* ====== 옵션: 수신 버퍼 키우기 (대량 트래픽 시 유용) ====== */
static void maybeGrowRcvbuf(int fd, int bytes)
{
    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &bytes, sizeof(bytes)) < 0) {
        printLog("setsockopt(SO_RCVBUF=%d) warn...[%s]", bytes, strerror(errno));
    }
}

/* ====== 메인 ====== */
static volatile sig_atomic_t g_stop = 0;
static void on_sigint(int sig) { 
    (void)sig;   // sig 변수를 쓰지 않으면 경고 억제
    g_stop = 1; 
}

int main(int argc, char** argv)
{
    if (argc < 3 || argc > 4) {
        fprintf(stderr,
            "Usage: %s <MULTICAST_IP> <PORT> [IFACE_IP]\n"
            "Example:\n"
            "  %s 239.255.0.1 5000\n"
            "  %s 239.255.0.1 5000 10.0.0.5\n",
            argv[0], argv[0], argv[0]);
        return EXIT_FAILURE;
    }

    const char* mcast_ip = argv[1];
    uint16_t    mcast_port = (uint16_t)strtoul(argv[2], NULL, 10);
    const char* iface_ip   = (argc >= 4) ? argv[3] : NULL;

    // 문자열 → in_addr
    struct in_addr grp_addr = {0};
    if (inet_pton(AF_INET, mcast_ip, &grp_addr) != 1) {
        printLog("Invalid MULTICAST_IP: %s", mcast_ip);
        return EXIT_FAILURE;
    }

    struct in_addr if_addr = {0};
    if (iface_ip && *iface_ip) {
        if (inet_pton(AF_INET, iface_ip, &if_addr) != 1) {
            printLog("Invalid IFACE_IP: %s", iface_ip);
            return EXIT_FAILURE;
        }
    } // else 0 → ANY

    int fd = createMulticastSocket();
    if (fd < 0) return EXIT_FAILURE;

    // 여러 수신기 동시 실행 대비
    if (enableReuse(fd) < 0) { close(fd); return EXIT_FAILURE; }

    // 바인드(INADDR_ANY 권장: 커널이 멀티캐스트 수신 처리)
    struct sockaddr_in bind_addr;
    if (bindMulticastPort(&bind_addr, fd, mcast_port) < 0) {
        close(fd);
        return EXIT_FAILURE;
    }

    // 인터페이스 지정하여 그룹 가입(미지정시 ANY)
    if (setMulticastSockOpt(fd, grp_addr.s_addr, if_addr.s_addr) < 0) {
        close(fd);
        return EXIT_FAILURE;
    }

    // (옵션) 수신 버퍼 확대
    maybeGrowRcvbuf(fd, 4*1024*1024);

    // SIGINT 핸들러 등록
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sigint;
    sigaction(SIGINT, &sa, NULL);

    printLog("[MCAST-RECV] group=%s port=%u iface=%s",
             mcast_ip, (unsigned)mcast_port, (iface_ip?iface_ip:"ANY"));
    printLog("Waiting packets... (Ctrl+C to stop)");

    // 수신 루프
    while (!g_stop) {
        char buf[2048];
        struct sockaddr_in src; socklen_t slen = sizeof(src);
        ssize_t n = recvfrom(fd, buf, sizeof(buf)-1, 0, (struct sockaddr*)&src, &slen);
        if (n < 0) {
            if (errno == EINTR && g_stop) break;
            printLog("recvfrom() fail...[%s]", strerror(errno));
            // 오류 지속 시 탈출할지 여부는 정책에 따라
            continue;
        }
        buf[n] = '\0';

        char src_ip[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &src.sin_addr, src_ip, sizeof(src_ip));
        printLog("<<< %s:%u len=%zd msg=\"%s\"",
                 src_ip, ntohs(src.sin_port), n, buf);
    }

    // 종료: 그룹 탈퇴 → 소켓 닫기
    if (dropMulticastSockOpt(fd, grp_addr.s_addr, if_addr.s_addr) < 0) {
        printLog("dropMulticastSockOpt() warn");
    }
    close(fd);
    printLog("Stopped.");
    return EXIT_SUCCESS;
}
