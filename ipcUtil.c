#include "ipcUtil.h"
#include "icdCommand.h"
#include <string.h>


int ipcBuildMsgIdFromWorker(int iWorkerId, void* pvMsgId)
{
    MSG_ID* pstMsgId = (MSG_ID *)pvMsgId;
    if (!pvMsgId) 
        return -1;

    pstMsgId->uchSrcId = (unsigned char)iWorkerId;

    switch (iWorkerId) {
    case UDS_1_SENSOR_FUSION:
    case UDS_1_ACU_CONTROLLER:
        pstMsgId->uchDstId = UDS_1_SVR_ID;
        break;

    case UDS_2_GPS_RECEIVER:
    case UDS_2_IMU_RECEIVER:
    case UDS_2_SP_RECEIVER:
    case UDS_2_EXTERN_RECEIVER:
    case UDS_2_KEYBOARD_RECEIVER:
        pstMsgId->uchDstId = UDS_2_SVR_ID;
        break;

    case UDS_3_SENSOR_FUSION:
        pstMsgId->uchDstId = UDS_3_SVR_ID;
        break;

    case UDS_4_KEYBOARD_RECEIVER:
        pstMsgId->uchDstId = UDS_4_SVR_ID;
        break;

    default:
        return -1;
    }

    return 0;
}

void ipcSendWorkerRegister(IO_CHANNEL* pstIoChannel, unsigned char uchWorkerType)
{
    if (!ioIsChannelAlive(pstIoChannel))
        return;
        
    unsigned char auchSendBuf[128];
    RES_ID stReg = { .chResult = uchWorkerType };    
    MSG_ID stMsgId;
    if (ipcBuildMsgIdFromWorker(pstIoChannel->iWorkerId, &stMsgId) < 0)
        return;
        
    if (makeResponseFrame(CMD_ID_INFO, &stMsgId, (unsigned char *)&stReg, auchSendBuf) != FRAME_OK)
        return;
        
    int iFrameSize = getFrameSizeWithCmd(CMD_ID_INFO, FRAME_TYPE_RESPONSE);
    fprintf(stderr, "[IPC] send worker register: type=%d, frame size=%d\n", uchWorkerType, iFrameSize);
    evbuffer_add(pstIoChannel->pstWriteBuffer, auchSendBuf, iFrameSize);
}

