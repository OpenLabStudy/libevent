#include "ipcUtil.h"
#include "frame.h"
#include "ioChannelUtil.h"
#include "netUds.h"


int ipcBuildMsgIdFromWorker(int iWorkerId, void* pvMsgId)
{
    MSG_ID* pstMsgId = (MSG_ID *)pvMsgId;
    if (!pvMsgId) 
        return -1;

    pstMsgId->uchSrcId = (unsigned char)iWorkerId;

    switch (iWorkerId) {
    case UDS_1_CLN1_ID:
    case UDS_1_CLN2_ID:
        pstMsgId->uchDstId = UDS_1_SVR_ID;
        break;

    case UDS_2_CLN1_ID:
    case UDS_2_CLN2_ID:
        pstMsgId->uchDstId = UDS_2_SVR_ID;
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
        
    if (makeResponseFrame(CMD_ID_INFO, &stMsgId, &stReg, auchSendBuf) != FRAME_OK)
        return;
        
    int iFrameSize = getFrameSizeWithCmd(CMD_ID_INFO, FRAME_TYPE_RESPONSE);
    evbuffer_add(pstIoChannel->pstWriteBuffer, auchSendBuf, iFrameSize);
    event_add(pstIoChannel->pstWriteEvent, NULL);
}
