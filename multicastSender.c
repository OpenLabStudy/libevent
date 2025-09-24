#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

static volatile sig_atomic_t g_stop = 0;
static void on_sigint(int sig) { 
    (void)sig;   // sig 변수를 쓰지 않으면 경고 억제
    g_stop = 1; 
}

static int set_uint8_sockopt(int fd, int level, int optname, uint8_t val, const char* optstr) {
    if (setsockopt(fd, level, optname, &val, sizeof(val)) < 0) {
        fprintf(stderr, "setsockopt(%s) failed: %s\n", optstr, strerror(errno));
        return -1;
    }
    return 0;
}

int main(int argc, char** argv)
{
    if (argc < 3 || argc > 5) {
        fprintf(stderr,
            "Usage: %s <MULTICAST_IP> <PORT> [IFACE_IP] [INTERVAL_MS]\n"
            "Example:\n"
            "  %s 239.255.0.1 5000\n"
            "  %s 239.255.0.1 5000 192.168.0.10 200\n",
            argv[0], argv[0], argv[0]);
        return EXIT_FAILURE;
    }

    const char* mcast_ip   = argv[1];
    uint16_t    mcast_port = (uint16_t)strtoul(argv[2], NULL, 10);
    const char* iface_ip   = (argc >= 4) ? argv[3] : NULL;
    int         interval_ms= (argc >= 5) ? atoi(argv[4]) : 1000;

    // --- 멀티캐스트 주소 검증 ---
    struct in_addr maddr;
    if (inet_pton(AF_INET, mcast_ip, &maddr) != 1) {
        fprintf(stderr, "Invalid multicast IP: %s\n", mcast_ip);
        return EXIT_FAILURE;
    }
    // 대충 224.0.0.0 ~ 239.255.255.255 범위 체크(느슨하게)
    uint32_t ip_be = ntohl(maddr.s_addr);
    if (ip_be < 0xE0000000 || ip_be > 0xEFFFFFFF) {
        fprintf(stderr, "Warning: %s is not in 224.0.0.0/4\n", mcast_ip);
    }

    // --- 소켓 생성 ---
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        fprintf(stderr, "socket() failed: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    // (선택) 멀티캐스트 TTL 설정: 동일 L2망 벗어나 라우팅할 수 있게 기본 1~16 권장
    uint8_t ttl = 16;
    if (set_uint8_sockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, ttl, "IP_MULTICAST_TTL") < 0) {
        close(fd); return EXIT_FAILURE;
    }

    // 루프백(자기 자신 수신) 허용: 테스트 시에는 1(on)이 편리
    uint8_t loop = 1;
    if (set_uint8_sockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, loop, "IP_MULTICAST_LOOP") < 0) {
        close(fd); return EXIT_FAILURE;
    }

    // (선택) 특정 송신 인터페이스 지정 (멀티 NIC 환경에서 유용)
    if (iface_ip && *iface_ip) {
        struct in_addr iface_addr;
        if (inet_pton(AF_INET, iface_ip, &iface_addr) != 1) {
            fprintf(stderr, "Invalid IFACE_IP: %s\n", iface_ip);
            close(fd); return EXIT_FAILURE;
        }
        if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &iface_addr, sizeof(iface_addr)) < 0) {
            fprintf(stderr, "setsockopt(IP_MULTICAST_IF) failed: %s\n", strerror(errno));
            close(fd); return EXIT_FAILURE;
        }
    }

    // 목적지(멀티캐스트 그룹)
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port   = htons(mcast_port);
    dst.sin_addr   = maddr;

    // SIGINT 핸들러
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sigint;
    sigaction(SIGINT, &sa, NULL);

    fprintf(stdout, "[MCAST-SENDER] group=%s port=%u iface=%s interval=%dms\n",
            mcast_ip, (unsigned)mcast_port, (iface_ip?iface_ip:"<default>"), interval_ms);
    fprintf(stdout, "Press Ctrl+C to stop.\n");

    // 주기적 송신 루프
    uint64_t seq = 0;
    while (!g_stop) {
        char buf[512];
        int n = snprintf(buf, sizeof(buf),
                         "MCAST MSG seq=%llu time=%ld",
                         (unsigned long long)seq, (long)time(NULL));
        if (n < 0) n = 0;
        if (n > (int)sizeof(buf)) n = sizeof(buf);

        ssize_t sent = sendto(fd, buf, (size_t)n, 0,
                              (struct sockaddr*)&dst, sizeof(dst));
        if (sent < 0) {
            fprintf(stderr, "sendto failed: %s\n", strerror(errno));
            break;
        }

        // 화면 출력(테스트용)
        fprintf(stdout, ">>> sent(%zd): %s\n", sent, buf);
        fflush(stdout);

        seq++;
        // sleep
        struct timespec ts = { .tv_sec = interval_ms/1000,
                               .tv_nsec = (long)(interval_ms%1000)*1000000L };
        nanosleep(&ts, NULL);
    }

    close(fd);
    fprintf(stdout, "Stopped.\n");
    return EXIT_SUCCESS;
}