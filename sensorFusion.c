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

 #include "icdCommand.h"
 #include "eventEngine.h"
 #include "udsSvr.h"


 typedef struct {
    char            chValid;
    unsigned long   ulUsec;
    RES_GPS_DATA    stGps;
} GPS_STATE;

typedef struct {
    double dRoll;
    double dPitch;
    double dYaw;
} IMU_DATA;
typedef struct {
    char            chValid;
    unsigned long   ulUsec;
    IMU_DATA        stImu;
} IMU_STATE;

typedef struct {
    double dAz;
    double dEl;
} SP_DATA;
typedef struct {
    char            chValid;
    unsigned long   ulUsec;
    SP_DATA         stSp;
} SP_STATE;

typedef struct {
    double dLatitude;
    double dLongitude;
    double dAltitude;
} EXTERN_DATA;
typedef struct {
    char            chValid;
    unsigned long   ulUsec;
    EXTERN_DATA     stExtern;
} EXTERN_STATE;

typedef struct {
    double dAz;
    double dEl;
} KEYBOARD_DATA;
typedef struct {
    char            chValid;
    unsigned long   ulUsec;
    KEYBOARD_DATA   stKeyboard;
} KEYBOARD_STATE;

typedef enum{
    RECV_NONE = 0,
    RECV_GPS,
    RECV_IMU,
    RECV_SP,
    RECV_EXTERN,
    RECV_KEYBOARD
}RECV_SENSOR_FLAG;

typedef struct {
    RECV_SENSOR_FLAG    eSensorFalg;
    GPS_STATE           stGpsState;
    IMU_STATE           stImuState;
    SP_STATE            stSpState;
    EXTERN_STATE        stExternState;
} SENSOR_STATE;


/* ========================================================================== */
/* Application-Level Read Processing (UDS Server)                             */
/* ========================================================================== */

static void sensorFusionRead(int iFd, short nEvent, void* pvData)
{
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    IO_EVENT_TYPE eEventType = pstIoChannel->ePendingLogicEvent;
    unsigned char auchRecvBuffer[2048];    
    unsigned char auchCmdResult[1000];
    unsigned char auchSendBuf[1024];
    unsigned short unCmd = 0;
    FRAME_ERR eErr;
    int iSendLen = 0;
    SENSOR_STATE* pstSharedState;
    pstSharedState = (SENSOR_STATE *)pstIoChannel->pstEventEngine->pvSharedData;
    switch (eEventType) {
    case IO_EVT_RX_DATA:
        while (1) {
            unsigned int uiRecvLen = evbuffer_get_length(pstIoChannel->pstReadBuffer);
            /* 최소 헤더도 안 왔으면 중단 */
            if (uiRecvLen < sizeof(FRAME_HEADER))
                break;

            memset(auchRecvBuffer, 0x00, sizeof(auchRecvBuffer));
            int iCopyLen = evbuffer_copyout(pstIoChannel->pstReadBuffer, auchRecvBuffer, uiRecvLen);
            fprintf(stderr,"\n### %s():%d ###\n",__func__,__LINE__);
            for(int i=1; i<=iCopyLen; i++){
                fprintf(stderr,"%02X ", auchRecvBuffer[i-1]);
                if(i%16==0)
                    fprintf(stderr,"\n");
            }

            
            /*추후 evbuffer에 삭제 크기 알 필요 있음*/
            eErr = frameDecode(auchRecvBuffer, iCopyLen, FRAME_TYPE_RESPONSE, &unCmd);
            if (eErr != FRAME_OK) {
                fprintf(stderr, "[UDS-CLI] frameDecode ERR: %s\n", frameErrToStr(eErr));
                evbuffer_drain(pstIoChannel->pstReadBuffer, 1);
                continue;
            }

            /* === CMD 먼저 추출 (가벼운 파싱) === */
            int iFrameSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_RESPONSE);
            if (iCopyLen < iFrameSize)
                break;                
            /* === 프레임 하나 소비 === */
            evbuffer_drain(pstIoChannel->pstReadBuffer, iFrameSize);
            if(unCmd == CDM_GPS_DATA){
                RES_GPS_DATA* pstGpsData = (RES_GPS_DATA *)(auchRecvBuffer+sizeof(FRAME_HEADER));
                pstSharedState->stGpsState.stGps.dLatitude = pstGpsData->dLatitude;
                pstSharedState->stGpsState.stGps.dLongitude = pstGpsData->dLongitude;
                pstSharedState->stGpsState.stGps.dAltitude = pstGpsData->dAltitude;
                fprintf(stderr,"LATITUDE %lf\n", pstGpsData->dLatitude);
                fprintf(stderr,"LONGITUDE %lf\n", pstGpsData->dLongitude);
                fprintf(stderr,"ALTITUDE %lf\n", pstGpsData->dAltitude);
            }
        }
        break;

    case IO_EVT_CHANNEL_CLOSED:
        printf("[UDS-SVR] channel closed fd=%d\n", pstIoChannel->iFd);
        event_active(pstIoChannel->pstShutdownEvent, 0, 0);
        break;

    case IO_EVT_ERROR:
        printf("[UDS-SVR] channel error fd=%d\n", pstIoChannel->iFd);
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


/* ========================================================================== */
/* Main Entry Point                                                           */
/* ========================================================================== */
int run(void)
{
    EVENT_ENGINE   stEventEngine;
    struct event*   pstSignalEvent;
    struct event*   pstEventAccept;
    stEventEngine.pstEventBase = event_base_new();
    if (!stEventEngine.pstEventBase) {
        fprintf(stderr, "[UDS-SVR] event_base_new() failed\n");
        return EXIT_FAILURE;
    }

    /* Dispatcher 초기화 */
    eventEngineInit(&stEventEngine);
    stEventEngine.pvSharedData = (SENSOR_STATE *)malloc(sizeof(SENSOR_STATE));

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
