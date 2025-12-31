/**
 * @file udsSvr.c
 * @brief Libevent 기반 UDS(UNIX Domain Socket) 서버 Application Layer
 *
 * TCP 서버(tcpSvr.c)와 완전히 동일한 구조를 사용하되,
 * 소켓 생성만 AF_UNIX 기반(netUdsCreateServer)으로 변경한 버전이다.
 *
 * - EVENT_CONTEXT + SOCK_CONTEXT 구조 사용
 * - acceptCb, readCallbackWrapper, eventCallbackWrapper는 eventSession.c 사용
 * - appReadCb / appEventCb 는 Application 레이어 콜백
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <signal.h>
 #include <unistd.h>
 #include <errno.h>
 #include <sys/time.h>

 #include "icdCommand.h"
 #include "eventEngine.h"
 #include "udsSvr.h"


 typedef struct {
    char            chValid;
    unsigned long   ulUsec;
    RES_LLA_DATA    stGps;
} GPS_STATE;

typedef struct {
    char            chValid;
    unsigned long   ulUsec;
    RES_RPY_DATA    stImu;
} IMU_STATE;

typedef struct {
    char            chValid;
    unsigned long   ulUsec;
    RES_AZ_EL_DATA  stSp;
} SP_STATE;

typedef struct {
    char            chValid;
    unsigned long   ulUsec;
    RES_LLA_DATA    stExtern;
} EXTERN_STATE;

typedef struct {
    char            chValid;
    unsigned long   ulUsec;
    RES_AZ_EL_DATA  stKeyboard;
} KEYBOARD_STATE;

typedef enum{
    RECV_NONE = 0,
    RECV_GPS,
    RECV_IMU,
    RECV_SP,
    RECV_EXTERN,
    RECV_KEYBOARD
}RECV_SENSOR_FLAG;

/* ========================================================================== */
/* Fusion Trigger                                                             */
/* ========================================================================== */
typedef enum {
    TRIG_NONE = 0,
    TRIG_GPS,
    TRIG_IMU,
    TRIG_EXTERN,
    TRIG_SP,
    TRIG_KEYBOARD
} FUSION_TRIGGER;
/* ========================================================================== */
/* SENSOR_STATE                                                               */
/* ========================================================================== */
typedef struct {
    GPS_STATE       stGpsState;
    IMU_STATE       stImuState;
    SP_STATE        stSpState;
    EXTERN_STATE    stExternState;
    KEYBOARD_STATE  stKeyboardState;

    FUSION_TRIGGER  eLastTrig;
    unsigned long   ulLastFusionUsec;
} SENSOR_STATE;

typedef struct {
    /* 🔹 기존 shared data */
    SENSOR_STATE    stSensor;

    /* 🔹 fusion 계산용 이벤트 */
    struct event*   pstFusionEvent;
    struct event*   pstCommandEvent;
    char            chFusionPending;
} SENSOR_FUSION_CTX;

/* ========================================================================== */
/* Constants                                                                  */
/* ========================================================================== */
#define GPS_IMU_SYNC_USEC        (80000)   // 80ms
#define FUSION_MIN_INTERVAL_USEC (30000)   // 30ms

/* ========================================================================== */
/* Time Utilities                                                             */
/* ========================================================================== */
static unsigned long nowUsec(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (unsigned long)tv.tv_sec * 1000000UL + tv.tv_usec;
}

/* ========================================================================== */
/* Forward Declarations                                                       */
/* ========================================================================== */
static void fusionEventCb(evutil_socket_t fd, short what, void* arg);
static void fusionDispatch(SENSOR_STATE* pstSensorState);
static FUSION_TRIGGER decideFusionTrigger(SENSOR_STATE* pstSensorState, unsigned long ulNowUsec);

