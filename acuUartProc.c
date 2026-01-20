#include "uartConfig.h"
#include "ipcUtil.h"
#include "acuUartProc.h"
#include "acuCtrl.h"

static void uartWriteCallback(int iFd, short nEvent, void *pvData)
{
    (void)nEvent;
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    unsigned char auchWriteBuffer[2048];
    int iEvBufferDataSize;
    int iReqId;
    int iWriteSize;
    iEvBufferDataSize = evbuffer_get_length(pstIoChannel->pstWriteBuffer);
    if (iEvBufferDataSize == 0) {
        event_del(pstIoChannel->pstWriteEvent);
        return;
    }    
    iWriteSize = evbuffer_remove(pstIoChannel->pstWriteBuffer, auchWriteBuffer, iEvBufferDataSize-sizeof(iReqId));
    evbuffer_remove(pstIoChannel->pstWriteBuffer, &iReqId, sizeof(iReqId));
    
    iWriteSize = write(pstIoChannel->iFd, auchWriteBuffer, iWriteSize);
    if (iWriteSize <= 0) {
        perror("write");
        return;
    }
    fprintf(stderr,"\n");
    fprintf(stderr,"### %s():%d Write Size:%d ###\n", __func__,__LINE__, iWriteSize);
    for(int i=1; i<=iWriteSize; i++){
        if(i&16 == 0)
            fprintf(stderr,"\n");
        fprintf(stderr,"%02x ", auchWriteBuffer[i-1]);
    }  
    fprintf(stderr,"\n");
    if (evbuffer_get_length(pstIoChannel->pstWriteBuffer) == 0)
        event_del(pstIoChannel->pstWriteEvent);
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
    EVENT_ENGINE* pstEngine = pstUartIo->pstEventEngine;
    IO_EVENT_TYPE eEventType = pstUartIo->ePendingLogicEvent;
    
    unsigned char aucUartBuf[2048];

    switch (eEventType)
    {
    case IO_EVT_RX_DATA: {
        pstUartIo->chFdCloseSet = FD_OPENED;
        int iLen = evbuffer_remove(pstUartIo->pstReadBuffer, aucUartBuf, sizeof(aucUartBuf));
        if (iLen <= 0)
            break;
        int iReqId=0;
        eventEngineHandleWorkerResponse(pstUartIo->pstEventEngine, pstUartIo,
                iReqId, aucUartBuf, iLen);

        fprintf(stderr, "[ACU] UART RX %d bytes\n", iLen);
        break;
    }

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


void createUartEventEngine(EVENT_ENGINE *pstEventEngine, char *pchUartPath)
{
    IO_CHANNEL *pstIoChannel = NULL;
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
    pstIoChannel = eventSourceCreateWithBev(pstEventEngine, stUartCtx.iFd,
        TYPE_UART, ROLE_WORKER, NULL, uartWriteCallback, uartReadCallback);
    pstIoChannel->iWorkerId = ACU_UART;
}