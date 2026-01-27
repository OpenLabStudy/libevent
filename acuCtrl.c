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
#include "ipcUtil.h"
#include "acuUtil.h"

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
    unsigned short      unCmd;
    COMMAND_STATE       stCommandState;
} ACU_CTRL_CTX;

#define ACU_UART_MONITORING_MSEC 400


static COMMAND_PATH decideProcessingPath(unsigned short unCmd)
{
    switch(unCmd)
    {
        case CMD_ID_INFO:
            return COMMAND_PATH_NONE;
        case CMD_POSITIONER_AZ_EL_SET:
            return ACU_UART;
        case CMD_ACU_MODE_SELECT:
            return ACU_UART;
        default:
            return COMMAND_PATH_FAIL;
    }
}

/* ========================================================================== */
/* Helper: build UART frame from IPC command                                   */
/*  - TODO: ACU UART 프로토콜에 맞게 구현                                     */
/* ========================================================================== */
static int buildAcuUartFrame(ACU_CTRL_CTX* pstAcuCtrlCtx, unsigned short unCmd, 
    unsigned char* puchRecvCmdData, unsigned char* pOut, unsigned int* pOutLen)
{
    int iSendLen = 0;
    if (!pOut || !pOutLen)
        return -1;

    /* payload (예시) */
    switch (unCmd) {
    case CMD_GET_CURRENT_AZ_EL_SET: {
        fprintf(stderr, "\nGet ACU Current AZ, EL Value\n");
            *pOutLen = readAzElFromAcu(pOut);
        break;
    }

    case CMD_POSITIONER_AZ_EL_SET: {
        REQ_POSITIONER_AZ_EL_SET *pstReqAzElSet = (REQ_POSITIONER_AZ_EL_SET *)puchRecvCmdData;
        double dAz, dEl;
        dAz = endianChange(pstReqAzElSet->chAzimuthDeg);
        dEl = endianChange(pstReqAzElSet->chElevationDeg);
        fprintf(stderr, "%s():%d ACU AZ/EL Set to AZ: %.2f, EL: %.2f\n",__func__,__LINE__, dAz, dEl);
        if(pstAcuCtrlCtx->stCommandState.chAcuMode == POSITION){
            *pOutLen = moveAzElPosition(dAz, dEl, pOut);
        }
        break;
    }

    case CMD_ACU_MODE_SELECT: {
        REQ_ACU_MODE *pstReqAcuMode	= (REQ_ACU_MODE*)puchRecvCmdData;
        fprintf(stderr, "\nACU Mode Change to %s\n", pstReqAcuMode->chAcuMode == POSITION ? "POSITION MODE" : "RATE MODE");
        pstAcuCtrlCtx->stCommandState.chAcuMode = pstReqAcuMode->chAcuMode;
        iSendLen = modeChange(pstReqAcuMode->chAcuMode, pOut);
        fprintf(stderr, "Total Send Length: %d, %02X\n", iSendLen, pstReqAcuMode->chAcuMode);
        *pOutLen = iSendLen;
        break;
    }
    default:
        break;
    }
    return 0;
}