/* ========================================================================== */
/* Trigger Decision                                                           */
/* ========================================================================== */
static FUSION_TRIGGER decideFusionTrigger(SENSOR_STATE* pstSensorState, unsigned long ulNowUsec)
{
    /* 1. KEYBOARD */
    if (pstSensorState->stKeyboardState.chValid)
        return TRIG_KEYBOARD;

    /* 2. SP */
    if (pstSensorState->stSpState.chValid)
        return TRIG_SP;

    /* 3. EXTERN (GPS + IMU 필요) */
    if (pstSensorState->stExternState.chValid &&
        pstSensorState->stGpsState.chValid &&
        pstSensorState->stImuState.chValid)
        return TRIG_EXTERN;

    /* 4. GPS + IMU */
    if (pstSensorState->stGpsState.chValid && pstSensorState->stImuState.chValid) {
        unsigned long dt =
            (pstSensorState->stGpsState.ulUsec > pstSensorState->stImuState.ulUsec) ?
            (pstSensorState->stGpsState.ulUsec - pstSensorState->stImuState.ulUsec) :
            (pstSensorState->stImuState.ulUsec - pstSensorState->stGpsState.ulUsec);

        if (dt > GPS_IMU_SYNC_USEC)
            return TRIG_NONE;

        if ((ulNowUsec - pstSensorState->ulLastFusionUsec) < FUSION_MIN_INTERVAL_USEC)
            return TRIG_NONE;

        return (pstSensorState->stGpsState.ulUsec >= pstSensorState->stImuState.ulUsec) ?
            TRIG_GPS : TRIG_IMU;
    }

    return TRIG_NONE;
}

/* ========================================================================== */
/* Fusion Dispatcher (계산 전용)                                              */
/* ========================================================================== */
static void fusionDispatch(SENSOR_STATE* pstSensorState)
{
    unsigned long ulNowUsec = nowUsec();
    FUSION_TRIGGER eTrigger = decideFusionTrigger(pstSensorState, ulNowUsec);

    if (eTrigger == TRIG_NONE)
        return;

    switch (eTrigger) {
    case TRIG_KEYBOARD:
        fprintf(stderr, "[FUSION] KEYBOARD override\n");
        pstSensorState->stKeyboardState.chValid = 0;
        break;

    case TRIG_SP:
        fprintf(stderr, "[FUSION] SP PID control\n");
        pstSensorState->stSpState.chValid = 0;
        break;

    case TRIG_EXTERN:
        fprintf(stderr, "[FUSION] EXTERN target calculation\n");
        pstSensorState->stExternState.chValid = 0;
        break;

    case TRIG_GPS:
    case TRIG_IMU:
        fprintf(stderr, "[FUSION] GPS+IMU attitude compensation (%s)\n",
                eTrigger == TRIG_GPS ? "GPS-trigger" : "IMU-trigger");
        break;

    default:
        break;
    }

    pstSensorState->eLastTrig = eTrigger;
    pstSensorState->ulLastFusionUsec = ulNowUsec;
}

/* ========================================================================== */
/* Fusion Event Callback                                                      */
/* ========================================================================== */
static void commandEventCb(int iFd, short nEvent, void* pvData)
{
    (void)nEvent;
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);

    IO_CHANNEL *pstIoChannel = (IO_CHANNEL *)pvData;
    EVENT_ENGINE *pstEventEngine = pstIoChannel->pstEventEngine;
    SENSOR_FUSION_CTX *pstSensorFusionCtx =
        (SENSOR_FUSION_CTX *)pstEventEngine->pvSharedData;

    unsigned char auchRecvBuffer[2048];
    int iRecvSize = read(iFd, auchRecvBuffer, sizeof(auchRecvBuffer));
    if (iRecvSize <= 0)
        return;

    unsigned short unCmd = 0;
    FRAME_ERR eErr = frameDecode(
        auchRecvBuffer, iRecvSize,
        FRAME_TYPE_REQUEST, &unCmd);

    if (eErr != FRAME_OK)
        return;

    unsigned char auchResult[UDS_MAX_SIZE];
    unsigned char uchaSendBuf[UDS_MAX_SIZE];
    int iResultSize = 0;

    eErr = commandHandler(auchRecvBuffer, auchResult, &iResultSize);
    if (eErr != FRAME_OK || iResultSize <= 0)
        return;

    MSG_ID stMsgId = { UDS_1_CLN1_ID, UDS_1_SVR_ID };
    unsigned int uiSendSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_RESPONSE);

    makeResponseFrame(unCmd, &stMsgId, auchResult, uchaSendBuf);
    write(iFd, uchaSendBuf, uiSendSize);
}

