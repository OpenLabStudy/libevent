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
/* Pending command (UART 응답 매칭용)                                         */
/*  - "현재 1개 pending"만 처리 (필요시 큐로 확장)                            */
/* ========================================================================== */
typedef struct {
    int             bInUse;
    unsigned short  unCmd;
    unsigned int    uiReqId;
    IO_CHANNEL*     pstUdsIo;   /* 응답을 보낼 UDS 채널 */
} ACU_PENDING_CMD;

/* ========================================================================== */
/* ACU CONTEXT (전역 대체)                                                     */
/* ========================================================================== */
typedef struct {
    ACU_STATE        eState;
    ACU_PENDING_CMD  stPending;

    struct event*    pstTimeoutEvt; /* 200ms timer (reused) */
    IO_CHANNEL*      pstUartIo;     /* UART IO channel */

    int              iIsUartAlive;     /* 1: 정상, 0: 비정상 */

    COMMAND_STATE    stCommandState;
} ACU_CTRL_CTX;

#define ACU_UART 0x0400



/* ========================================================================== */
/* Helper: build UART frame from IPC command                                   */
/*  - TODO: ACU UART 프로토콜에 맞게 구현                                     */
/* ========================================================================== */
static int buildAcuUartFrame(const IPC_CMD_CTX* pstCmdCtx,
                             unsigned char* pOut,
                             unsigned int* pOutLen)
{
    int iSendLen = 0;
    if (!pstCmdCtx || !pOut || !pOutLen)
        return -1;

    /* payload (예시) */
    switch (pstCmdCtx->unCmd) {
    case CMD_POSITIONER_AZ_EL_SET: {
        fprintf(stderr, "ACU AZ/EL Set to AZ: %.2f, EL: %.2f\n",
                pstCmdCtx->u.stPositionerAzElSet.dAz,
                pstCmdCtx->u.stPositionerAzElSet.dEl);
        if(pstCmdCtx->u.stAcuMode.chAcuMode == POSITION_SLAVE){
            *pOutLen = moveAzElPosition(pstCmdCtx->u.stPositionerAzElSet.dAz, pstCmdCtx->u.stPositionerAzElSet.dEl, pOut);
        }
        break;
    }
    case CMD_ACU_MODE_SELECT:
        fprintf(stderr, "\nACU Mode Change to %s\n",
                pstCmdCtx->u.stAcuMode.chAcuMode == POSITION_SLAVE ? "POSITION MODE" : "RATE MODE");
        iSendLen = modeChange(pstCmdCtx->u.stAcuMode.chAcuMode, pOut);
        fprintf(stderr, "Total Send Length: %d, %02X\n", iSendLen, pstCmdCtx->u.stAcuMode.chAcuMode);
        for(int i=1; i<=iSendLen; i++){
            if(i&16 == 0)
                fprintf(stderr,"\n");
            fprintf(stderr,"%02x ", pOut[i-1]);
        }
        break;
    default:
        break;
    }

    /* checksum 등... */

    *pOutLen = 16; /* 예시 */
    return 0;
}

/* ========================================================================== */
/* Helper: parse UART response                                                 */
/*  - TODO: ACU UART 응답을 파싱해서 OK/FAIL 코드 반환                        */
/* ========================================================================== */
static unsigned char parseAcuUartResponse(const unsigned char* pBuf, int iLen,
                                          unsigned short unExpectedCmd)
{
    (void)unExpectedCmd;

    if (!pBuf || iLen <= 0)
        return RESP_FAIL;

    /* 예시: 응답의 특정 바이트가 0x01이면 OK로 가정 */
    /* 실제 프로토콜에 맞게 구현하세요. */
    if (iLen >= 1 && pBuf[0] == 0x06)
        return RESP_OK;

    return RESP_FAIL;
}

