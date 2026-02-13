#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "control.h"
#include "core.h"

#include "eventEngine.h"
#include "ioChannelUtil.h"
#include "udsClientRuntime.h"
#include "uartConfig.h"

static void applyActions(IO_CHANNEL* pstUdsIo,
                         IO_CHANNEL* pstUartIo,
                         EVENT_ENGINE* pstEventEngine,
                         ACU_ACTION_LIST* pstAcuActionList)
{
    for (int i = 0; i < pstAcuActionList->iCount; ++i) {
        ACU_ACTION* pstAcuAction = &pstAcuActionList->stAcuAction[i];

        switch (pstAcuAction->eAcuActionType) {
        case ACU_ACT_UART_REQUEST:
            evbuffer_add(pstUartIo->pstRequestBuffer,
                         pstAcuAction->uchUartData, pstAcuAction->uiUartDataLen);
            event_active(pstUartIo->pstRequestEvent, 0, 0);
            break;

        case ACU_ACT_ENGINE_WORKER_RESPONSE:
            eventEngineHandleWorkerResponse(pstEventEngine, pstUartIo,
                                            pstAcuAction->uiReqSeq,
                                            pstAcuAction->uchFrameBuffer, pstAcuAction->uiFrameLen);
            break;

        default:
            break;
        }
    }
}

static void uartReadCallback(int iFd, short nEvent, void* pvData)
{
    (void)iFd; (void)nEvent;

    IO_CHANNEL* pstUartIo = (IO_CHANNEL*)pvData;
    EVENT_ENGINE* pstEventEngine = pstUartIo->pstEventEngine;
    ACU_CTRL_CTX* pstAcuCtrlCtx =  (ACU_CTRL_CTX*)pstEventEngine->pvSharedData;

    unsigned char achBuf[2048];
    int iDataSize = evbuffer_remove(pstUartIo->pstReadBuffer, achBuf, sizeof(achBuf));

    if (iDataSize <= 0)
        return;

    ACU_ACTION_LIST stAcuActionList;
    acuCore_onUartRx(pstAcuCtrlCtx, achBuf, iDataSize, &stAcuActionList);
    applyActions(NULL, pstUartIo, pstEventEngine, &stAcuActionList);
}

int run(char* uartPath)
{
    EVENT_ENGINE engine;
    memset(&engine, 0, sizeof(engine));

    engine.pstEventBase = event_base_new();

    ACU_CTRL_CTX* ctx =
        calloc(1, sizeof(ACU_CTRL_CTX));
    acuCore_init(ctx);
    engine.pvSharedData = ctx;

    UART_CTX uartCtx = {
        .pchDevPath = uartPath,
        .iBaudrate = 115200
    };

    uartOpen(&uartCtx);

    IO_CHANNEL* uartIo =
        eventSourceCreateWithBev(&engine,
                                 uartCtx.iFd,
                                 TYPE_UART,
                                 ROLE_WORKER,
                                 NULL,
                                 NULL,
                                 uartReadCallback);

    event_base_dispatch(engine.pstEventBase);

    return 0;
}

#ifndef GOOGLE_TEST
int main(int argc, char* argv[])
{
    if (argc < 2) {
        printf("Usage: %s /dev/ttyUSB0\n", argv[0]);
        return -1;
    }

    return run(argv[1]);
}
#endif
