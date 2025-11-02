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
    printf("\n[UDP SERVER] SIGINT caught. Exiting...\n");
    udpStop(ctx);
    event_base_loopbreak(ctx->stCoreCtx.pstEventBase);
}

int main(int argc, char *argv[])
{
    unsigned short port = (argc > 1) ? atoi(argv[1]) : 9001;
    struct event_base *base = event_base_new();

    UDP_CTX udpCtx;
    udpInit(&udpCtx, base, 10, NET_MODE_SERVER);

    if (udpServerStart(&udpCtx, port) < 0) {
        fprintf(stderr, "Failed to start UDP server\n");
        return 1;
    }

    struct event *sig = evsignal_new(base, SIGINT, sigint_cb, &udpCtx);
    event_add(sig, NULL);

    printf("[UDP SERVER] Listening on port %d\n", port);
    event_base_dispatch(base);

    event_free(sig);
    udpStop(&udpCtx);
    event_base_free(base);
    return 0;
}
