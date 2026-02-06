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

#include "eventEngine.h"
#include "icdCommand.h"
#include "cmdRegistry.h"
#include "netUds.h"
#include "netCore.h"
#include "ioChannelUtil.h"
#include "lineOfSight.h"
#include "runtime.h"
#include "udsClientRuntime.h"
#include "udsServerRuntime.h"

typedef struct {
    char            chValid;
    unsigned long   ulUsec;
    RES_GPS_DATA    stGps;
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

typedef struct{
    char chTrackingSelect;
    char chTrackingStartStop;
    double dCurrHeading;
    IMU_DATA stCurrImuData;
    AUTO_TRACKING_WAIT stAutoTrackingWait;
} COMMAND_STATE;
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
    COMMAND_STATE   stCommandState;

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
/* Trigger Decision                                                           */
/* ========================================================================== */
static FUSION_TRIGGER decideFusionTrigger(SENSOR_STATE* pstSensorState, unsigned long ulNowUsec)
{
    (void)ulNowUsec;
    //KEYBOARD수신이 가장 우선 처리되어야 함
    if (pstSensorState->stKeyboardState.chValid)
        return TRIG_KEYBOARD;

    if (pstSensorState->stSpState.chValid)
        return TRIG_SP;

    if (pstSensorState->stExternState.chValid &&
        pstSensorState->stGpsState.chValid)
        return TRIG_EXTERN;

    /* 4. GPS + IMU */
    #if 0
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
    #else
    //FOR TEST
    if (pstSensorState->stImuState.chValid )
        return TRIG_IMU;
    #endif

    return TRIG_NONE;
}

/* ========================================================================== */
/* Fusion Dispatcher (방위각, 고각 계산 전용 알고리즘 선택)                     */
/* ========================================================================== */
static void fusionDispatch(EVENT_ENGINE* pstEventEngine)
{
    SENSOR_STATE* pstSensorState = &((SENSOR_FUSION_CTX*)pstEventEngine->pvSharedData)->stSensor;
    SENSOR_FUSION_CTX* pstSensorFusionCtx = (SENSOR_FUSION_CTX*)pstEventEngine->pvSharedData;
    unsigned long ulNowUsec = nowUsec();
    FUSION_TRIGGER eTrigger = decideFusionTrigger(pstSensorState, ulNowUsec);
    unsigned char auchSendBuf[UDS_MAX_BUFFER_SIZE];
    unsigned char auchCtrlAzElData[UDS_MAX_BUFFER_SIZE];
    RES_AZ_EL_DATA *pstCtrlAzElData = (RES_AZ_EL_DATA *)auchCtrlAzElData;

    if (eTrigger == TRIG_NONE)
        return;

    IO_CHANNEL *pstIoChannel = ioFindChannelByWorkerId(pstEventEngine, SF_SND_AZ_EL_TO_AC);
    if (!ioIsChannelAlive(pstIoChannel)){
        return;
    }
    
    switch (eTrigger) {
    case TRIG_KEYBOARD:
        fprintf(stderr, "[SF_SND_AZ_EL_TO_AC] KEYBOARD override\n");
        pstSensorState->stKeyboardState.chValid = 0;
        //현재 수신된 키보드값으로 ACU제어 값 생성 후 UDS3으로 전송
        // pstSensorState->stKeyboardState.stKeyboard.dAz;
        // pstSensorState->stKeyboardState.stKeyboard.dEl;
        break;

    case TRIG_SP:
        fprintf(stderr, "[SF_SND_AZ_EL_TO_AC] SP PID control\n");
        pstSensorState->stSpState.chValid = 0;
        // PID제어 알고리즘 수행 후 ACU제어 값 생성 후 UDS3으로 전송
        // pstSensorState->stSpState.stSp.dAz;
        // pstSensorState->stSpState.stSp.dEl;
        break;

    case TRIG_EXTERN:
        fprintf(stderr, "[SF_SND_AZ_EL_TO_AC] EXTERN target calculation\n");
        pstSensorState->stExternState.chValid = 0;
        // GPS+IMU 융합 후 EXTERN 타겟 좌표 계산 → ACU제어 값 생성 후 UDS3으로 전송
        // pstSensorState->stExternState.stExtern.dLatitude;
        // pstSensorState->stExternState.stExtern.dLongitude;
        // pstSensorState->stExternState.stExtern.dAltitude;
        break;

    case TRIG_GPS:
    case TRIG_IMU:
    {
        // fprintf(stderr, "[SF_SND_AZ_EL_TO_AC] GPS+IMU attitude compensation (%s)\n",
        //         eTrigger == TRIG_GPS ? "GPS-trigger" : "IMU-trigger");
        double dAz, dEl;
        // GPS+IMU 융합 후 자세교정을 위한 알고리즘 수행후 → ACU제어 값 생성 후 UDS3으로 전송        
        pstSensorState->stGpsState.chValid = 0;
        pstSensorState->stImuState.chValid = 0;
        pstSensorFusionCtx->stCommandState.stCurrImuData.dRoll = pstSensorState->stImuState.stImu.dRoll;
        pstSensorFusionCtx->stCommandState.stCurrImuData.dPitch = pstSensorState->stImuState.stImu.dPitch;
        pstSensorFusionCtx->stCommandState.stCurrImuData.dYaw = pstSensorState->stImuState.stImu.dYaw;

        stabilizerCompute(  &pstSensorFusionCtx->stCommandState.stCurrImuData,
                            &pstSensorFusionCtx->stCommandState.stAutoTrackingWait,
                            &dAz, &dEl);
        pstCtrlAzElData->dAz = dAz;
        pstCtrlAzElData->dEl = -dEl;
        fprintf(stderr,"AZ:%lf, EL:%lf(%lf), Heading:%lf\n", dAz, dEl, -dEl, 
            pstSensorFusionCtx->stCommandState.dCurrHeading);
        break;
    }
    default:
        break;
    }
    fprintf(stderr,"### [%s & %s] ###\n",
        (pstSensorFusionCtx->stCommandState.stAutoTrackingWait.chWaitOnOff == AUTO_TRACKING_ON)?"AUTO TRACKING ON":"AUTO TRACKING OFF",
        (pstSensorFusionCtx->stCommandState.chTrackingStartStop == TRACKING_START)?"TRACKING START":"TRACKING STOP");
    if(pstSensorFusionCtx->stCommandState.stAutoTrackingWait.chWaitOnOff == AUTO_TRACKING_ON 
            || pstSensorFusionCtx->stCommandState.chTrackingStartStop == TRACKING_START)
    {
        MSG_ID stMsgId = { SF_SND_AZ_EL_TO_AC, AC_RCV_AZ_EL_FROM_SF };
        createCmdResponse(CMD_CTRL_AZ_EL_DATA, auchCtrlAzElData, &stMsgId, auchSendBuf);
        int iResultSize = getFrameSizeWithCmd(CMD_CTRL_AZ_EL_DATA, FRAME_TYPE_RESPONSE);
        evbuffer_add(pstIoChannel->pstWriteBuffer, auchSendBuf, iResultSize);
        event_add(pstIoChannel->pstWriteEvent, NULL);
    }

    pstSensorState->eLastTrig = eTrigger;
    pstSensorState->ulLastFusionUsec = ulNowUsec;
}


static void applyCommand(SENSOR_FUSION_CTX* pstSensorFusionCtx, unsigned short unCmd, 
    char *pchCmdData,  char *pchCmdResult)
{
    switch(unCmd)
    {
        case CMD_TRACKING_SELECT:
        {
            REQ_TRACKING_SELECT* pstReqTrackingSelect   = (REQ_TRACKING_SELECT *)pchCmdData;
            RES_TRACKING_SELECT* pstResTrackingSelect   = (RES_TRACKING_SELECT *)pchCmdResult;
            if(pstReqTrackingSelect->chTrackingSelect == SELF_TRACKING || 
                pstReqTrackingSelect->chTrackingSelect == EXTERNAL_DEV_TRACKING){
                pstSensorFusionCtx->stCommandState.chTrackingSelect = pstReqTrackingSelect->chTrackingSelect;
                pstResTrackingSelect->chResult = 0x01;
            }else{
                pstResTrackingSelect->chResult = 0x00;
            }
        }
        break;
        case CMD_AUTO_TRACKING_WAIT:
        {            
            REQ_AUTO_TRACKING_WAIT *pstReqAutoTrackingWait	                        = (REQ_AUTO_TRACKING_WAIT *)pchCmdData;
	        RES_AUTO_TRACKING_WAIT *pstReqUserData			                        = (RES_AUTO_TRACKING_WAIT *)pchCmdResult;
            
            pstSensorFusionCtx->stCommandState.stAutoTrackingWait.chWaitOnOff       = pstReqAutoTrackingWait->chWaitOnOff;
            pstSensorFusionCtx->stCommandState.stAutoTrackingWait.dStandbyAz        = pstReqAutoTrackingWait->dStandbyAz;
            pstSensorFusionCtx->stCommandState.stAutoTrackingWait.dStandbyEl        = pstReqAutoTrackingWait->dStandbyEl;
            pstSensorFusionCtx->stCommandState.stAutoTrackingWait.dStandbyYaw       = pstSensorFusionCtx->stCommandState.stCurrImuData.dYaw;
            pstReqUserData->chResult = 0x01;
            // calcRefDCM(&pstSensorFusionCtx->stCommandState.stCurrImuData, 
            //     pstSensorFusionCtx->stCommandState.stAutoTrackingWait.dRefDcm);
            fprintf(stderr,"AUTO TRACKING WAIT %s, AZ:%.3lf, EL:%.3lf, HEADING:%.3lf\n",
                            (pstReqAutoTrackingWait->chWaitOnOff==0x01)? "ON" : "OFF",
                            pstSensorFusionCtx->stCommandState.stAutoTrackingWait.dStandbyAz,
                            pstSensorFusionCtx->stCommandState.stAutoTrackingWait.dStandbyEl,
                            pstSensorFusionCtx->stCommandState.dCurrHeading );      
        }
        break;
        case CMD_ID_INFO:
        {
            RES_ID *pstResId = (RES_ID *)(pchCmdResult);
            pstResId->chId = (char)SF_RCV_CMD_FROM_TC;
            fprintf(stderr, "RES_ID %04X\n", pstResId->chId);
        }
        break;
        default:
        break;
    }
}


static void commandEventCb(int iFd, short nEvent, void* pvData)
{
    (void)iFd;
    (void)nEvent;
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    IO_EVENT_TYPE eEventType = pstIoChannel->ePendingLogicEvent;
    SENSOR_FUSION_CTX* pstSensorFusionCtx =
        (SENSOR_FUSION_CTX*)pstIoChannel->pstEventEngine->pvSharedData;

    char achRecvBuffer[UDS_MAX_BUFFER_SIZE];
    unsigned short unCmd = 0;
    FRAME_ERR eErr;
    
    switch (eEventType) {
    case IO_EVT_CHANNEL_CLOSED:
    case IO_EVT_ERROR:
        ioMarkChannelDead(pstIoChannel, pstIoChannel->ePendingLogicEvent);
        event_active(pstIoChannel->pstShutdownEvent, 0, 0);
        break;
    case IO_EVT_RX_DATA:
        while (1) {
            int iRecvLen = evbuffer_get_length(pstIoChannel->pstReadBuffer);
            fprintf(stderr,"SF_RCV_CMD_FROM_TC %s():%d Recv Size is %d ###\n", __func__, __LINE__, iRecvLen);
            if (iRecvLen < (int)sizeof(FRAME_HEADER))
                break;

            memset(achRecvBuffer, 0x00, sizeof(achRecvBuffer));
            int iCopyLen = evbuffer_copyout(pstIoChannel->pstReadBuffer, achRecvBuffer, iRecvLen);
            eErr = frameDecode(achRecvBuffer, iCopyLen, FRAME_TYPE_REQUEST, &unCmd);
            if (eErr != FRAME_OK) {
                fprintf(stderr, "[SF_RCV_CMD_FROM_TC] frameDecode ERR: %s\n", frameErrToStr(eErr));
                int iDeleteDataSize = findFrameHeader(achRecvBuffer, iCopyLen);
                evbuffer_drain(pstIoChannel->pstReadBuffer, iDeleteDataSize);
                continue;
            }
            int iFrameSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_REQUEST);
            /* === 프레임 소비 === */
            char achCmdData[128];
            char achCmdResult[128];
            char achResult[128];
            unsigned int uiReqId;
            int iResultSize;
            memset(achCmdData, 0x0, sizeof(achCmdData));
            memset(achCmdResult, 0x0, sizeof(achCmdResult));
            memset(achResult, 0x0, sizeof(achResult));
            evbuffer_drain(pstIoChannel->pstReadBuffer, iFrameSize);
            evbuffer_remove(pstIoChannel->pstReadBuffer, &uiReqId, sizeof(unsigned int));
            eErr = cmdDispatch(achRecvBuffer, iCopyLen, achCmdData);
            if (eErr != FRAME_OK){
                fprintf(stderr,"SF_RCV_CMD_FROM_TC ERROR:%s\n", frameErrToStr(eErr));
                continue;
            }            
            applyCommand(pstSensorFusionCtx, unCmd, achCmdData, achCmdResult);
            MSG_ID stMsgId = { SF_RCV_CMD_FROM_TC, TC_SND_CMD_TO_CLN };
            eErr = createCmdResponse(unCmd, achCmdResult, &stMsgId, achResult);
            iResultSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_RESPONSE);
            evbuffer_add(pstIoChannel->pstWriteBuffer, achResult, iResultSize);
            event_add(pstIoChannel->pstWriteEvent, NULL);
        }
        break;
    default:
        /* TX-only: ignore */
        break;
    }
    pstIoChannel->ePendingLogicEvent = IO_EVENT_NONE;
}