/* ========================================================================== */
/* UART Timeout Callback (200ms)                                               */
/*  - pending cmd에 대해 TIMEOUT 응답 전송                                    */
/* ========================================================================== */
static void acuUartTimeoutCb(evutil_socket_t fd, short what, void* arg)
{
    (void)fd; (void)what;

    ACU_CTRL_CTX* pstCtx = (ACU_CTRL_CTX*)arg;
    if (!pstCtx)
        return;

    if (pstCtx->eState != ACU_STATE_WAIT_RESPONSE || !pstCtx->stPending.bInUse) {
        return;
    }

    fprintf(stderr, "[ACU] UART TIMEOUT CMD=0x%04X\n", pstCtx->stPending.unCmd);

    /* build payload with TIMEOUT */
    unsigned char aucPayload[UDS_MAX_BUFFER_SIZE];
    memset(aucPayload, 0, sizeof(aucPayload));

    switch (pstCtx->stPending.unCmd) {
    case CMD_POSITIONER_AZ_EL_SET:
        ((RES_POSITIONER_AZ_EL_SET*)aucPayload)->chResult = (char)RESP_TIMEOUT;
        break;
    case CMD_ACU_MODE_SELECT:
        ((RES_ACU_MODE*)aucPayload)->chResult = (char)RESP_TIMEOUT;
        break;
    default:
        /* not expected */
        break;
    }

    /* send UDS response now */
    sendUdsResponse(pstCtx->stPending.pstUdsIo, pstCtx->stPending.unCmd, 
                       pstCtx->stPending.uiReqId, aucPayload, sizeof(aucPayload));

    /* clear pending */
    pstCtx->stPending.bInUse = 0;
    pstCtx->stPending.unCmd = 0;
    pstCtx->stPending.uiReqId = 0;
    pstCtx->stPending.pstUdsIo = NULL;

    pstCtx->eState = ACU_STATE_IDLE;
    pstCtx->iIsUartAlive = 0;
}


