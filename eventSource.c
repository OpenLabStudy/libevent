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
    event_callback_fn  pfRead, event_callback_fn pfWrite)
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

    pstIoChannel->pstReadEvent = event_new(pstEventEngine->pstEventBase, 
        iFd, EV_READ|EV_PERSIST, pfRead, pstIoChannel);
    event_add(pstIoChannel->pstReadEvent, NULL);

    pstIoChannel->pstReadEvent = event_new(pstEventEngine->pstEventBase, 
        iFd, EV_READ|EV_PERSIST, pfWrite, pstIoChannel);
    event_add(pstIoChannel->pstWriteEvent, NULL);

    eventEngineAttachSource(pstEventEngine, pstIoChannel);

    return pstIoChannel;
}

/* --------------------------------------------------------- */
/* raw FD 기반 IO_CHANNEL 생성                             */
/* --------------------------------------------------------- */
IO_CHANNEL* eventSourceCreateWithFd(EVENT_ENGINE* pstEventEngine, int iFd,
    IO_TYPE eType, IO_ROLE eRole,
    bufferevent_data_cb  pfRead, bufferevent_event_cb pfEvent)
{
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    if (!pstEventEngine || !pstEventEngine->pstEventBase)
        return NULL;
        fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL*)calloc(1, sizeof(IO_CHANNEL));
    if (!pstIoChannel)
        return NULL;
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    pstIoChannel->iFd            = iFd;
    pstIoChannel->eType          = eType;
    pstIoChannel->eRole          = eRole;
    pstIoChannel->pstEventBase  = pstEventEngine->pstEventBase;

    // bev1 = bufferevent_new(pair[0], readcb, writecb, errorcb, NULL);
    /* === 4) STDIN 이벤트 등록 === */
    pstIoChannel->pstEvent = event_new(
        pstEventEngine->pstEventBase,
        iFd,
        EV_READ | EV_PERSIST,
        pfRead,
        pstIoChannel);
    if (pstIoChannel->pstEvent)
        event_add(pstIoChannel->pstEvent, NULL);

    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    eventEngineAttachSource(pstEventEngine, pstIoChannel);
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
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
