#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include "internal.h"

#include "ioChannelUtil.h"
#include "acuUtil.h"
#include "icdCommand.h"
#include "cmdRegistry.h"
#include "timeUtil.h"

static unsigned char parseAcuUartResponse(const char* pchBuf, int iLen)
{
    if (!pchBuf || iLen <= 0)
        return RESP_FAIL;

    if (iLen >= 1 && pchBuf[0] == 0x06)
        return RESP_OK;

    return RESP_FAIL;
}

void uartWriteCallback(int iFd, short nEvent, void *pvData)
{
    (void)iFd; (void)nEvent;

    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    EVENT_ENGINE* pstEngine = pstIoChannel->pstEventEngine;

    unsigned char auchUartWriteData[2048];
    int iTotalSize = evbuffer_get_length(pstIoChannel->pstWriteBuffer);
    if (iTotalSize == 0) {
        event_del(pstIoChannel->pstWriteEvent);
        return;
    }

    int iWriteSize = evbuffer_remove(pstIoChannel->pstWriteBuffer, auchUartWriteData, iTotalSize);
    if(iWriteSize > 0){
        // fprintf(stderr,"### TotalSize=%d WriteSize=%d [ReqID=%d] ###\n", iTotalSize, iWriteSize, pstEngine->uiRequestSeq);
        iWriteSize = write(pstIoChannel->iFd, auchUartWriteData, iWriteSize);
        (void)iWriteSize;
        (void)pstEngine;
        event_del(pstIoChannel->pstWriteEvent);
    }
}

void uartReadCallback(int iFd, short nEvent, void *pvData)
{
    (void)iFd; (void)nEvent;

    IO_CHANNEL* pstUartIo = (IO_CHANNEL*)pvData;
    IO_EVENT_TYPE eEventType = pstUartIo->ePendingLogicEvent;

    EVENT_ENGINE* pstEngine = pstUartIo->pstEventEngine;
    ACU_CTRL_CTX* pstCtx = (ACU_CTRL_CTX*)pstEngine->pvSharedData;

    char acUartBuf[2048];
    char auchSendData[UDS_MAX_BUFFER_SIZE];
    MSG_ID stMsgId;
    int iSendSize;

    switch (eEventType)
    {
    case IO_EVT_RX_DATA: {
        pstUartIo->chFdCloseSet = FD_OPENED;

        int iLen = evbuffer_remove(pstUartIo->pstReadBuffer, acUartBuf, sizeof(acUartBuf));
        if (iLen <= 0) break;

        unsigned char uchResult = parseAcuUartResponse(acUartBuf, iLen);

        char achCmdResult[128];
        memset(achCmdResult, 0, sizeof(achCmdResult));

        switch (pstCtx->unCmd) {

        case CMD_POSITIONER_AZ_EL_SET:
        {
            RES_POSITIONER_AZ_EL_SET* pstRes = (RES_POSITIONER_AZ_EL_SET *)achCmdResult;
            stMsgId.uchSrcId = AC_RCV_CMD_FROM_TC;
            stMsgId.uchDstId = TC_SND_CMD_TO_CLN;
            pstRes->chResult = (uchResult == RESP_OK)?0x01:0x00;

            createCmdResponse(CMD_POSITIONER_AZ_EL_SET, achCmdResult, &stMsgId, auchSendData, 0x01);
            iSendSize = getFrameSizeWithCmd(CMD_POSITIONER_AZ_EL_SET, FRAME_TYPE_RESPONSE);

            eventEngineHandleWorkerResponse(pstUartIo->pstEventEngine, pstUartIo,
                        pstEngine->uiRequestSeq, auchSendData, iSendSize);
            break;
        }

        case CMD_ACU_MODE_SELECT:
        {
            RES_ACU_MODE* pstRes = (RES_ACU_MODE *)achCmdResult;
            stMsgId.uchSrcId = AC_RCV_CMD_FROM_TC;
            stMsgId.uchDstId = TC_SND_CMD_TO_CLN;
            pstRes->chResult = (uchResult == RESP_OK)?0x01:0x00;

            createCmdResponse(CMD_ACU_MODE_SELECT, achCmdResult, &stMsgId, auchSendData, 0x01);
            iSendSize = getFrameSizeWithCmd(CMD_ACU_MODE_SELECT, FRAME_TYPE_RESPONSE);

            eventEngineHandleWorkerResponse(pstUartIo->pstEventEngine, pstUartIo,
                        pstEngine->uiRequestSeq, auchSendData, iSendSize);
            break;
        }

        case CMD_POSITIONER_AZ_EL:
        {
            SEND_CURR_AZ_EL* pstSend = (SEND_CURR_AZ_EL *)achCmdResult;
            pstSend->iTime = timePackHMSms();

            char *chSplitData[8];
            stMsgId.uchSrcId = AC_SND_AZ_EL_TO_TC;
            stMsgId.uchDstId = TC_RCV_AZ_EL_FROM_AC;

            int iSplitCnt = splitAcuDataString(acUartBuf, ';', chSplitData, 2);
            if(iSplitCnt == 2){
                pstSend->iAz = (int)(atof(chSplitData[0]) * 1000.0);
                pstSend->iEl = (int)(atof(chSplitData[1]) * 1000.0);
            }

            IO_CHANNEL* pstUdsIo = ioFindChannelByWorkerId(pstUartIo->pstEventEngine, AC_SND_AZ_EL_TO_TC);
            if (ioIsChannelAlive(pstUdsIo)) {
                iSendSize = getFrameSizeWithCmd(CMD_POSITIONER_AZ_EL, FRAME_TYPE_REQUEST);
                if(createCmdRequest(CMD_POSITIONER_AZ_EL, &stMsgId, achCmdResult, auchSendData) == FRAME_OK){
                    eventEngineHandleWorkerResponse(pstUartIo->pstEventEngine, pstUartIo,
                                            pstEngine->uiRequestSeq, auchSendData, iSendSize);
                }
            }
        }
        break;

        default:
            break;
        }
    }

    pstCtx->eAcuState = ACU_STATE_IDLE;
    break;

    case IO_EVT_CHANNEL_CLOSED:
    case IO_EVT_ERROR:
        pstUartIo->chFdCloseSet = FD_CLOSED;
        fprintf(stderr, "[ACU] UART channel closed fd=%d\n", pstUartIo->iFd);
        event_active(pstUartIo->pstShutdownEvent, 0, 0);
        break;

    default:
        break;
    }

    pstUartIo->ePendingLogicEvent = IO_EVENT_NONE;
}
