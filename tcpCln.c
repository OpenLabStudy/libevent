/**
 * @file tcpCln.c
 * @brief EVENT_SOURCE + netTcp 기반 TCP Client (NO GLOBAL VARIABLES, stdin 이벤트 기반)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>

#include <event2/event.h>

#include "eventEngine.h"
#include "icdCommand.h"
#include "tcpSvr.h"

static void ioChannelHandleEvent(int iFd, short nEvent, void* pvData)
{
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    IO_EVENT_TYPE eEventType = pstIoChannel->ePendingLogicEvent;
    unsigned char auchRecvBuffer[2048];
    unsigned char auchResultBuffer[64];
    unsigned short unCmd = 0;
    FRAME_ERR eErr;

    switch (eEventType) {
    case IO_EVT_RX_DATA:
        while (1) {
            size_t tRecvLen = evbuffer_get_length(pstIoChannel->pstReadBuffer);
            /* 최소 헤더 */
            if (tRecvLen < sizeof(FRAME_HEADER))
                break;

            if (tRecvLen > sizeof(auchRecvBuffer))
                tRecvLen = sizeof(auchRecvBuffer);

            int iCopyLen = evbuffer_copyout(pstIoChannel->pstReadBuffer, auchRecvBuffer, tRecvLen);
            /* === 프레임 검증 === */
            eErr = frameDecode(auchRecvBuffer, iCopyLen, FRAME_TYPE_RESPONSE, &unCmd);
            if (eErr != FRAME_OK) {
                fprintf(stderr,"\n");
                for(int i=1; i<=iCopyLen; i++){
                    if(i&16 == 0)
                        fprintf(stderr,"\n");
                    fprintf(stderr,"%02x ", auchRecvBuffer[i-1]);
                }  
                fprintf(stderr,"\n");
                fprintf(stderr, "[TCP-CLI] frameDecode ERR: %s\n", frameErrToStr(eErr));
                int iOffset = findFrameHeader(auchRecvBuffer, iCopyLen);
                if (iOffset > 0) {
                    /* 앞부분 garbage 제거 */
                    evbuffer_drain(pstIoChannel->pstReadBuffer, iOffset);
                    fprintf(stderr,"[TCP-CLI] resync: drop %d bytes, retry decode\n", iOffset);
                } else if (iOffset == -2) {
                    /* STX half-match: 데이터 더 수신 */
                    evbuffer_drain(pstIoChannel->pstReadBuffer, iCopyLen-1);
                    fprintf(stderr,"[TCP-CLI] STX half match, wait more data\n");
                } else {
                    /* STX 자체가 없음 → 전부 드랍 */
                    evbuffer_drain(pstIoChannel->pstReadBuffer, iCopyLen);
                    fprintf(stderr, "[TCP-CLI] no STX, drop all\n");
                }
                continue;
            }
            int iFrameSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_RESPONSE);
            if (iCopyLen < iFrameSize)
                break;
            evbuffer_drain(pstIoChannel->pstReadBuffer, iFrameSize);
            parseAndDumpResponse(auchRecvBuffer, auchResultBuffer);
        }
        break;

    case IO_EVT_CHANNEL_CLOSED:
        printf("[TCP-CLI] channel closed fd=%d\n", pstIoChannel->iFd);
        event_active(pstIoChannel->pstShutdownEvent, 0, 0);
        break;

    case IO_EVT_ERROR:
        printf("[TCP-CLI] channel error fd=%d\n", pstIoChannel->iFd);
        event_active(pstIoChannel->pstShutdownEvent, 0, 0);
        break;

    default:
        break;
    }

    pstIoChannel->ePendingLogicEvent = IO_EVENT_NONE;
}

