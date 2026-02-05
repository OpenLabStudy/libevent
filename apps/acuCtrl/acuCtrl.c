#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <termios.h>
#include <stdint.h>

#include "uartConfig.h"
#include "acuUtil.h"
#include "icdCommand.h"
#include "cmdRegistry.h"
#include "netUds.h"
#include "netCore.h"
#include "ioChannelUtil.h"
#include "runtime.h"
#include "udsClientRuntime.h"
#include "udsServerRuntime.h"
#include "timeUtil.h"

/* ========================================================================== */
/* ACU STATE                                                                  */
/* ========================================================================== */
typedef enum {
    ACU_STATE_IDLE = 0,
    ACU_STATE_WAIT_RESPONSE
} ACU_STATE;

/* ========================================================================== */
/* COMMAND STATE (ACU 내부 설정값)                                             */
/* ========================================================================== */
typedef struct {
    char    chSendOnOff;
    char    chAcuMode;
    double  dAzOffset;
    double  dElOffset;
} COMMAND_STATE;

/* ========================================================================== */
/* ACU CONTEXT (전역 대체)                                                     */
/* ========================================================================== */
typedef struct {
    ACU_STATE           eState;
    unsigned int        uiReqId;
    unsigned int        uiLocalReqId;
    unsigned short      unCmd;
    COMMAND_STATE       stCommandState;
} ACU_CTRL_CTX;

#define RESP_OK        0x01
#define RESP_FAIL      0x00
#define RESP_BUSY      0x02
#define RESP_TIMEOUT   0x03
#define RESP_INTERNAL  0x04

#define ACU_UART_MONITORING_MSEC 400


void initAcuCtrlCtx(ACU_CTRL_CTX* pstAcuCtrlCtx)
{
    pstAcuCtrlCtx->eState                       = ACU_STATE_IDLE;
    pstAcuCtrlCtx->uiLocalReqId                 = 1;
    pstAcuCtrlCtx->uiReqId                      = 1;
    pstAcuCtrlCtx->unCmd                        = CMD_UNKNOWN;
    pstAcuCtrlCtx->stCommandState.chAcuMode     = ACU_MODE_NONE;
    pstAcuCtrlCtx->stCommandState.chSendOnOff   = AZ_EL_SEND_OFF;
    pstAcuCtrlCtx->stCommandState.dAzOffset     = 0.0;
    pstAcuCtrlCtx->stCommandState.dElOffset     = 0.0;
}

static COMMAND_PATH decideProcessingPath(unsigned short unCmd)
{
    switch(unCmd)
    {
        case CMD_ID_INFO:
            return COMMAND_PATH_NONE;
        case CMD_POSITIONER_AZ_EL_SET:
            return ACU_CTRL_UART;
        case CMD_ACU_MODE_SELECT:
            return ACU_CTRL_UART;
        case CMD_POSITIONER_AZ_EL:
            return ACU_CTRL_UART;
        default:
            return COMMAND_PATH_FAIL;
    }
}


