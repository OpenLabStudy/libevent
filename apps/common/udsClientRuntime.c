#include "udsClientRuntime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "netUds.h"
#include "netCore.h"
#include "eventSource.h"
#include "cmdRegistry.h"
#include "icdCommand.h"

/* ============================================================
 * 공통 RX 핸들러
 * ============================================================ */
static const char* tagOrDefault(int iId)
{
    switch(iId){
        case GPS_RECEIVER:
            return "GPS-RECEIVER";
        default :
            return "UDS-CLIENT";
    }
}

static void applyCommand(int iId, unsigned short unCmd, char *pchCmdResult)
{
    switch (unCmd)
    {
    case CMD_ID_INFO:
        ((RES_ID*)pchCmdResult)->chResult = (char)iId;
        break; 
    default:
        fprintf(stderr, "[GPS] Unsupported CMD\n");
        break;
    }
}

static void udsClientRecvCommandCb(int iFd, short nEvent, void *pvArg)
{
    (void)iFd;
    (void)nEvent;

    IO_CHANNEL *pstIoChannel = (IO_CHANNEL *)pvArg;
    IO_EVENT_TYPE eEventType = pstIoChannel->ePendingLogicEvent;
    FRAME_ERR eErr;
    unsigned short unCmd = 0;
    char achRecvBuffer[UDS_MAX_BUFFER_SIZE];
    switch (eEventType) {
    case IO_EVT_CHANNEL_CLOSED:
    case IO_EVT_ERROR:
        ioMarkChannelDead(pstIoChannel, pstIoChannel->ePendingLogicEvent);
        break;
    case IO_EVT_RX_DATA:
    {
        int iRecvLen = evbuffer_get_length(pstIoChannel->pstReadBuffer);
        fprintf(stderr,"### %s():%d Recv Size is %d ###\n", __func__, __LINE__, iRecvLen);
        if (iRecvLen < (int)sizeof(FRAME_HEADER))
            break;

        memset(achRecvBuffer, 0x00, sizeof(achRecvBuffer));
        int iCopyLen = evbuffer_copyout(pstIoChannel->pstReadBuffer, achRecvBuffer, iRecvLen);
        eErr = frameDecode(achRecvBuffer, iCopyLen, FRAME_TYPE_REQUEST, &unCmd);
        if (eErr != FRAME_OK) {
            fprintf(stderr, "[%s] frameDecode ERR: %s\n", tagOrDefault(pstIoChannel->iWorkerId), frameErrToStr(eErr));
            int iOffset = findFrameHeader(achRecvBuffer, iCopyLen);
            if (iOffset > 0) {
                /* 앞부분 garbage 제거 */
                evbuffer_drain(pstIoChannel->pstReadBuffer, iOffset);
                fprintf(stderr,"[%s] resync: drop %d bytes, retry decode\n", tagOrDefault(pstIoChannel->iWorkerId), iOffset);
            } else if (iOffset == -2) {
                /* STX half-match: 데이터 더 수신 */
                evbuffer_drain(pstIoChannel->pstReadBuffer, iCopyLen-1);
                fprintf(stderr,"[%s] STX half match, wait more data\n", tagOrDefault(pstIoChannel->iWorkerId));
            } else {
                /* STX 자체가 없음 → 전부 드랍 */
                evbuffer_drain(pstIoChannel->pstReadBuffer, iCopyLen);
                fprintf(stderr, "[%s] no STX, drop all\n", tagOrDefault(pstIoChannel->iWorkerId));
            }
        }
        int iFrameSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_REQUEST);
        /* === 프레임 소비 === */
        char achCmdData[128];
        char achCmdResult[128];
        char achResult[128];
        memset(achCmdData, 0x0, sizeof(achCmdData));
        memset(achCmdResult, 0x0, sizeof(achCmdResult));
        memset(achResult, 0x0, sizeof(achResult));
        unsigned int uiReqId;
        int iResultSize;
        evbuffer_drain(pstIoChannel->pstReadBuffer, iFrameSize);
        evbuffer_remove(pstIoChannel->pstReadBuffer, &uiReqId, sizeof(unsigned int));
        eErr = cmdDispatch(achRecvBuffer, iCopyLen, achCmdData);
        if (eErr != FRAME_OK){
            fprintf(stderr,"### %s():%d %s ###\n",__func__,__LINE__, frameErrToStr(eErr));
        }            
        applyCommand(pstIoChannel->iWorkerId, unCmd, achCmdResult);
        MSG_ID stMsgId = { (char)pstIoChannel->iWorkerId, SF_SENSOR_RECEIVER };
        eErr = createCmdResponse(unCmd, achCmdResult, &stMsgId, achResult);
        iResultSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_RESPONSE);
        evbuffer_add(pstIoChannel->pstWriteBuffer, achResult, iResultSize);
        event_add(pstIoChannel->pstWriteEvent, NULL);
    }

    default:
        break;
    }
    pstIoChannel->ePendingLogicEvent = IO_EVENT_NONE;
}

