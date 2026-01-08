#include "ipcUtil.h"

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