static void applyCommand(ACU_CTRL_CTX* pstAcuCtrlCtx, unsigned short unCmd, 
    char* pchCmdData, char* pchOut, unsigned int* pOutLen)
{
    switch(unCmd)
    {
        case CMD_POSITIONER_AZ_EL:         
            fprintf(stderr, "\nGet ACU Current AZ, EL Value\n");
            *pOutLen = readAzElFromAcu(pchOut);
        break;

        case CMD_POSITIONER_AZ_EL_SET: 
        {
            REQ_POSITIONER_AZ_EL_SET *pstReqAzElSet = (REQ_POSITIONER_AZ_EL_SET *)pchCmdData;
            double dAz, dEl;
            dAz = endianChange(pstReqAzElSet->chAzimuthDeg);
            dEl = endianChange(pstReqAzElSet->chElevationDeg);
            fprintf(stderr, "%s():%d ACU AZ/EL Set to AZ: %.2f, EL: %.2f\n",__func__,__LINE__, dAz, dEl);
            if(pstAcuCtrlCtx->stCommandState.chAcuMode == POSITION){
                *pOutLen = moveAzElPosition(dAz, dEl, pchOut);
            }
            break;
        }
        case CMD_ACU_MODE_SELECT: 
        {            
            REQ_ACU_MODE *pstReqAcuMode	= (REQ_ACU_MODE*)pchCmdData;
            fprintf(stderr, "\nACU Mode Change to %s\n", pstReqAcuMode->chAcuMode == POSITION ? "POSITION MODE" : "RATE MODE");
            pstAcuCtrlCtx->stCommandState.chAcuMode = pstReqAcuMode->chAcuMode;
            *pOutLen = modeChange(pstReqAcuMode->chAcuMode, pchOut);
            break;
        }
        case CMD_ID_INFO:
        {
            RES_ID *pstResId = (RES_ID *)(pchCmdData);
            pstResId->chResult = (char)AC_RCV_CMD_FROM_TC;
            fprintf(stderr, "RES_ID %04X\n", pstResId->chResult);
            *pOutLen = sizeof(RES_ID);
        }
        break;
        default:
        break;
    }
}


/* ========================================================================== */
/* Helper: parse UART response                                                 */
/*  - TODO: ACU UART 응답을 파싱해서 OK/FAIL 코드 반환                        */
/* ========================================================================== */
static unsigned char parseAcuUartResponse(const char* pchBuf, int iLen)
{
    if (!pchBuf || iLen <= 0)
        return RESP_FAIL;

    /* 예시: 응답의 특정 바이트가 0x01이면 OK로 가정 */
    /* 실제 프로토콜에 맞게 구현하세요. */
    if (iLen >= 1 && pchBuf[0] == 0x06)
        return RESP_OK;

    return RESP_FAIL;
}

static void uartWriteCallback(int iFd, short nEvent, void *pvData)
{
    (void)iFd;
    (void)nEvent;
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    EVENT_ENGINE* pstEngine = pstIoChannel->pstEventEngine;
    unsigned char auchUartWriteData[2048];    
    int iTotalSize, iWriteSize;
    iTotalSize = evbuffer_get_length(pstIoChannel->pstWriteBuffer);
    if (iTotalSize == 0) {
        event_del(pstIoChannel->pstWriteEvent);
        return;
    }
    iWriteSize = evbuffer_remove(pstIoChannel->pstWriteBuffer, auchUartWriteData, iTotalSize);
    if(iWriteSize > 0){        
        fprintf(stderr,"### TotalSize is %d, ACU Write Size is %d [Req ID%d]###\n", iTotalSize, iWriteSize, pstEngine->uiRequestSeq);
        iWriteSize = write(pstIoChannel->iFd, auchUartWriteData, iWriteSize);
        event_del(pstIoChannel->pstWriteEvent);
    }
}

