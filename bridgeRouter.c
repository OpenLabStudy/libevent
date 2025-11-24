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


static BRIDGE_CONTEXT* gpstBridgeCtx = NULL;


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
    gpstBridgeCtx             = pstCtx;
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
        stMsgId.uchSrcId = gpstBridgeCtx->uchTcpSrcId;
        stMsgId.uchDstId = 0xFF; // ignored (mask controls UDS)

        eErr = requestFrame(auchRecvBuf, &stMsgId, iFrameSize, &unCmd);
        if (eErr != FRAME_OK)
        {
            printf("[TCP] requestFrame ERR: %s\n", frameErrToStr(eErr));
            continue;
        }

        /* === 명령 처리 === */
        eErr = commandHandler(auchRecvBuf, &stMsgId, iFrameSize, auchPayloadBuf, &iSendLen);
        if (eErr != FRAME_OK || iSendLen <= 0)
            continue;

        //todo UDS를 통해 전송될 데이터가 있으면
        int iWriteLen = makeReqFrame(unCmd, &stMsgId, auchPayloadBuf, auchSendBuf);

        reqCtxStart(gpstBridgeCtx->pstReqCtx,
                    unCmd,
                    gpstBridgeCtx->pstReqCtx->unTargetMask,
                    pstBev,
                    gpstBridgeCtx->pstEvBase,
                    gpstBridgeCtx->iTimeoutMs);

        udsClientBroadcastMask(gpstBridgeCtx->pstUdsTable,
                            gpstBridgeCtx->pstReqCtx->unTargetMask,
                            auchSendBuf,
                            iWriteLen);
        //TCP 클라이언트로 보낼 처리 결과 만들기 

        

        printf("[BRIDGE] Broadcast CMD=0x%04X Mask=0x%X\n",
            unCmd, gpstBridgeCtx->pstReqCtx->unTargetMask);
    }
}


/* ========================================================================== */
/* UDS → TCP (Response Aggregation)                                           */
/* ========================================================================== */

void bridgeUdsReadCb(struct bufferevent* pstBev, void* pvData)
{
    unsigned char auchRecvBuf[MAX_FRAME_BUF];
    unsigned char auchSendBuf[MAX_FRAME_BUF];

    MSG_ID stMsgId;
    unsigned short unCmd = 0;
    FRAME_ERR eErr;
    int iFrameSize, iCopyLen, iSendLen;

    struct evbuffer* pstInput = bufferevent_get_input(pstBev);

    while (1)
    {
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

        /* Extract metadata */
        stMsgId.uchSrcId = ((SOCK_CONTEXT*)pvData)->uchSrcId;
        stMsgId.uchDstId = gpstBridgeCtx->uchTcpSrcId;

        eErr = requestFrame(auchRecvBuf, &stMsgId, iFrameSize, &unCmd);
        if (eErr != FRAME_OK)
            continue;

        bool bResult = true;
        reqCtxOnUdsResponse(gpstBridgeCtx->pstReqCtx, stMsgId.uchSrcId, bResult);

        /* Check if all responses received */
        REQUEST_STATE eState = reqCtxEvaluateAndComplete(gpstBridgeCtx->pstReqCtx);

        if (eState == REQ_SUCCESS || eState == REQ_FAILED || eState == REQ_TIMEOUT)
        {
            /* Forward original response to TCP */
            iSendLen = makeResFrame(unCmd, &stMsgId, &auchRecvBuf[FRAME_HEADER_MIN_SIZE], auchSendBuf);
            bufferevent_write(gpstBridgeCtx->pstReqCtx->pstTcpBev, auchSendBuf, iSendLen);
            printf("[BRIDGE] Final Response → TCP, CMD=0x%04X State=%d\n", unCmd, eState);
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