static void fusionEventCb(int iFd, short nEvent, void* pvData)
{
    (void)iFd;
    (void)nEvent;
    EVENT_ENGINE* pstEventEngine = (EVENT_ENGINE *)pvData;
    SENSOR_FUSION_CTX* pstSensorFusionCtx = (SENSOR_FUSION_CTX*)pstEventEngine->pvSharedData;

    pstSensorFusionCtx->chFusionPending = 0;
    fusionDispatch(pstEventEngine);
}


/* ========================================================================== */
/* Application-Level Read Processing (UDS Server)                             */
/* ========================================================================== */
static void sensorFusionRead(int iFd, short nEvent, void* pvData)
{
    (void)iFd;
    (void)nEvent;
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    SENSOR_FUSION_CTX* pstSensorFusionCtx =
        (SENSOR_FUSION_CTX*)pstIoChannel->pstEventEngine->pvSharedData;
    SENSOR_STATE* pstSensorState = &pstSensorFusionCtx->stSensor;
    IO_EVENT_TYPE eEventType = pstIoChannel->ePendingLogicEvent;
    char achRecvBuffer[UDS_MAX_BUFFER_SIZE];
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

            memset(achRecvBuffer, 0x00, sizeof(achRecvBuffer));
            int iCopyLen = evbuffer_copyout(pstIoChannel->pstReadBuffer, achRecvBuffer, uiRecvLen);
            /*추후 evbuffer에 삭제 크기 알 필요 있음*/
            eErr = frameDecode(achRecvBuffer, iCopyLen, FRAME_TYPE_RESPONSE, &unCmd);
            if (eErr != FRAME_OK) {
                fprintf(stderr, "[SF_RCV_SENSOR_DATA] frameDecode ERR: %s\n", frameErrToStr(eErr));
                int iDeleteDataSize = findFrameHeader(achRecvBuffer, iCopyLen);
                evbuffer_drain(pstIoChannel->pstReadBuffer, iDeleteDataSize);
                continue;
            }

            int iFrameSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_RESPONSE);
            if (iCopyLen < iFrameSize)
                break;                
            evbuffer_drain(pstIoChannel->pstReadBuffer, iFrameSize);
            char achCmdData[128];
            char achResult[128];
            memset(achCmdData, 0x0, sizeof(achCmdData));
            memset(achResult, 0x0, sizeof(achResult));
            eErr = cmdDispatch(achRecvBuffer, iCopyLen, achCmdData);
            switch(unCmd){
                case CDM_GPS_DATA: {
                    memcpy(&pstSensorState->stGpsState.stGps, achRecvBuffer+sizeof(FRAME_HEADER), sizeof(RES_GPS_DATA));
                    pstSensorState->stGpsState.chValid = 1;
                    pstSensorState->stGpsState.ulUsec  = ulUsec;
                    // fprintf(stderr,"GPS LATITUDE %.8lf, LONGITUDE %.8lf, ALTITUDE %.3f, HEADING %.3f\n", 
                    //     pstSensorState->stGpsState.stGps.dLatitude, pstSensorState->stGpsState.stGps.dLongitude, 
                    //     pstSensorState->stGpsState.stGps.fAltitude, pstSensorState->stGpsState.stGps.fHeading);
                    break;                
                }
                case CDM_IMU_DATA: {
                    memcpy(&pstSensorState->stImuState.stImu, achRecvBuffer+sizeof(FRAME_HEADER), sizeof(RES_RPY_DATA));
                    pstSensorState->stImuState.chValid = 1;
                    pstSensorState->stImuState.ulUsec  = ulUsec;
                    pstSensorFusionCtx->stCommandState.dCurrHeading = pstSensorState->stImuState.stImu.dYaw;                    
                    // fprintf(stderr,"IMU ROLL %lf, PITCH %lf, YAW %lf [%02X]\n", pstSensorState->stImuState.stImu.dRoll,
                    //     pstSensorState->stImuState.stImu.dPitch, pstSensorState->stImuState.stImu.dYaw, pstSensorFusionCtx->chFusionPending);
                    break;
                }
                case CDM_SP_DATA: {
                    memcpy(&pstSensorState->stSpState.stSp, achRecvBuffer+sizeof(FRAME_HEADER), sizeof(RES_AZ_EL_DATA));
                    pstSensorState->stSpState.chValid = 1;
                    pstSensorState->stSpState.ulUsec  = ulUsec;
                    fprintf(stderr,"SP AZ %lf, EL %lf\n", pstSensorState->stSpState.stSp.dAz, pstSensorState->stSpState.stSp.dEl);
                    break;
                }
                case CDM_EXTERN_DATA: {
                    memcpy(&pstSensorState->stExternState.stExtern, achRecvBuffer+sizeof(FRAME_HEADER), sizeof(RES_LLA_DATA));
                    pstSensorState->stExternState.chValid = 1;
                    pstSensorState->stExternState.ulUsec  = ulUsec;
                    fprintf(stderr,"EXTERN LATITUDE %.8lf, LONGITUDE %.8lf, ALTITUDE %.3lf\n", pstSensorState->stExternState.stExtern.dLatitude, 
                    pstSensorState->stExternState.stExtern.dLongitude, pstSensorState->stExternState.stExtern.dAltitude);
                    break;                
                }
                case CDM_KEYBOARD_DATA: {
                    memcpy(&pstSensorState->stKeyboardState.stKeyboard, achRecvBuffer+sizeof(FRAME_HEADER), sizeof(RES_AZ_EL_DATA));
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
        fprintf(stderr,"[SF_RCV_SENSOR_DATA] channel error fd=%d\n", pstIoChannel->iFd);
        ioMarkChannelDead(pstIoChannel, eEventType);
        event_active(pstIoChannel->pstShutdownEvent, 0, 0);
        break;

    default:
        break;
    }
    pstIoChannel->ePendingLogicEvent = IO_EVENT_NONE;
}



