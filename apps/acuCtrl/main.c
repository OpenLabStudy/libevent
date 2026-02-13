#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "internal.h"

#include "uartConfig.h"
#include "netUds.h"
#include "netCore.h"
#include "ioChannelUtil.h"
#include "udsClientRuntime.h"
#include "udsServerRuntime.h"

int run(char *pchUartPath)
{
    ioIgnoreSigpipeOnce();

    EVENT_ENGINE stEventEngine;

    UART_CTX stUartCtx = {
        .pchDevPath     = pchUartPath,
        .iBaudrate      = 115200,
        .iFd            = -1,
        .iBackoffMsec   = 200
    };

    UDS_CLIENT_RUNTIME_CFG stRcvCmdUdsClnRuntimeCfg = {
        .chWorkerId         = (char)AC_RCV_CMD_FROM_TC,
        .chDstWorkerId      = (char)TC_SND_CMD_TO_CLN,
        .pchUdsPath         = UDS_1_PATH,
        .eRole              = ROLE_REQUESTER,
        .eType              = TYPE_UDS_CLI,
        .pchTag             = "AC_RCV_CMD_FROM_TC"
    };

    UDS_SERVER_RUNTIME_CFG stRcvSensorDataUdsSvrRuntimeCfg = {
        .pchUdsPath         = UDS_3_PATH,
        .chWorkerId         = (char)AC_RCV_AZ_EL_FROM_SF,
        .chDstWorkerId      = (char)SF_SND_AZ_EL_TO_AC,
        .eRole              = ROLE_REQUESTER,
        .eType              = TYPE_UDS_SVR,
        .pfWrite            = writeNone,
        .pfIoHandler        = recvAzElFromSensorFusion,
        .pchTag             = "AC_RCV_AZ_EL_FROM_SF"
    };

    UDS_SERVER_RUNTIME_CFG stSndAzElDataUdsSvrRuntimeCfg = {
        .pchUdsPath         = UDS_4_PATH,
        .chWorkerId         = (char)AC_SND_AZ_EL_TO_TC,
        .chDstWorkerId      = (char)TC_RCV_AZ_EL_FROM_AC,
        .eRole              = ROLE_REQUESTER,
        .eType              = TYPE_UDS_SVR,
        .pfWrite            = sendCurrAzElToTC,
        .pfIoHandler        = NULL,
        .pchTag             = "AC_SND_AZ_EL_TO_TC",
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

    if (uartOpen(&stUartCtx) < 0) {
        fprintf(stderr, "[ACU] uartOpen failed: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    IO_CHANNEL* pstIoChannel = eventSourceCreateWithBev(&stEventEngine, stUartCtx.iFd,
        TYPE_UART, ROLE_WORKER, NULL, uartWriteCallback, uartReadCallback);
    pstIoChannel->chWorkerId = (char)ACU_CTRL_UART;

    UDS_CLIENT_RUNTIME *pstRcvCmdUdsClnRuntime = udsClientRuntimeCreate(&stEventEngine,
        &stRcvCmdUdsClnRuntimeCfg, commandEventCb, NULL);

    UDS_SERVER_RUNTIME *pstAzElRcvSvr =
        udsServerRuntimeCreate(&stEventEngine, &stRcvSensorDataUdsSvrRuntimeCfg, pstAcuCtrlCtx);
    UDS_SERVER_RUNTIME *pstAzElSndSvr =
        udsServerRuntimeCreate(&stEventEngine, &stSndAzElDataUdsSvrRuntimeCfg, pstAcuCtrlCtx);

    /* polling timer(원본 주석 유지) */
    // struct timeval tvPoll = {0, 100 * 1000};
    // struct event *pstPollEvt = event_new(stEventEngine.pstEventBase, -1,
    //               EV_PERSIST | EV_TIMEOUT,
    //               acuAzElPollingCb, &stEventEngine);
    // event_add(pstPollEvt, &tvPoll);

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