static void fusionEventCb(int iFd, short nEvent, void* pvData)
{
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    (void)iFd;
    (void)nEvent;
    SENSOR_FUSION_CTX* pstSensorFusionCtx = (SENSOR_FUSION_CTX*)pvData;

    pstSensorFusionCtx->chFusionPending = 0;
    fusionDispatch(&pstSensorFusionCtx->stSensor);
}




/* ========================================================================== */
/* Application-Level Read Processing (UDS Server)                             */
/* ========================================================================== */

static void sensorFusionRead(int iFd, short nEvent, void* pvData)
{
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    SENSOR_FUSION_CTX* pstSensorFusionCtx =
        (SENSOR_FUSION_CTX*)pstIoChannel->pstEventEngine->pvSharedData;
    SENSOR_STATE* pstSensorState = &pstSensorFusionCtx->stSensor;        
    IO_EVENT_TYPE eEventType = pstIoChannel->ePendingLogicEvent;
    unsigned char auchRecvBuffer[2048];
    unsigned short unCmd = 0;
    FRAME_ERR eErr;
    unsigned long ulUsec = nowUsec();
    switch (eEventType) {
    case IO_EVT_RX_DATA:
        while (1) {
            unsigned int uiRecvLen = evbuffer_get_length(pstIoChannel->pstReadBuffer);
            /* 최소 헤더도 안 왔으면 중단 */
            if (uiRecvLen < sizeof(FRAME_HEADER))
                break;

            memset(auchRecvBuffer, 0x00, sizeof(auchRecvBuffer));
            int iCopyLen = evbuffer_copyout(pstIoChannel->pstReadBuffer, auchRecvBuffer, uiRecvLen);
            /*추후 evbuffer에 삭제 크기 알 필요 있음*/
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
            if (iCopyLen < iFrameSize)
                break;
                
            evbuffer_drain(pstIoChannel->pstReadBuffer, iFrameSize);
            switch(unCmd){
                case CDM_GPS_DATA: {
                    memcpy(&pstSensorState->stGpsState.stGps, auchRecvBuffer+sizeof(FRAME_HEADER), sizeof(RES_LLA_DATA));
                    pstSensorState->stGpsState.chValid = 1;
                    pstSensorState->stGpsState.ulUsec  = ulUsec;
                    fprintf(stderr,"GPS LATITUDE %lf, LONGITUDE %lf, ALTITUDE %lf\n", pstSensorState->stGpsState.stGps.dLatitude,
                        pstSensorState->stGpsState.stGps.dLongitude, pstSensorState->stGpsState.stGps.dAltitude);
                    break;                
                }
                case CDM_IMU_DATA: {
                    memcpy(&pstSensorState->stImuState.stImu, auchRecvBuffer+sizeof(FRAME_HEADER), sizeof(RES_RPY_DATA));
                    pstSensorState->stImuState.chValid = 1;
                    pstSensorState->stImuState.ulUsec  = ulUsec;
                    fprintf(stderr,"IMU ROLL %lf, PITCH %lf, YAW %lf\n", pstSensorState->stImuState.stImu.dRoll,
                        pstSensorState->stImuState.stImu.dPitch, pstSensorState->stImuState.stImu.dYaw);
                    break;
                }
                case CDM_SP_DATA: {
                    memcpy(&pstSensorState->stSpState.stSp, auchRecvBuffer+sizeof(FRAME_HEADER), sizeof(RES_AZ_EL_DATA));
                    pstSensorState->stSpState.chValid = 1;
                    pstSensorState->stSpState.ulUsec  = ulUsec;
                    fprintf(stderr,"SP AZ %lf, EL %lf\n", pstSensorState->stSpState.stSp.dAz, pstSensorState->stSpState.stSp.dEl);
                    break;
                }
                case CDM_EXTERN_DATA: {
                    memcpy(&pstSensorState->stExternState.stExtern, auchRecvBuffer+sizeof(FRAME_HEADER), sizeof(RES_LLA_DATA));
                    pstSensorState->stExternState.chValid = 1;
                    pstSensorState->stExternState.ulUsec  = ulUsec;
                    fprintf(stderr,"EXTERN LATITUDE %lf, LONGITUDE %lf, ALTITUDE %lf\n", pstSensorState->stExternState.stExtern.dLatitude, 
                    pstSensorState->stExternState.stExtern.dLongitude, pstSensorState->stExternState.stExtern.dAltitude);
                    break;                
                }
                case CDM_KEYBOARD_DATA: {
                    memcpy(&pstSensorState->stKeyboardState.stKeyboard, auchRecvBuffer+sizeof(FRAME_HEADER), sizeof(RES_AZ_EL_DATA));
                    pstSensorState->stKeyboardState.chValid = 1;
                    pstSensorState->stKeyboardState.ulUsec  = ulUsec;
                    fprintf(stderr,"KEYBOARD AZ %lf, EL %lf\n", pstSensorState->stKeyboardState.stKeyboard.dAz, pstSensorState->stKeyboardState.stKeyboard.dEl);
                    break;                
                }                
            }
            /*계산 이벤트 트리거 */
            if (!pstSensorFusionCtx->chFusionPending) {
                pstSensorFusionCtx->chFusionPending = 1;
                event_active(pstSensorFusionCtx->pstFusionEvent, 0, 0);
            }
        }
        break;

    case IO_EVT_CHANNEL_CLOSED:
    case IO_EVT_ERROR:
        fprintf(stderr,"[UDS-SVR] channel error fd=%d\n", pstIoChannel->iFd);
        ioMarkChannelDead(pstIoChannel, eEventType);
        event_active(pstIoChannel->pstShutdownEvent, 0, 0);
        break;

    default:
        break;
    }
    pstIoChannel->ePendingLogicEvent = IO_EVENT_NONE;
}


