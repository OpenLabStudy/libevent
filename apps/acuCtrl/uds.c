#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <event2/buffer.h>

#include "internal.h"
#include "ioChannelUtil.h"
#include "acuUtil.h"
#include "icdCommand.h"
#include "cmdRegistry.h"
#include "timeUtil.h"

/* ============================================================================
 * UDS stream consumption policy (UNIFIED)
 *
 * - 모든 UDS readBuffer/writeBuffer는 "프레임 단위"로만 소비한다.
 * - frameDecode 실패 시: findFrameHeader 기반으로 sync, 단 0이면 1바이트 drain(스핀 방지)
 * - 부분 프레임이면: 기다린다(NEED_MORE)
 * - UDS1/UDS3는 [FRAME][reqId(4)] 형태로 들어온다는 기존 규칙을 그대로 유지
 * - UDS4(writeBuffer) 쪽은 [FRAME]만 소비(기존 규칙 유지)
 * ========================================================================== */

typedef enum {
    CONSUME_NEED_MORE = 0,
    CONSUME_OK        = 1,
    CONSUME_SYNCED    = -1
} CONSUME_RC;

/* "copyout" 상한. (너무 크게 copyout 하지 않음) */
static inline size_t clampCopyLen(size_t avail, size_t maxLen)
{
    return (avail > maxLen) ? maxLen : avail;
}

/* frameDecode 실패 시, 헤더 재동기화 */
static CONSUME_RC resyncOnDecodeFail(struct evbuffer* in, const char* tmp, size_t tmpLen)
{
    int del = findFrameHeader(tmp, (int)tmpLen);
    if (del <= 0) del = 1; /* 헤더가 전혀 없으면 1바이트씩 흘려보내며 동기화 */
    evbuffer_drain(in, (size_t)del);
    return CONSUME_SYNCED;
}

/* [FRAME][reqId(4)] 형태: 프레임 1개가 "완성"인지 확인하고, 완성이면 outCmd/outFrameSize 채움 */
static CONSUME_RC peekOneFrameWithReqId(struct evbuffer* in,
                                       FRAME_TYPE eFrameType,
                                       unsigned short* outCmd,
                                       int* outFrameSize)
{
    size_t avail = evbuffer_get_length(in);
    if (avail < sizeof(FRAME_HEADER))
        return CONSUME_NEED_MORE;

    /* decode를 위해 일부만 copyout (너무 큰 copyout 방지) */
    char tmp[UDS_MAX_BUFFER_SIZE];
    size_t copyLen = clampCopyLen(avail, sizeof(tmp));
    int got = evbuffer_copyout(in, tmp, (int)copyLen);
    if (got < (int)sizeof(FRAME_HEADER))
        return CONSUME_NEED_MORE;

    unsigned short cmd = 0;
    FRAME_ERR err = frameDecode(tmp, got, eFrameType, &cmd);
    if (err != FRAME_OK) {
        // fprintf(stderr, "[UDS] frameDecode ERR: %s\n", frameErrToStr(err));
        return resyncOnDecodeFail(in, tmp, (size_t)got);
    }

    int frameSize = getFrameSizeWithCmd(cmd, eFrameType);
    size_t need = (size_t)frameSize + sizeof(unsigned int); /* +reqId */
    if (avail < need)
        return CONSUME_NEED_MORE;

    *outCmd = cmd;
    *outFrameSize = frameSize;
    return CONSUME_OK;
}

/* [FRAME] 형태: UDS4(writeBuffer)에서 프레임 1개 완성 확인 */
static CONSUME_RC peekOneFrameOnly(struct evbuffer* in,
                                  FRAME_TYPE eFrameType,
                                  unsigned short* outCmd,
                                  int* outFrameSize)
{
    size_t avail = evbuffer_get_length(in);
    if (avail < sizeof(FRAME_HEADER))
        return CONSUME_NEED_MORE;

    char tmp[2048]; /* UDS4에 쌓이는 프레임은 보통 작음. 필요 시 키워도 됨 */
    size_t copyLen = clampCopyLen(avail, sizeof(tmp));
    int got = evbuffer_copyout(in, tmp, (int)copyLen);
    if (got < (int)sizeof(FRAME_HEADER))
        return CONSUME_NEED_MORE;

    unsigned short cmd = 0;
    FRAME_ERR err = frameDecode(tmp, got, eFrameType, &cmd);
    if (err != FRAME_OK) {
        // fprintf(stderr, "[UDS4] frameDecode ERR: %s\n", frameErrToStr(err));
        return resyncOnDecodeFail(in, tmp, (size_t)got);
    }

    int frameSize = getFrameSizeWithCmd(cmd, eFrameType);
    if (avail < (size_t)frameSize)
        return CONSUME_NEED_MORE;

    *outCmd = cmd;
    *outFrameSize = frameSize;
    return CONSUME_OK;
}

