#include "core.h"
#include <string.h>
#include <stdio.h>

/* 기존 코드의 결과 코드 유지 */
#define RESP_OK        0x01
#define RESP_FAIL      0x00

static inline void actionsReset(ACU_ACTION_LIST* pstOutAcuActionList)
{
    if (!pstOutAcuActionList)
        return;

    memset(pstOutAcuActionList, 0, sizeof(*pstOutAcuActionList));
}

static inline void actionsPush(ACU_ACTION_LIST* pstOutAcuActionList, const ACU_ACTION* pstAcuAction)
{
    if (!pstOutAcuActionList || !pstAcuAction)
        return;

    if (pstOutAcuActionList->iCount >= (int)(sizeof(pstOutAcuActionList->stAcuAction)/sizeof(pstOutAcuActionList->stAcuAction[0])))
        return;

    pstOutAcuActionList->stAcuAction[pstOutAcuActionList->iCount++] = *pstAcuAction;
}

/* ========================================================================== */
/* 기존 acuCtrl.c 로직을 core로 이관                                           */
/* ========================================================================== */
static COMMAND_PATH decideProcessingPath(unsigned short unCmd)
{
    switch(unCmd)
    {
        case CMD_ID_INFO:
            return COMMAND_PATH_NONE;
        case CMD_POSITIONER_AZ_EL_SET:
        case CMD_ACU_MODE_SELECT:
        case CMD_POSITIONER_AZ_EL:
            return ACU_CTRL_UART;
        default:
            return COMMAND_PATH_FAIL;
    }
}

static void createAcuUartData(ACU_CTRL_CTX* pstAcuCtrlCtx, unsigned short unCmd,
                              const void* pvCmdData,
                              char* pchOutData, unsigned int* puiOutLen)
{
    if (!pstAcuCtrlCtx || !pchOutData || !puiOutLen) 
        return;

    *puiOutLen = 0;
    switch(unCmd)
    {
        case CMD_POSITIONER_AZ_EL:
            *puiOutLen = (unsigned int)readAzElFromAcu(pchOutData);
            break;

        case CMD_POSITIONER_AZ_EL_SET:
        {
            const REQ_POSITIONER_AZ_EL_SET* pstReqPositionAzElSet = (const REQ_POSITIONER_AZ_EL_SET*)pvCmdData;
            double dAz = endianChange(pstReqPositionAzElSet->chAzimuthDeg);
            double dEl = endianChange(pstReqPositionAzElSet->chElevationDeg);

            fprintf(stderr, "%s(): ACU AZ/EL Set to AZ: %.2f, EL: %.2f\n", __func__, dAz, dEl);

            if (pstAcuCtrlCtx->stCommandState.chAcuMode == POSITION) {
                *puiOutLen = (unsigned int)moveAzElPosition(dAz, dEl, pchOutData);
            } else if (pstAcuCtrlCtx->stCommandState.chAcuMode == RATE) {
                *puiOutLen = (unsigned int)moveAzElRate(dAz, dEl, pchOutData);
            } else {
                /* mode 미설정이면 payload 0 */
                *puiOutLen = 0;
            }
            break;
        }

        case CMD_ACU_MODE_SELECT:
        {
            const REQ_ACU_MODE* pstReqAcuMode = (const REQ_ACU_MODE*)pvCmdData;
            fprintf(stderr, "ACU Mode Change to %s\n",
                    (pstReqAcuMode->chAcuMode == POSITION) ? "POSITION MODE" : "RATE MODE");
            pstAcuCtrlCtx->stCommandState.chAcuMode = pstReqAcuMode->chAcuMode;
            *puiOutLen = (unsigned int)modeChange(pstReqAcuMode->chAcuMode, pchOutData);
            break;
        }

        default:
            break;
    }
}

/* TODO: 실제 ACU UART 프로토콜에 맞게 강화 필요 */
static unsigned char parseAcuUartResponse(const unsigned char* puchBuffer, unsigned int uiLen)
{
    if (!puchBuffer || uiLen == 0) 
        return RESP_FAIL;

    if (puchBuffer[0] == 0x06) 
        return RESP_OK;

    return RESP_FAIL;
}