/* ========================================================================== */
/* Send UART + register pending                                                */
/* ========================================================================== */
static int acuSendUartAndPend(ACU_CTRL_CTX* pstCtx, const IPC_CMD_CTX* pstCmdCtx,
                              IO_CHANNEL* pstUdsIo, unsigned int uiReqId)
{
    if (!pstCtx || !pstCmdCtx ||!pstUdsIo ||!pstCtx->pstUartIo)
        return -1;

    if (pstCtx->eState != ACU_STATE_IDLE) {
        return -1;
    }

    if (pstCtx->stPending.bInUse) {
        return -1;
    }


    if(pstCtx->iIsUartAlive == 0){
        //UART가 정상적으로 연결되었는지 확인 필요
        fprintf(stderr, "[ACU] UART not alive, cannot send CMD=0x%04X\n", pstCmdCtx->unCmd);
        return -1;
    }

    unsigned char aucFrame[256];
    unsigned int  uiFrameLen = 0;
    memset(aucFrame, 0, sizeof(aucFrame));
    if (buildAcuUartFrame(pstCmdCtx, aucFrame, &uiFrameLen) < 0 || uiFrameLen == 0) {
        return -1;
    }
    fprintf(stderr, "\n### %s():%d ###\n", __func__, __LINE__);

    /* pending 등록 */
    pstCtx->stPending.bInUse   = 1;
    pstCtx->stPending.unCmd    = pstCmdCtx->unCmd;
    pstCtx->stPending.uiReqId  = uiReqId;
    pstCtx->stPending.pstUdsIo = pstUdsIo;

    /* UART write queue */
    evbuffer_add(pstCtx->pstUartIo->pstWriteBuffer, aucFrame, uiFrameLen);
    event_active(pstCtx->pstUartIo->pstWriteEvent, EV_WRITE, 0);

    /* 200ms timeout start (reuse event) 응답이 없는경우 처리 필요*/
    if (pstCtx->pstTimeoutEvt) {
        struct timeval tv = {0, 200 * 1000};
        evtimer_del(pstCtx->pstTimeoutEvt);
        evtimer_add(pstCtx->pstTimeoutEvt, &tv);
    }

    pstCtx->eState = ACU_STATE_WAIT_RESPONSE;
    return 0;
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
            fprintf(stderr, "[ACU] UART RX but no pending\n");
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
            int iSplitCnt = splitAcuDataString(aucUartBuf, ';', chSplitData, 2);
            if(iSplitCnt == 2){
                dAz = atof(chSplitData[0]);
                dEl = atof(chSplitData[1]);
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

//* ========================================================================== */
/* ACU Command Execute                                                        */
/* ========================================================================== */
static void executeIpcCommand(const IPC_CMD_CTX* pstCmdCtx, IO_CHANNEL* pstUdsIo, unsigned int uiReqId)
{
    EVENT_ENGINE* pstEngine = pstUdsIo->pstEventEngine;
    ACU_CTRL_CTX* pstCtx = (ACU_CTRL_CTX*)pstEngine->pvSharedData;

    unsigned char aucPayload[UDS_MAX_BUFFER_SIZE];
    memset(aucPayload, 0, sizeof(aucPayload));
    switch (pstCmdCtx->unCmd)
    {
    case CMD_ID_INFO: 
        fprintf(stderr, "### CMD_ID_INFO RESPONSE ###\n");
        ((RES_POSITIONER_DEG_SEND*)aucPayload)->chResult = (char)pstUdsIo->iWorkerId;
        // Fill in the payload with ID info as needed
        sendUdsResponse(pstUdsIo, pstCmdCtx->unCmd, uiReqId, aucPayload, sizeof(aucPayload));
        break;
        
    case CMD_POSITIONER_DEG_SEND:
        fprintf(stderr, "ACU AZ/EL Send %s\n", pstCmdCtx->u.stPositionerAzElSendCtrl.chSendOnOff == AZ_EL_SEND_ON ? "ON" :"OFF");
        pstCtx->stCommandState.chSendOnOff = pstCmdCtx->u.stPositionerAzElSendCtrl.chSendOnOff;
        ((RES_POSITIONER_DEG_SEND*)aucPayload)->chResult = (char)RESP_OK;
        sendUdsResponse(pstUdsIo, pstCmdCtx->unCmd, uiReqId, aucPayload, sizeof(aucPayload));
        break;

    case CMD_AZ_EL_OFFSET_SET:
    //f0 f0 00 00 00 10 10 b1 00 00 10 00 00 00 14 00 00 00 17 fc ff ff 01 00 00 00
    //0.2, 0.23
    //f0 f0 00 00 00 10 10 b1 00 00 10 00 00 00 0c 00 00 00 62 3f ff ff 02 00 00 00 
    //0.123, 0.987
        fprintf(stderr, "ACU AZ Offset=%.2f EL Offset=%.2f\n",
            ((double)pstCmdCtx->u.stAzElOffsetSet.iAzOffset) / 100.0,
            ((double)pstCmdCtx->u.stAzElOffsetSet.iElOffset) / 100.0);
        pstCtx->stCommandState.dAzOffset = ((double)pstCmdCtx->u.stAzElOffsetSet.iAzOffset) / 100.0;
        pstCtx->stCommandState.dElOffset = ((double)pstCmdCtx->u.stAzElOffsetSet.iElOffset) / 100.0;
        ((RES_AZ_EL_OFFSET_SET*)aucPayload)->chResult = (char)RESP_OK;
        sendUdsResponse(pstUdsIo, pstCmdCtx->unCmd, uiReqId, aucPayload, sizeof(aucPayload));
        break;

    case CMD_POSITIONER_AZ_EL_SET:
    case CMD_ACU_MODE_SELECT:
        fprintf(stderr, "[ACU] Sending CMD=0x%04X to UART\n", pstCmdCtx->unCmd);
        if (acuSendUartAndPend(pstCtx, pstCmdCtx, pstUdsIo, uiReqId) < 0) {
            /* busy or internal error => 즉시 실패 응답 */
            if (pstCmdCtx->unCmd == CMD_POSITIONER_AZ_EL_SET){                
                ((RES_POSITIONER_AZ_EL_SET*)aucPayload)->chResult = (char)RESP_OK;
            } else {
                ((RES_ACU_MODE*)aucPayload)->chResult = (char)RESP_OK;
            }

            sendUdsResponse(pstUdsIo, pstCmdCtx->unCmd, uiReqId, aucPayload, sizeof(aucPayload));
        }
        break;    

    default:
        fprintf(stderr, "[ACU] Unsupported CMD\n");
        break;
    }
}

static void commandEventCb(int iFd, short nEvent, void *pvData)
{
    (void)iFd;
    (void)nEvent;
    IO_CHANNEL *pstIoChannel = (IO_CHANNEL *)pvData;
    IO_EVENT_TYPE eEventType = pstIoChannel->ePendingLogicEvent;

    unsigned char auchRecvBuffer[UDS_MAX_BUFFER_SIZE];
    unsigned short unCmd = 0;
    FRAME_ERR eErr;
    IPC_CMD_CTX stCmdCtx;

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
            unsigned char auchResult[UDS_MAX_BUFFER_SIZE];
            int iRecvLen = evbuffer_get_length(pstIoChannel->pstReadBuffer);            
            /* 최소 헤더도 없으면 중단 */
            if (iRecvLen < sizeof(FRAME_HEADER))
                break;
            fprintf(stderr, "Recv Size is %d\n", iRecvLen);
            memset(auchRecvBuffer, 0x00, sizeof(auchRecvBuffer));
            int iCopyLen = evbuffer_copyout(pstIoChannel->pstReadBuffer, auchRecvBuffer, iRecvLen);
            fprintf(stderr,"### %s():%d ###\n", __func__,__LINE__);
            for(int i=1; i<=iCopyLen; i++){
                if(i&16 == 0)
                    fprintf(stderr,"\n");
                fprintf(stderr,"%02x ", auchRecvBuffer[i-1]);
            }
            /* frameDecode에 대한 처리가 완전한지 확인 필요*/
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
            fprintf(stderr, "Recv CMD is %d\n", unCmd);
            int iFrameSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_REQUEST);

            /* consume frame(+reqId) */
            evbuffer_drain(pstIoChannel->pstReadBuffer, iFrameSize + (int)sizeof(unsigned int));

            unsigned int uiReqId = 0;
            memcpy(&uiReqId, auchRecvBuffer + iFrameSize, sizeof(unsigned int));

            /* parse payload -> IPC_CMD_CTX */
            if (ipcHandleCommand(unCmd, auchRecvBuffer + sizeof(FRAME_HEADER), &stCmdCtx) < 0) {
                fprintf(stderr, "[ACU] ipcHandleCommand failed CMD=0x%04X\n", unCmd);
                /* 최소한의 즉시 실패 응답 */
                sendUdsResponse(pstIoChannel, unCmd, uiReqId, NULL, 0);
                continue;
            }

            /* execute command (immediate or deferred) */
            executeIpcCommand(&stCmdCtx, pstIoChannel, uiReqId);

            /* NOTE:
             * - immediate cmd: executeIpcCommand() sends UDS response here
             * - deferred cmd: UDS response will be sent in uartReadCallback() or timeout cb
             */
        }
        break;
    default:
        break;
    }
    pstIoChannel->ePendingLogicEvent = IO_EVENT_NONE;
}

