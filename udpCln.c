/**
 * @file udpCln.c
 * @brief UDP 클라이언트 테스트 (netModule 기반)
 *
 * 사용법:
 *   ./udpCln <server_ip> <server_port> [my_port]
 * 예:
 *   ./udpCln 127.0.0.1 9001       # 임의 포트 바인드
 *   ./udpCln 127.0.0.1 9001 5000  # 로컬 5000 포트 바인드
 */

 #include "netModule/protocols/udp.h"
 #include "netModule/core/netCore.h"
 #include <event2/event.h>
 #include <signal.h>
 #include <stdio.h>
 #include <stdlib.h>
 
 static void sigint_cb(evutil_socket_t sig, short ev, void *arg)
 {
     (void)sig; (void)ev;
     UDP_CTX *ctx = (UDP_CTX *)arg;
     printf("\n[UDP CLIENT] SIGINT caught. Exiting...\n");
     udpStop(ctx);
     event_base_loopbreak(ctx->stCoreCtx.pstEventBase);
 }
 
 int main(int argc, char *argv[])
 {
     if (argc < 3) {
         printf("Usage: %s <server_ip> <server_port> [my_port]\n", argv[0]);
         return 1;
     }
 
     const char *ip = argv[1];
     unsigned short svr_port = (unsigned short)atoi(argv[2]);
     unsigned short my_port  = (argc >= 4) ? (unsigned short)atoi(argv[3]) : 0;  // 0이면 커널이 임의 포트 할당
 
     struct event_base *base = event_base_new();
     if (!base) {
         fprintf(stderr, "Failed to create event_base\n");
         return 1;
     }
 
     UDP_CTX udpCtx;
     udpInit(&udpCtx, base, 20, NET_MODE_CLIENT);
 
     // 올바른 호출 (IP, 서버 포트, 내 바인드 포트)
     if (udpClientStart(&udpCtx, ip, svr_port, my_port) < 0) {
         fprintf(stderr, "[UDP CLIENT] Failed to start (dst=%s:%u, bind=%u)\n",
                 ip, svr_port, my_port);
         event_base_free(base);
         return 1;
     }
 
     struct event *sig = evsignal_new(base, SIGINT, sigint_cb, &udpCtx);
     if (sig) event_add(sig, NULL);
 
     printf("[UDP CLIENT] dst=%s:%u, bind=%u\n", ip, svr_port, my_port);
     printf("[UDP CLIENT] Type messages and press Enter.\n");
 
     event_base_dispatch(base);
 
     if (sig) 
        event_free(sig);
        
     udpStop(&udpCtx);
     event_base_free(base);
     return 0;
 }
 