/* ========================================================================== */
/* UART Read Callback (응답 수신)                                              */
/*  - pending cmd와 매칭해서 UDS 응답 생성/전송                               */
/* ========================================================================== */
static void uartReadCallback(int iFd, short nEvent, void *pvData)
{
    (void)iFd;
    (void)nEvent;

    IO_CHANNEL* pstUartIo = (IO_CHANNEL*)pvData;
    IO_EVENT_TYPE eEventType = pstUartIo->ePendingLogicEvent;

    EVENT_ENGINE* pstEngine = pstUartIo->pstEventEngine;
    ACU_CTRL_CTX* pstCtx = (ACU_CTRL_CTX*)pstEngine->pvSharedData;
    char acUartBuf[2048];
    MSG_ID stMsgId;
    switch (eEventType)
    {
    case IO_EVT_RX_DATA: {
        pstUartIo->chFdCloseSet = FD_OPENED;
        int iLen = evbuffer_remove(pstUartIo->pstReadBuffer, acUartBuf, sizeof(acUartBuf));
        if (iLen <= 0)
            break;

        fprintf(stderr, "[ACU] UART RX %d bytes, Request ID %d, CMD is %04X\n", iLen, 
            pstEngine->uiRequestSeq, pstCtx->unCmd);
        unsigned char uchResult = parseAcuUartResponse(acUartBuf, iLen);

        char achCmdResult[128];
        memset(achCmdResult, 0, sizeof(achCmdResult));        
        switch (pstCtx->unCmd) {
        case CMD_POSITIONER_AZ_EL_SET:
        {            
            RES_POSITIONER_AZ_EL_SET* pstResPositionerAzElSet = (RES_POSITIONER_AZ_EL_SET *)achCmdResult;
            stMsgId.uchSrcId = AC_RCV_CMD_FROM_TC;
            stMsgId.uchDstId = TC_SND_CMD_TO_CLN;
            pstResPositionerAzElSet->chResult = (uchResult == RESP_OK)?0x01:0x00;            
            break;
        }
        case CMD_ACU_MODE_SELECT:
        {            
            RES_ACU_MODE* pstResAcuMode = (RES_ACU_MODE *)achCmdResult;
            stMsgId.uchSrcId = AC_RCV_CMD_FROM_TC;
            stMsgId.uchDstId = TC_SND_CMD_TO_CLN;
            pstResAcuMode->chResult = (uchResult == RESP_OK)?0x01:0x00;            
            break;
        }
        case CMD_POSITIONER_AZ_EL:
        {            
            SEND_CURR_AZ_EL* pstSendCurrAzEl = (SEND_CURR_AZ_EL *)achCmdResult;
            int iCurrTime;
            pstSendCurrAzEl->iTime = timePackHMSms();
            char *chSplitData[8];
            stMsgId.uchSrcId = AC_SND_AZ_EL_TO_TC;
            stMsgId.uchDstId = TC_RCV_AZ_EL_FROM_AC;
            int iSplitCnt = splitAcuDataString(acUartBuf, ';', chSplitData, 2);
            if(iSplitCnt == 2){
                pstSendCurrAzEl->iAz = (atof(chSplitData[0]) * 1000);
                pstSendCurrAzEl->iEl = (atof(chSplitData[1]) * 1000);
                fprintf(stderr,"ACU Current AZ EL Value is %d[%.03lf], %d[%.03lf]\n", 
                    pstSendCurrAzEl->iAz, ((double)pstSendCurrAzEl->iAz/1000.0), 
                    pstSendCurrAzEl->iEl, ((double)pstSendCurrAzEl->iEl)/1000.0);
            }
        }
        break;
        default:
            break;
        }
        char achResult[UDS_MAX_BUFFER_SIZE];
        memset(achResult, 0, sizeof(achResult));
        createCmdResponse(pstCtx->unCmd, achCmdResult, &stMsgId, achResult);
        int iResultSize = getFrameSizeWithCmd(pstCtx->unCmd, FRAME_TYPE_RESPONSE);
        eventEngineHandleWorkerResponse(pstUartIo->pstEventEngine, pstUartIo,
                    pstEngine->uiRequestSeq, achResult, iResultSize);

    }    
    pstCtx->eState = ACU_STATE_IDLE;
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

/* ============================================================
 * [ADDED] 100ms AZ/EL Polling Timer Callback
 * ============================================================ */
static void acuAzElPollingCb(int iFd, short nEvent, void *pvData)
{
    (void)iFd;
    (void)nEvent;
    EVENT_ENGINE *pstEventEngine = (EVENT_ENGINE *)pvData;
    ACU_CTRL_CTX* pstAcuCtrlCtx = (ACU_CTRL_CTX*)pstEventEngine->pvSharedData;
    unsigned int uiReqId;
    char achCmdData[128];
    char achResult[128];
    unsigned int uiUartDataSize;
    IO_CHANNEL* pstIoChannel = ioFindChannelByWorkerId(pstEventEngine, AC_SND_AZ_EL_TO_TC);
    if(pstIoChannel == NULL){
        return;
    }
    COMMAND_PATH eCommandPath = ACU_CTRL_UART;
    pstAcuCtrlCtx->unCmd = CMD_POSITIONER_AZ_EL;
    applyCommand(pstAcuCtrlCtx, CMD_POSITIONER_AZ_EL, achCmdData, achResult, &uiUartDataSize);
    if(pstAcuCtrlCtx->uiLocalReqId == pstEventEngine->uiRequestSeq){
        pstAcuCtrlCtx->uiLocalReqId++;
    }
    pstEventEngine->uiRequestSeq = pstAcuCtrlCtx->uiLocalReqId++;    
    evbuffer_add(pstIoChannel->pstRequestBuffer, &eCommandPath, sizeof(eCommandPath));
    evbuffer_add(pstIoChannel->pstRequestBuffer, achResult, uiUartDataSize);
    event_active(pstIoChannel->pstRequestEvent, 0, 0);
}


static void commandEventCb(int iFd, short nEvent, void *pvData)
{
    (void)iFd;
    (void)nEvent;
    IO_CHANNEL *pstIoChannel = (IO_CHANNEL *)pvData;
    EVENT_ENGINE* pstEventEngine = pstIoChannel->pstEventEngine;
    IO_EVENT_TYPE eEventType = pstIoChannel->ePendingLogicEvent;
    char achRecvBuffer[UDS_MAX_BUFFER_SIZE];
    unsigned short unCmd = 0;
    FRAME_ERR eErr;
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
            int iRecvLen = evbuffer_get_length(pstIoChannel->pstReadBuffer);            
            /* 최소 헤더도 없으면 중단 */
            if (iRecvLen < (int)sizeof(FRAME_HEADER))
                break;
            memset(achRecvBuffer, 0x00, sizeof(achRecvBuffer));
            int iCopyLen = evbuffer_copyout(pstIoChannel->pstReadBuffer, achRecvBuffer, iRecvLen);
            eErr = frameDecode(achRecvBuffer, iCopyLen, FRAME_TYPE_REQUEST, &unCmd);
            if (eErr != FRAME_OK)
            {
                fprintf(stderr, "[UDS-SVR] frameDecode ERR: %s\n", frameErrToStr(eErr));
                int iDeleteDataSize = findFrameHeader(achRecvBuffer, iCopyLen);
                    evbuffer_drain(pstIoChannel->pstReadBuffer, iDeleteDataSize);
                continue;
            }
            pstAcuCtrlCtx->unCmd = unCmd;
            unsigned int uiReqId;
            char achCmdData[128];
            char achResult[128];
            unsigned int uiUartDataSize;
            int iResultSize;            
            int iFrameSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_REQUEST);
            memset(achCmdData, 0x0, sizeof(achCmdData));
            memset(achResult, 0x0, sizeof(achResult));
            evbuffer_drain(pstIoChannel->pstReadBuffer, iFrameSize);            
            evbuffer_remove(pstIoChannel->pstReadBuffer, &uiReqId, sizeof(unsigned int));
            pstEventEngine->uiRequestSeq = uiReqId;
            COMMAND_PATH eCommandPath = decideProcessingPath(unCmd);
            eErr = cmdDispatch(achRecvBuffer, iCopyLen, achCmdData);
            if (eErr != FRAME_OK){
                fprintf(stderr,"### %s():%d %s ###\n",__func__,__LINE__, frameErrToStr(eErr));
                continue;
            }            
            applyCommand(pstAcuCtrlCtx, unCmd, achCmdData, achResult, &uiUartDataSize);
            if (eCommandPath == COMMAND_PATH_NONE) {
                MSG_ID stMsgId = { AC_RCV_CMD_FROM_TC, TC_SND_CMD_TO_CLN };
                eErr = createCmdResponse(unCmd, achCmdData, &stMsgId, achResult);
                iResultSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_RESPONSE);
                evbuffer_add(pstIoChannel->pstWriteBuffer, achResult, iResultSize);
                event_add(pstIoChannel->pstWriteEvent, NULL);
            } else if (eCommandPath == ACU_CTRL_UART) {
                evbuffer_add(pstIoChannel->pstRequestBuffer, &eCommandPath, sizeof(eCommandPath));
                evbuffer_add(pstIoChannel->pstRequestBuffer, achResult, uiUartDataSize);
                event_active(pstIoChannel->pstRequestEvent, 0, 0);
            }else{
                fprintf(stderr,"### %s():%d Path Number is %d ###\n",__func__,__LINE__, eCommandPath);
            }           
        }
        break;
    default:
        break;
    }
    pstIoChannel->ePendingLogicEvent = IO_EVENT_NONE;
}