/* ============================================================
 * reconnect 타이머 콜백
 * ============================================================ */
static void udsClientReconnectCb(evutil_socket_t fd, short ev, void *pvArg)
{
    (void)fd;
    (void)ev;

    UDS_CLIENT_RUNTIME *pstUdsClnRuntime = (struct UDS_CLIENT_RUNTIME *)pvArg;
    if (!pstUdsClnRuntime || !pstUdsClnRuntime->pstEventEngine)
        return;

    if (pstUdsClnRuntime->pstIoChannel && ioIsChannelAlive(pstUdsClnRuntime->pstIoChannel))
        return;

    int sock = netUdsCreateClient(pstUdsClnRuntime->pchUdsPath);
    if (sock < 0) {
        fprintf(stderr, "[%s] reconnect failed\n",
                pstUdsClnRuntime->pchTag ? pstUdsClnRuntime->pchTag : "UDS");
        return;
    }

    netSetNonblock(sock);

    IO_CHANNEL *pstIoChannel = eventSourceCreateWithBev(pstUdsClnRuntime->pstEventEngine, sock,
                                                TYPE_UDS_CLI, ROLE_REQUESTER,
                                                NULL, pstUdsClnRuntime->pfWriteRespCb,
                                                pstUdsClnRuntime->pfRecvCommandCb);
    if (!pstIoChannel) {
        close(sock);
        return;
    }

    pstIoChannel->iWorkerId = pstUdsClnRuntime->iSelfWorkerId;
    pstIoChannel->chFdCloseSet = FD_OPENED;
    pstUdsClnRuntime->pstIoChannel = pstIoChannel;
    fprintf(stderr, "[%s] connected\n", pstUdsClnRuntime->pchTag ? pstUdsClnRuntime->pchTag : "UDS");
}

/* ============================================================
 * public API
 * ============================================================ */
UDS_CLIENT_RUNTIME* udsClientRuntimeCreate(EVENT_ENGINE *pstEventEngine,
                        const UDS_CLIENT_RUNTIME_CFG *pstCfg,
                        void (*pfRecvCommandCb)(int, short, void*),
                        void (*pfWriteRespCb)(int, short, void*))
{
    if (!pstEventEngine || !pstCfg)
        return NULL;

    UDS_CLIENT_RUNTIME *pstUdsClnRuntime = calloc(1, sizeof(UDS_CLIENT_RUNTIME));
    if (!pstUdsClnRuntime)
        return NULL;

    pstUdsClnRuntime->pstEventEngine = pstEventEngine;
    pstUdsClnRuntime->iSelfWorkerId  = pstCfg->iSelfWorkerId;
    pstUdsClnRuntime->iDstWorkerId   = pstCfg->iDstWorkerId;
    pstUdsClnRuntime->pchUdsPath     = pstCfg->pchUdsPath;
    pstUdsClnRuntime->pchTag         = pstCfg->pchTag;    
    pstUdsClnRuntime->pfWriteRespCb     = pfWriteRespCb;
    if(pfRecvCommandCb == NULL){
        pstUdsClnRuntime->pfRecvCommandCb   = udsClientRecvCommandCb;
    }else{
        pstUdsClnRuntime->pfRecvCommandCb   = pfRecvCommandCb;
    }

    struct timeval tv = {1, 0};
    pstUdsClnRuntime->pstReconnectEvent = event_new(pstEventEngine->pstEventBase, -1,
                                                    EV_PERSIST | EV_TIMEOUT,
                                                    udsClientReconnectCb,
                                                    pstUdsClnRuntime);
    if (!pstUdsClnRuntime->pstReconnectEvent) {
        free(pstUdsClnRuntime);
        return NULL;
    }

    event_add(pstUdsClnRuntime->pstReconnectEvent, &tv);
    return pstUdsClnRuntime;
}

void udsClientRuntimeDestroy(UDS_CLIENT_RUNTIME **ppstUdsClnRuntime)
{
    if (!ppstUdsClnRuntime || !*ppstUdsClnRuntime)
        return;

    UDS_CLIENT_RUNTIME *pstUdsClnRuntime = *ppstUdsClnRuntime;

    if (pstUdsClnRuntime->pstReconnectEvent) {
        event_del(pstUdsClnRuntime->pstReconnectEvent);
        event_free(pstUdsClnRuntime->pstReconnectEvent);
    }

    /* IO_CHANNEL은 eventEngineCleanup에서 정리됨 */
    pstUdsClnRuntime->pstIoChannel = NULL;

    free(pstUdsClnRuntime);
    *ppstUdsClnRuntime = NULL;
}