/* ========================================================================== */
/* Public API                                                                  */
/* ========================================================================== */

void acuCoreInit(ACU_CTRL_CTX* pstAcuCtrlCtx)
{
    if (!pstAcuCtrlCtx)
        return;

    pstAcuCtrlCtx->eState                       = ACU_STATE_IDLE;
    pstAcuCtrlCtx->stPending.bInUse             = 0;
    pstAcuCtrlCtx->stPending.unCmd              = CMD_UNKNOWN;
    pstAcuCtrlCtx->stPending.uiReqId            = 0;
    pstAcuCtrlCtx->stPending.pstUdsIo           = NULL; /* core에서는 사용하지 않지만, 기존 필드 유지 */

    pstAcuCtrlCtx->iIsUartAlive                 = 1;
    pstAcuCtrlCtx->iIsSendCommand               = 0;
    memset(&pstAcuCtrlCtx->stLastAzElRxTime, 0, sizeof(pstAcuCtrlCtx->stLastAzElRxTime));

    pstAcuCtrlCtx->stCommandState.chAcuMode     = ACU_MODE_NONE;
    pstAcuCtrlCtx->stCommandState.chSendOnOff   = AZ_EL_SEND_OFF;
    pstAcuCtrlCtx->stCommandState.dAzOffset     = 0.0;
    pstAcuCtrlCtx->stCommandState.dElOffset     = 0.0;
}

/* UDS 요청을 처리해서:
 * - 즉시 응답할지 (ID_INFO)
 * - UART로 보낼지 (AZEL_SET, MODE_SELECT, AZEL polling)
 * 를 action으로 반환
 */