/* ============================================================
* Accept 콜백
* ============================================================ */
static void acceptCb(evutil_socket_t iListenFd, short nKindOfEvent, void* pvArg)
{
    (void)nKindOfEvent;
    EVENT_ENGINE* pstEventEngine = (EVENT_ENGINE *)pvArg;

    struct sockaddr_in stClientAddr;
    socklen_t uiClientLen = sizeof(stClientAddr);

    int iClientSock = accept(iListenFd, (struct sockaddr*)&stClientAddr, &uiClientLen);
    if (iClientSock < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            perror("[UDS-SVR] accept");
        return;
    }

    printf("[UDS-SVR] New client FD=%d\n", iClientSock);

    netSetNonblock(iClientSock);

    eventSourceCreateWithBev(pstEventEngine, iClientSock,
        TYPE_TCP_SVR, ROLE_REQUESTER,
        NULL, NULL, sensorFusionRead);
}


/* ============================================================
* SIGINT 콜백
* ============================================================ */
static void signalCb(evutil_socket_t sig, short events, void* pvArg)
{
    EVENT_ENGINE* pstEventEngine = (EVENT_ENGINE *)pvArg;

    fprintf(stderr,"\n[UDS-SVR] SIGINT → shutdown\n");
    if(pstEventEngine->pstEventBase)
        event_base_loopexit(pstEventEngine->pstEventBase, NULL);
}