void printMenu()
{
    fprintf(stderr,"1. keepalive\n");
    fprintf(stderr,"2. ibit\n");
    fprintf(stderr,"3. position az el set\n");
    fprintf(stderr,"4. tracking select\n");
    fprintf(stderr,"5. tracking control\n");
    fprintf(stderr,"6. position degree send\n");
    fprintf(stderr,"7. acu mode select\n");
    fprintf(stderr,"8. az el offset set\n");
    fprintf(stderr,"0. exit\n");
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
    printMenu();

    char achInput[128] = {0};
    unsigned char auSendBuf[1024];
    FRAME_ERR eErr = FRAME_OK;
    MSG_ID stMsgId = { TCP_CLN_ID, TCP_SVR_ID };

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
        fprintf(stderr, "[TCP-CLI] (입력 없음)\n");
        return;
    }

    // --------- 숫자인지 검사 ----------
    for (char *q = p; *q; q++) {
        if (*q < '0' || *q > '9') {
            fprintf(stderr, "[TCP-CLI] 숫자만 입력하세요. (입력: '%s')\n", p);
            return;
        }
    }

    int sel = atoi(p);

    switch (sel) {
    case 1:
        fprintf(stderr,"[TCP-CLI] REQ_KEEP_ALIVE\n");
        REQ_KEEP_ALIVE stReqKeepAlive;
        stReqKeepAlive.chTmp = 0x01;
        eErr = makeRequestFrame(CMD_KEEP_ALIVE, &stMsgId, &stReqKeepAlive, auSendBuf);
        break;

    case 2:
        fprintf(stderr,"[TCP-CLI] REQ_IBIT\n");
        REQ_BIT stReqBit;
        stReqBit.chBit = 0x01;
        eErr = makeRequestFrame(CMD_IBIT, &stMsgId, &stReqBit, auSendBuf);
        break;
        
    case 3: {
        fprintf(stderr,"[TCP-CLI] REQ_POSITIONER_AZ_EL_SET\n");
        double az = 0.0, el = 0.0;
        if (!readDouble("  AZ(도): ", &az) || !readDouble("  EL(도): ", &el)) {
            fprintf(stderr, "[TCP-CLI] 잘못된 값입니다.\n");
            return;
        }
        //f0 f0 00 00 00 18 10 b1 00 00 04 3f f1 f7 ce d9 16 87 2b 40 02 91 68 72 b0 20 c5 9d ff ff 
        REQ_POSITIONER_AZ_EL_SET stReqPositionAzElSet;
        memset(&stReqPositionAzElSet, 0, sizeof(stReqPositionAzElSet));
        endianChange1(az, stReqPositionAzElSet.chAzimuthDeg);
        endianChange1(el, stReqPositionAzElSet.chElevationDeg);        
        eErr = makeRequestFrame(CMD_POSITIONER_AZ_EL_SET, &stMsgId, &stReqPositionAzElSet, auSendBuf);
        break;
    }

    case 4: {
        fprintf(stderr,"[TCP-CLI] REQ_TRACKING_SELECT\n");
        fprintf(stderr,
            "  0: IDLE\n"
            "  1: SELF_TRACKING\n"
            "  2: PROGRAMMED_TRACKING\n"
            "  3: EXTERNAL_DEV_TRACKING\n");

        int iSelect = 0;
        if (!readIntChoice("  선택: ", 3, &iSelect)) {
            fprintf(stderr, "[TCP-CLI] 잘못된 선택입니다.\n");
            return;
        }

        REQ_TRACKING_SELECT stReqTrackingSelect;
        stReqTrackingSelect.chTrackingSelect = (char)iSelect;
        eErr = makeRequestFrame(CMD_TRACKING_SELECT, &stMsgId, &stReqTrackingSelect, auSendBuf);
        break;
    }

    case 5: {
        fprintf(stderr,"[TCP-CLI] REQ_TRACKING_CONTROL\n");
        fprintf(stderr, "  0: STOP\n  1: START\n");
        int iSelect = 0;
        if (!readIntChoice("  선택: ", 1, &iSelect)) {
            fprintf(stderr, "[TCP-CLI] 잘못된 선택입니다.\n");
            return;
        }

        REQ_TRACKING_CONTROL stReqTrackingConrol;
        stReqTrackingConrol.chStartStop = (char)iSelect;
        eErr = makeRequestFrame(CMD_TRACKING_CONTROL, &stMsgId, &stReqTrackingConrol, auSendBuf);
        break;
    }

    case 6: {
        fprintf(stderr,"[TCP-CLI] REQ_POSITIONER_DEG_SEND\n");
        fprintf(stderr, "  0: OFF\n  1: ON\n");
        int iSelect = 0;
        if (!readIntChoice("  선택: ", 1, &iSelect)) {
            fprintf(stderr, "[TCP-CLI] 잘못된 선택입니다.\n");
            return;
        }

        REQ_POSITIONER_DEG_SEND stReqPositionerDegSend;
        stReqPositionerDegSend.chSendOnOff = (char)iSelect;
        eErr = makeRequestFrame(CMD_POSITIONER_DEG_SEND, &stMsgId, &stReqPositionerDegSend, auSendBuf);
        break;
    }

    case 7: {
        fprintf(stderr,"[TCP-CLI] REQ_ACU_MODE_SELECT\n");
        fprintf(stderr, "  0: RATE\n  1: POSITION\n");
        int iSelect = 0;
        if (!readIntChoice("  선택: ", 1, &iSelect)) {
            fprintf(stderr, "[TCP-CLI] 잘못된 선택입니다.\n");
            return;
        }

        REQ_ACU_MODE stReqAcuMode;
        stReqAcuMode.chAcuMode = (iSelect == 0) ? RATE : POSITION;
        eErr = makeRequestFrame(CMD_ACU_MODE_SELECT, &stMsgId, &stReqAcuMode, auSendBuf);
        break;
    }

    case 8: {
        //f0 f0 00 00 00 10 10 b1 00 00 10 00 00 00 0c 00 00 00 62 3f ff ff 06 00 00 00 
        //0.123, 0.987
        fprintf(stderr,"[TCP-CLI] REQ_AZ_EL_OFFSET_SET\n");
        double az = 0.0, el = 0.0;
        fprintf(stderr,"### %s():%d ###\n", __func__, __LINE__);
        if (!readDouble("  AZ Offset: ", &az) || !readDouble("  EL Offset: ", &el)) {
            fprintf(stderr, "[TCP-CLI] 잘못된 값입니다.\n");
            return;
        }
        fprintf(stderr,"### %s():%d ###\n", __func__, __LINE__);

        REQ_AZ_EL_OFFSET_SET stReqAzElOffsetSet;
        stReqAzElOffsetSet.iAzOffset = htonl((int)(az * 100.0));
        stReqAzElOffsetSet.iElOffset = htonl((int)(el * 100.0));
        fprintf(stderr,"### %s():%d ###\n", __func__, __LINE__);
        eErr = makeRequestFrame(CMD_AZ_EL_OFFSET_SET, &stMsgId, &stReqAzElOffsetSet, auSendBuf);
        fprintf(stderr,"### %s():%d ###\n", __func__, __LINE__);
        break;
    }

    case 0:
        pstIoChannel->ePendingLogicEvent = IO_EVT_CHANNEL_CLOSED;
        event_active(pstIoChannel->pstShutdownEvent, 0, 0);
        return;

    default:
        fprintf(stderr, "[TCP-CLI] 잘못된 메뉴 번호입니다. (0~8)\n");
        return;
    }

    if (eErr == FRAME_OK) {
        int iSendLen = getFrameSizeWithData(auSendBuf, FRAME_TYPE_REQUEST);
        evbuffer_add(pstIoChannel->pstWriteBuffer, auSendBuf, iSendLen);
        event_add(pstIoChannel->pstWriteEvent, NULL);
    }
}