/* ========================================================================== */
/* Main Entry Point                                                           */
/* ========================================================================== */
int run(void)
{
    ioIgnoreSigpipeOnce();
    EVENT_ENGINE    stEventEngine;
    UDS_SERVER_RUNTIME_CFG stRcvDataUdsSvrRuntimeCfg = {
        .pchUdsPath     = UDS_2_PATH,
        .pchTag         = "SF_RCV_SENSOR_DATA",
        .chWorkerId     = SF_RCV_SENSOR_DATA,
        .chDstWorkerId  = IMU_SND_TO_SF|GPS_SND_TO_SF,
        .eRole          = ROLE_REQUESTER,
        .eType          = TYPE_UDS_SVR,
        .pfWrite        = NULL,
        .pfIoHandler    = sensorFusionRead
    };
    UDS_CLIENT_RUNTIME_CFG stRcvCmdUdsClnRuntimeCfg = {
        .chWorkerId     = SF_RCV_CMD_FROM_TC,
        .chDstWorkerId  = TC_SND_CMD_TO_CLN,
        .pchUdsPath     = UDS_1_PATH,
        .eRole          = ROLE_REQUESTER,
        .eType          = TYPE_UDS_CLI,
        .pchTag         = "SF_RCV_CMD_FROM_TC"
    };    
    UDS_CLIENT_RUNTIME_CFG stSndAzElUdsClnRuntimeCfg = {
        .chWorkerId     = SF_SND_AZ_EL_TO_AC,
        .chDstWorkerId  = AC_RCV_AZ_EL_FROM_SF,
        .pchUdsPath     = UDS_3_PATH,
        .eRole          = ROLE_REQUESTER,
        .eType          = TYPE_UDS_CLI,
        .pchTag         = "SF_SND_AZ_EL_TO_AC"
    };
    

    stEventEngine.pstEventBase = event_base_new();
    if (!stEventEngine.pstEventBase) {
        fprintf(stderr, "[SENSOR_FUSION] event_base_new() failed\n");
        return EXIT_FAILURE;
    }

    /* Dispatcher 초기화 */
    eventEngineInit(&stEventEngine, 0);
    /* shared context는 여기서 1회만 생성 */
    SENSOR_FUSION_CTX* pstSensorFusionCtx = calloc(1, sizeof(SENSOR_FUSION_CTX));
    stEventEngine.pvSharedData = pstSensorFusionCtx;

    pstSensorFusionCtx->pstFusionEvent = event_new(stEventEngine.pstEventBase, -1, 0,
                  fusionEventCb, &stEventEngine);
    /* Accept 이벤트 등록 */
    UDS_SERVER_RUNTIME *pstAzElSvr =
        udsServerRuntimeCreate(&stEventEngine, &stRcvDataUdsSvrRuntimeCfg, pstSensorFusionCtx);

    UDS_CLIENT_RUNTIME *pstRcvCmdUdsClnRuntime = udsClientRuntimeCreate(&stEventEngine, 
        &stRcvCmdUdsClnRuntimeCfg, commandEventCb, NULL);
    usleep(3 * 1000);
    //todo udsClientRuntimeCreate을 거의 동시 진입시 놓치는 경우 발생
    UDS_CLIENT_RUNTIME *pstSndAzElUdsClnRuntime = udsClientRuntimeCreate(&stEventEngine, 
        &stSndAzElUdsClnRuntimeCfg, NULL, NULL);
    APP_SIGNAL_HANDLE *pstSigHandle = appSignalCreate(&stEventEngine, "SENSOR-FUSION");
    fprintf(stderr, "[SENSOR_FUSION] Listening at %s\n", UDS_2_PATH);
        
    event_base_dispatch(stEventEngine.pstEventBase);
    udsClientRuntimeDestroy(&pstRcvCmdUdsClnRuntime);
    udsClientRuntimeDestroy(&pstSndAzElUdsClnRuntime);
    udsServerRuntimeDestroy(&pstAzElSvr);
    appSignalDestroy(&pstSigHandle);

    /* === 종료 처리 === */
    eventEngineCleanup(&stEventEngine);
    event_base_free(stEventEngine.pstEventBase);
    free(pstSensorFusionCtx);

    fprintf(stderr,"[SENSOR_FUSION] Terminated.\n");
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
