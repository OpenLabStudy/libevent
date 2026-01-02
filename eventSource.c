#include "eventSource.h"
#include "eventEngine.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

#include "mti670Imu.h"

void readCallback(int iFd, short nEvent, void* pvData)
{
    unsigned char auchRecvBuffer[2048];
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;

    if (pstIoChannel->chFdCloseSet == 0x01)
        return;

    memset(auchRecvBuffer, 0x0, sizeof(auchRecvBuffer));
    int iReadSize = read(pstIoChannel->iFd, auchRecvBuffer, sizeof(auchRecvBuffer));    
    if (iReadSize == 0) {
        /* ===== 상대 정상 종료 ===== */
        pstIoChannel->ePendingLogicEvent = IO_EVT_CHANNEL_CLOSED;
        pstIoChannel->chFdCloseSet = 0x01;
    }else if (iReadSize < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;   /* 아무 이벤트도 발생시키지 않음 */
        }
        /* ===== 에러 ===== */
        pstIoChannel->chFdCloseSet = 0x01;
        pstIoChannel->ePendingLogicEvent = IO_EVT_ERROR;
    }else{        
        pstIoChannel->ePendingLogicEvent = IO_EVT_RX_DATA;
        evbuffer_add(pstIoChannel->pstReadBuffer, auchRecvBuffer, iReadSize);
    }
    event_active(pstIoChannel->pstLogicEvent, 0, 0);
}


void writeCallback(int iFd, short nEvent, void* pvData)
{
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    unsigned char auchWriteBuffer[2048];
    int iWriteSize;
    iWriteSize = evbuffer_get_length(pstIoChannel->pstWriteBuffer);
    if (iWriteSize == 0) {
        event_del(pstIoChannel->pstWriteEvent);
        return;
    }    
    iWriteSize = evbuffer_remove(pstIoChannel->pstWriteBuffer, auchWriteBuffer, sizeof(auchWriteBuffer));
    iWriteSize = write(pstIoChannel->iFd, auchWriteBuffer, iWriteSize);
    if (iWriteSize <= 0) {
        perror("write");
        return;
    }
    if (evbuffer_get_length(pstIoChannel->pstWriteBuffer) == 0)
        event_del(pstIoChannel->pstWriteEvent);
}


void eventEngineShutdownCb(int iFd, short nEvent, void* pvData)
{
    EVENT_ENGINE* pstEventEngine = (EVENT_ENGINE*)pvData;

    fprintf(stderr, "[ENGINE] shutdown requested\n");
    event_base_loopexit(pstEventEngine->pstEventBase, NULL);
}


void eventEngineDispatchSrcCb(evutil_socket_t iFd, short nEvent, void* pvData)
{
    (void)iFd;
    (void)nEvent;
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL*)pvData;
    EVENT_ENGINE* pstEventEngine = pstIoChannel->pstEventEngine;

    IO_CHANNEL** ppCurIoChannel = &pstEventEngine->pstIoChannelList;

    fprintf(stderr, "[DISPATCH] enter destroy fd=%d\n", pstIoChannel->iFd);
    while (*ppCurIoChannel) {
        if (*ppCurIoChannel == pstIoChannel) {
            *ppCurIoChannel = pstIoChannel->pstNextIoChannel;
            pstIoChannel->pstNextIoChannel = NULL;
            break;
        }
        ppCurIoChannel = &(*ppCurIoChannel)->pstNextIoChannel;
    }
    eventSourceDestroy(pstIoChannel);
}