static void uds1ReconnectCb(evutil_socket_t fd, short nEvent, void *pvArg)
{
    (void)fd;
    (void)nEvent;

    EVENT_ENGINE *pstEventEngine = (EVENT_ENGINE *)pvArg;
    /* 이미 살아있으면 재접속 불필요 */
    IO_CHANNEL *pstCmdIo = ioFindChannelByWorkerId(pstEventEngine, UDS_1_CLN1_ID);
    if (pstCmdIo && ioIsChannelAlive(pstCmdIo)){
        fprintf(stderr,"### %s():%d %u work id : %d###\n",__func__,__LINE__, pstCmdIo, pstCmdIo->iWorkerId);
        return;
    }
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);

    int iSock = netUdsCreateClient(UDS_1_PATH);
    if (iSock < 0) {
        fprintf(stderr, "[UDS#1] reconnect failed, retry later\n");
        return; /* 타이머는 계속 살아있음 */
    }

    fprintf(stderr, "[UDS#1] reconnected!\n");
    netSetNonblock(iSock);
    IO_CHANNEL *pstNewIo = eventSourceCreateWithBev(pstEventEngine, iSock,
            TYPE_UDS_CLI, ROLE_REQUESTER,
            NULL, NULL, commandEventCb);
    if (!pstNewIo) {
        close(iSock);
        return;
    }
    pstNewIo->iWorkerId = UDS_1_CLN1_ID;

    /* 🔹 worker register */
    ipcSendWorkerRegister(pstNewIo, WORKER_SENSOR_FUSION);

    fprintf(stderr, "[UDS-CLI] reconnected (%u fd=%d)\n", pstNewIo, pstNewIo->iFd);
}


/* ========================================================================== */
/* Main Entry Point                                                           */
/* ========================================================================== */
int run(void)
{
    EVENT_ENGINE    stEventEngine;
    struct event*   pstSignalEvent;
    struct event*   pstEventAccept;
    struct event*   pstUdsRetryEvent = NULL;
    struct timeval stRertyTimeOut = {1, 0};

    stEventEngine.pstEventBase = event_base_new();
    if (!stEventEngine.pstEventBase) {
        fprintf(stderr, "[UDS-SVR] event_base_new() failed\n");
        return EXIT_FAILURE;
    }

    /* Dispatcher 초기화 */
    eventEngineInit(&stEventEngine);
    /* 🔹 shared context는 여기서 1회만 생성 */
    SENSOR_FUSION_CTX* pstSensorFusionCtx = calloc(1, sizeof(SENSOR_FUSION_CTX));
    stEventEngine.pvSharedData = pstSensorFusionCtx;

    pstSensorFusionCtx->pstFusionEvent = event_new(stEventEngine.pstEventBase, -1, 0,
                  fusionEventCb, pstSensorFusionCtx);

    pstUdsRetryEvent = event_new(stEventEngine.pstEventBase,
                  -1, EV_PERSIST | EV_TIMEOUT,
                  uds1ReconnectCb, &stEventEngine);
    event_add(pstUdsRetryEvent, &stRertyTimeOut);

    int iListenFd = netUdsCreateServer(UDS_2_PATH);
    if (iListenFd < 0) {
        fprintf(stderr, "[UDS-SVR] netUdsCreateServer() failed\n");
        return EXIT_FAILURE;
    }
    /* Accept 이벤트 등록 */
    pstEventAccept = event_new(stEventEngine.pstEventBase, iListenFd, 
            EV_READ | EV_PERSIST, acceptCb, &stEventEngine);
    event_add(pstEventAccept, NULL);

    /* SIGINT 처리 등록 */
    pstSignalEvent = evsignal_new(stEventEngine.pstEventBase, 
        SIGINT, signalCb, &stEventEngine);
    event_add(pstSignalEvent, NULL);

    fprintf(stderr, "[UDS-SVR] Listening at %s\n", UDS_2_PATH);
    
    event_base_dispatch(stEventEngine.pstEventBase);
    if (pstUdsRetryEvent) {
        event_del(pstUdsRetryEvent);
        event_free(pstUdsRetryEvent);
        pstUdsRetryEvent = NULL;   
    }

    if(pstSignalEvent){
        event_del(pstSignalEvent);
        event_free(pstSignalEvent);
        pstSignalEvent =  NULL;
    }

    if(pstEventAccept){
        event_del(pstEventAccept);
        event_free(pstEventAccept);
        pstEventAccept =  NULL;
    }  

    /* === 종료 처리 === */
    eventEngineCleanup(&stEventEngine);
    event_base_free(stEventEngine.pstEventBase);
    free(pstSensorFusionCtx);

    fprintf(stderr,"[UDS-SVR] Terminated.\n");
    return 0;
}


/* === main === */
#ifndef GOOGLE_TEST
int main(int argc, char** argv)
{
    (void)argc; (void)argv;
    return run();
}
#endif
