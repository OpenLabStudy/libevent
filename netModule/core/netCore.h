#ifndef NET_CORE_H
#define NET_CORE_H

#include <event2/event.h>
#include <event2/bufferevent.h>

typedef struct {
    struct event_base   *pstEventBase;
    struct event        *pstSigIntEvent;
} NET_CORE;

NET_CORE*   netCoreCreate(void);
void        netCoreRun(NET_CORE* core);
void        netCoreDestroy(NET_CORE* core);

#endif
