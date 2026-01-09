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

    // /* build payload with TIMEOUT */
    // unsigned char aucPayload[UDS_MAX_BUFFER_SIZE];
    // memset(aucPayload, 0, sizeof(aucPayload));

    // switch (pstCtx->stPending.unCmd) {
    // case CMD_POSITIONER_AZ_EL_SET:
    //     ((RES_POSITIONER_AZ_EL_SET*)aucPayload)->chResult = (char)RESP_TIMEOUT;
    //     break;
    // case CMD_ACU_MODE_SELECT:
    //     ((RES_ACU_MODE*)aucPayload)->chResult = (char)RESP_TIMEOUT;
    //     break;
    // default:
    //     /* not expected */
    //     break;
    // }

    // /* send UDS response now */
    // sendUdsResponse(pstCtx->stPending.pstUdsIo, pstCtx->stPending.unCmd, 
    //                    pstCtx->stPending.uiReqId, aucPayload, sizeof(aucPayload));

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
static int acuSendUartAndPend(ACU_CTRL_CTX* pstCtx, unsigned short unCmd, IO_CHANNEL* pstUartIo, unsigned char* aucFrame, unsigned int uiFrameLen)
{
    if (!pstCtx || !pstUartIo)
        return -1;

    fprintf(stderr,"### %s():%d ###\n", __func__, __LINE__);
    if (pstCtx->eState != ACU_STATE_IDLE || pstCtx->stPending.bInUse) {
        return -1;
    }

    fprintf(stderr,"### %s():%d ###\n", __func__, __LINE__);
    if(pstCtx->iIsUartAlive == 0){
        fprintf(stderr, "[ACU] UART not alive, cannot send CMD=0x%04X\n", unCmd);
        return -1;
    }
    /* pending 등록 */
    pstCtx->stPending.bInUse   = 1;
    pstCtx->stPending.unCmd    = unCmd;
    pstCtx->stPending.uiReqId++;
    pstCtx->stPending.pstUdsIo = pstUartIo;
    fprintf(stderr,"### %s():%d ###\n", __func__, __LINE__);
    /* UART write queue */
    evbuffer_add(pstUartIo->pstWriteBuffer, aucFrame, uiFrameLen);
    event_active(pstUartIo->pstWriteEvent, EV_WRITE, 0);
    fprintf(stderr,"### %s():%d ###\n", __func__, __LINE__);
    /* 300ms timeout start (reuse event) */
    if (pstCtx->pstTimeoutEvt) {
        fprintf(stderr,"### %s():%d ###\n", __func__, __LINE__);
        struct timeval tv = {0, 300 * 1000};
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
    fprintf(stderr,"### %s():%d ###\n", __func__, __LINE__);
    IO_CHANNEL* pstUartIo = (IO_CHANNEL*)pvData;
    IO_EVENT_TYPE eEventType = pstUartIo->ePendingLogicEvent;

    EVENT_ENGINE* pstEngine = pstUartIo->pstEventEngine;
    ACU_CTRL_CTX* pstCtx = (ACU_CTRL_CTX*)pstEngine->pvSharedData;

    unsigned char aucUartBuf[2048];
fprintf(stderr,"### %s():%d %d ###\n", __func__, __LINE__,eEventType);
    switch (eEventType)
    {
    case IO_EVT_RX_DATA: {
        fprintf(stderr,"### %s():%d ###\n", __func__, __LINE__);
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
        fprintf(stderr,"### %s():%d ###\n", __func__, __LINE__);
        /* parse response -> OK/FAIL */
        unsigned char ucResult = parseAcuUartResponse(aucUartBuf, iLen, pstCtx->stPending.unCmd);
        fprintf(stderr,"### %s():%d ###\n", __func__, __LINE__);
        /* build payload and send UDS response */
        unsigned char aucPayload[UDS_MAX_BUFFER_SIZE];
        memset(aucPayload, 0, sizeof(aucPayload));

        switch (pstCtx->stPending.unCmd) {
        case 1:
            fprintf(stderr, "### CMD_ACU_MODE_SELECT RESPONSE %s ###\n", ucResult == RESP_OK ? "OK" : "FAIL");
            break;
        case 2:
            fprintf(stderr, "### CMD_POSITIONER_AZ_EL_SET RESPONSE %s ###\n", ucResult == RESP_OK ? "OK" : "FAIL");
            break;
        case 3:{
            double dAz, dEl;
            char *chSplitData[8];
            int iSplitCnt = splitAcuDataString(aucUartBuf, ';', chSplitData, 2);
            if(iSplitCnt != 2){
                fprintf(stderr, "The received serial data is abnormal.");
                fprintf(stderr, "Serial recv data : %s", aucUartBuf);
                return false;
            }

            dAz = atof(chSplitData[0]);
            dEl = atof(chSplitData[1]);
            fprintf(stderr, "### AZ_EL_READ RESPONSE Az: %.3f, El: %.3f ###\n", dAz, dEl);
            break;
        }
        default:
            break;
        }

        sendUdsResponse(pstCtx->stPending.pstUdsIo, pstCtx->stPending.unCmd, 
                           pstCtx->stPending.uiReqId, aucPayload, sizeof(aucPayload));

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

static int acuRequestAzEl(IO_CHANNEL* pstIoChannel)
{
    EVENT_ENGINE* pstEngine = pstIoChannel->pstEventEngine;
    ACU_CTRL_CTX* pstCtx = (ACU_CTRL_CTX*)pstEngine->pvSharedData;
    unsigned char auSendBuf[64];
    int iSendLen = 0;

    if (!pstCtx || pstCtx->eState != ACU_STATE_IDLE)
        return -1;

    /* ACU READ AZ/EL command */
    iSendLen = readAzElFromAcu(auSendBuf);
    acuSendUartAndPend(pstCtx, 3, pstIoChannel, auSendBuf, iSendLen);
}
       

static void acuPeriodicReadCb(evutil_socket_t fd, short what, void* pvData)
{    
    (void)fd; (void)what;
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    EVENT_ENGINE* pstEngine = pstIoChannel->pstEventEngine;
    ACU_CTRL_CTX* pstCtx = (ACU_CTRL_CTX*)pstEngine->pvSharedData;
    if (!pstCtx)
        return;

    /* 다른 명령 처리 중이면 skip */
    if (pstCtx->eState != ACU_STATE_IDLE)
        return;

    /* UART 살아있을 때만 */
    if (!pstCtx->iIsUartAlive)
        return;

    acuRequestAzEl(pstIoChannel);
}




void printMenu()
{
    fprintf(stderr,"1. ACU MODE\n");
    fprintf(stderr,"2. AZ/EL SET\n");
    fprintf(stderr,"0. EXIT\n");
}

/* ============================================================
* stdin 이벤트 콜백
* ============================================================ */
/* ------------------------------------------------------------
 * 문자열 입력 (공백/엔터 방어)
 * ------------------------------------------------------------ */
static int readLine(char *buf, size_t sz)
{
    if (!fgets(buf, sz, stdin))
        return 0;

    buf[strcspn(buf, "\n")] = '\0';

    char *p = buf;
    while (*p == ' ' || *p == '\t') p++;

    char *end = p + strlen(p);
    while (end > p && (end[-1] == ' ' || end[-1] == '\t'))
        *--end = '\0';

    if (*p == '\0')
        return 0;

    if (p != buf)
        memmove(buf, p, strlen(p) + 1);

    return 1;
}

/* ------------------------------------------------------------
 * double 입력 (유효성 검증)
 * ------------------------------------------------------------ */
static int readDouble(const char *prompt, double *out)
{
    char buf[128];

    fprintf(stderr, "%s", prompt);

    if (!readLine(buf, sizeof(buf)))
        return 0;

    char *endptr = NULL;
    double v = strtod(buf, &endptr);

    if (endptr == buf || *endptr != '\0')
        return 0;

    *out = v;
    return 1;
}

/* ------------------------------------------------------------
 * 정수(enum) 선택 (0~max 범위 검사)
 * ------------------------------------------------------------ */
static int readIntChoice(const char *prompt, int max, int *out)
{
    char buf[64];

    fprintf(stderr, "%s", prompt);

    if (!readLine(buf, sizeof(buf)))
        return 0;

    for (char *p = buf; *p; p++) {
        if (*p < '0' || *p > '9')
            return 0;
    }

    int v = atoi(buf);

    if (v < 0 || v > max)
        return 0;

    *out = v;
    return 1;
}

static void stdinReadCb(int iFd, short nEvents, void* pvData)
{
    (void)iFd;
    (void)nEvents;

    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    EVENT_ENGINE* pstEngine = pstIoChannel->pstEventEngine;
    ACU_CTRL_CTX* pstCtx = (ACU_CTRL_CTX*)pstEngine->pvSharedData;
    unsigned short unCmd=0;
    printMenu();

    char achInput[128] = {0};
    unsigned char auSendBuf[1024];
    int iSendLen = 0;
    
    if (!fgets(achInput, sizeof(achInput), stdin)) {
        pstIoChannel->ePendingLogicEvent = IO_EVT_CHANNEL_CLOSED;
        event_active(pstIoChannel->pstLogicEvent, 0, 0);
        return;
    }

    // --------- 개행 제거 ----------
    achInput[strcspn(achInput, "\n")] = '\0';

    // --------- 앞/뒤 공백 제거 ----------
    char *p = achInput;
    while (*p == ' ' || *p == '\t')
        p++;

    char *end = p + strlen(p);
    while (end > p && (end[-1] == ' ' || end[-1] == '\t'))
        *--end = '\0';

    // --------- 빈 입력 처리 ----------
    if (*p == '\0') {
        fprintf(stderr, "(입력 없음)\n");
        return;
    }

    // --------- 숫자인지 검사 ----------
    for (char *q = p; *q; q++) {
        if (*q < '0' || *q > '3') {
            fprintf(stderr, "숫자만 입력하세요. (입력: '%s')\n", p);
            return;
        }
    }

    int sel = atoi(p);
    unCmd = (unsigned short)sel;
    switch (sel) {
    case 1:
        
        fprintf(stderr,"ACU MODE\n");
        fprintf(stderr,
            "  0: POSITION MODE\n"
            "  1: RATE MODE\n");
        int iSelect = 0;
        if (!readIntChoice("  선택: ", 1, &iSelect)) {
            fprintf(stderr, "[TCP-CLI] 잘못된 선택입니다.\n");
            return;
        }
        if(iSelect == 0){
            fprintf(stderr,"POSITION MODE SELECTED\n");
            pstCtx->stCommandState.chAcuMode = POSITION_SLAVE;
        }else if(iSelect == 1){
            fprintf(stderr,"RATE MODE SELECTED\n");
            pstCtx->stCommandState.chAcuMode = RATE_SLAVE;
        }        
        iSendLen = modeChange(pstCtx->stCommandState.chAcuMode, auSendBuf);
        break;

    case 2:
        fprintf(stderr,"AZ/EL SET\n");
        double az = 0.0, el = 0.0;
        if (!readDouble("  AZ(도): ", &az) || !readDouble("  EL(도): ", &el)) {
            fprintf(stderr, "[TCP-CLI] 잘못된 값입니다.\n");
            return;
        }

        if(pstCtx->stCommandState.chAcuMode == POSITION_SLAVE){
            fprintf(stderr,"POSITION MODE SELECTED\n");
            iSendLen = moveAzElPosition(az, el, auSendBuf);
        }else if(pstCtx->stCommandState.chAcuMode == RATE_SLAVE){
            fprintf(stderr,"RATE MODE SELECTED\n");
            iSendLen = moveAzElRate(az, el, auSendBuf);
        }
        break;
    
    case 0:
        pstIoChannel->ePendingLogicEvent = IO_EVT_CHANNEL_CLOSED;
        event_active(pstIoChannel->pstShutdownEvent, 0, 0);
        return;

    default:
        fprintf(stderr, "[TCP-CLI] 잘못된 메뉴 번호입니다. (0~8)\n");
        return;
    }
    if(iSendLen > 0){
        fprintf(stderr,"### %s():%d %s ###\n", __func__, __LINE__, auSendBuf);
        acuSendUartAndPend(pstCtx, unCmd, pstIoChannel, auSendBuf, iSendLen);
    }
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
    IO_CHANNEL *pstIoChannel = NULL;
    struct event *pstSignalEvent = NULL;
    struct event *pstAzElPollEvt = NULL;
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
    pstAcuCtrlCtx->iIsUartAlive = 1;
    stEventEngine.pvSharedData = pstAcuCtrlCtx;
    pstAcuCtrlCtx->pstTimeoutEvt = evtimer_new(stEventEngine.pstEventBase, acuUartTimeoutCb, pstAcuCtrlCtx);
    if (!pstAcuCtrlCtx->pstTimeoutEvt) {
        fprintf(stderr, "[ACU] evtimer_new failed\n");
        return EXIT_FAILURE;
    }

    /* UART open */
    if (uartOpen(&stUartCtx) < 0) {
        fprintf(stderr, "[ACU] uartOpen failed: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    pstIoChannel = eventSourceCreateWithBev(&stEventEngine, stUartCtx.iFd,
        TYPE_UART, ROLE_REQUESTER, NULL, NULL, uartReadCallback);
    pstIoChannel->iWorkerId = ACU_UART;

    pstAzElPollEvt = event_new(stEventEngine.pstEventBase, -1, EV_PERSIST, acuPeriodicReadCb, pstIoChannel);
    struct timeval tv = {0, 500 * 1000};  // 100ms
    event_add(pstAzElPollEvt, &tv);

    /* ------------------- */
    /* stdin 이벤트 등록   */
    /* ------------------- */
    struct event* evStdin = event_new(stEventEngine.pstEventBase,
        STDIN_FILENO, EV_READ | EV_PERSIST,
        stdinReadCb, pstIoChannel);
    if (!evStdin) {
        printf("[TCP-CLI] evStdin create failed\n");
        event_base_free(stEventEngine.pstEventBase);
        return -1;
    }
    event_add(evStdin, NULL);


    pstSignalEvent = evsignal_new(stEventEngine.pstEventBase, SIGINT, signalCb, &stEventEngine);
    event_add(pstSignalEvent, NULL);

    event_base_dispatch(stEventEngine.pstEventBase);

    if(evStdin){
        event_del(evStdin);
        event_free(evStdin);
        evStdin =  NULL;
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
