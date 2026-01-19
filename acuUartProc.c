#include "uartConfig.h"
#include "ipcUtil.h"
#include "acuUartProc.h"
#include "acuCtrl.h"

#define ACU_UART_MONITORING_MSEC 400

/* ========================================================================== */
/* Helper: parse UART response                                                 */
/*  - TODO: ACU UART 응답을 파싱해서 OK/FAIL 코드 반환                        */
/* ========================================================================== */
static unsigned char parseAcuUartResponse(const unsigned char* pBuf, int iLen,
                                          unsigned short unExpectedCmd)
{
    (void)unExpectedCmd;
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    if (!pBuf || iLen <= 0)
        return RESP_FAIL;

    /* 예시: 응답의 특정 바이트가 0x01이면 OK로 가정 */
    /* 실제 프로토콜에 맞게 구현하세요. */ 
    if (iLen >= 1 && pBuf[0] == 0x06)
        return RESP_OK;

    return RESP_FAIL;
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

    switch (eEventType)
    {
    case IO_EVT_RX_DATA: {
        pstCtx->iIsUartAlive = 1;
        int iLen = evbuffer_remove(pstUartIo->pstReadBuffer, aucUartBuf, sizeof(aucUartBuf));
        if (iLen <= 0)
            break;

        fprintf(stderr, "[ACU] UART RX %d bytes\n", iLen);

        /* pending 없으면 버리고 끝 */
        if (!pstCtx->stPending.bInUse || pstCtx->eState != ACU_STATE_WAIT_RESPONSE) {
            fprintf(stderr, "[ACU] UART RX but no pending bInUse:%d eState:%d\n", pstCtx->stPending.bInUse, pstCtx->eState);
            break;
        }

        /* timeout stop */
        if (pstCtx->pstTimeoutEvt) {
            evtimer_del(pstCtx->pstTimeoutEvt);
        }

        /* parse response -> OK/FAIL */
        unsigned char ucResult = parseAcuUartResponse(aucUartBuf, iLen, pstCtx->stPending.unCmd);

        /* build payload and send UDS response */
        unsigned char aucPayload[UDS_MAX_BUFFER_SIZE];
        memset(aucPayload, 0, sizeof(aucPayload));

        switch (pstCtx->stPending.unCmd) {
        case CMD_POSITIONER_AZ_EL_SET:
            ((RES_POSITIONER_AZ_EL_SET*)aucPayload)->chResult = (ucResult == RESP_OK) ? 0x01 : 0x00;
            break;
        case CMD_ACU_MODE_SELECT:
            ((RES_ACU_MODE*)aucPayload)->chResult = (ucResult == RESP_OK) ? 0x01 : 0x00;
            break;
        case CMD_GET_AZ_EL_DATA:
        {
            double dAz, dEl;
            char *chSplitData[8];
            pstCtx->iIsSendCommand = 0;
            gettimeofday(&pstCtx->stLastAzElRxTime, NULL);
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

        /* clear pending */
        pstCtx->stPending.bInUse = 0;
        pstCtx->stPending.unCmd = 0;
        pstCtx->stPending.uiReqId = 0;
        pstCtx->stPending.pstUdsIo = NULL;

        pstCtx->eState = ACU_STATE_IDLE;
        break;
    }

    case IO_EVT_CHANNEL_CLOSED:
    case IO_EVT_ERROR:
        pstCtx->iIsUartAlive = 0;
        fprintf(stderr, "[ACU] UART channel closed fd=%d\n", pstUartIo->iFd);
        event_active(pstUartIo->pstShutdownEvent, 0, 0);
        break;

    default:
        break;
    }

    pstUartIo->ePendingLogicEvent = IO_EVENT_NONE;
}


/* ========================================================================== */
/* Helper: build UART frame from IPC command                                   */
/*  - TODO: ACU UART 프로토콜에 맞게 구현                                     */
/* ========================================================================== */
static int buildAcuUartFrame(ACU_CTRL_CTX* pstCtx, const IPC_CMD_CTX* pstCmdCtx,
                             unsigned char* pOut, unsigned int* pOutLen)
{
    int iSendLen = 0;
    if (!pstCmdCtx || !pOut || !pOutLen)
        return -1;

    /* payload (예시) */
    switch (pstCmdCtx->unCmd) {
    case CMD_GET_CURRENT_AZ_EL_SET: {
        fprintf(stderr, "\nGet ACU Current AZ, EL Value\n");
            *pOutLen = readAzElFromAcu(pOut);
        break;
    }
    case CMD_POSITIONER_AZ_EL_SET: {
        fprintf(stderr, "\nACU Mode is %s\n",
            pstCtx->stCommandState.chAcuMode == POSITION ? "POSITION MODE" : "RATE MODE");
        fprintf(stderr, "%s():%d ACU AZ/EL Set to AZ: %.2f, EL: %.2f\n",__func__,__LINE__,
                pstCmdCtx->u.stPositionerAzElSet.dAz,
                pstCmdCtx->u.stPositionerAzElSet.dEl);
        if(pstCtx->stCommandState.chAcuMode == POSITION){
            *pOutLen = moveAzElPosition(pstCmdCtx->u.stPositionerAzElSet.dAz, pstCmdCtx->u.stPositionerAzElSet.dEl, pOut);
        }
        break;
    }
    case CMD_ACU_MODE_SELECT:
        fprintf(stderr, "\nACU Mode Change to %s\n",
                pstCmdCtx->u.stAcuMode.chAcuMode == POSITION ? "POSITION MODE" : "RATE MODE");
                pstCtx->stCommandState.chAcuMode = pstCmdCtx->u.stAcuMode.chAcuMode;
        iSendLen = modeChange(pstCmdCtx->u.stAcuMode.chAcuMode, pOut);
        fprintf(stderr, "Total Send Length: %d, %02X\n", iSendLen, pstCmdCtx->u.stAcuMode.chAcuMode);
        break;
        
    default:
        break;
    }

    /* checksum 등... */

    *pOutLen = 16; /* 예시 */
    return 0;
}

/* ========================================================================== */
/* Send UART + register pending                                                */
/* ========================================================================== */
int acuSendUartAndPend(ACU_CTRL_CTX* pstCtx, const IPC_CMD_CTX* pstCmdCtx,
                              IO_CHANNEL* pstUdsIo, unsigned int uiReqId)
{
    if (!pstCtx || !pstCmdCtx ||!pstCtx->pstUartIo){
        fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
        return -1;
    }

    if (pstCtx->eState != ACU_STATE_IDLE) {
        fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
        return -1;
    }

    if (pstCtx->stPending.bInUse) {
        fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
        return -1;
    }

    if(pstCtx->iIsUartAlive == 0){
        fprintf(stderr, "[ACU] UART not alive, cannot send CMD=0x%04X\n", pstCmdCtx->unCmd);
        return -1;
    }

    unsigned char aucFrame[256];
    unsigned int  uiFrameLen = 0;
    memset(aucFrame, 0, sizeof(aucFrame));
    if (buildAcuUartFrame(pstCtx, pstCmdCtx, aucFrame, &uiFrameLen) < 0 || uiFrameLen == 0) {
        fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
        return -1;
    }

    /* pending 등록 */
    pstCtx->stPending.bInUse   = 1;
    pstCtx->stPending.unCmd    = pstCmdCtx->unCmd;
    pstCtx->stPending.uiReqId  = uiReqId;
    pstCtx->stPending.pstUdsIo = pstUdsIo;

    /* UART write queue */
    evbuffer_add(pstCtx->pstUartIo->pstWriteBuffer, aucFrame, uiFrameLen);
    event_active(pstCtx->pstUartIo->pstWriteEvent, EV_WRITE, 0);

    /* 200ms timeout start (reuse event) 응답이 없는경우 처리*/
    if (pstCtx->pstTimeoutEvt) {
        struct timeval tv = {0, 200 * 1000};
        evtimer_del(pstCtx->pstTimeoutEvt);
        evtimer_add(pstCtx->pstTimeoutEvt, &tv);
    }

    pstCtx->eState = ACU_STATE_WAIT_RESPONSE;
    return 0;
}


/* ============================================================
 * [ADDED] UART Alive Monitor (Polling 기반)
 * ============================================================ */
static void uartAliveMonitorCb(int iFd, short nEvent, void *pvData)
{
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    (void)iFd;
    (void)nEvent;
    ACU_CTRL_CTX* pstCtx = pvData;

    struct timeval now;
    gettimeofday(&now, NULL);

    long diffMs =
        (now.tv_sec  - pstCtx->stLastAzElRxTime.tv_sec) * 1000 +
        (now.tv_usec - pstCtx->stLastAzElRxTime.tv_usec) / 1000;

    if (diffMs >= ACU_UART_MONITORING_MSEC) {
        if (pstCtx->iIsUartAlive) {
            fprintf(stderr, "[ACU] AZ/EL polling timeout (%ld ms)\n", diffMs);
        }
        pstCtx->iIsUartAlive = 0;
        pstCtx->iIsSendCommand = 0;
    }
}


/* ============================================================
 * [ADDED] 100ms AZ/EL Polling Timer Callback
 * ============================================================ */
static void acuAzElPollingCb(int iFd, short nEvent, void *pvData)
{
    (void)iFd;
    (void)nEvent;
    ACU_CTRL_CTX* pstCtx = (ACU_CTRL_CTX*)pvData;

    if (!pstCtx)
        return;

    /* UART 죽은 상태면 polling 중단 */
    // if (!pstCtx->iIsUartAlive && pstCtx->iIsSendCommand)
    //     return;

    unsigned char aucFrame[256];
    unsigned int  uiFrameLen = 0;
    memset(aucFrame, 0, sizeof(aucFrame));
    IPC_CMD_CTX stCmdCtx={0,};
    stCmdCtx.unCmd = CMD_GET_CURRENT_AZ_EL_SET;

    fprintf(stderr, "[ACU] Sending CMD=0x%04X to UART\n", stCmdCtx.unCmd);
    if (acuSendUartAndPend(pstCtx, &stCmdCtx, NULL, 0) < 0) {        
        pstCtx->iIsSendCommand = 1;   // [ADDED]
        pstCtx->eState = ACU_STATE_WAIT_RESPONSE;
    }    
}

void createUartEventEngine(EVENT_ENGINE *pstEventEngine, char *pchUartPath)
{
    IO_CHANNEL *pstIoChannel = NULL;
    ACU_CTRL_CTX* pstAcuCtrlCtx = NULL;
    UART_CTX stUartCtx = {
        .pchDevPath     = pchUartPath,
        .iBaudrate      = 115200,
        .iFd            = -1,
        .iBackoffMsec   = 200
    };

    /* UART open */
    if (uartOpen(&stUartCtx) < 0) {
        fprintf(stderr, "[ACU] uartOpen failed: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    pstAcuCtrlCtx->iIsUartAlive = 0;
    pstIoChannel = eventSourceCreateWithBev(pstEventEngine, stUartCtx.iFd,
        TYPE_UART, ROLE_REQUESTER, NULL, NULL, uartReadCallback);
    pstIoChannel->iWorkerId = ACU_UART;

    /* ============================
    * [ADDED] Polling Timer
    * ============================ */
    struct timeval tvPoll = {0, 100 * 1000}; // 100ms
    struct event* pstPollEvt = event_new(pstEventEngine->pstEventBase,
                -1, EV_PERSIST | EV_TIMEOUT, acuAzElPollingCb, pstAcuCtrlCtx);
    event_add(pstPollEvt, &tvPoll);

    /* ============================
    * [ADDED] Alive Monitor Timer
    * ============================ */
    struct timeval tvAlive = {0, ACU_UART_MONITORING_MSEC * 1000};
    struct event* pstAliveEvt = event_new(pstEventEngine->pstEventBase,
                -1, EV_PERSIST | EV_TIMEOUT, uartAliveMonitorCb, pstAcuCtrlCtx);
    event_add(pstAliveEvt, &tvAlive);
}