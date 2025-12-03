/**
 * @file bridgeRouter.c
 * @brief TCP ↔ UDS 라우팅 처리 모듈 구현
 */

#include "bridgeRouter.h"
#include <stdio.h>
#include <string.h>
#include <event2/event.h>
#include <event2/buffer.h>



#define MAX_FRAME_BUF 2048



/* ========================================================================== */
/* Public API Implementation                                                   */
/* ========================================================================== */

void bridgeInit(BRIDGE_CONTEXT* pstCtx,
                UDS_CLIENT_TABLE* pstUdsTable,
                REQUEST_CONTEXT* pstReqCtx,
                struct event_base* pstEvBase,
                unsigned char uchTcpSrcId,
                int iTimeoutMs)
{
    pstCtx->pstUdsTable       = pstUdsTable;
    pstCtx->pstReqCtx         = pstReqCtx;
    pstCtx->pstEvBase         = pstEvBase;
    pstCtx->uchTcpSrcId       = uchTcpSrcId;
    pstCtx->iTimeoutMs        = iTimeoutMs;

    printf("[BRIDGE] Init complete (Timeout=%dms)\n", iTimeoutMs);
}


/* ========================================================================== */
/* TCP → UDS (Request Dispatch)                                               */
/* ========================================================================== */

void bridgeTcpReadCb(struct bufferevent* pstBev, void* pvData)
{
    unsigned char auchRecvBuf[MAX_FRAME_BUF];
    unsigned char auchPayloadBuf[1024];
    unsigned char auchSendBuf[MAX_FRAME_BUF];
    
    MSG_ID stMsgId;
    unsigned short unCmd = 0;
    FRAME_ERR eErr;
    int iCopyLen, iFrameSize, iSendLen;

    SOCK_CONTEXT* pSockCtx = (SOCK_CONTEXT*)pvData;
    BRIDGE_CONTEXT* pBridge = (BRIDGE_CONTEXT*)pSockCtx->pvUserCtx;

    struct evbuffer* pstInput = bufferevent_get_input(pstBev);

    while (1)
    {
        iSendLen = 0;
        size_t tRecvSize = evbuffer_get_length(pstInput);
        if (tRecvSize < FRAME_HEADER_MIN_SIZE)
            break;

        if (tRecvSize > sizeof(auchRecvBuf))
            tRecvSize = sizeof(auchRecvBuf);

        iCopyLen = evbuffer_copyout(pstInput, auchRecvBuf, tRecvSize);
        iFrameSize = getFrameSize(auchRecvBuf);

        if (iFrameSize <= 0)
        {
            evbuffer_drain(pstInput, 1);
            continue;
        }

        if (iCopyLen < iFrameSize)
            break;

        evbuffer_drain(pstInput, iFrameSize);

        /* Parse request */
        stMsgId.uchSrcId = pSockCtx->uchSrcId;
        stMsgId.uchDstId = 0x1F; // ignored (mask controls UDS)
        eErr = requestFrame(auchRecvBuf, &stMsgId, iFrameSize, &unCmd);        
        if (eErr != FRAME_OK)
        {
            printf("[TCP] requestFrame ERR: %s\n", frameErrToStr(eErr));
            continue;
        }

        /* === 명령 처리 === */
        eErr = commandHandler(auchRecvBuf, &stMsgId, iFrameSize, auchPayloadBuf, &iSendLen);
        fprintf(stderr,"### %s():%d SRC %02X, DST %02X ###\n",__func__, __LINE__, stMsgId.uchSrcId, stMsgId.uchDstId);
        if (eErr != FRAME_OK || iSendLen <= 0)
            continue;

        //todo UDS를 통해 전송될 데이터가 있으면
        int iWriteLen;
        makeReqFrame(unCmd, &stMsgId, auchSendBuf, &iWriteLen);
        fprintf(stderr,"### %s():%d SRC %02X, DST %02X ###\n",__func__, __LINE__, stMsgId.uchSrcId, stMsgId.uchDstId);
        pBridge->pstReqCtx->unTargetMask = 0x1F;
        fprintf(stderr,"Target Mast is %04x\n", pBridge->pstReqCtx->unTargetMask);
        reqCtxStart(pBridge->pstReqCtx,
                    unCmd,
                    pBridge->pstReqCtx->unTargetMask,
                    pstBev,
                    pBridge->pstEvBase,
                    pBridge->iTimeoutMs);

        int iSentCount = udsClientBroadcastMask(pBridge->pstUdsTable,
                            pBridge->pstReqCtx->unTargetMask,
                            auchSendBuf,
                            iWriteLen);
        //TCP 클라이언트로 보낼 처리 결과 만들기 

        

        printf("[BRIDGE] Broadcast CMD=0x%04X Mask=0x%X, Sned Count is %d\n",
            unCmd, pBridge->pstReqCtx->unTargetMask, iSentCount);
    }
}





