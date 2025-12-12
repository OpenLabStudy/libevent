#include "eventSource.h"
#include "eventEngine.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

/* --------------------------------------------------------- */
/* bufferevent 기반 IO_CHANNEL 생성                        */
/* --------------------------------------------------------- */
IO_CHANNEL* eventSourceCreateWithBev(
    EVENT_ENGINE* pstEventEngine, int iFd,
    IO_TYPE eType, IO_ROLE eRole,
    bufferevent_data_cb  pfRead, bufferevent_event_cb pfEvent)
{
    if (!pstEventEngine || !pstEventEngine->pstEventBase){
        fprintf(stderr, "[eventSource] pstEventEngine is NULL\n");
        return NULL;
    }

    IO_CHANNEL* pstIoChannel = (IO_CHANNEL*)calloc(1, sizeof(IO_CHANNEL));
    if (!pstIoChannel) 
        return NULL;

    pstIoChannel->iFd            = iFd;
    pstIoChannel->eType          = eType;
    pstIoChannel->eRole          = eRole;
    pstIoChannel->pstEventBase  = pstEventEngine->pstEventBase;

    pstIoChannel->pstBufferEvent = bufferevent_socket_new(
        pstEventEngine->pstEventBase, iFd, BEV_OPT_CLOSE_ON_FREE);
    if (!pstIoChannel->pstBufferEvent) {
        free(pstIoChannel);
        return NULL;
    }

    bufferevent_setcb(pstIoChannel->pstBufferEvent, 
        pfRead, 
        NULL, 
        pfEvent, 
        pstIoChannel);
    bufferevent_enable(pstIoChannel->pstBufferEvent, 
        EV_READ | EV_WRITE);

    eventEngineAttachSource(pstEventEngine, pstIoChannel);

    return pstIoChannel;
}

/* --------------------------------------------------------- */
/* raw FD 기반 IO_CHANNEL 생성                             */
/* --------------------------------------------------------- */
IO_CHANNEL* eventSourceCreateWithFd( EVENT_ENGINE* pstEventEngine, int iFd, 
    IO_TYPE eType, IO_ROLE eRole, event_callback_fn pfEvent)
{
    if (!pstEventEngine || !pstEventEngine->pstEventBase)
        return NULL;

    IO_CHANNEL* pstIoChannel = (IO_CHANNEL*)calloc(1, sizeof(IO_CHANNEL));
    if (!pstIoChannel)
        return NULL;

    pstIoChannel->iFd            = iFd;
    pstIoChannel->eType          = eType;
    pstIoChannel->eRole          = eRole;
    pstIoChannel->pstEventBase  = pstEventEngine->pstEventBase;

    struct event *event_new(struct event_base *, evutil_socket_t, short, event_callback_fn, void *);
    pstIoChannel->pstEvent = event_new(
        pstEventEngine->pstEventBase,
        iFd,
        EV_READ | EV_PERSIST,
        pfEvent,
        pstIoChannel);
    if (!pstIoChannel->pstEvent) {
        free(pstIoChannel);
        return NULL;
    }

    event_add(pstIoChannel->pstEvent, NULL);
    eventEngineAttachSource(pstEventEngine, pstIoChannel);
    return pstIoChannel;
}

/* --------------------------------------------------------- */
/* IO_CHANNEL 파괴                                          */
/* --------------------------------------------------------- */
void eventSourceDestroy(IO_CHANNEL* pstIoChannel)
{
    if (!pstIoChannel) 
        return;

    if (pstIoChannel->pstBufferEvent) {
        bufferevent_free(pstIoChannel->pstBufferEvent);
        pstIoChannel->pstBufferEvent = NULL;
        /* fd는 BEV_OPT_CLOSE_ON_FREE로 닫힘 */
        pstIoChannel->iFd = -1;
    }

    if (pstIoChannel->pstEvent) {
        event_free(pstIoChannel->pstEvent);
        pstIoChannel->pstEvent = NULL;
    }

    if (pstIoChannel->iFd >= 0) {
        close(pstIoChannel->iFd);
        pstIoChannel->iFd = -1;
    }

    free(pstIoChannel);
}