/* --------------------------------------------------------- */
/* bufferevent 기반 IO_CHANNEL 생성                        */
/* --------------------------------------------------------- */
IO_CHANNEL* eventSourceCreateWithBev( EVENT_ENGINE* pstEventEngine, int iFd,
    IO_TYPE eType, IO_ROLE eRole, 
    event_callback_fn pfRead, event_callback_fn pfWrite, event_callback_fn pfEvent)
{
    if (!pstEventEngine || !pstEventEngine->pstEventBase){
        fprintf(stderr, "[eventSource] pstEventEngine is NULL\n");
        return NULL;
    }

    IO_CHANNEL* pstIoChannel = (IO_CHANNEL*)calloc(1, sizeof(IO_CHANNEL));
    if (!pstIoChannel) 
        return NULL;

    pstIoChannel->iFd                   = iFd;
    pstIoChannel->chFdCloseSet          = 0x00;
    pstIoChannel->iWorkerId             = 0;
    pstIoChannel->eType                 = eType;
    pstIoChannel->eRole                 = eRole;
    pstIoChannel->ePendingLogicEvent    = IO_EVENT_NONE; 
    pstIoChannel->pstNextIoChannel      = NULL;
    pstIoChannel->pstEventEngine        = pstEventEngine;

    if(pfRead == NULL){
        pstIoChannel->pstReadEvent = event_new(pstEventEngine->pstEventBase, 
            iFd, EV_READ|EV_PERSIST, readCallback, pstIoChannel);
    }else{
        pstIoChannel->pstReadEvent = event_new(pstEventEngine->pstEventBase, 
            iFd, EV_READ|EV_PERSIST, pfRead, pstIoChannel);
    }
    
    pstIoChannel->pstReadBuffer = evbuffer_new();
    event_add(pstIoChannel->pstReadEvent, NULL);

    if(pfWrite == NULL){
        pstIoChannel->pstWriteEvent = event_new(pstEventEngine->pstEventBase, 
            iFd, EV_WRITE|EV_PERSIST, writeCallback, pstIoChannel);
    }else{
        pstIoChannel->pstWriteEvent = event_new(pstEventEngine->pstEventBase, 
            iFd, EV_WRITE|EV_PERSIST, pfWrite, pstIoChannel);
    }
    pstIoChannel->pstWriteBuffer = evbuffer_new();

    if(eType == TYPE_TCP_SVR || eType == TYPE_UDS_SVR ){
        pstIoChannel->pstShutdownEvent = event_new(pstEventEngine->pstEventBase, 
                    -1, EV_PERSIST, eventEngineDispatchSrcCb, pstIoChannel);
    }else{
        pstIoChannel->pstShutdownEvent = event_new(pstEventEngine->pstEventBase, 
            -1, EV_PERSIST, eventEngineShutdownCb, pstEventEngine);
    }

    pstIoChannel->pstLogicEvent = event_new(pstEventEngine->pstEventBase,
        -1/* FD 없음 */,  EV_PERSIST, pfEvent,  pstIoChannel);
    if(eRole == ROLE_REQUESTER){
        pstIoChannel->pstRequestEvent = event_new(pstEventEngine->pstEventBase,
            -1/* FD 없음 */,  EV_PERSIST, eventEngineHandleRequest,  pstIoChannel);
        pstIoChannel->pstRequestBuffer = evbuffer_new();
    }else{
        pstIoChannel->pstRequestBuffer = NULL;
        pstIoChannel->pstRequestEvent = NULL;
    }


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

    if (pstIoChannel->pstReadEvent) {
        event_free(pstIoChannel->pstReadEvent);
        pstIoChannel->pstReadEvent = NULL;
    }
    if (pstIoChannel->pstReadBuffer) {
        evbuffer_free(pstIoChannel->pstReadBuffer);
        pstIoChannel->pstReadBuffer = NULL;
    }

    if (pstIoChannel->pstWriteEvent) {
        event_free(pstIoChannel->pstWriteEvent);
        pstIoChannel->pstWriteEvent = NULL;
    }
    if (pstIoChannel->pstWriteBuffer) {
        evbuffer_free(pstIoChannel->pstWriteBuffer);
        pstIoChannel->pstWriteBuffer = NULL;
    }

    if (pstIoChannel->pstRequestEvent) {
        event_free(pstIoChannel->pstRequestEvent);
        pstIoChannel->pstRequestEvent = NULL;
    }
    if (pstIoChannel->pstRequestBuffer) {
        evbuffer_free(pstIoChannel->pstRequestBuffer);
        pstIoChannel->pstRequestBuffer = NULL;
    }

    if (pstIoChannel->pstShutdownEvent) {
        event_free(pstIoChannel->pstShutdownEvent);
        pstIoChannel->pstShutdownEvent = NULL;
    }
    if (pstIoChannel->pstLogicEvent) {
        event_free(pstIoChannel->pstLogicEvent);
        pstIoChannel->pstLogicEvent = NULL;
    }

    if (pstIoChannel->iFd >= 0) {
        close(pstIoChannel->iFd);
        pstIoChannel->iFd = -1;
    }
    pstIoChannel->pstNextIoChannel = NULL;
    free(pstIoChannel);
}