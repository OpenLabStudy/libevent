#include "eventSource.h"
#include "dispatcher.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

void baseContextInit(BASE_CONTEXT* pstCtx, uint16_t usMyId)
{
    if (!pstCtx)
        return;
    memset(pstCtx, 0, sizeof(BASE_CONTEXT));
    pstCtx->usMyId = usMyId;
}

void baseContextCleanup(BASE_CONTEXT* pstCtx)
{
    if (!pstCtx)
        return;

    if (pstCtx->pstSignalEvent) {
        event_free(pstCtx->pstSignalEvent);
        pstCtx->pstSignalEvent = NULL;
    }
    if (pstCtx->pstMainTimer) {
        event_free(pstCtx->pstMainTimer);
        pstCtx->pstMainTimer = NULL;
    }

    /* pstEventBase는 main()에서 event_base_free() */
    pstCtx->pstEventBase = NULL;
}

/* --------------------------------------------------------- */
/* bufferevent 기반 EVENT_SOURCE 생성                        */
/* --------------------------------------------------------- */
EVENT_SOURCE* eventSourceCreateWithBev(
    DISPATCHER* pstDispatcher, int iFd,
    SRC_TYPE eType, SRC_ROLE eRole,
    bufferevent_data_cb  pfRead, bufferevent_event_cb pfEvent)
{
    if (!pstDispatcher || !pstDispatcher->pstBaseCtx ||
        !pstDispatcher->pstBaseCtx->pstEventBase){
        fprintf(stderr, "[eventSource] Dispatcher is NULL\n");
        return NULL;
    }

    EVENT_SOURCE* pstEventSrc = calloc(1, sizeof(EVENT_SOURCE));
    if (!pstEventSrc) 
        return NULL;

    pstEventSrc->iFd            = iFd;
    pstEventSrc->eType          = eType;
    pstEventSrc->eRole          = eRole;
    pstEventSrc->pstDispatcher  = pstDispatcher;

    pstEventSrc->pstBufferEvent = bufferevent_socket_new(
        pstDispatcher->pstBaseCtx->pstEventBase, 
        iFd, BEV_OPT_CLOSE_ON_FREE);
    if (!pstEventSrc->pstBufferEvent) {
        free(pstEventSrc);
        return NULL;
    }

    bufferevent_setcb(pstEventSrc->pstBufferEvent, 
        pfRead, 
        NULL, 
        pfEvent, 
        pstEventSrc);
    bufferevent_enable(pstEventSrc->pstBufferEvent, 
        EV_READ | EV_WRITE);

    dispatcherAttachSource(pstDispatcher, pstEventSrc);

    return pstEventSrc;
}


EVENT_SOURCE* eventSourceCreateBevStandalone(
    struct event_base* base,
    int                fd,
    SRC_TYPE           eType,
    bufferevent_data_cb         pfRead,
    bufferevent_event_cb        pfEvent)
{
    if (!base) 
        return NULL;

    EVENT_SOURCE* pstEventSrc = calloc(1, sizeof(EVENT_SOURCE));
    if (!pstEventSrc) 
        return NULL;

    pstEventSrc->iFd       = fd;
    pstEventSrc->eType     = eType;
    pstEventSrc->eRole     = SRC_ROLE_NONE;
    pstEventSrc->pstDispatcher = NULL; 

    pstEventSrc->pstBufferEvent = bufferevent_socket_new(
        base,
        fd,
        BEV_OPT_CLOSE_ON_FREE);
    if (!pstEventSrc->pstBufferEvent) {
        free(pstEventSrc);
        return NULL;
    }

    bufferevent_setcb(pstEventSrc->pstBufferEvent, pfRead, NULL, pfEvent, pstEventSrc);
    bufferevent_enable(pstEventSrc->pstBufferEvent, EV_READ | EV_WRITE);

    return pstEventSrc; /* Dispatcher에 attach 하지 않음 */
}


/* --------------------------------------------------------- */
/* raw FD 기반 EVENT_SOURCE 생성                             */
/* --------------------------------------------------------- */
EVENT_SOURCE* eventSourceCreateWithFd(
    DISPATCHER* pstDispatcher,
    int         iFd,
    SRC_TYPE    eType,
    SRC_ROLE    eRole,
    event_callback_fn pfEvent)
{
    if (!pstDispatcher || !pstDispatcher->pstBaseCtx ||
        !pstDispatcher->pstBaseCtx->pstEventBase)
        return NULL;

    EVENT_SOURCE* pstEventSrc = (EVENT_SOURCE*)calloc(1, sizeof(EVENT_SOURCE));
    if (!pstEventSrc)
        return NULL;

    pstEventSrc->iFd            = iFd;
    pstEventSrc->eType          = eType;
    pstEventSrc->eRole          = eRole;
    pstEventSrc->pstDispatcher  = pstDispatcher;

    struct event *event_new(struct event_base *, evutil_socket_t, short, event_callback_fn, void *);
    pstEventSrc->pstEvent = event_new(
        pstDispatcher->pstBaseCtx->pstEventBase,
        iFd,
        EV_READ | EV_PERSIST,
        pfEvent,
        pstEventSrc);
    if (!pstEventSrc->pstEvent) {
        free(pstEventSrc);
        return NULL;
    }

    event_add(pstEventSrc->pstEvent, NULL);
    dispatcherAttachSource(pstDispatcher, pstEventSrc);
    return pstEventSrc;
}

/* --------------------------------------------------------- */
/* EVENT_SOURCE 파괴                                          */
/* --------------------------------------------------------- */
void eventSourceDestroy(EVENT_SOURCE* pstEventSrc)
{
    if (!pstEventSrc) 
        return;

    if (pstEventSrc->pstDispatcher)
        dispatcherDetachSource(pstEventSrc->pstDispatcher, pstEventSrc);

    if (pstEventSrc->pstBufferEvent) {
        bufferevent_free(pstEventSrc->pstBufferEvent);
        pstEventSrc->pstBufferEvent = NULL;
        /* fd는 BEV_OPT_CLOSE_ON_FREE로 닫힘 */
        pstEventSrc->iFd = -1;
    }

    if (pstEventSrc->pstEvent) {
        event_free(pstEventSrc->pstEvent);
        pstEventSrc->pstEvent = NULL;
    }

    if (pstEventSrc->iFd >= 0) {
        close(pstEventSrc->iFd);
        pstEventSrc->iFd = -1;
    }

    free(pstEventSrc);
}