// static void stdinReadCb(int iFd, short nEvents, void* pvData)
// {
//     int iSelec;
//     fprintf(stderr, "[TCP-CLI] stdin fired\n");
//     (void)nEvents;

//     IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
//     printMenu();

//     char achInput[1024];
//     unsigned char auSendBuf[1024];
//     FRAME_ERR eErr;

//     if (!fgets(achInput, sizeof(achInput), stdin)) {
//         pstIoChannel->ePendingLogicEvent = IO_EVT_CHANNEL_CLOSED;
//         event_active(pstIoChannel->pstLogicEvent, 0, 0);
//         return;
//     }

//     achInput[strcspn(achInput, "\n")] = '\0';
//     MSG_ID stMsgId = { TCP_CLN_ID, TCP_SVR_ID };

//     if (!strcmp(achInput, "keepalive")) {
//         fprintf(stderr,"[TCP-CLI] REQ_KEEP_ALIVE\n");
//         eErr = makeRequestFrame(CMD_KEEP_ALIVE, &stMsgId, auSendBuf);
//     } else if (!strcmp(achInput, "ibit")) {
//         fprintf(stderr,"[TCP-CLI] REQ_IBIT\n");
//         eErr = makeRequestFrame(CMD_IBIT, &stMsgId, auSendBuf);
//     } else if (!strcmp(achInput, "quit") || !strcmp(achInput, "exit")) {
//         pstIoChannel->ePendingLogicEvent = IO_EVT_CHANNEL_CLOSED;
//         event_active(pstIoChannel->pstShutdownEvent, 0, 0);
//         return;
//     } else {
//         fprintf(stderr, "Available commands:\n" 
//             " keepalive\n"
//             "  ibit\n"
//             "  quit\n");
//         return;
//     }