static void writeNone(int iFd, short nEvent, void* pvData)
{
    (void)iFd;
    (void)nEvent;
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;    
    size_t tRecvLen = evbuffer_get_length(pstIoChannel->pstWriteBuffer);
    fprintf(stderr,"### %s():%d Size is %d ###\n",__func__,__LINE__, tRecvLen);
    evbuffer_drain(pstIoChannel->pstWriteBuffer, tRecvLen);
}

static void recvAzElFromSensorFusion(int iFd, short nEvent, void* pvData)
{
    (void)iFd;
    (void)nEvent;
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    EVENT_ENGINE* pstEventEngine = pstIoChannel->pstEventEngine;
    IO_EVENT_TYPE eEventType = pstIoChannel->ePendingLogicEvent;
    ACU_CTRL_CTX* pstAcuCtrlCtx = (ACU_CTRL_CTX*)pstEventEngine->pvSharedData;

    char achRecvBuffer[UDS_MAX_BUFFER_SIZE];
    unsigned short unCmd = 0;
    FRAME_ERR eErr;
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    switch (eEventType) {
    case IO_EVT_RX_DATA:
        while (1) {
            size_t tRecvLen = evbuffer_get_length(pstIoChannel->pstReadBuffer);
            /* 최소 헤더도 없으면 중단 */
            if (tRecvLen < sizeof(FRAME_HEADER))
                break;

            memset(achRecvBuffer, 0x00, sizeof(achRecvBuffer));
            int iCopyLen = evbuffer_copyout(pstIoChannel->pstReadBuffer,
                                            achRecvBuffer, tRecvLen);
            /* frameDecode에 대한 처리가 완전한지 확인 필요*/
            eErr = frameDecode(achRecvBuffer, iCopyLen, FRAME_TYPE_RESPONSE, &unCmd);
            if (eErr != FRAME_OK) {
                fprintf(stderr, "[UDS-SVR] frameDecode ERR: %s\n", frameErrToStr(eErr));
                int iDeleteDataSize = findFrameHeader(achRecvBuffer, iCopyLen);
                evbuffer_drain(pstIoChannel->pstReadBuffer, iDeleteDataSize);
                continue;
            }
            int iFrameSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_RESPONSE);
            evbuffer_drain(pstIoChannel->pstReadBuffer, iFrameSize + sizeof(unsigned int));
            if(unCmd == CMD_ID_INFO){
                fprintf(stderr,"### %s():%d ID is %d[%d] ###\n", __func__,__LINE__,
                    (int)getIdInfo(achRecvBuffer+sizeof(FRAME_HEADER)), pstIoChannel->iWorkerId);                 
            }else{
                /* === 프레임 소비 === */                
                RES_AZ_EL_DATA* pstAzElData;
                pstAzElData = (RES_AZ_EL_DATA *)(achRecvBuffer + sizeof(FRAME_HEADER));
                fprintf(stderr,"Azimuth: %lf, Elevation: %lf\n", pstAzElData->dAz, pstAzElData->dEl);

                //TODO ACU UART로 명령 전송 및 응답 수신
                char achSendAcuCtrlData[128];
                unsigned int uiUartDataSize;
                memset(achSendAcuCtrlData, 0x0, sizeof(achSendAcuCtrlData));
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
                    fprintf(stderr,"ACU MODE is not Position/Rate\n");
                    break;
                }
                evbuffer_add(pstIoChannel->pstRequestBuffer, &eCommandPath, sizeof(eCommandPath));
                evbuffer_add(pstIoChannel->pstRequestBuffer, achSendAcuCtrlData, uiUartDataSize);
                event_active(pstIoChannel->pstRequestEvent, 0, 0);
            }
        }
        break;

    case IO_EVT_CHANNEL_CLOSED:
    case IO_EVT_ERROR:
        printf("[UDS-SVR] channel closed fd=%d\n", pstIoChannel->iFd);
        event_active(pstIoChannel->pstShutdownEvent, 0, 0);
        break;

    default:
        break;
    }

    pstIoChannel->ePendingLogicEvent = IO_EVENT_NONE;
}

