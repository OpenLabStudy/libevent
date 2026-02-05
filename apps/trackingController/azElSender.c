#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>

#include "eventEngine.h"
#include "netTcp.h"
#include "netUds.h"
#include "cmdRegistry.h"
#include "netCore.h"
#include "icdCommand.h"
#include "runtime.h"
#include "udsClientRuntime.h"
#include "tcpServerRuntime.h"

// static void sendCurrAzElToCtrlPc(int iFd, short nEvent, void* pvData)
// {
//     (void)iFd;
//     (void)nEvent;
//     IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
//     unsigned char auchWriteBuffer[2048];
//     int iWriteSize;
//     iWriteSize = evbuffer_get_length(pstIoChannel->pstWriteBuffer);
//     if (iWriteSize == 0) {
//         event_del(pstIoChannel->pstWriteEvent);
//         return;
//     }
//     iWriteSize = evbuffer_remove(pstIoChannel->pstWriteBuffer, auchWriteBuffer, iWriteSize);
//     MSG_ID stMsgId = { TCP_SVR_ID, TCP_CLN_ID };
//     repackageResponse(auchWriteBuffer, &stMsgId, iWriteSize);
//     iWriteSize = write(pstIoChannel->iFd, auchWriteBuffer, iWriteSize);
//     if (iWriteSize <= 0) {
//         perror("write");
//         return;
//     }    
//     if (evbuffer_get_length(pstIoChannel->pstWriteBuffer) == 0)
//         event_del(pstIoChannel->pstWriteEvent);
// }

static void recvDataFromAC(int iFd, short nEvent, void* pvData)
{
    (void)iFd;
    (void)nEvent;
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    EVENT_ENGINE* pstEventEngine = pstIoChannel->pstEventEngine;
    IO_EVENT_TYPE eEventType = pstIoChannel->ePendingLogicEvent;
    char achRecvBuffer[UDS_MAX_BUFFER_SIZE];
    unsigned short unCmd = 0;
    FRAME_ERR eErr;
    
    switch (eEventType) {
    case IO_EVT_CHANNEL_CLOSED:
    case IO_EVT_ERROR:
        ioMarkChannelDead(pstIoChannel, pstIoChannel->ePendingLogicEvent);
        event_active(pstIoChannel->pstShutdownEvent, 0, 0);
        break;
    case IO_EVT_RX_DATA:
        while (1) {
            int iRecvLen = evbuffer_get_length(pstIoChannel->pstReadBuffer);
            fprintf(stderr,"SF_RCV_CMD_FROM_TC %s():%d Recv Size is %d ###\n", __func__, __LINE__, iRecvLen);
            if (iRecvLen < (int)sizeof(FRAME_HEADER))
                break;

            memset(achRecvBuffer, 0x00, sizeof(achRecvBuffer));
            int iCopyLen = evbuffer_copyout(pstIoChannel->pstReadBuffer, achRecvBuffer, iRecvLen);            
            eErr = frameDecode(achRecvBuffer, iCopyLen, FRAME_TYPE_RESPONSE, &unCmd);
            if (eErr != FRAME_OK) {
                fprintf(stderr, "[SF_RCV_CMD_FROM_TC] frameDecode ERR: %s\n", frameErrToStr(eErr));
                int iDeleteDataSize = findFrameHeader(achRecvBuffer, iCopyLen);
                evbuffer_drain(pstIoChannel->pstReadBuffer, iDeleteDataSize);
                continue;
            }
            int iFrameSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_RESPONSE);

            /* === 프레임 소비 === */
            char achCmdData[128];
            char achResult[128];
            unsigned int uiReqId;
            int iResultSize;
            memset(achCmdData, 0x0, sizeof(achCmdData));
            memset(achResult, 0x0, sizeof(achResult));
            evbuffer_drain(pstIoChannel->pstReadBuffer, iFrameSize);
            evbuffer_remove(pstIoChannel->pstReadBuffer, &uiReqId, sizeof(unsigned int));
            fprintf(stderr,"### %s():%d Request Id is %d ###\n",__func__,__LINE__, uiReqId);
            if(unCmd == CMD_ID_INFO){
                fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
                ((RES_ID*)achCmdData)->chId = (char)TC_RCV_AZ_EL_FROM_AC;
                fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
                MSG_ID stMsgId = { TC_RCV_AZ_EL_FROM_AC, AC_SND_AZ_EL_TO_TC };
                fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
                eErr = createCmdResponse(unCmd, achCmdData, &stMsgId, achResult);
                fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
                iResultSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_RESPONSE);
                fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
                evbuffer_add(pstIoChannel->pstWriteBuffer, achResult, iResultSize);
                fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
                event_add(pstIoChannel->pstWriteEvent, NULL);
            }else if(unCmd == CMD_POSITIONER_AZ_EL){
                eErr = cmdDispatch(achRecvBuffer, iCopyLen, achCmdData);
                if(eErr != FRAME_OK){
                    fprintf(stderr, "[KEYBOARD-RECEIVER] frameDecode ERR: %s\n", frameErrToStr(eErr));
                    break;
                }                
                MSG_ID stMsgId = { TCP_SVR_ID, TCP_CLN_ID };
                createCmdResponse(CMD_KEYBOARD_AZ_EL, achCmdData, &stMsgId, achResult);
                iResultSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_RESPONSE);
                evbuffer_add(pstIoChannel->pstWriteBuffer, achResult, iResultSize);
                event_add(pstIoChannel->pstWriteEvent, NULL);
            }else{
                fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
            }            
        }
        break;
    default:
        /* TX-only: ignore */
        break;
    }
    pstIoChannel->ePendingLogicEvent = IO_EVENT_NONE;
}