static void applyCommand(ACU_CTRL_CTX* pstAcuCtrlCtx, unsigned short unCmd, 
    unsigned char* puchCmdData, unsigned char* pOut, unsigned int* pOutLen)
{
    memset(pOut, 0x0, sizeof(pOut));
    switch(unCmd)
    {
        case CMD_GET_CURRENT_AZ_EL_SET:         
            fprintf(stderr, "\nGet ACU Current AZ, EL Value\n");
            *pOutLen = readAzElFromAcu(pOut);
        break;

        case CMD_POSITIONER_AZ_EL_SET: 
        {
            REQ_POSITIONER_AZ_EL_SET *pstReqAzElSet = (REQ_POSITIONER_AZ_EL_SET *)puchCmdData;
            double dAz, dEl;
            dAz = endianChange(pstReqAzElSet->chAzimuthDeg);
            dEl = endianChange(pstReqAzElSet->chElevationDeg);
            fprintf(stderr, "%s():%d ACU AZ/EL Set to AZ: %.2f, EL: %.2f\n",__func__,__LINE__, dAz, dEl);
            if(pstAcuCtrlCtx->stCommandState.chAcuMode == POSITION){
                *pOutLen = moveAzElPosition(dAz, dEl, pOut);
            }
            break;
        }
        case CMD_ACU_MODE_SELECT: 
        {
            REQ_ACU_MODE *pstReqAcuMode	= (REQ_ACU_MODE*)puchCmdData;
            fprintf(stderr, "\nACU Mode Change to %s\n", pstReqAcuMode->chAcuMode == POSITION ? "POSITION MODE" : "RATE MODE");
            pstAcuCtrlCtx->stCommandState.chAcuMode = pstReqAcuMode->chAcuMode;
            *pOutLen = modeChange(pstReqAcuMode->chAcuMode, pOut);
            fprintf(stderr, "Total Send Length: %d, %02X\n", *pOutLen, pstReqAcuMode->chAcuMode);            
            break;
        }
        case CMD_ID_INFO:
        {
            RES_ID *pstResId = (RES_ID *)(puchCmdData);
            pstResId->chResult = (char)AC_CMD_RECEIVER;
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
static unsigned char parseAcuUartResponse(const unsigned char* pBuf, int iLen)
{
    if (!pBuf || iLen <= 0)
        return RESP_FAIL;

    /* 예시: 응답의 특정 바이트가 0x01이면 OK로 가정 */
    /* 실제 프로토콜에 맞게 구현하세요. */
    if (iLen >= 1 && pBuf[0] == 0x06)
        return RESP_OK;

    return RESP_FAIL;
}


static int findCrLf(const unsigned char *puchBuf, int iBufLen)
{
    int i;

    for (i = 0; i < iBufLen - 1; i++) {
        if (puchBuf[i] == 0x0D && puchBuf[i + 1] == 0x0A) {
            return i+2;   // 0x0A 위치 반환
        }
    }

    return -1;  // 못 찾은 경우
}

static void uartWriteCallback(int iFd, short nEvent, void *pvData)
{
    (void)nEvent;
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    EVENT_ENGINE* pstEngine = pstIoChannel->pstEventEngine;
    unsigned char auchWriteEvBuffer[2048];
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
        fprintf(stderr,"### %s():%d Write Size:%d ###\n", __func__,__LINE__, iWriteSize);
        for(int i=1; i<=iWriteSize; i++){
            if(i&16 == 0)
                fprintf(stderr,"\n");
            fprintf(stderr,"%02x ", auchWriteEvBuffer[i-1]);
        }  
        fprintf(stderr,"\n");
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

    unsigned char aucUartBuf[2048];
    
    int iFrameSize = 0;
    switch (eEventType)
    {
    case IO_EVT_RX_DATA: {
        fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
        pstUartIo->chFdCloseSet = FD_OPENED;
        int iLen = evbuffer_remove(pstUartIo->pstReadBuffer, aucUartBuf, sizeof(aucUartBuf));
        fprintf(stderr,"### %s():%d %d###\n",__func__,__LINE__, iLen);
        if (iLen <= 0)
            break;

        fprintf(stderr, "[ACU] UART RX %d bytes, Request ID %d, CMD is %04X\n", iLen, 
            pstEngine->uiRequestSeq, pstCtx->unCmd);
        unsigned char uchResult = parseAcuUartResponse(aucUartBuf, iLen);

        unsigned char auchCmdResult[128];
        memset(auchCmdResult, 0, sizeof(auchCmdResult));
        iFrameSize = getDataSize(pstCtx->unCmd, FRAME_TYPE_RESPONSE);
        switch (pstCtx->unCmd) {
        case CMD_POSITIONER_AZ_EL_SET:
        {
            fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
            RES_POSITIONER_AZ_EL_SET* pstResPositionerAzElSet = (RES_POSITIONER_AZ_EL_SET *)auchCmdResult;
            pstResPositionerAzElSet->chResult = (uchResult == RESP_OK)?0x01:0x00;            
            break;
        }
        case CMD_ACU_MODE_SELECT:
        {
            RES_ACU_MODE* pstResAcuMode = (RES_ACU_MODE *)auchCmdResult;
            fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
            pstResAcuMode->chResult = (uchResult == RESP_OK)?0x01:0x00;            
            break;
        }
        case CMD_GET_ACU_AZ_EL_DATA:
        {
            fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
            double dAz, dEl;
            char *chSplitData[8];
            // gettimeofday(&pstCtx->stLastAzElRxTime, NULL);
            int iSplitCnt = splitAcuDataString(aucUartBuf, ';', chSplitData, 2);
            if(iSplitCnt == 2){
                dAz = atof(chSplitData[0]);
                dEl = atof(chSplitData[1]);
                fprintf(stderr,"ACU Current AZ EL Value is %.03lf, %.03lf\n", dAz, dEl);
            }
        }
        break;
        default:
            break;
        }
        unsigned char auchResult[UDS_MAX_BUFFER_SIZE];
        memset(auchResult, 0, sizeof(auchResult));
        fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
        //명령 주체에 따라 변경 필요
        MSG_ID stMsgId = { AC_CMD_RECEIVER, TC_UDS_CMD_CTRL };
        FRAME_ERR eErr = createCmdResponse(pstCtx->unCmd, auchCmdResult, &stMsgId, auchResult);
        int iResultSize = getFrameSizeWithCmd(pstCtx->unCmd, FRAME_TYPE_RESPONSE);
        fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);        
        eventEngineHandleWorkerResponse(pstUartIo->pstEventEngine, pstUartIo,
                    pstEngine->uiRequestSeq, auchResult, iResultSize);

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
 * [ADDED] UART Alive Monitor (Polling 기반)
 * ============================================================ */
static void uartAliveMonitorCb(int iFd, short nEvent, void *pvData)
{
    // fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    // (void)iFd;
    // (void)nEvent;
    // ACU_CTRL_CTX* pstCtx = pvData;

    // struct timeval now;
    // gettimeofday(&now, NULL);

    // long diffMs =
    //     (now.tv_sec  - pstCtx->stLastAzElRxTime.tv_sec) * 1000 +
    //     (now.tv_usec - pstCtx->stLastAzElRxTime.tv_usec) / 1000;

    // if (diffMs >= ACU_UART_MONITORING_MSEC) {
    //     if (pstCtx->iIsUartAlive) {
    //         fprintf(stderr, "[ACU] AZ/EL polling timeout (%ld ms)\n", diffMs);
    //     }
    //     pstCtx->iIsUartAlive = 0;
    //     pstCtx->iIsSendCommand = 0;
    // }
}

/* ============================================================
 * [ADDED] 100ms AZ/EL Polling Timer Callback
 * ============================================================ */
static void acuAzElPollingCb(int iFd, short nEvent, void *pvData)
{
    (void)iFd;
    (void)nEvent;

    // ACU_CTRL_CTX* pstCtx = (ACU_CTRL_CTX*)pvData;

    // if (!pstCtx || !pstCtx->pstUartIo)
    //     return;

    // /* UART 죽은 상태면 polling 중단 */
    // if (!pstCtx->iIsUartAlive && pstCtx->iIsSendCommand)
    //     return;

    // unsigned char aucFrame[256];
    // unsigned int  uiFrameLen = 0;
    // memset(aucFrame, 0, sizeof(aucFrame));
    // IPC_CMD_CTX stCmdCtx;
    // stCmdCtx.unCmd = CMD_GET_CURRENT_AZ_EL_SET;

    // fprintf(stderr, "[ACU] Sending CMD=0x%04X to UART\n", pstCmdCtx->unCmd);
    // if (acuSendUartAndPend(pstCtx, &stCmdCtx, pstUdsIo, uiReqId) < 0) {
    //     /* busy or internal error => 즉시 실패 응답 */
    //     if (pstCmdCtx->unCmd == CMD_POSITIONER_AZ_EL_SET){                
    //         ((RES_POSITIONER_AZ_EL_SET*)aucPayload)->chResult = (char)RESP_OK;
    //     } else {
    //         ((RES_ACU_MODE*)aucPayload)->chResult = (char)RESP_OK;
    //     }

    //     sendUdsResponse(pstUdsIo, pstCmdCtx->unCmd, uiReqId, aucPayload, sizeof(aucPayload));
    // }



    // if (buildAcuUartFrame(pstCtx, &stCmdCtx, aucFrame, &uiFrameLen) < 0 || uiFrameLen == 0) {
    //     return -1;
    // }

    // /* pending 등록 */
    // pstCtx->stPending.bInUse   = 1;

    // /* UART write */
    // evbuffer_add(pstCtx->pstUartIo->pstWriteBuffer, aucFrame, uiFrameLen);
    // event_active(pstCtx->pstUartIo->pstWriteEvent, EV_WRITE, 0);

    // pstCtx->iIsSendCommand = 1;   // [ADDED]
    // pstCtx->eState = ACU_STATE_WAIT_RESPONSE;
}




//* ========================================================================== */
/* ACU Command Execute                                                        */
/* ========================================================================== */
// static int executeIpcCommand(const IPC_CMD_CTX* pstCmdCtx, IO_CHANNEL* pstUdsIo, unsigned int uiReqId, unsigned char *puchUartSndData)
// {
//     EVENT_ENGINE* pstEngine = pstUdsIo->pstEventEngine;
//     ACU_CTRL_CTX* pstCtx = (ACU_CTRL_CTX*)pstEngine->pvSharedData;

//     int iRetSize=0;
//     unsigned char aucPayload[UDS_MAX_BUFFER_SIZE];
//     memset(aucPayload, 0, sizeof(aucPayload));
//     pstCtx->stAcuPendingCmd.unCmd = pstCmdCtx->unCmd;
//     pstCtx->stAcuPendingCmd.uiReqId = uiReqId;
//     switch (pstCmdCtx->unCmd)
//     {
//     case CMD_ID_INFO: 
//         fprintf(stderr, "### CMD_ID_INFO RESPONSE ###\n");
//         ((RES_POSITIONER_DEG_SEND*)aucPayload)->chResult = (char)pstUdsIo->iWorkerId;
//         // Fill in the payload with ID info as needed
//         sendUdsResponse(pstUdsIo, pstCmdCtx->unCmd, uiReqId, aucPayload, sizeof(aucPayload));
//         break;
        
//     case CMD_POSITIONER_DEG_SEND:
//         fprintf(stderr, "ACU AZ/EL Send %s\n", pstCmdCtx->u.stPositionerAzElSendCtrl.chSendOnOff == AZ_EL_SEND_ON ? "ON" :"OFF");
//         pstCtx->stCommandState.chSendOnOff = pstCmdCtx->u.stPositionerAzElSendCtrl.chSendOnOff;
//         ((RES_POSITIONER_DEG_SEND*)aucPayload)->chResult = (char)RESP_OK;
//         sendUdsResponse(pstUdsIo, pstCmdCtx->unCmd, uiReqId, aucPayload, sizeof(aucPayload));
//         break;

//     case CMD_AZ_EL_OFFSET_SET:
//         fprintf(stderr, "ACU AZ Offset=%.2f EL Offset=%.2f\n",
//             ((double)pstCmdCtx->u.stAzElOffsetSet.iAzOffset) / 100.0,
//             ((double)pstCmdCtx->u.stAzElOffsetSet.iElOffset) / 100.0);
//         pstCtx->stCommandState.dAzOffset = ((double)pstCmdCtx->u.stAzElOffsetSet.iAzOffset) / 100.0;
//         pstCtx->stCommandState.dElOffset = ((double)pstCmdCtx->u.stAzElOffsetSet.iElOffset) / 100.0;
//         ((RES_AZ_EL_OFFSET_SET*)aucPayload)->chResult = (char)RESP_OK;
//         sendUdsResponse(pstUdsIo, pstCmdCtx->unCmd, uiReqId, aucPayload, sizeof(aucPayload));
//         break;

//     case CMD_POSITIONER_AZ_EL_SET:
//     case CMD_ACU_MODE_SELECT:
//     {
//         fprintf(stderr, "[ACU] Sending CMD=0x%04X to UART\n", pstCmdCtx->unCmd);        
//         buildAcuUartFrame(pstCtx, pstCmdCtx, puchUartSndData, &iRetSize);    
//         break;
//     }

//     default:
//         fprintf(stderr, "[ACU] Unsupported CMD\n");
//         break;
//     }
//     return iRetSize;
// }


static void commandEventCb(int iFd, short nEvent, void *pvData)
{
    (void)iFd;
    (void)nEvent;
    IO_CHANNEL *pstIoChannel = (IO_CHANNEL *)pvData;
    EVENT_ENGINE* pstEventEngine = pstIoChannel->pstEventEngine;
    IO_EVENT_TYPE eEventType = pstIoChannel->ePendingLogicEvent;
    unsigned char auchRecvBuffer[UDS_MAX_BUFFER_SIZE];
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
            unsigned char uchaSendBuf[UDS_MAX_BUFFER_SIZE];
            int iRecvLen = evbuffer_get_length(pstIoChannel->pstReadBuffer);            
            /* 최소 헤더도 없으면 중단 */
            if (iRecvLen < sizeof(FRAME_HEADER))
                break;
            fprintf(stderr, "Recv Size is %d\n", iRecvLen);
            memset(auchRecvBuffer, 0x00, sizeof(auchRecvBuffer));
            int iCopyLen = evbuffer_copyout(pstIoChannel->pstReadBuffer, auchRecvBuffer, iRecvLen);
            eErr = frameDecode(auchRecvBuffer, iCopyLen, FRAME_TYPE_REQUEST, &unCmd);
            if (eErr != FRAME_OK)
            {
                fprintf(stderr, "[UDS-SVR] frameDecode ERR: %s\n", frameErrToStr(eErr));
                int iOffset = findFrameHeader(auchRecvBuffer, iCopyLen);
                if (iOffset > 0) {
                    /* 앞부분 garbage 제거 */
                    evbuffer_drain(pstIoChannel->pstReadBuffer, iOffset);
                    fprintf(stderr, "[UDS-SVR] resync: drop %d bytes, retry decode\n", iOffset);
                } else if (iOffset == -2) {
                    /* STX half-match: 데이터 더 수신 */
                    evbuffer_drain(pstIoChannel->pstReadBuffer, iCopyLen - 1);
                    fprintf(stderr, "[UDS-SVR] STX half match, wait more data\n");
                } else {
                    /* STX 자체가 없음 → 전부 드랍 */
                    evbuffer_drain(pstIoChannel->pstReadBuffer, iCopyLen);
                    fprintf(stderr, "[UDS-SVR] no STX, drop all\n");
                }
                continue;
            }
            pstAcuCtrlCtx->unCmd = unCmd;
            unsigned int uiReqId;
            unsigned char auchCmdData[128];
            unsigned char auchResult[128];
            unsigned int uiUartDataSize;
            int iResultSize;
            int iFrameSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_REQUEST);
            evbuffer_drain(pstIoChannel->pstReadBuffer, iFrameSize);            
            evbuffer_remove(pstIoChannel->pstReadBuffer, &uiReqId, sizeof(unsigned int));
            pstEventEngine->uiRequestSeq = uiReqId;
            COMMAND_PATH eCommandPath = decideProcessingPath(unCmd);
            eErr = cmdDispatch(auchRecvBuffer, iCopyLen, auchCmdData);
            if (eErr != FRAME_OK){
                fprintf(stderr,"### %s():%d %s ###\n",__func__,__LINE__, frameErrToStr(eErr));
                continue;
            }
            applyCommand(pstAcuCtrlCtx, unCmd, auchCmdData, auchResult, &uiUartDataSize);
            if (eCommandPath == COMMAND_PATH_NONE) {
                fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
                MSG_ID stMsgId = { AC_CMD_RECEIVER, TC_UDS_CMD_CTRL };
                eErr = createCmdResponse(unCmd, auchCmdData, &stMsgId, auchResult);
                iResultSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_RESPONSE);
                fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
                evbuffer_add(pstIoChannel->pstWriteBuffer, auchResult, iResultSize);
                event_add(pstIoChannel->pstWriteEvent, NULL);
            } else if (eCommandPath == ACU_UART) {
                evbuffer_add(pstIoChannel->pstRequestBuffer, &eCommandPath, sizeof(eCommandPath));
                evbuffer_add(pstIoChannel->pstRequestBuffer, auchResult, uiUartDataSize);
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

static void recvAzElFromSensorFusion(int iFd, short nEvent, void* pvData)
{
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    IO_EVENT_TYPE eEventType = pstIoChannel->ePendingLogicEvent;

    unsigned char auchRecvBuffer[UDS_MAX_BUFFER_SIZE];
    unsigned short unCmd = 0;
    FRAME_ERR eErr;

    fprintf(stderr,"### %s():%d ###\n", __func__, __LINE__);

    switch (eEventType) {
    case IO_EVT_RX_DATA:
        while (1) {
            size_t tRecvLen = evbuffer_get_length(pstIoChannel->pstReadBuffer);
            /* 최소 헤더도 없으면 중단 */
            if (tRecvLen < sizeof(FRAME_HEADER))
                break;

            memset(auchRecvBuffer, 0x00, sizeof(auchRecvBuffer));
            int iCopyLen = evbuffer_copyout(pstIoChannel->pstReadBuffer,
                                            auchRecvBuffer, tRecvLen);
            /* frameDecode에 대한 처리가 완전한지 확인 필요*/
            eErr = frameDecode(auchRecvBuffer, iCopyLen, FRAME_TYPE_RESPONSE, &unCmd);
            if (eErr != FRAME_OK) {
                fprintf(stderr, "[UDS-SVR] frameDecode ERR: %s\n", frameErrToStr(eErr));
                int iOffset = findFrameHeader(auchRecvBuffer, iCopyLen);
                if (iOffset >= 0) {
                    /* 앞부분 garbage 제거 */
                    evbuffer_drain(pstIoChannel->pstReadBuffer, iOffset);
                    fprintf(stderr,"[UDS-SVR] resync: drop %d bytes, retry decode\n", iOffset);
                } else if (iOffset == -2) {
                    /* STX half-match: 데이터 더 수신 */
                    evbuffer_drain(pstIoChannel->pstReadBuffer, iCopyLen-1);
                    fprintf(stderr,"[UDS-SVR] STX half match, wait more data\n");
                } else {
                    /* STX 자체가 없음 → 전부 드랍 */
                    evbuffer_drain(pstIoChannel->pstReadBuffer, iCopyLen);
                    fprintf(stderr, "[UDS-SVR] no STX, drop all\n");
                }
                continue;
            }
            int iFrameSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_RESPONSE);
            fprintf(stderr,"### %s():%d %d ###\n", __func__, __LINE__, iFrameSize);
            /* === 프레임 소비 === */
            evbuffer_drain(pstIoChannel->pstReadBuffer, iFrameSize + sizeof(unsigned int));
            RES_AZ_EL_DATA* pstAzElData;
            pstAzElData = (RES_AZ_EL_DATA *)(auchRecvBuffer + sizeof(FRAME_HEADER));
            fprintf(stderr,"Azimuth: %lf, Elevation: %lf\n", pstAzElData->dAz, pstAzElData->dEl);

            //TODO ACU UART로 명령 전송 및 응답 수신
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
    
}

/* ============================================================
* Accept 콜백
* ============================================================ */
static void acceptUds3Cb(evutil_socket_t iListenFd, short nKindOfEvent, void* pvArg)
{
    (void)nKindOfEvent;
    EVENT_ENGINE* pstEventEngine = (EVENT_ENGINE *)pvArg;

    struct sockaddr_in stClientAddr;
    socklen_t uiClientLen = sizeof(stClientAddr);

    int iClientSock = accept(iListenFd, (struct sockaddr*)&stClientAddr, &uiClientLen);
    if (iClientSock < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            perror("[UDS_3_SVR] accept");
        return;
    }

    printf("[UDS_3_SVR] New client FD=%d\n", iClientSock);
    netSetNonblock(iClientSock);

    IO_CHANNEL *pstNewIo = eventSourceCreateWithBev(pstEventEngine, iClientSock,
        TYPE_TCP_SVR, ROLE_REQUESTER,
        NULL, NULL, recvAzElFromSensorFusion);
    if (!pstNewIo) {
        close(iClientSock);
        return;
    }      
    pstNewIo->iWorkerId = AC_AZ_EL_RECEIVER;
    pstNewIo->chFdCloseSet =  FD_OPENED;
}

static void acceptUds4Cb(evutil_socket_t iListenFd, short nKindOfEvent, void* pvArg)
{
    (void)nKindOfEvent;
    EVENT_ENGINE* pstEventEngine = (EVENT_ENGINE *)pvArg;

    struct sockaddr_in stClientAddr;
    socklen_t uiClientLen = sizeof(stClientAddr);

    int iClientSock = accept(iListenFd, (struct sockaddr*)&stClientAddr, &uiClientLen);
    if (iClientSock < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            perror("[UDS_4_SVR] accept");
        return;
    }

    printf("[UDS_4_SVR] New client FD=%d\n", iClientSock);
    netSetNonblock(iClientSock);

    IO_CHANNEL *pstNewIo = eventSourceCreateWithBev(pstEventEngine, iClientSock,
        TYPE_TCP_SVR, ROLE_REQUESTER,
        NULL, NULL, sendCurrentAzElValue);
    if (!pstNewIo) {
        close(iClientSock);
        return;
    }      
    pstNewIo->iWorkerId = AC_CURR_AZ_EL_SENDER;
    pstNewIo->chFdCloseSet =  FD_OPENED;
}


/* ============================================================
 * SIGINT
 * ============================================================ */
static void signalCb(evutil_socket_t sig, short events, void *pvArg)
{
    (void)sig;
    (void)events;
    EVENT_ENGINE *pstEventEngine = (EVENT_ENGINE *)pvArg;

    fprintf(stderr, "\n[ACU] SIGINT → shutdown\n");
    if (pstEventEngine->pstEventBase)
        event_base_loopexit(pstEventEngine->pstEventBase, NULL);
}

static void uds1ReconnectCb(evutil_socket_t fd, short nEvent, void *pvArg)
{
    (void)fd;
    (void)nEvent;

    EVENT_ENGINE *pstEventEngine = (EVENT_ENGINE *)pvArg;
    /* 이미 살아있으면 재접속 불필요 */
    IO_CHANNEL *pstImuTxIo = ioFindChannelByWorkerId(pstEventEngine, AC_CMD_RECEIVER);

    if (ioIsChannelAlive(pstImuTxIo))
        return;

    int iSock = netUdsCreateClient(UDS_1_PATH);
    if (iSock < 0) {
        fprintf(stderr, "[UDS#1] reconnect failed, retry later\n");
        return; /* 타이머는 계속 살아있음 */
    }

    fprintf(stderr, "[UDS#1] reconnected!\n");
    IO_CHANNEL *pstNewIo = eventSourceCreateWithBev(pstEventEngine, iSock,
                                                    TYPE_UDS_CLI, ROLE_REQUESTER,
                                                    NULL, NULL, commandEventCb);
    if (!pstNewIo) {
        close(iSock);
        return;
    }
    pstNewIo->chFdCloseSet =  FD_OPENED;
    pstNewIo->iWorkerId = AC_CMD_RECEIVER;
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
    struct timeval stRertyTimeOut = {3, 0};

    stEventEngine.pstEventBase = event_base_new();
    if (!stEventEngine.pstEventBase) {
        fprintf(stderr, "[ACU] event_base_new() failed\n");
        return EXIT_FAILURE;
    }
    eventEngineInit(&stEventEngine, 1);
    ACU_CTRL_CTX* pstAcuCtrlCtx = calloc(1, sizeof(ACU_CTRL_CTX));
    stEventEngine.pvSharedData = pstAcuCtrlCtx;

    /* UART open */
    if (uartOpen(&stUartCtx) < 0) {
        fprintf(stderr, "[ACU] uartOpen failed: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    pstIoChannel = eventSourceCreateWithBev(&stEventEngine, stUartCtx.iFd,
        TYPE_UART, ROLE_WORKER, NULL, uartWriteCallback, uartReadCallback);
    pstIoChannel->iWorkerId = ACU_UART;

    pstUdsRetryEvent = event_new(stEventEngine.pstEventBase,
                                 -1, EV_PERSIST | EV_TIMEOUT,
                                 uds1ReconnectCb, &stEventEngine);
    event_add(pstUdsRetryEvent, &stRertyTimeOut);

    /* Accept 이벤트 등록 */
    int iListenUds3Fd = netUdsCreateServer(UDS_3_PATH);
    if (iListenUds3Fd < 0) {
        fprintf(stderr, "[ACU_CTRL] netUdsCreateServer() failed\n");
        return EXIT_FAILURE;
    }
    pstEventAcceptUds3 = event_new(stEventEngine.pstEventBase, iListenUds3Fd, 
            EV_READ | EV_PERSIST, acceptUds3Cb, &stEventEngine);
    event_add(pstEventAcceptUds3, NULL);

    int iListenUds4Fd = netUdsCreateServer(UDS_4_PATH);
    if (iListenUds4Fd < 0) {
        fprintf(stderr, "[ACU_CTRL] netUdsCreateServer() failed\n");
        return EXIT_FAILURE;
    }
    pstEventAcceptUds4 = event_new(stEventEngine.pstEventBase, iListenUds4Fd,
            EV_READ | EV_PERSIST, acceptUds4Cb, &stEventEngine);
    event_add(pstEventAcceptUds4, NULL);

    /* ============================
    * [ADDED] Polling Timer
    * ============================ */
    // struct timeval tvPoll = {0, 100 * 1000}; // 100ms
    // struct event* pstPollEvt =
    //     event_new(stEventEngine.pstEventBase,
    //             -1, EV_PERSIST | EV_TIMEOUT,
    //             acuAzElPollingCb, pstAcuCtrlCtx);
    // event_add(pstPollEvt, &tvPoll);

    // /* ============================
    // * [ADDED] Alive Monitor Timer
    // * ============================ */
    // struct timeval tvAlive = {0, ACU_UART_MONITORING_MSEC * 1000};
    // struct event* pstAliveEvt =
    //     event_new(stEventEngine.pstEventBase,
    //             -1, EV_PERSIST | EV_TIMEOUT,
    //             uartAliveMonitorCb, pstAcuCtrlCtx);
    // event_add(pstAliveEvt, &tvAlive);



    pstSignalEvent = evsignal_new(stEventEngine.pstEventBase, SIGINT, signalCb, &stEventEngine);
    event_add(pstSignalEvent, NULL);

    event_base_dispatch(stEventEngine.pstEventBase);

    if (pstUdsRetryEvent){
        event_del(pstUdsRetryEvent);
        event_free(pstUdsRetryEvent);
        pstUdsRetryEvent = NULL;
    }

    if(pstEventAcceptUds3) {
        event_del(pstEventAcceptUds3);
        event_free(pstEventAcceptUds3);
        pstEventAcceptUds3 =  NULL;
    }

    if(pstEventAcceptUds4) {
        event_del(pstEventAcceptUds4);
        event_free(pstEventAcceptUds4);
        pstEventAcceptUds4 =  NULL;
    }  

    if (pstSignalEvent) {
        event_del(pstSignalEvent);
        event_free(pstSignalEvent);
        pstSignalEvent = NULL;
    }

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
