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

static void tcpRecvKeyboard(int iFd, short nEvent, void* pvData)
{
    (void)iFd;
    (void)nEvent;
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    IO_EVENT_TYPE eEventType = pstIoChannel->ePendingLogicEvent;

    unsigned char auchRecvBuffer[64];
    unsigned char auchCmdData[32];
    char achUdsReqBuffer[64];
    char achTcpRespResult[4];
    char achTcpRespBuffer[64];
    RES_KEYBOARD_AZ_EL* pstResKeyboardAzEl = (RES_KEYBOARD_AZ_EL*)achTcpRespResult;
    unsigned short unCmd = 0;
    FRAME_ERR eErr;
    int iSendSize;
    pstResKeyboardAzEl->chResult = 0x00;
    switch (eEventType) {
    case IO_EVT_RX_DATA:{
        unsigned int uiTotalRcvSize = evbuffer_get_length(pstIoChannel->pstReadBuffer);
        /* 최소 헤더도 안 왔으면 중단 */
        if (uiTotalRcvSize < (int)sizeof(FRAME_HEADER))
            break;
        memset(auchRecvBuffer, 0x00, sizeof(auchRecvBuffer));
        int iCopyLen = evbuffer_copyout(pstIoChannel->pstReadBuffer, auchRecvBuffer, uiTotalRcvSize);
        eErr = frameDecode(auchRecvBuffer, iCopyLen, FRAME_TYPE_REQUEST, &unCmd);
        if (eErr != FRAME_OK) {
            fprintf(stderr, "[KEYBOARD-RECEIVER] frameDecode ERR: %s\n", frameErrToStr(eErr));
            int iDeleteDataSize = findFrameHeader(auchRecvBuffer, iCopyLen);
            evbuffer_drain(pstIoChannel->pstReadBuffer, iDeleteDataSize);
            iCopyLen-=iDeleteDataSize;
        }
        int iFrameSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_REQUEST);
        if (iCopyLen < iFrameSize)
            break;

        for(int iIndex=1; iIndex<=iFrameSize; iIndex++){
            if(iIndex % 16 == 0)
                fprintf(stderr,"\n");
            fprintf(stderr,"%02x ", auchRecvBuffer[iIndex]);
        }
        fprintf(stderr,"\n");            
        eErr = cmdDispatch(auchRecvBuffer, iCopyLen, auchCmdData);
        if(eErr != FRAME_OK){
            fprintf(stderr, "[KEYBOARD-RECEIVER] frameDecode ERR: %s\n", frameErrToStr(eErr));
            break;
        }
        MSG_ID stMsgId = { KEYBOARD_SND_TO_SF, SF_RCV_SENSOR_DATA };        
        if(createCmdRequest(unCmd, &stMsgId, auchCmdData, achUdsReqBuffer) == FRAME_OK){
            IO_CHANNEL* pstKeyboardSndUdsIo = ioFindChannelByWorkerId(pstIoChannel->pstEventEngine, KEYBOARD_SND_TO_SF);
            if (ioIsChannelAlive(pstKeyboardSndUdsIo)) {
                iSendSize = getFrameSizeWithCmd(CMD_KEYBOARD_AZ_EL, FRAME_TYPE_REQUEST);
                evbuffer_add(pstKeyboardSndUdsIo->pstWriteBuffer, achUdsReqBuffer, iSendSize);
                event_add(pstKeyboardSndUdsIo->pstWriteEvent, NULL);
                pstResKeyboardAzEl->chResult = 0x01;
            }
        }
        stMsgId.uchSrcId = TCP_SVR_ID;
        stMsgId.uchDstId = TCP_CLN_ID;
        iSendSize = getFrameSizeWithCmd(CMD_KEYBOARD_AZ_EL, FRAME_TYPE_RESPONSE);
        createCmdResponse(CMD_KEYBOARD_AZ_EL, achTcpRespResult, &stMsgId, achTcpRespBuffer);
        evbuffer_add(pstIoChannel->pstWriteBuffer, achTcpRespBuffer, iSendSize);
        event_add(pstIoChannel->pstWriteEvent, NULL);
        break;
    }
    case IO_EVT_CHANNEL_CLOSED:
    case IO_EVT_ERROR:
        printf("[KEYBOARD-RECEIVER] channel closed fd=%d\n", pstIoChannel->iFd);
        event_active(pstIoChannel->pstShutdownEvent, 0, 0);
        break;

    default:
        break;
    }
    pstIoChannel->ePendingLogicEvent = IO_EVENT_NONE;
}


// static void applyCommand(unsigned short unCmd, char *pchCmdData, char *pchCmdResult)
// {
//     (void)pchCmdData;
//     switch (unCmd)
//     {
//     case CMD_ID_INFO:
//         ((RES_ID*)pchCmdResult)->chResult = (char)KEYBOARD_SND_TO_SF;
//         break; 
//     default:
//         fprintf(stderr, "[KEYBOARD] Unsupported CMD\n");
//         break;
//     }
// }

// static void ioChannelHandleEvent(int iFd, short nEvent, void* pvData)
// {
//     (void)iFd;
//     (void)nEvent;
//     IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
//     IO_EVENT_TYPE eEventType = pstIoChannel->ePendingLogicEvent;
//     FRAME_ERR eErr;
//     unsigned short unCmd = 0;
//     char achRecvBuffer[UDS_MAX_BUFFER_SIZE];
//     switch (eEventType) {
//     case IO_EVT_CHANNEL_CLOSED:
//     case IO_EVT_ERROR:
//         ioMarkChannelDead(pstIoChannel, pstIoChannel->ePendingLogicEvent);
//         break;
//     case IO_EVT_RX_DATA:
//     {
//         int iRecvLen = evbuffer_get_length(pstIoChannel->pstReadBuffer);
//         fprintf(stderr,"### %s():%d Recv Size is %d ###\n", __func__, __LINE__, iRecvLen);
//         if (iRecvLen < (int)sizeof(FRAME_HEADER))
//             break;