/* ============================================================
* main()
* ============================================================ */
int run()
{
    EVENT_ENGINE   stEventEngine;
    TCP_SERVER_RUNTIME_CFG stTcpSndAzElSvrRuntimeCfg = {
        .unPort         = AZ_EL_SND_PORT,
        .chWorkerId     = (char)TC_SND_AZ_EL_TO_CTRL_PC,
        .chDstWorkerId  = (char)CTRL_PC,
        .eRole          = ROLE_REQUESTER,
        .eType          = TYPE_TCP_SVR,
        .pfWrite        = NULL,
        .pfIoHandler    = NULL,
        .pchTag         = "TC_SND_AZ_EL_TO_CTRL_PC"
    };
    UDS_CLIENT_RUNTIME_CFG stUdsClnRuntimeCfg = {
        .chWorkerId     = (char)TC_RCV_AZ_EL_FROM_AC,
        .chDstWorkerId  = (char)AC_SND_AZ_EL_TO_TC,
        .pchUdsPath     = UDS_4_PATH,
        .eRole          = ROLE_REQUESTER,
        .eType          = TYPE_UDS_CLI,
        .pchTag         = "TC_RCV_AZ_EL_FROM_AC",
    };

    stEventEngine.pstEventBase = event_base_new();
    if (!stEventEngine.pstEventBase) {
        fprintf(stderr,"event_base_new failed\n");
        return -1;
    }
    eventEngineInit(&stEventEngine, 0);
    TCP_SERVER_RUNTIME *pstTcpSndAzElSvr = tcpServerRuntimeCreate(&stEventEngine, &stTcpSndAzElSvrRuntimeCfg, NULL); 
    UDS_CLIENT_RUNTIME *pstUdsClnRuntime = udsClientRuntimeCreate(&stEventEngine, &stUdsClnRuntimeCfg, 
        recvDataFromAC, NULL);
    APP_SIGNAL_HANDLE *pstSigHandle = appSignalCreate(&stEventEngine, "SEND_CURRENT_AZ_EL_TO_CTRL_PC");
    fprintf(stderr,"[SEND_CURRENT_AZ_EL_TO_CTRL_PC] Listening on port %d\n", KEYBOARD_RCV_PORT);

    /* 이벤트 루프 시작 */
    event_base_dispatch(stEventEngine.pstEventBase);

    udsClientRuntimeDestroy(&pstUdsClnRuntime);
    tcpServerRuntimeDestroy(&pstTcpSndAzElSvr);
    appSignalDestroy(&pstSigHandle);
    
    /* 종료 처리 */
    eventEngineCleanup(&stEventEngine);
    event_base_free(stEventEngine.pstEventBase);

    fprintf(stderr,"[SEND_CURRENT_AZ_EL_TO_CTRL_PC] Terminated.\n");
    return 0;
}

/* === main === */
#ifndef GOOGLE_TEST
int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    return run();
}
#endif