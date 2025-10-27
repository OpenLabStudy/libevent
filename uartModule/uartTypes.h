#ifndef UART_TYPES_H
#define UART_TYPES_H

#include <event2/event.h>
#include <event2/bufferevent.h>

typedef struct {
    const char          *pchDevPath;
    int                 iFd;
    struct event_base   *pstEventBase;
    struct event        *pstEventSigint;
    struct event        *pstEventReopen;
    struct bufferevent  *pstBev;
    int                 iBackoffMsec;
    int                 iBaudrate;
} UART_CTX;

#endif