void acuCoreOnUdsCommand(ACU_CTRL_CTX* pstAcuCtrlCtx, unsigned short unCmd,
                          unsigned int uiReqId, const void* pvCmdData, unsigned int uiCmdDataLen,
                          ACU_ACTION_LIST* pstOutAcuActionList)
{
    (void)uiCmdDataLen;
    actionsReset(pstOutAcuActionList);
    
    if (!pstAcuCtrlCtx)
        return;

    COMMAND_PATH eCmdPath = decideProcessingPath(unCmd);
    if (eCmdPath == COMMAND_PATH_FAIL) {
        ACU_ACTION stAcuAction = {
            .eAcuActionType = ACU_ACT_PROTOCOL_ERROR, 
            .iProtoErrCode = -1
        };
        actionsPush(pstOutAcuActionList, &stAcuAction);
        return;
    }

    /* 공통 MsgId (기존과 동일) */
    MSG_ID stMsgId = { AC_RCV_CMD_FROM_TC, TC_SND_CMD_TO_CLN };

    /* COMMAND_PATH_NONE: 즉시 응답 (ID_INFO 같은 케이스) */
    if (eCmdPath == COMMAND_PATH_NONE) {
        unsigned char uchFrame[2048] = {0};

        /* cmdData를 그대로 response payload에 넣는 형태(기존 코드의 RES_ID 처리 방식과 호환) */
        /* 여기서는 createCmdResponse가 payload를 어디서 가져오는지(achCmdData vs cmdData) 차이가 있으니
         * 기존 패턴 유지: cmdDispatch 결과(cmdData 버퍼)가 들어온다고 가정
         */
        if (createCmdResponse(unCmd, (char*)pvCmdData, &stMsgId, (char*)uchFrame, 0x01) == FRAME_OK) {
            ACU_ACTION stAcuAction = {.eAcuActionType = ACU_ACT_UDS_WRITE_CURRENT};
            memcpy(stAcuAction.uchFrameBuffer, uchFrame, getFrameSizeWithCmd(unCmd, FRAME_TYPE_RESPONSE));
            stAcuAction.uiFrameLen = (unsigned int)getFrameSizeWithCmd(unCmd, FRAME_TYPE_RESPONSE);
            actionsPush(pstOutAcuActionList, &stAcuAction);
        } else {
            ACU_ACTION stAcuAction = {.eAcuActionType = ACU_ACT_PROTOCOL_ERROR, .iProtoErrCode = -2};
            actionsPush(pstOutAcuActionList, &stAcuAction);
        }
        return;
    }

    /* UART로 보내는 케이스: 일단 “수신/처리 시작” 응답을 한 번 보내고(기존 0x00),
     * UART request를 올린다.
     */
    {
        /* (1) 즉시 응답 프레임(accepted) */
        unsigned char uchAckFrame[2048] = {0};
        int iRespSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_RESPONSE);

        if (createCmdResponse(unCmd, (char*)pvCmdData, &stMsgId, (char*)uchAckFrame, 0x00) == FRAME_OK) {
            ACU_ACTION stAcuAction = {.eAcuActionType = ACU_ACT_UDS_WRITE_CURRENT};
            memcpy(stAcuAction.uchFrameBuffer, uchAckFrame, (unsigned int)iRespSize);
            stAcuAction.uiFrameLen = (unsigned int)iRespSize;
            actionsPush(pstOutAcuActionList, &stAcuAction);
        }

        /* (2) UART payload 생성 */
        char chUartPayload[2048] = {0};
        unsigned int uiUartLen = 0;

        /* CMD_POSITIONER_AZ_EL_SET / CMD_ACU_MODE_SELECT / CMD_POSITIONER_AZ_EL */
        createAcuUartData(pstAcuCtrlCtx, unCmd, pvCmdData, chUartPayload, &uiUartLen);

        /* pending 설정 (UART 응답 매칭) */
        pstAcuCtrlCtx->eState = ACU_STATE_WAIT_RESPONSE;
        pstAcuCtrlCtx->stPending.bInUse = 1;
        pstAcuCtrlCtx->stPending.unCmd  = unCmd;
        pstAcuCtrlCtx->stPending.uiReqId = uiReqId;

        /* (3) UART request action */
        ACU_ACTION stAcuAction = {.eAcuActionType = ACU_ACT_UART_REQUEST};
        stAcuAction.eCmdPath = ACU_CTRL_UART;
        memcpy(stAcuAction.uchUartData, chUartPayload, uiUartLen);
        stAcuAction.uiUartDataLen = uiUartLen;
        actionsPush(pstOutAcuActionList, &stAcuAction);
    }
}

/* UART 응답을 받았을 때:
 * - pending cmd 기반으로 최종 응답 프레임을 생성하여
 *   eventEngineHandleWorkerResponse로 라우팅하는 action 반환(기존과 동일)
 * - CMD_POSITIONER_AZ_EL인 경우, AZ/EL을 파싱해서 TC로 보내는 프레임도 생성(기존 로직 유지)
 */