/* [FRAME][reqId] 소비 */
static void consumeFrameAndReqId(struct evbuffer* in, int frameSize, unsigned int* outReqId)
{
    evbuffer_drain(in, (size_t)frameSize);
    if (outReqId) {
        evbuffer_remove(in, outReqId, sizeof(unsigned int));
    } else {
        evbuffer_drain(in, sizeof(unsigned int));
    }
}

/* ============================================================================
 * 100ms Polling Timer Callback (원본 유지)
 * ========================================================================== */
void acuAzElPollingCb(int iFd, short nEvent, void *pvData)
{
    (void)iFd; (void)nEvent;

    EVENT_ENGINE *pstEventEngine = (EVENT_ENGINE *)pvData;
    ACU_CTRL_CTX* pstAcuCtrlCtx = (ACU_CTRL_CTX*)pstEventEngine->pvSharedData;

    char achCmdData[128];
    char achResult[128];
    unsigned int uiUartDataSize;

    IO_CHANNEL* pstIoChannel = ioFindChannelByWorkerId(pstEventEngine, AC_SND_AZ_EL_TO_TC);
    if(pstIoChannel == NULL) return;

    COMMAND_PATH eCommandPath = ACU_CTRL_UART;

    pstAcuCtrlCtx->unCmd = CMD_POSITIONER_AZ_EL;
    createAcuUartData(pstAcuCtrlCtx, CMD_POSITIONER_AZ_EL, achCmdData, achResult, &uiUartDataSize);

    if(pstAcuCtrlCtx->uiLocalReqId == pstEventEngine->uiRequestSeq){
        pstAcuCtrlCtx->uiLocalReqId++;
    }
    pstEventEngine->uiRequestSeq = pstAcuCtrlCtx->uiLocalReqId++;

    evbuffer_add(pstIoChannel->pstRequestBuffer, &eCommandPath, sizeof(eCommandPath));
    evbuffer_add(pstIoChannel->pstRequestBuffer, achResult, uiUartDataSize);
    event_active(pstIoChannel->pstRequestEvent, 0, 0);
}

/* ============================================================================
 * UDS1: commandEventCb (TC 명령 수신) - frame 소비 통일 적용
 * ========================================================================== */