/* ========================================================================== */
/* UDS → TCP (Response Aggregation)                                           */
/* ========================================================================== */

void bridgeUdsReadCb(struct bufferevent* pstBev, void* pvData)
{
    SOCK_CONTEXT* pSockCtx = (SOCK_CONTEXT*)pvData;
    BRIDGE_CONTEXT* pBridge = (BRIDGE_CONTEXT*)pSockCtx->pvUserCtx;
    unsigned char auchRecvBuf[MAX_FRAME_BUF];
    unsigned char auchSendBuf[MAX_FRAME_BUF];
    MSG_ID stMsgId;
    unsigned short unCmd = 0;
    FRAME_ERR eErr;
    int iFrameSize, iCopyLen, iSendLen;
    bool bResult;
    struct evbuffer* pstInput = bufferevent_get_input(pstBev);
    fprintf(stderr,"UDS Server ID is %02X\n", pSockCtx->uchSrcId);
    unsigned char uchSrcId, uchDstId;
    while (1) {
        size_t tRecvSize = evbuffer_get_length(pstInput);
        if (tRecvSize < FRAME_HEADER_MIN_SIZE)
            break;
            
        if (tRecvSize > sizeof(auchRecvBuf))
            tRecvSize = sizeof(auchRecvBuf);
            
        iCopyLen = evbuffer_copyout(pstInput, auchRecvBuf, tRecvSize);
        iFrameSize = getFrameSize(auchRecvBuf);
        
        if (iFrameSize <= 0) {
            evbuffer_drain(pstInput, 1);
            continue;
        }
        if (iCopyLen < iFrameSize)
            break;
        evbuffer_drain(pstInput, iFrameSize);

        /* Extract metadata */
        stMsgId.uchSrcId = ((SOCK_CONTEXT*)pvData)->uchSrcId;
        eErr = responseFrame(auchRecvBuf, &stMsgId, iFrameSize);
        if (eErr != FRAME_OK)
            bResult = false;
        else
            bResult = true;
        
        uchSrcId = getSrcId(auchRecvBuf);
        uchDstId = getDstId(auchRecvBuf);
        fprintf(stderr,"### %s():%d Client ID %02X %02X###\n", __func__,__LINE__, uchSrcId, uchDstId);
        if(udsClientFindFreeSlot(pBridge->pstUdsTable, uchSrcId) == NEED_REGISTER){
            if(!udsClientRegister(pBridge->pstUdsTable, pSockCtx->pstBufferEvent, uchSrcId)){
                fprintf(stderr,"### %s():%d ###\n", __func__,__LINE__);                
            }
        }else{       
            reqCtxOnUdsResponse(pBridge->pstReqCtx, uchSrcId, bResult);
            fprintf(stderr,"### %s():%d ###\n", __func__,__LINE__);
            /* Check if all responses received */
            REQUEST_STATE eState = reqCtxEvaluateAndComplete(pBridge->pstReqCtx);
            if (eState == REQ_SUCCESS || eState == REQ_FAILED || eState == REQ_TIMEOUT)
            {
                /* Forward original response to TCP */
                iSendLen = makeResFrame(unCmd, &stMsgId, &auchRecvBuf[FRAME_HEADER_MIN_SIZE], auchSendBuf);
                bufferevent_write(pBridge->pstReqCtx->pstTcpBev, auchSendBuf, iSendLen);
                printf("[BRIDGE] Final Response → TCP, CMD=0x%04X State=%d\n", unCmd, eState);
            }
        }
    }
}


/* ========================================================================== */
/* Event Logging (Optional)                                                   */
/* ========================================================================== */

void bridgeTcpEventCb(struct bufferevent* pstBev, short events, void* ctx)
{
    if (events & BEV_EVENT_EOF)
        printf("[TCP] Disconnected\n");
}

void bridgeUdsEventCb(struct bufferevent* pstBev, short events, void* ctx)
{
    if (events & BEV_EVENT_EOF)
        printf("[UDS] Disconnected\n");
}