void acuCoreOnUartRx(ACU_CTRL_CTX* pstAcuCtrlCtx,
                      const unsigned char* uchUartBytes, unsigned int uiUartLen,
                      ACU_ACTION_LIST* pstOutAcuActionList)
{
    actionsReset(pstOutAcuActionList);
    if (!pstAcuCtrlCtx || !uchUartBytes || uiUartLen == 0)
        return;

    unsigned char uchResult = parseAcuUartResponse(uchUartBytes, uiUartLen);

    /* pending 없는 UART 응답이면: 무시 또는 alive 체크만 */
    if (!pstAcuCtrlCtx->stPending.bInUse) {
        pstAcuCtrlCtx->eState = ACU_STATE_IDLE;
        return;
    }

    unsigned short unCmd = pstAcuCtrlCtx->stPending.unCmd;
    unsigned int uiReqId = pstAcuCtrlCtx->stPending.uiReqId;

    /* 기존과 동일: ACU가 끝나면 IDLE */
    pstAcuCtrlCtx->stPending.bInUse = 0;
    pstAcuCtrlCtx->eState = ACU_STATE_IDLE;

    /* 최종 응답 프레임 생성 */
    unsigned char uchFrame[2048] = {0};
    MSG_ID stMsgId = { AC_RCV_CMD_FROM_TC, TC_SND_CMD_TO_CLN };
    char chCmdResult[256] = {0};
    if (unCmd == CMD_POSITIONER_AZ_EL_SET) {
        RES_POSITIONER_AZ_EL_SET* pstResPositionAzElSet = (RES_POSITIONER_AZ_EL_SET*)chCmdResult;
        pstResPositionAzElSet->chResult = (uchResult == RESP_OK) ? 0x01 : 0x00;

        if (createCmdResponse(unCmd, chCmdResult, &stMsgId, (char*)uchFrame, 0x01) == FRAME_OK) {
            ACU_ACTION stAcuAction = {.eAcuActionType = ACU_ACT_ENGINE_WORKER_RESPONSE, .uiReqSeq = uiReqId};
            stAcuAction.uiFrameLen = (unsigned int)getFrameSizeWithCmd(unCmd, FRAME_TYPE_RESPONSE);
            memcpy(stAcuAction.uchFrameBuffer, uchFrame, stAcuAction.uiFrameLen);
            actionsPush(pstOutAcuActionList, &stAcuAction);
        }
        return;
    }

    if (unCmd == CMD_ACU_MODE_SELECT) {
        RES_ACU_MODE* pstResAcuMode = (RES_ACU_MODE*)chCmdResult;
        pstResAcuMode->chResult = (uchResult == RESP_OK) ? 0x01 : 0x00;
        if (createCmdResponse(unCmd, chCmdResult, &stMsgId, (char*)uchFrame, 0x01) == FRAME_OK) {
            ACU_ACTION stAcuAction = {.eAcuActionType = ACU_ACT_ENGINE_WORKER_RESPONSE, .uiReqSeq = uiReqId};
            stAcuAction.uiFrameLen = (unsigned int)getFrameSizeWithCmd(unCmd, FRAME_TYPE_RESPONSE);
            memcpy(stAcuAction.uchFrameBuffer, uchFrame, stAcuAction.uiFrameLen);
            actionsPush(pstOutAcuActionList, &stAcuAction);
        }
        return;
    }

    if (unCmd == CMD_POSITIONER_AZ_EL) {
        /* uartBytes가 "az;el" 문자열로 온다는 기존 전제 유지 */
        SEND_CURR_AZ_EL* pstSendCurrAzEl = (SEND_CURR_AZ_EL*)chCmdResult;
        pstSendCurrAzEl->iTime = timePackHMSms();
        char* pchSplit[8] = {0};
        int iSplitCnt = splitAcuDataString((const char*)uchUartBytes, ';', pchSplit, 2);
        if (iSplitCnt == 2) {
            pstSendCurrAzEl->iAz = (int)(atof(pchSplit[0]) * 1000.0);
            pstSendCurrAzEl->iEl = (int)(atof(pchSplit[1]) * 1000.0);
        }

        MSG_ID stMsgId = { AC_SND_AZ_EL_TO_TC, TC_RCV_AZ_EL_FROM_AC };
        unsigned char uchReqFrame[2048] = {0};
        if (createCmdRequest(unCmd, &stMsgId, chCmdResult, (char*)uchReqFrame) == FRAME_OK) {
            /* 기존은 eventEngineHandleWorkerResponse로 보냈으니 동일하게 라우팅 */
            ACU_ACTION stAcuAction = {.eAcuActionType = ACU_ACT_ENGINE_WORKER_RESPONSE, .uiReqSeq = uiReqId};
            stAcuAction.uiFrameLen = (unsigned int)getFrameSizeWithCmd(unCmd, FRAME_TYPE_REQUEST);
            memcpy(stAcuAction.uchFrameBuffer, uchReqFrame, stAcuAction.uiFrameLen);
            actionsPush(pstOutAcuActionList, &stAcuAction);
        }
        return;
    }
}