int ipcHandleCommand(unsigned short unCmd, const unsigned char* pReqPayload, IPC_CMD_CTX* pstCmdCtx)
{
    if (!pstCmdCtx)
        return -1;

    memset(pstCmdCtx, 0, sizeof(*pstCmdCtx));
    pstCmdCtx->unCmd = unCmd;
    fprintf(stderr, "### %s():%d CMD=0x%04X ###\n", __func__, __LINE__, unCmd);

    switch (unCmd) {
    case CMD_IBIT:
    case CMD_RBIT:
    case CMD_CBIT:{
        const REQ_BIT* pstReqBit = (const REQ_BIT*)pReqPayload;
        pstCmdCtx->u.stBit.chBit = pstReqBit->chBit;
        pstCmdCtx->eResult = ACU_CMD_OK;
        return 0;
    }
    case CMD_TRACKING_SELECT: {
        const REQ_TRACKING_SELECT* pstReqTrackingSelect = (const REQ_TRACKING_SELECT*)pReqPayload;
        pstCmdCtx->u.stTrackingSelect.chTrackingSelect = pstReqTrackingSelect->chTrackingSelect;
        pstCmdCtx->eResult = ACU_CMD_OK;
        return 0;
    }
    case CMD_TRACKING_CONTROL:{
        const REQ_TRACKING_CONTROL* pstReqTrackingCtrl = (const REQ_TRACKING_CONTROL*)pReqPayload;
        pstCmdCtx->u.stTrackingControl.chTrackingStartStop = pstReqTrackingCtrl->chStartStop;
        pstCmdCtx->eResult = ACU_CMD_OK;
        return 0;
    }
    case CMD_POSITIONER_AZ_EL_SET: {
        fprintf(stderr, "### %s():%d ACU MODE=%d ###\n", __func__, __LINE__, pstCmdCtx->u.stAcuMode.chAcuMode);
        fprintf(stderr,"iAzOffset:%d, iElOffset:%d\n", pstCmdCtx->u.stAzElOffsetSet.iAzOffset, pstCmdCtx->u.stAzElOffsetSet.iElOffset);
        const REQ_POSITIONER_AZ_EL_SET* pstReqPositionerAzElSet = (const REQ_POSITIONER_AZ_EL_SET*)pReqPayload;
        pstCmdCtx->u.stPositionerAzElSet.dAz = endianChange(pstReqPositionerAzElSet->chAzimuthDeg);
        pstCmdCtx->u.stPositionerAzElSet.dEl = endianChange(pstReqPositionerAzElSet->chElevationDeg);          
        pstCmdCtx->eResult = ACU_CMD_OK;
        return 0;
    }
    case CMD_POSITIONER_DEG_SEND: {
        const REQ_POSITIONER_DEG_SEND* pstReqPositionerDegSend = (const REQ_POSITIONER_DEG_SEND*)pReqPayload;
        pstCmdCtx->u.stPositionerAzElSendCtrl.chSendOnOff = pstReqPositionerDegSend->chSendOnOff;
        pstCmdCtx->eResult = ACU_CMD_OK;
        return 0;
    }
    case CMD_ACU_MODE_SELECT: {
        const REQ_ACU_MODE* pstReqAcuMode = (const REQ_ACU_MODE*)pReqPayload;
        pstCmdCtx->u.stAcuMode.chAcuMode = pstReqAcuMode->chAcuMode;
        fprintf(stderr, "### %s():%d ACU MODE=%d ###\n", __func__, __LINE__, pstCmdCtx->u.stAcuMode.chAcuMode);
        pstCmdCtx->eResult = ACU_CMD_OK;
        return 0;
    }
    case CMD_AZ_EL_OFFSET_SET: {
        const REQ_AZ_EL_OFFSET_SET* pstReqAzElOffsetSet = (const REQ_AZ_EL_OFFSET_SET*)pReqPayload;
        pstCmdCtx->u.stAzElOffsetSet.iAzOffset = ntohl(pstReqAzElOffsetSet->iAzOffset);
        pstCmdCtx->u.stAzElOffsetSet.iElOffset = ntohl(pstReqAzElOffsetSet->iElOffset);
        fprintf(stderr,"iAzOffset:%d, iElOffset:%d\n", pstReqAzElOffsetSet->iAzOffset, pstReqAzElOffsetSet->iElOffset);
        fprintf(stderr,"iAzOffset:%d, iElOffset:%d\n", pstCmdCtx->u.stAzElOffsetSet.iAzOffset, pstCmdCtx->u.stAzElOffsetSet.iElOffset);
        pstCmdCtx->eResult = ACU_CMD_OK;
        return 0;
    }
    case CMD_ID_INFO: {
        pstCmdCtx->eResult = ACU_CMD_OK;
        return 0;
    }
    default:
        pstCmdCtx->eResult = ACU_CMD_INVALID;
        return -1;
    }
}

/* ========================================================================== */
/* Helper: send UDS response (frame + reqId)                                   */
/* ========================================================================== */
void sendUdsResponse(IO_CHANNEL* pstUdsIo, unsigned short unCmd, unsigned int uiReqId,
                               const unsigned char* pPayload, unsigned int uiPayloadMax)
{
    if (!pstUdsIo)
        return;

    unsigned char aucSendBuf[UDS_MAX_BUFFER_SIZE];
    unsigned char aucPayload[UDS_MAX_BUFFER_SIZE];

    memset(aucSendBuf, 0, sizeof(aucSendBuf));
    memset(aucPayload, 0, sizeof(aucPayload));

    if (pPayload && uiPayloadMax > 0) {
        /* payload는 각 RES_* 구조체 크기만큼만 실제로 의미 있음 */
        memcpy(aucPayload, pPayload, (uiPayloadMax > sizeof(aucPayload)) ? sizeof(aucPayload) : uiPayloadMax);
    }

    // MSG_ID stMsgId;
    // ipcBuildMsgIdFromWorker(pstUdsIo->iWorkerId, &stMsgId);
    // makeResponseFrame(unCmd, &stMsgId, aucPayload, aucSendBuf);
    // unsigned int uiFrameSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_RESPONSE);
    unsigned int uiFrameSize = getDataSize(unCmd, FRAME_TYPE_RESPONSE);

    /* append reqId */
    // memcpy(aucSendBuf + uiFrameSize, &uiReqId, sizeof(unsigned int));

    /* queue */
    evbuffer_add(pstUdsIo->pstWriteBuffer, &unCmd, sizeof(unsigned short));
    evbuffer_add(pstUdsIo->pstWriteBuffer, pPayload, uiFrameSize);
    evbuffer_add(pstUdsIo->pstWriteBuffer, &uiReqId, sizeof(unsigned int));
    fprintf(stderr, "[IPC] send UDS response CMD=0x%04X, REQ ID=%u, frame size=%u\n",
            unCmd, uiReqId, uiFrameSize + (unsigned int)sizeof(unsigned int));
    event_add(pstUdsIo->pstWriteEvent, NULL);
}