static void recvControlAzElFromSensorFusion(int iFd, short nEvent, void* pvData)
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

    eventSourceCreateWithBev(pstEventEngine, iClientSock,
        TYPE_TCP_SVR, ROLE_REQUESTER,
        NULL, NULL, recvControlAzElFromSensorFusion);
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

    eventSourceCreateWithBev(pstEventEngine, iClientSock,
        TYPE_TCP_SVR, ROLE_REQUESTER,
        NULL, NULL, sendCurrentAzElValue);
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
    IO_CHANNEL *pstImuTxIo = ioFindChannelByWorkerId(pstEventEngine, UDS_1_ACU_CONTROLLER);

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
    pstNewIo->iWorkerId = UDS_1_ACU_CONTROLLER;
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
    struct timeval stRertyTimeOut = {1, 0};

    stEventEngine.pstEventBase = event_base_new();
    if (!stEventEngine.pstEventBase) {
        fprintf(stderr, "[ACU] event_base_new() failed\n");
        return EXIT_FAILURE;
    }
    eventEngineInit(&stEventEngine);
    ACU_CTRL_CTX* pstAcuCtrlCtx = calloc(1, sizeof(ACU_CTRL_CTX));
    pstAcuCtrlCtx->iIsUartAlive = 0;
    stEventEngine.pvSharedData = pstAcuCtrlCtx;

    /* UART open */
    if (uartOpen(&stUartCtx) < 0) {
        fprintf(stderr, "[ACU] uartOpen failed: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    pstAcuCtrlCtx->iIsUartAlive = 1;
    pstIoChannel = eventSourceCreateWithBev(&stEventEngine, stUartCtx.iFd,
        TYPE_UART, ROLE_REQUESTER, NULL, NULL, uartReadCallback);
    pstIoChannel->iWorkerId = ACU_UART;
    pstAcuCtrlCtx->pstUartIo = pstIoChannel;

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