//         memset(achRecvBuffer, 0x00, sizeof(achRecvBuffer));
//         int iCopyLen = evbuffer_copyout(pstIoChannel->pstReadBuffer, achRecvBuffer, iRecvLen);
//         eErr = frameDecode(achRecvBuffer, iCopyLen, FRAME_TYPE_REQUEST, &unCmd);
//         if (eErr != FRAME_OK) {
//             fprintf(stderr, "[KEYBOARD_SND_TO_SF] frameDecode ERR: %s\n", frameErrToStr(eErr));
//             int iDeleteDataSize = findFrameHeader(achRecvBuffer, iCopyLen);
//             evbuffer_drain(pstIoChannel->pstReadBuffer, iDeleteDataSize);
//         }
//         int iFrameSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_REQUEST);
//         /* === 프레임 소비 === */
//         char achCmdData[128];
//         char achCmdResult[128];
//         char achResult[128];
//         memset(achCmdData, 0x0, sizeof(achCmdData));
//         memset(achCmdResult, 0x0, sizeof(achCmdResult));
//         memset(achResult, 0x0, sizeof(achResult));
//         unsigned int uiReqId;
//         int iResultSize;
//         evbuffer_drain(pstIoChannel->pstReadBuffer, iFrameSize);
//         evbuffer_remove(pstIoChannel->pstReadBuffer, &uiReqId, sizeof(unsigned int));
//         eErr = cmdDispatch(achRecvBuffer, iCopyLen, achCmdData);
//         if (eErr != FRAME_OK){
//             fprintf(stderr,"### %s():%d %s ###\n",__func__,__LINE__, frameErrToStr(eErr));
//         }            
//         applyCommand(unCmd, achCmdData, achCmdResult);
//         MSG_ID stMsgId = { KEYBOARD_SND_TO_SF, SF_RCV_SENSOR_DATA };
//         eErr = createCmdResponse(unCmd, achCmdResult, &stMsgId, achResult);
//         iResultSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_RESPONSE);
//         // sendUdsResponse(pstIoChannel, unCmd, uiReqId, auchResult, iResultSize);
//         evbuffer_add(pstIoChannel->pstWriteBuffer, achResult, iResultSize);
//         event_add(pstIoChannel->pstWriteEvent, NULL);
//     }

//     default:
//         break;
//     }
//     pstIoChannel->ePendingLogicEvent = IO_EVENT_NONE;
// }

/* ============================================================
* main()
* ============================================================ */
int run()
{
    EVENT_ENGINE   stEventEngine;
    TCP_SERVER_RUNTIME_CFG stTcpKeyRcvSvrRuntimeCfg = {
        .unPort         = KEYBOARD_RCV_PORT,        
        .chWorkerId     = (char)TC_RCV_AZ_EL_FROM_CTRL_PC,
        .chDstWorkerId  = (char)CTRL_PC,
        .eRole          = ROLE_REQUESTER,
        .eType          = TYPE_TCP_SVR,
        .pfWrite        = NULL,
        .pfIoHandler    = tcpRecvKeyboard,
        .pchTag         = "TC_RCV_AZ_EL_FROM_CTRL_PC"
    };
    UDS_CLIENT_RUNTIME_CFG stUdsClnRuntimeCfg = {
        .chWorkerId     = (char)KEYBOARD_SND_TO_SF,
        .chDstWorkerId  = (char)SF_RCV_SENSOR_DATA,
        .pchUdsPath     = UDS_2_PATH,
        .eRole          = ROLE_REQUESTER,
        .eType          = TYPE_UDS_CLI,
        .pchTag         = "KEYBOARD_SND_TO_SF",
    };

    stEventEngine.pstEventBase = event_base_new();
    if (!stEventEngine.pstEventBase) {
        fprintf(stderr,"event_base_new failed\n");
        return -1;
    }
    eventEngineInit(&stEventEngine, 0);
    TCP_SERVER_RUNTIME *pstTcpRcvKeySvr = tcpServerRuntimeCreate(&stEventEngine, &stTcpKeyRcvSvrRuntimeCfg, NULL); 
    UDS_CLIENT_RUNTIME *pstUdsClnRuntime = udsClientRuntimeCreate(&stEventEngine, &stUdsClnRuntimeCfg, NULL, NULL);
    APP_SIGNAL_HANDLE *pstSigHandle = appSignalCreate(&stEventEngine, "KEYBOARD_SND_TO_SF");
    fprintf(stderr,"[KEYBOARD-RECEIVER] Listening on port %d\n", KEYBOARD_RCV_PORT);

    /* 이벤트 루프 시작 */
    event_base_dispatch(stEventEngine.pstEventBase);

    udsClientRuntimeDestroy(&pstUdsClnRuntime);
    tcpServerRuntimeDestroy(&pstTcpRcvKeySvr);
    appSignalDestroy(&pstSigHandle);
    
    /* 종료 처리 */
    eventEngineCleanup(&stEventEngine);
    event_base_free(stEventEngine.pstEventBase);

    fprintf(stderr,"[KEYBOARD-RECEIVER] Terminated.\n");
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