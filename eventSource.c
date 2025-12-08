#include "eventSource.h"
#include "dispatcher.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

/* --------------------------------------------------------- */
/* bufferevent read/event 래퍼                               */
/* --------------------------------------------------------- */
// static void bevReadCb(struct bufferevent* pstBufferEvent, void* pvArg)
// {
//     fprintf(stderr,"### %s():%d ###\n", __func__,__LINE__);
//     EVENT_SOURCE* pstEventSrc = (EVENT_SOURCE*)pvArg;
//     if (!pstEventSrc || !pstEventSrc->pfOnRead)
//         return;
//         fprintf(stderr,"### %s():%d ###\n", __func__,__LINE__);
//     unsigned char auchBuffer[2048];
//     int iLen = bufferevent_read(pstBufferEvent, auchBuffer, sizeof(auchBuffer));
//     if (iLen > 0) {
//         fprintf(stderr,"### %s():%d ###\n", __func__,__LINE__);
//         pstEventSrc->pfOnRead(pstEventSrc, auchBuffer, iLen);
//     }
//     fprintf(stderr,"### %s():%d ###\n", __func__,__LINE__);
// }

// static void bevEventCb(struct bufferevent* pstBufferEvent, short nKindOfEvent, void* pvArg)
// {
//     (void)pstBufferEvent;
//     EVENT_SOURCE* pstEventSrc = (EVENT_SOURCE*)pvArg;
//     if (!pstEventSrc || !pstEventSrc->pfOnEvent)
//         return;

//     pstEventSrc->pfOnEvent(pstEventSrc, nKindOfEvent);
// }

// /* --------------------------------------------------------- */
// /* raw FD 이벤트 콜백                                        */
// /* --------------------------------------------------------- */
// static void rawFdEventCb(evutil_socket_t iFd, short nKindOfEvent, void* pvArg)
// {
//     EVENT_SOURCE* pstEventSrc = (EVENT_SOURCE*)pvArg;
//     if (!pstEventSrc) 
//         return;

//     if (nKindOfEvent & EV_READ) {
//         if (!pstEventSrc->pfOnRead)
//             return;

//         unsigned char auchBuffer[2048];
//         int iLen = (int)read(iFd, auchBuffer, sizeof(auchBuffer));
//         if (iLen > 0) {
//             pstEventSrc->pfOnRead(pstEventSrc, auchBuffer, iLen);
//         } else if (iLen == 0) { /* EOF */
//             if (pstEventSrc->pfOnEvent)
//                 pstEventSrc->pfOnEvent(pstEventSrc, BEV_EVENT_EOF);
//         } else {
//             if (errno != EAGAIN && errno != EWOULDBLOCK) {
//                 if (pstEventSrc->pfOnEvent)
//                     pstEventSrc->pfOnEvent(pstEventSrc, BEV_EVENT_ERROR);
//             }
//         }
//     }
// }

/* --------------------------------------------------------- */
/* bufferevent 기반 EVENT_SOURCE 생성                        */
/* --------------------------------------------------------- */
EVENT_SOURCE* eventSourceCreateWithBev(
    DISPATCHER* pstDispatcher,
    int         iFd,
    SRC_TYPE    eType,
    SRC_ROLE    eRole,
    ES_READ_CB  pfRead,
    ES_EVENT_CB pfEvent)
{
    if (!pstDispatcher || !pstDispatcher->pstBaseCtx ||
        !pstDispatcher->pstBaseCtx->pstEventBase)
    {
        fprintf(stderr, "[eventSource] Dispatcher is NULL\n");
        return NULL;
    }

    EVENT_SOURCE* pstEventSrc = calloc(1, sizeof(EVENT_SOURCE));
    if (!pstEventSrc) 
        return NULL;

    pstEventSrc->iFd        = iFd;
    pstEventSrc->eType      = eType;
    pstEventSrc->eRole      = eRole;
    pstEventSrc->pstDispatcher = pstDispatcher;

    pstEventSrc->pstBev = bufferevent_socket_new(
        pstDispatcher->pstBaseCtx->pstEventBase,
        iFd,
        BEV_OPT_CLOSE_ON_FREE);
    if (!pstEventSrc->pstBev) {
        free(pstEventSrc);
        return NULL;
    }

    bufferevent_setcb(pstEventSrc->pstBev, pfRead, NULL, pfEvent, pstEventSrc);
    bufferevent_enable(pstEventSrc->pstBev, EV_READ | EV_WRITE);

    dispatcherAttachSource(pstDispatcher, pstEventSrc);

    return pstEventSrc;
}


EVENT_SOURCE* eventSourceCreateBevStandalone(
    struct event_base* base,
    int                fd,
    SRC_TYPE           eType,
    ES_READ_CB         pfRead,
    ES_EVENT_CB        pfEvent)
{
    if (!base) 
        return NULL;

    EVENT_SOURCE* pstEventSrc = calloc(1, sizeof(EVENT_SOURCE));
    if (!pstEventSrc) 
        return NULL;

    pstEventSrc->iFd       = fd;
    pstEventSrc->eType     = eType;
    pstEventSrc->eRole     = SRC_ROLE_NONE;
    pstEventSrc->pstDispatcher = NULL; /* 중요 */

    pstEventSrc->pstBev = bufferevent_socket_new(
        base,
        fd,
        BEV_OPT_CLOSE_ON_FREE);
    if (!pstEventSrc->pstBev) {
        free(pstEventSrc);
        return NULL;
    }

    bufferevent_setcb(pstEventSrc->pstBev, pfRead, NULL, pfEvent, pstEventSrc);
    bufferevent_enable(pstEventSrc->pstBev, EV_READ | EV_WRITE);

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
    ES_READ_CB  pfRead,
    ES_EVENT_CB pfEvent)
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

    pstEventSrc->pstEvent = event_new(
        pstDispatcher->pstBaseCtx->pstEventBase,
        iFd,
        EV_READ | EV_PERSIST,
        pfRead,
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

    if (pstEventSrc->pstBev) {
        bufferevent_free(pstEventSrc->pstBev);
        pstEventSrc->pstBev = NULL;
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
