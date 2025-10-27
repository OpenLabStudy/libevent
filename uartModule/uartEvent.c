#include "uartEvent.h"
#include "uartManager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <event2/buffer.h> 

static void sigintCallback(evutil_socket_t sig, short ev, void* pvData)
{
    UART_CTX* pstUartCtx = (UART_CTX*)pvData;
    fprintf(stderr, "[INFO] SIGINT caught. exiting...\n");
    event_base_loopexit(pstUartCtx->pstEventBase, NULL);
}

static void readCallback(struct bufferevent *bev, void *pvData) {
    struct evbuffer* in = bufferevent_get_input(bev);
    char* pchLine;
    size_t n_read;
    while ((pchLine = evbuffer_readln(in, &n_read, EVBUFFER_EOL_LF)) != NULL) {
        printf("Received: %s\n", pchLine);
        free(pchLine);
    }
}

static void eventCallback(struct bufferevent *bev, short events, void *pvData) {
    UART_CTX* pstUartCtx = (UART_CTX*)pvData;
    if (events & (BEV_EVENT_ERROR | BEV_EVENT_EOF))
        uartEventScheduleReopen(pstUartCtx);
}

static void reopenCallback(evutil_socket_t fd, short ev, void* pvData) {
    UART_CTX* pstUartCtx = (UART_CTX*)pvData;
    if (uartOpen(pstUartCtx) == 0) {
        fprintf(stderr, "[INFO] Reopened %s\n", pstUartCtx->pchDevPath);
        uartEventAttach(pstUartCtx);
        pstUartCtx->iBackoffMsec = 200;
    } else {
        if (pstUartCtx->iBackoffMsec < 2000) 
            pstUartCtx->iBackoffMsec *= 2;
        struct timeval tv = { \
            pstUartCtx->iBackoffMsec / 1000, \
            (pstUartCtx->iBackoffMsec % 1000) * 1000 \
        };
        evtimer_add(pstUartCtx->pstEventReopen, &tv);
    }
}

void uartEventAttach(UART_CTX* pstUartCtx) {
    pstUartCtx->pstBev = bufferevent_socket_new(
        pstUartCtx->pstEventBase,
        pstUartCtx->iFd,
        BEV_OPT_CLOSE_ON_FREE | BEV_OPT_DEFER_CALLBACKS
    );
    bufferevent_setcb(pstUartCtx->pstBev, readCallback, NULL, eventCallback, pstUartCtx);
    bufferevent_enable(pstUartCtx->pstBev, EV_READ);
}

void uartEventScheduleReopen(UART_CTX* pstUartCtx) {
    if (pstUartCtx->pstBev) {
        bufferevent_free(pstUartCtx->pstBev);
        pstUartCtx->pstBev = NULL;
    }
    uartClose(pstUartCtx);
    struct timeval tv = { pstUartCtx->iBackoffMsec / 1000, (pstUartCtx->iBackoffMsec % 1000) * 1000 };
    evtimer_add(pstUartCtx->pstEventReopen, &tv);
}

void uartEventInit(UART_CTX* pstUartCtx)
{
    pstUartCtx->pstEventBase = event_base_new();
    pstUartCtx->pstEventReopen = evtimer_new(pstUartCtx->pstEventBase, reopenCallback, pstUartCtx);
    pstUartCtx->pstEventSigint = evsignal_new(pstUartCtx->pstEventBase, SIGINT, sigintCallback, pstUartCtx);
    event_add(pstUartCtx->pstEventSigint, NULL);
}

void uartEventCleanup(UART_CTX* pstUartCtx)
{
    if (pstUartCtx->pstBev) 
        bufferevent_free(pstUartCtx->pstBev);
    if (pstUartCtx->pstEventReopen) 
        event_free(pstUartCtx->pstEventReopen);
    if (pstUartCtx->pstEventSigint) 
        event_free(pstUartCtx->pstEventSigint);
    if (pstUartCtx->pstEventBase) 
        event_base_free(pstUartCtx->pstEventBase);
    uartClose(pstUartCtx);
}