static void sendCurrentAzElValue(int iFd, short nEvent, void* pvData)
{
    (void)iFd;
    (void)nEvent;
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    EVENT_ENGINE* pstEventEngine = pstIoChannel->pstEventEngine;
    IO_EVENT_TYPE eEventType = pstIoChannel->ePendingLogicEvent;
    ACU_CTRL_CTX* pstAcuCtrlCtx = (ACU_CTRL_CTX*)pstEventEngine->pvSharedData;
    FRAME_ERR eErr;
    unsigned char auchWriteBuffer[2048];
    unsigned short unCmd = 0;
    int iWriteSize;
    iWriteSize = evbuffer_get_length(pstIoChannel->pstWriteBuffer);
    if (iWriteSize == 0) {
        event_del(pstIoChannel->pstWriteEvent);
        return;
    }
    iWriteSize = evbuffer_remove(pstIoChannel->pstWriteBuffer, auchWriteBuffer, iWriteSize);
    eErr = frameDecode(auchWriteBuffer, iWriteSize, FRAME_TYPE_RESPONSE, &unCmd);
    if (eErr != FRAME_OK) {
        fprintf(stderr, "[UDS-SVR] frameDecode ERR: %s\n", frameErrToStr(eErr));
        int iDeleteDataSize = findFrameHeader(auchWriteBuffer, iWriteSize);
        evbuffer_drain(pstIoChannel->pstReadBuffer, iDeleteDataSize);
    }
    if(unCmd == CMD_POSITIONER_AZ_EL){
        MSG_ID stMsgId = { AC_SND_AZ_EL_TO_TC, TC_RCV_AZ_EL_FROM_AC };
        repackageResponse(auchWriteBuffer, &stMsgId, iWriteSize);
        iWriteSize = write(pstIoChannel->iFd, auchWriteBuffer, iWriteSize);
        if (iWriteSize <= 0) {
            perror("write");
            return;
        }    
        if (evbuffer_get_length(pstIoChannel->pstWriteBuffer) == 0)
            event_del(pstIoChannel->pstWriteEvent);
    }else{
        fprintf(stderr,"### %s():%d %d ###\n",__func__,__LINE__, unCmd);
    }
    
}


