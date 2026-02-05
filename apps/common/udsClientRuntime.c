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

static void udsClientRecvCommandCb(int iFd, short nEvent, void* pvData)
{
    (void)iFd;
    (void)nEvent;
    IO_CHANNEL *pstIoChannel = (IO_CHANNEL *)pvData;
    IO_EVENT_TYPE eEventType = pstIoChannel->ePendingLogicEvent;
    FRAME_ERR eErr;
    MSG_ID stMsgId;
    unsigned short unCmd = 0;
    char achRecvBuffer[UDS_MAX_BUFFER_SIZE];
    char achCmdData[64];
    char achTcpRespBuffer[64];
    unsigned int uiReqId;
    int iResultSize;
    switch (eEventType) {
    case IO_EVT_CHANNEL_CLOSED:
    case IO_EVT_ERROR:
        ioMarkChannelDead(pstIoChannel, pstIoChannel->ePendingLogicEvent);
        break;
    case IO_EVT_RX_DATA:{
        unsigned int uiTotalRcvSize = evbuffer_get_length(pstIoChannel->pstReadBuffer);
        if (uiTotalRcvSize < (int)sizeof(FRAME_HEADER))
            break;

        memset(achRecvBuffer, 0x00, sizeof(achRecvBuffer));
        int iCopyLen = evbuffer_copyout(pstIoChannel->pstReadBuffer, achRecvBuffer, uiTotalRcvSize);
        eErr = frameDecode(achRecvBuffer, iCopyLen, FRAME_TYPE_REQUEST, &unCmd);
        if (eErr != FRAME_OK) {
            fprintf(stderr, "[%s] frameDecode ERR: %s\n", getWorkerName(pstIoChannel->chWorkerId), frameErrToStr(eErr));
            int iDeleteDataSize = findFrameHeader(achRecvBuffer, iCopyLen);
            evbuffer_drain(pstIoChannel->pstReadBuffer, iDeleteDataSize);
            iCopyLen-=iDeleteDataSize;
        }
        int iFrameSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_REQUEST);
        if (iCopyLen < iFrameSize)
            break;
        evbuffer_drain(pstIoChannel->pstReadBuffer, iFrameSize);
        evbuffer_remove(pstIoChannel->pstReadBuffer, &uiReqId, sizeof(unsigned int));
        if(unCmd == CMD_ID_INFO){
            ((RES_ID*)achCmdData)->chId = (char)pstIoChannel->chWorkerId;
        }else{
            eErr = cmdDispatch(achRecvBuffer, iCopyLen, achCmdData);
            if(eErr != FRAME_OK){
                fprintf(stderr, "[KEYBOARD-RECEIVER] frameDecode ERR: %s\n", frameErrToStr(eErr));
                break;
            }
        }        
        stMsgId.uchSrcId = pstIoChannel->chWorkerId;
        stMsgId.uchDstId = pstIoChannel->chDstWorkerId;
        iResultSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_RESPONSE);
        eErr = createCmdResponse(unCmd, achCmdData, &stMsgId, achTcpRespBuffer);        
        evbuffer_add(pstIoChannel->pstWriteBuffer, achTcpRespBuffer, iResultSize);
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
static void udsClientReconnectCb(int iFd, short nEvent, void* pvData)
{
    (void)iFd;
    (void)nEvent;    
    UDS_CLIENT_RUNTIME *pstUdsClnRt = (UDS_CLIENT_RUNTIME *)pvData;
    if (!pstUdsClnRt || !pstUdsClnRt->pstEventEngine)
        return;
    
    if (pstUdsClnRt->pstIoChannel && ioIsChannelAlive(pstUdsClnRt->pstIoChannel))
        return;
    
    int sock = netUdsCreateClient(pstUdsClnRt->pchUdsPath);
    if (sock < 0) {
        fprintf(stderr, "[%s] reconnect failed\n",
                pstUdsClnRt->pchTag ? pstUdsClnRt->pchTag : "UDS");
        return;
    }
    netSetNonblock(sock);

    IO_CHANNEL *pstIoChannel = eventSourceCreateWithBev(pstUdsClnRt->pstEventEngine, sock,
                                                pstUdsClnRt->eType, pstUdsClnRt->eRole,
                                                NULL, pstUdsClnRt->pfWriteRespCb,
                                                pstUdsClnRt->pfRecvCommandCb);
    if (!pstIoChannel) {
        close(sock);
        return;
    }

    pstIoChannel->chWorkerId    = pstUdsClnRt->chWorkerId;
    pstIoChannel->chDstWorkerId  = pstUdsClnRt->chDstWorkerId;
    pstIoChannel->chFdCloseSet  = FD_OPENED;
    pstUdsClnRt->pstIoChannel   = pstIoChannel;
    fprintf(stderr, "[%s] connected\n", pstUdsClnRt->pchTag ? pstUdsClnRt->pchTag : "UDS");
}

/* ============================================================
 * public API
 * ============================================================ */
UDS_CLIENT_RUNTIME* udsClientRuntimeCreate(EVENT_ENGINE *pstEventEngine,
                        const UDS_CLIENT_RUNTIME_CFG *pstUdsClnRtCfg,
                        void (*pfRecvCommandCb)(int, short, void*),
                        void (*pfWriteRespCb)(int, short, void*))
{
    if (!pstEventEngine || !pstUdsClnRtCfg)
        return NULL;

    UDS_CLIENT_RUNTIME *pstUdsClnRuntime = calloc(1, sizeof(UDS_CLIENT_RUNTIME));
    if (!pstUdsClnRuntime)
        return NULL;

    pstUdsClnRuntime->pstEventEngine    = pstEventEngine;
    pstUdsClnRuntime->chWorkerId        = pstUdsClnRtCfg->chWorkerId;
    pstUdsClnRuntime->chDstWorkerId     = pstUdsClnRtCfg->chDstWorkerId;
    pstUdsClnRuntime->pchUdsPath        = pstUdsClnRtCfg->pchUdsPath;
    pstUdsClnRuntime->pchTag            = pstUdsClnRtCfg->pchTag;
    pstUdsClnRuntime->eRole             = pstUdsClnRtCfg->eRole;
    pstUdsClnRuntime->eType             = pstUdsClnRtCfg->eType;
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

void udsClientRuntimeDestroy(UDS_CLIENT_RUNTIME **ppstUdsClnRt)
{
    if (!ppstUdsClnRt || !*ppstUdsClnRt)
        return;

    UDS_CLIENT_RUNTIME* pstUdsClnRt = *ppstUdsClnRt;

    if (pstUdsClnRt->pstReconnectEvent) {
        event_del(pstUdsClnRt->pstReconnectEvent);
        event_free(pstUdsClnRt->pstReconnectEvent);
    }

    /* IO_CHANNEL은 eventEngineCleanup에서 정리됨 */
    pstUdsClnRt->pstIoChannel = NULL;

    free(pstUdsClnRt);
    pstUdsClnRt = NULL;
}