//     if (eErr == FRAME_OK) {
//         int iSendLen = getFrameSizeWithData(auSendBuf, FRAME_TYPE_REQUEST);
//         evbuffer_add(pstIoChannel->pstWriteBuffer, auSendBuf, iSendLen);
//         event_add(pstIoChannel->pstWriteEvent, NULL);
//     }
// }


/* ============================================================
* SIGINT 콜백
* ============================================================ */
static void signalCb(evutil_socket_t sig, short events, void* pvArg)
{
    EVENT_ENGINE* pstEventEngine = (EVENT_ENGINE *)pvArg;

    fprintf(stderr,"\n[TCP-CLI] SIGINT → shutdown\n");
    if(pstEventEngine->pstEventBase)
        event_base_loopexit(pstEventEngine->pstEventBase, NULL);
}

int run()
{
    EVENT_ENGINE   stEventEngine;
    fprintf(stderr, "[TCP-CLI] isatty(stdin)=%d\n", isatty(STDIN_FILENO));
    stEventEngine.pstEventBase = event_base_new();
    if (!stEventEngine.pstEventBase) {
        printf("[TCP-CLI] event_base_new failed\n");
        return -1;
    }
    eventEngineInit(&stEventEngine, 0);

    /* ------------------- */
    /* TCP 연결            */
    /* ------------------- */
    int iClientSock = netTcpCreateClient("127.0.0.1", SERVER_PORT);
    if (iClientSock < 0) {
        perror("netTcpCreateClient");
        event_base_free(stEventEngine.pstEventBase);
        return -1;
    }

    printf("[TCP-CLI] Connecting to 127.0.0.1:5000...\n");

    /* ------------------- */
    /* EVENT_SOURCE 생성   */
    /* ------------------- */
    netSetNonblock(iClientSock);
    eventSourceCreateWithBev(&stEventEngine, iClientSock,
        TYPE_TCP_CLI, ROLE_WORKER,
        NULL, NULL, ioChannelHandleEvent
    );

    /* ------------------- */
    /* stdin 이벤트 등록   */
    /* ------------------- */
    struct event* evStdin = event_new(stEventEngine.pstEventBase,
        STDIN_FILENO, EV_READ | EV_PERSIST,
        stdinReadCb, stEventEngine.pstIoChannelList);
    if (!evStdin) {
        printf("[TCP-CLI] evStdin create failed\n");
        event_base_free(stEventEngine.pstEventBase);
        return -1;
    }
    event_add(evStdin, NULL);

    struct event   *pstSignalEvent;
    /* SIGINT 처리 등록 */
    pstSignalEvent = evsignal_new(stEventEngine.pstEventBase, 
        SIGINT, signalCb, &stEventEngine);
    event_add(pstSignalEvent, NULL);
    /* ------------------- */
    /* 이벤트 루프 실행    */
    /* ------------------- */
    event_base_dispatch(stEventEngine.pstEventBase);

    if(evStdin){
        event_del(evStdin);
        event_free(evStdin);
        evStdin =  NULL;
    }
    if(pstSignalEvent){
        event_del(pstSignalEvent);
        event_free(pstSignalEvent);
        pstSignalEvent =  NULL;
    }
    
    eventEngineCleanup(&stEventEngine);
    event_base_free(stEventEngine.pstEventBase);

    return 0;
}

/* === main === */
#ifndef GOOGLE_TEST
int main(int argc, char** argv)
{
    return run();
}
#endif