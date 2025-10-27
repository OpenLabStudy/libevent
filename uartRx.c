#include "./uartModule/uartManager.h"
#include "./uartModule/uartEvent.h"
#include <string.h>
#include <stdio.h>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("사용법: %s /dev/pts/X\n", argv[0]);
        return 1;
    }

    UART_CTX stUartCtx = {0};
    stUartCtx.pchDevPath = argv[1];
    stUartCtx.iFd = -1;
    stUartCtx.iBackoffMsec = 200;

    uartEventInit(&stUartCtx);

    if (uartOpen(&stUartCtx) == 0) {
        uartEventAttach(&stUartCtx);
    } else {
        printf("[WARN] initial open failed. retry...\n");
        struct timeval tv = { \
            stUartCtx.iBackoffMsec / 1000, \
            (stUartCtx.iBackoffMsec % 1000) * 1000 \
        };
        evtimer_add(stUartCtx.pstEventReopen, &tv);
    }

    printf("[INFO] UART module running. Press Ctrl+C to exit.\n");
    event_base_dispatch(stUartCtx.pstEventBase);
    uartEventCleanup(&stUartCtx);
    return 0;
}