void commandEventCb(int iFd, short nEvent, void *pvData)
{
    (void)iFd; (void)nEvent;

    IO_CHANNEL *pstIoChannel = (IO_CHANNEL *)pvData;
    EVENT_ENGINE* pstEventEngine = pstIoChannel->pstEventEngine;
    IO_EVENT_TYPE eEventType = pstIoChannel->ePendingLogicEvent;

    ACU_CTRL_CTX* pstAcuCtrlCtx = (ACU_CTRL_CTX*)pstEventEngine->pvSharedData;

    switch (eEventType)
    {
    case IO_EVT_CHANNEL_CLOSED:
    case IO_EVT_ERROR:
        ioMarkChannelDead(pstIoChannel, pstIoChannel->ePendingLogicEvent);
        event_active(pstIoChannel->pstShutdownEvent, 0, 0);
        break;

    case IO_EVT_RX_DATA:
        while (1)
        {
            unsigned short unCmd = 0;
            int frameSize = 0;

            CONSUME_RC rc = peekOneFrameWithReqId(pstIoChannel->pstReadBuffer,
                                                 FRAME_TYPE_REQUEST,
                                                 &unCmd, &frameSize);
            if (rc == CONSUME_NEED_MORE) break;
            if (rc == CONSUME_SYNCED)    continue; /* sync 후 다시 시도 */

            /* 여기부터는 "프레임 1개 + reqId 4바이트"가 보장 */
            pstAcuCtrlCtx->unCmd = unCmd;

            unsigned int uiReqId = 0;
            char achFrame[UDS_MAX_BUFFER_SIZE];
            memset(achFrame, 0, sizeof(achFrame));

            /* 프레임은 frameSize 만큼만 copyout 해서 처리(불필요 copy 방지) */
            evbuffer_copyout(pstIoChannel->pstReadBuffer, achFrame, frameSize);
            consumeFrameAndReqId(pstIoChannel->pstReadBuffer, frameSize, &uiReqId);

            pstEventEngine->uiRequestSeq = uiReqId;

            char achCmdData[128];
            char achResult[128];
            unsigned int uiUartDataSize = 0;

            memset(achCmdData, 0, sizeof(achCmdData));
            memset(achResult, 0, sizeof(achResult));

            FRAME_ERR eErr = cmdDispatch(achFrame, frameSize, achCmdData);
            if (eErr != FRAME_OK) {
                fprintf(stderr,"[ACU][UDS1] cmdDispatch ERR: %s\n", frameErrToStr(eErr));
                continue;
            }

            COMMAND_PATH eCommandPath = decideProcessingPath(unCmd);
            createAcuUartData(pstAcuCtrlCtx, unCmd, achCmdData, achResult, &uiUartDataSize);

            MSG_ID stMsgId = { AC_RCV_CMD_FROM_TC, TC_SND_CMD_TO_CLN };
            int iResultSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_RESPONSE);

            if (eCommandPath == COMMAND_PATH_NONE) {
                createCmdResponse(unCmd, achCmdData, &stMsgId, achResult, 0x01);
                evbuffer_add(pstIoChannel->pstWriteBuffer, achResult, iResultSize);
                event_add(pstIoChannel->pstWriteEvent, NULL);
            }
            else if (eCommandPath == ACU_CTRL_UART) {
                /* 원본 동작 유지: ack=0x00 response를 writeBuffer에 쌓고,
                   UART requestBuffer에 commandPath + payload */
                createCmdResponse(unCmd, achCmdData, &stMsgId, achResult, 0x00);
                evbuffer_add(pstIoChannel->pstWriteBuffer, achResult, iResultSize);

                evbuffer_add(pstIoChannel->pstRequestBuffer, &eCommandPath, sizeof(eCommandPath));
                evbuffer_add(pstIoChannel->pstRequestBuffer, achResult, uiUartDataSize);
                event_active(pstIoChannel->pstRequestEvent, 0, 0);
            }
            else {
                fprintf(stderr,"[ACU][UDS1] Unsupported path=%d cmd=%04X\n", eCommandPath, unCmd);
            }
        }
        break;

    default:
        break;
    }

    pstIoChannel->ePendingLogicEvent = IO_EVENT_NONE;
}

/* ============================================================================
 * writeNone (원본 유지)
 * ========================================================================== */
void writeNone(int iFd, short nEvent, void* pvData)
{
    (void)iFd; (void)nEvent;
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;

    int iRecvLen = evbuffer_get_length(pstIoChannel->pstWriteBuffer);
    evbuffer_drain(pstIoChannel->pstWriteBuffer, iRecvLen);
}

/* ============================================================================
 * UDS3: recvAzElFromSensorFusion - frame 소비 통일 적용
 * ========================================================================== */
