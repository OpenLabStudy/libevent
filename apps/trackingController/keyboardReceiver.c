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

static void sendKeyboardResponse(int iFd, short nEvent, void* pvData)
{
    (void)iFd;
    (void)nEvent;
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;    
    unsigned char auchWriteBuffer[2048];
    RES_KEYBOARD_AZ_EL stResKeyboardAzEl;
    stResKeyboardAzEl.chResult = 0x01;
    int iWriteSize;
    MSG_ID stMsgId = { TCP_SVR_ID, TCP_CLN_ID };
    createCmdResponse(CMD_KEYBOARD_AZ_EL, &stResKeyboardAzEl, &stMsgId, auchWriteBuffer);
    iWriteSize = getFrameSizeWithCmd(CMD_KEYBOARD_AZ_EL, FRAME_TYPE_RESPONSE);
    iWriteSize = write(pstIoChannel->iFd, auchWriteBuffer, iWriteSize);
    if (iWriteSize <= 0) {
        perror("write");
        return;
    }
}

static void recvKeyboard(int iFd, short nEvent, void* pvData)
{
    (void)iFd;
    (void)nEvent;
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    EVENT_ENGINE* pstEventEngine = pstIoChannel->pstEventEngine;
    IO_EVENT_TYPE eEventType = pstIoChannel->ePendingLogicEvent;

    char achRecvBuffer[2048];
    char achTcpSendBuffer[128];
    char achTcpRespBuffer[64];
    unsigned short unCmd = 0;
    FRAME_ERR eErr;

    switch (eEventType) {
    case IO_EVT_RX_DATA:
        while (1) {
            unsigned int uiRecvLen = evbuffer_get_length(pstIoChannel->pstReadBuffer);
            /* 최소 헤더도 안 왔으면 중단 */
            if (uiRecvLen < (int)sizeof(FRAME_HEADER))
                break;
            memset(achRecvBuffer, 0x00, sizeof(achRecvBuffer));
            int iCopyLen = evbuffer_copyout(pstIoChannel->pstReadBuffer, achRecvBuffer, uiRecvLen);
            eErr = frameDecode(achRecvBuffer, iCopyLen, FRAME_TYPE_REQUEST, &unCmd);
            if (eErr != FRAME_OK) {
                fprintf(stderr, "[KEYBOARD-RECEIVER] frameDecode ERR: %s\n", frameErrToStr(eErr));
                int iDeleteDataSize = findFrameHeader(achRecvBuffer, iCopyLen);
                evbuffer_drain(pstIoChannel->pstReadBuffer, iDeleteDataSize);
                continue;
            }
            /* === CMD 먼저 추출 (가벼운 파싱) === */
            int iFrameSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_REQUEST);
            if (iCopyLen < iFrameSize)
                break;
            for(int iIndex=1; iIndex<=iFrameSize; iIndex++){
                if(iIndex % 16 == 0)
                    fprintf(stderr,"\n");
                fprintf(stderr,"%02x ", achRecvBuffer[iIndex]);
            }
            fprintf(stderr,"\n");
            /* === 프레임 하나 소비 === */
            evbuffer_drain(pstIoChannel->pstReadBuffer, iFrameSize);
            unsigned char auchKeyboardData[128];
            eErr = cmdDispatch(achRecvBuffer, iCopyLen, auchKeyboardData);
            switch(unCmd){
                case CDM_KEYBOARD_DATA: {
                    IO_CHANNEL* pstKeyboardIo = ioFindChannelByWorkerId(pstIoChannel->pstEventEngine, KEYBOARD_SND_TO_SF);
                    if (ioIsChannelAlive(pstKeyboardIo)) {
                        unsigned char auchSendBuf[UDS_MAX_BUFFER_SIZE];
                        MSG_ID stMsgId = { KEYBOARD_SND_TO_SF, SF_RCV_SENSOR_DATA };
                        createCmdResponse(CDM_KEYBOARD_DATA, auchKeyboardData, &stMsgId, auchSendBuf);
                        int iResultSize = getFrameSizeWithCmd(CDM_KEYBOARD_DATA, FRAME_TYPE_RESPONSE);
                        evbuffer_add(pstKeyboardIo->pstWriteBuffer, auchSendBuf, iResultSize);
                        event_add(pstKeyboardIo->pstWriteEvent, NULL);                    
                    }
                    //TCP 운용PC로부터 응답 생성
                    event_add(pstIoChannel->pstWriteEvent, NULL);
                    break;                
                }
                default:
                    fprintf(stderr,"Receiver Keyboard Data Error\n");
                    break;
            }
        }
        break;

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


static void applyCommand(unsigned short unCmd, char *pchCmdData, char *pchCmdResult)
{
    (void)pchCmdData;
    switch (unCmd)
    {
    case CMD_ID_INFO:
        ((RES_ID*)pchCmdResult)->chResult = (char)KEYBOARD_SND_TO_SF;
        break; 
    default:
        fprintf(stderr, "[KEYBOARD] Unsupported CMD\n");
        break;
    }
}

static void ioChannelHandleEvent(int iFd, short nEvent, void* pvData)
{
    (void)iFd;
    (void)nEvent;
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    IO_EVENT_TYPE eEventType = pstIoChannel->ePendingLogicEvent;
    FRAME_ERR eErr;
    unsigned short unCmd = 0;
    char achRecvBuffer[UDS_MAX_BUFFER_SIZE];
    switch (eEventType) {
    case IO_EVT_CHANNEL_CLOSED:
    case IO_EVT_ERROR:
        ioMarkChannelDead(pstIoChannel, pstIoChannel->ePendingLogicEvent);
        break;
    case IO_EVT_RX_DATA:
    {
        int iRecvLen = evbuffer_get_length(pstIoChannel->pstReadBuffer);
        fprintf(stderr,"### %s():%d Recv Size is %d ###\n", __func__, __LINE__, iRecvLen);
        if (iRecvLen < (int)sizeof(FRAME_HEADER))
            break;

        memset(achRecvBuffer, 0x00, sizeof(achRecvBuffer));
        int iCopyLen = evbuffer_copyout(pstIoChannel->pstReadBuffer, achRecvBuffer, iRecvLen);
        eErr = frameDecode(achRecvBuffer, iCopyLen, FRAME_TYPE_REQUEST, &unCmd);
        if (eErr != FRAME_OK) {
            fprintf(stderr, "[KEYBOARD_SND_TO_SF] frameDecode ERR: %s\n", frameErrToStr(eErr));
            int iDeleteDataSize = findFrameHeader(achRecvBuffer, iCopyLen);
            evbuffer_drain(pstIoChannel->pstReadBuffer, iDeleteDataSize);
        }
        int iFrameSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_REQUEST);
        /* === 프레임 소비 === */
        char achCmdData[128];
        char achCmdResult[128];
        char achResult[128];
        memset(achCmdData, 0x0, sizeof(achCmdData));
        memset(achCmdResult, 0x0, sizeof(achCmdResult));
        memset(achResult, 0x0, sizeof(achResult));
        unsigned int uiReqId;
        int iResultSize;
        evbuffer_drain(pstIoChannel->pstReadBuffer, iFrameSize);
        evbuffer_remove(pstIoChannel->pstReadBuffer, &uiReqId, sizeof(unsigned int));
        eErr = cmdDispatch(achRecvBuffer, iCopyLen, achCmdData);
        if (eErr != FRAME_OK){
            fprintf(stderr,"### %s():%d %s ###\n",__func__,__LINE__, frameErrToStr(eErr));
        }            
        applyCommand(unCmd, achCmdData, achCmdResult);
        MSG_ID stMsgId = { KEYBOARD_SND_TO_SF, SF_RCV_SENSOR_DATA };
        eErr = createCmdResponse(unCmd, achCmdResult, &stMsgId, achResult);
        iResultSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_RESPONSE);
        // sendUdsResponse(pstIoChannel, unCmd, uiReqId, auchResult, iResultSize);
        evbuffer_add(pstIoChannel->pstWriteBuffer, achResult, iResultSize);
        event_add(pstIoChannel->pstWriteEvent, NULL);
    }

    default:
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
    TCP_SERVER_RUNTIME_CFG stTcpKeyRcvSvrRuntimeCfg = {
        .unPort         = KEYBOARD_RCV_PORT,        
        .iSelfWorkerId  = TC_RCV_AZ_EL_FROM_CTRL_PC,
        .iDstWorkerId   = CTRL_PC,
        .eRole          = ROLE_REQUESTER,
        .eType          = TYPE_TCP_SVR,
        .pfWrite        = NULL,
        .pfIoHandler    = recvKeyboard,
        .pchTag         = "TC_RCV_AZ_EL_FROM_CTRL_PC"
    };
    UDS_CLIENT_RUNTIME_CFG stUdsClnRuntimeCfg = {
        .iSelfWorkerId  = KEYBOARD_SND_TO_SF,
        .iDstWorkerId   = SF_RCV_SENSOR_DATA,
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