/* ============================================================
 * Main
 * ============================================================ */
int run(char *pchUartPath)
{
    // 이미 끊어진 소켓에 write() 했을 때 프로세스가 즉사(SIGPIPE)하는 것을 막는다.
    ioIgnoreSigpipeOnce();

    EVENT_ENGINE stEventEngine;
    IO_CHANNEL *pstIoChannel = NULL;
    struct event*   pstEventAcceptUds3;
    struct event*   pstEventAcceptUds4;
    struct event *pstSignalEvent = NULL;
    struct event *pstUdsRetryEvent = NULL;
    UART_CTX stUartCtx = {
        .pchDevPath     = pchUartPath,
        .iBaudrate      = 115200,
        .iFd            = -1,
        .iBackoffMsec   = 200
    };
    UDS_CLIENT_RUNTIME_CFG stRcvCmdUdsClnRuntimeCfg = {
        .iSelfWorkerId  = AC_RCV_CMD_FROM_TC,
        .iDstWorkerId   = TC_SND_CMD_TO_CLN,
        .pchUdsPath     = UDS_1_PATH,
        .eRole          = ROLE_REQUESTER,
        .eType          = TYPE_UDS_CLI,
        .pchTag         = "AC_RCV_CMD_FROM_TC"
    };
    UDS_SERVER_RUNTIME_CFG stRcvSensorDataUdsSvrRuntimeCfg = {
        .pchUdsPath     = UDS_3_PATH,        
        .iSelfWorkerId  = AC_RCV_AZ_EL_FROM_SF,
        .iDstWorkerId   = SF_SND_AZ_EL_TO_AC,
        .eRole          = ROLE_REQUESTER,
        .eType          = TYPE_UDS_SVR,
        .pfWrite        = writeNone,
        .pfIoHandler    = recvAzElFromSensorFusion,
        .pchTag         = "AC_RCV_AZ_EL_FROM_SF"
    };
    UDS_SERVER_RUNTIME_CFG stSndAzElDataUdsSvrRuntimeCfg = {
        .pchUdsPath     = UDS_4_PATH,        
        .iSelfWorkerId  = AC_SND_AZ_EL_TO_TC,
        .iDstWorkerId   = TC_RCV_AZ_EL_FROM_AC,
        .eRole          = ROLE_REQUESTER,
        .eType          = TYPE_UDS_SVR,
        .pfWrite        = sendCurrentAzElValue,
        .pfIoHandler    = NULL,
        .pchTag         = "AC_SND_AZ_EL_TO_TC",
    };

    stEventEngine.pstEventBase = event_base_new();
    if (!stEventEngine.pstEventBase) {
        fprintf(stderr, "[ACU] event_base_new() failed\n");
        return EXIT_FAILURE;
    }
    eventEngineInit(&stEventEngine, 1);
    ACU_CTRL_CTX* pstAcuCtrlCtx = calloc(1, sizeof(ACU_CTRL_CTX));
    initAcuCtrlCtx(pstAcuCtrlCtx);
    stEventEngine.pvSharedData = pstAcuCtrlCtx;    

    /* UART open */
    if (uartOpen(&stUartCtx) < 0) {
        fprintf(stderr, "[ACU] uartOpen failed: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    pstIoChannel = eventSourceCreateWithBev(&stEventEngine, stUartCtx.iFd,
        TYPE_UART, ROLE_WORKER, NULL, uartWriteCallback, uartReadCallback);
    pstIoChannel->iWorkerId = ACU_CTRL_UART;

    UDS_CLIENT_RUNTIME *pstRcvCmdUdsClnRuntime = udsClientRuntimeCreate(&stEventEngine, 
        &stRcvCmdUdsClnRuntimeCfg, commandEventCb, NULL);

    /* Accept 이벤트 등록 */
    UDS_SERVER_RUNTIME *pstAzElRcvSvr =
        udsServerRuntimeCreate(&stEventEngine, &stRcvSensorDataUdsSvrRuntimeCfg, pstAcuCtrlCtx);
    UDS_SERVER_RUNTIME *pstAzElSndSvr =
        udsServerRuntimeCreate(&stEventEngine, &stSndAzElDataUdsSvrRuntimeCfg, pstAcuCtrlCtx);

    /* === 100ms 주기 타이머 생성 === */
    struct timeval tvPoll = {0, 100 * 1000}; // 100ms
    struct event *pstPollEvt = event_new(stEventEngine.pstEventBase, -1,
                  EV_PERSIST | EV_TIMEOUT,
                  acuAzElPollingCb, &stEventEngine);

    event_add(pstPollEvt, &tvPoll);
        
    APP_SIGNAL_HANDLE *pstSigHandle = appSignalCreate(&stEventEngine, "ACU-CTRL");

    event_base_dispatch(stEventEngine.pstEventBase);
    udsServerRuntimeDestroy(&pstAzElRcvSvr);
    udsServerRuntimeDestroy(&pstAzElSndSvr);
    udsClientRuntimeDestroy(&pstRcvCmdUdsClnRuntime);
    appSignalDestroy(&pstSigHandle);

    eventEngineCleanup(&stEventEngine);
    event_base_free(stEventEngine.pstEventBase);

    fprintf(stderr, "[ACU] Terminated.\n");
    return EXIT_SUCCESS;
}

#ifndef GOOGLE_TEST
int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s /dev/ttyUSB0\n", argv[0]);
        return EXIT_FAILURE;
    }
    return run(argv[1]);
}
#endif