void recvAzElFromSensorFusion(int iFd, short nEvent, void* pvData)
{
    (void)iFd; (void)nEvent;

    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    EVENT_ENGINE* pstEventEngine = pstIoChannel->pstEventEngine;
    IO_EVENT_TYPE eEventType = pstIoChannel->ePendingLogicEvent;

    ACU_CTRL_CTX* pstAcuCtrlCtx = (ACU_CTRL_CTX*)pstEventEngine->pvSharedData;

    switch (eEventType) {

    case IO_EVT_RX_DATA:
        while (1) {
            unsigned short unCmd = 0;
            int frameSize = 0;

            CONSUME_RC rc = peekOneFrameWithReqId(pstIoChannel->pstReadBuffer,
                                                 FRAME_TYPE_RESPONSE,
                                                 &unCmd, &frameSize);
            if (rc == CONSUME_NEED_MORE) break;
            if (rc == CONSUME_SYNCED)    continue;

            /* 프레임+reqId 소비 */
            char achFrame[UDS_MAX_BUFFER_SIZE];
            memset(achFrame, 0, sizeof(achFrame));

            evbuffer_copyout(pstIoChannel->pstReadBuffer, achFrame, frameSize);
            consumeFrameAndReqId(pstIoChannel->pstReadBuffer, frameSize, NULL); /* reqId는 여기서 사용 안 함 */

            if(unCmd == CMD_ID_INFO){
                fprintf(stderr,"[ACU][UDS3] ID=%d[%02X] worker=%d\n",
                        (int)getIdInfo(achFrame + sizeof(FRAME_HEADER)),
                        pstIoChannel->chWorkerId,
                        pstIoChannel->chWorkerId);
                continue;
            }

            /* 기존 로직 유지 */
            RES_AZ_EL_DATA* pstAzElData = (RES_AZ_EL_DATA *)(achFrame + sizeof(FRAME_HEADER));

            char achSendAcuCtrlData[128];
            unsigned int uiUartDataSize = 0;

            memset(achSendAcuCtrlData, 0, sizeof(achSendAcuCtrlData));
            COMMAND_PATH eCommandPath = ACU_CTRL_UART;

            if(pstAcuCtrlCtx->uiLocalReqId == pstEventEngine->uiRequestSeq){
                pstAcuCtrlCtx->uiLocalReqId++;
            }
            pstEventEngine->uiRequestSeq = pstAcuCtrlCtx->uiLocalReqId++;

            if(pstAcuCtrlCtx->stCommandState.chAcuMode == POSITION)
                uiUartDataSize = moveAzElPosition(pstAzElData->dAz, pstAzElData->dEl, achSendAcuCtrlData);
            else if(pstAcuCtrlCtx->stCommandState.chAcuMode == RATE)
                uiUartDataSize = moveAzElRate(pstAzElData->dAz, pstAzElData->dEl, achSendAcuCtrlData);
            else{
                fprintf(stderr,"[ACU][UDS3] ACU MODE is not Position/Rate\n");
                break;
            }

            evbuffer_add(pstIoChannel->pstRequestBuffer, &eCommandPath, sizeof(eCommandPath));
            evbuffer_add(pstIoChannel->pstRequestBuffer, achSendAcuCtrlData, uiUartDataSize);
            event_active(pstIoChannel->pstRequestEvent, 0, 0);
        }
        break;

    case IO_EVT_CHANNEL_CLOSED:
    case IO_EVT_ERROR:
        printf("[ACU][UDS3] channel closed fd=%d\n", pstIoChannel->iFd);
        event_active(pstIoChannel->pstShutdownEvent, 0, 0);
        break;

    default:
        break;
    }

    pstIoChannel->ePendingLogicEvent = IO_EVENT_NONE;
}

/* ============================================================================
 * UDS4: sendCurrAzElToTC - frame 소비 통일 적용 (reqId 없음, writeBuffer 사용)
 * ========================================================================== */
void sendCurrAzElToTC(int iFd, short nEvent, void* pvData)
{
    (void)iFd; (void)nEvent;

    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;

    while (1) {
        unsigned short unCmd = 0;
        int frameSize = 0;

        CONSUME_RC rc = peekOneFrameOnly(pstIoChannel->pstWriteBuffer,
                                         FRAME_TYPE_REQUEST,
                                         &unCmd, &frameSize);
        if (rc == CONSUME_NEED_MORE) return;
        if (rc == CONSUME_SYNCED)    continue;

        /* 프레임 1개만 copyout 후 소비 */
        unsigned char frameBuf[2048];
        unsigned char cmdData[32];
        memset(frameBuf, 0, sizeof(frameBuf));
        memset(cmdData, 0, sizeof(cmdData));

        evbuffer_copyout(pstIoChannel->pstWriteBuffer, frameBuf, frameSize);
        evbuffer_drain(pstIoChannel->pstWriteBuffer, (size_t)frameSize);

        if(unCmd != CMD_POSITIONER_AZ_EL){
            fprintf(stderr,"[ACU][UDS4] Unsupported cmd=%04X\n", unCmd);
            continue;
        }

        FRAME_ERR eErr = cmdDispatch((char*)frameBuf, frameSize, (char*)cmdData);
        if(eErr != FRAME_OK){
            fprintf(stderr, "[ACU][UDS4] cmdDispatch ERR: %s\n", frameErrToStr(eErr));
            continue;
        }

        MSG_ID stMsgId = { AC_SND_AZ_EL_TO_TC, TC_RCV_AZ_EL_FROM_AC };

        unsigned char outBuf[2048];
        memset(outBuf, 0, sizeof(outBuf));

        if(createCmdRequest(unCmd, &stMsgId, (char*)cmdData, (char*)outBuf) == FRAME_OK) {
            int writeSize = getFrameSizeWithCmd(CMD_POSITIONER_AZ_EL, FRAME_TYPE_REQUEST);
            int wr = write(pstIoChannel->iFd, outBuf, writeSize);
            if (wr <= 0) {
                perror("[ACU][UDS4] write");
                return;
            }

            if (evbuffer_get_length(pstIoChannel->pstWriteBuffer) == 0) {
                event_del(pstIoChannel->pstWriteEvent);
            }
        } else {
            fprintf(stderr,"[ACU][UDS4] createCmdRequest failed cmd=%04X\n", unCmd);
        }
    }
}
