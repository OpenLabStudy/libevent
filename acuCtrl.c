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
#include "acuCtrl.h"

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


/* ============================================================
 * Main
 * ============================================================ */
int run(char *pchUartPath)
{
    // 이미 끊어진 소켓에 write() 했을 때 프로세스가 즉사(SIGPIPE)하는 것을 막는다.
    ioIgnoreSigpipeOnce();
    EVENT_ENGINE stEventEngine;
    ACU_CTRL_CTX* pstAcuCtrlCtx;
    struct event* pstSignalEvent = NULL;   

    stEventEngine.pstEventBase = event_base_new();
    if (!stEventEngine.pstEventBase) {
        fprintf(stderr, "[ACU] event_base_new() failed\n");
        return EXIT_FAILURE;
    }
    eventEngineInit(&stEventEngine);
    pstAcuCtrlCtx = calloc(1, sizeof(ACU_CTRL_CTX));
    pstAcuCtrlCtx->iIsUartAlive = 0;
    stEventEngine.pvSharedData = pstAcuCtrlCtx;

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
