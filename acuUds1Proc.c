#include "ipcUtil.h"
#include "acuCtrl.h"

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
        fprintf(stderr, "ACU AZ Offset=%.2f EL Offset=%.2f\n",
            ((double)pstCmdCtx->u.stAzElOffsetSet.iAzOffset) / 100.0,
            ((double)pstCmdCtx->u.stAzElOffsetSet.iElOffset) / 100.0);
        pstCtx->stCommandState.dAzOffset = ((double)pstCmdCtx->u.stAzElOffsetSet.iAzOffset) / 100.0;
        pstCtx->stCommandState.dElOffset = ((double)pstCmdCtx->u.stAzElOffsetSet.iElOffset) / 100.0;
        ((RES_AZ_EL_OFFSET_SET*)aucPayload)->chResult = (char)RESP_OK;
        sendUdsResponse(pstUdsIo, pstCmdCtx->unCmd, uiReqId, aucPayload, sizeof(aucPayload));
        break;

    case CMD_POSITIONER_AZ_EL_SET:
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




static void uds1ReconnectCb(evutil_socket_t fd, short nEvent, void *pvArg)
{
    (void)fd;
    (void)nEvent;

    EVENT_ENGINE *pstEventEngine = (EVENT_ENGINE *)pvArg;
    /* 이미 살아있으면 재접속 불필요 */
    IO_CHANNEL *pstUds1Io = ioFindChannelByWorkerId(pstEventEngine, UDS_1_ACU_CONTROLLER);

    if (ioIsChannelAlive(pstUds1Io))
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

void createUds1EventEngine(EVENT_ENGINE *pstEventEngine)
{
    struct event* pstUdsRetryEvent = NULL;
    struct timeval stRertyTimeOut = {1, 0};

    pstUdsRetryEvent = event_new(pstEventEngine->pstEventBase,
                                 -1, EV_PERSIST | EV_TIMEOUT,
                                 uds1ReconnectCb, pstEventEngine);
    event_add(pstUdsRetryEvent, &stRertyTimeOut);
}
