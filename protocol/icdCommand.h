/**
 * @file icdCommand.h
 * @brief ICD 기반 프레임 명령 처리 정의 및 응답 데이터 구조체 정의
 *
 * 본 헤더는 ICD(Interface Control Document) 통신 규약에 필요한
 * 명령 코드(Command ID), 요청/응답 구조체, API 함수 선언을 포함한다.
 *
 * - 명령 코드 정의 (REQ/RES 오프셋 포함)
 * - 각 명령에 대응하는 요청/응답 데이터 구조체 정의
 * - 명령 처리 함수(keepAlive / iBit) 선언
 *
 * @see icdCommand.c
 */

#ifndef ICD_COMMAND_H
#define ICD_COMMAND_H

#include <stdint.h>
#include <stddef.h>

/* ========================================================================== */
/* Command ID Definition                                                      */
/* ========================================================================== */

/**
 * @enum COMMAND_ID
 * @brief 명령 정의 (요청/응답 공통 Command ID)
 */
/* 추적 모드 */
enum TRACKING_MODE {
	IDLE = 0x00,
	SELF_TRACKING,
	PROGRAMMED_TRACKING,
	EXTERNAL_DEV_TRACKING
};

enum TRACKING_START_STOP {
	TRACKING_STOP = 0x00,
	TRACKING_START
};

enum AZ_EL_SEND_FLAG {
	AZ_EL_SEND_OFF = 0x00,
	AZ_EL_SEND_ON
};

enum ACU_MODE {
	RATE = 0x00,
	POSITION,
	ACU_MODE_NONE
};

enum AUTO_TRACKING_FLAG {
	AUTO_TRACKING_OFF = 0x00,
	AUTO_TRACKING_ON
};

enum COMMAND_ID
{    
    CMD_KEEP_ALIVE = 0x0000,           /**< 통신 상태 유지 */
    CMD_IBIT,                  /**< 초기 Built-In-Test */
    CMD_RBIT,
    CMD_CBIT,
    CMD_POSITIONER_AZ_EL_SET,
    CMD_TRACKING_SELECT,
    CMD_TRACKING_START_POINT_SET,
    CMD_CANNON_BALL_TRAJECTORY_INFO,
    CMD_SHELTER_COORDINATE_INFO = 0x0008,
    CMD_MCC_COORDINATE_INFO,
    CMD_CANNON_COORDINATE_INFO,
    CMD_TRACKING_CONTROL,
    CMD_POSITIONER_DEG_SEND,
    CMD_ACU_MODE_SELECT,
    CMD_TIME_SYNQ_CHECK,
    CMD_TIME_SYNQ_SET,
    CMD_AZ_EL_OFFSET_SET = 0x0010,
	CMD_EXTERN_DEV_SAMPLE_COUNT, //ICD v3.0에서 삭제함
	CMD_GET_ACU_AZ_EL_DATA,
	CMD_FPGA_TIME_SYNQ_SET,
	CMD_FPGA_TIME_SYNQ_CHECK,
	CMD_RADAR_COORDINATE_INFO,//ICD v3.0에서 삭제함
	CMD_PRE_PROGRAM_START_POINT,
	CMD_EL_CALIBRATION_CONFIG,
	CMD_TRUE_NORTH_SET,
	CMD_SCAN_START_STOP,//ICD v3.0에서 삭제함
	CMD_KALMAN_FILTER_SET,
	CMD_ALTITUDE_OFFSET_SET,
	CMD_EXTERN_PARAM_SET,
	CMD_IMU_OFFSET_SET,
	CMD_TARGET_LLA_SET,
	CMD_AUTO_TRACKING_WAIT,
	CMD_POSITIONER_AZ_EL = 0x0020,
	CMD_AN_HEUNG_TEST_SET,
	CMD_KEYBOARD_AZ_EL = 0x0030,
	CMD_SRIP_RING_SET = 0x0090,
	CMD_COMMAND_FAIL = 0x00FF,
    CDM_GPS_DATA = 0x7001,
    CDM_IMU_DATA,
    CDM_SP_DATA,
    CDM_EXTERN_DATA,
    CDM_KEYBOARD_DATA,
	CMD_CTRL_AZ_EL_DATA,
    CMD_ID_INFO     = 0x1000, /**< 장비 정보 요청 */
	CMD_UNKNOWN		= 0xFFFF
};



/* ========================================================================== */
/* Struct Definition (PACKED)                                                 */
/* ========================================================================== */

/** @brief 구조체 byte alignment 강제 */
#define PACKED __attribute__((__packed__))

/**
 * @struct REQ_ID
 * @brief CMD_ID_INFO 요청 구조체
 */
typedef struct PACKED {
    char chTmp;
} REQ_ID;

/**
 * @struct RES_ID
 * @brief CMD_ID_INFO 응답 구조체
 */
typedef struct PACKED {
    char chId;
} RES_ID;

/**
 * @struct REQ_KEEP_ALIVE
 * @brief Keep-Alive 요청 구조체
 */
typedef struct PACKED {
    char chTmp;
} REQ_KEEP_ALIVE;

/**
 * @struct RES_KEEP_ALIVE
 * @brief Keep-Alive 응답 구조체
 */
typedef struct PACKED {
    char chResult;
} RES_KEEP_ALIVE;

/**
 * @struct REQ_IBIT
 * @brief IBIT 요청 구조체
 */
typedef struct PACKED {
    char chBit;
} REQ_BIT;

/**
 * @struct RES_IBIT
 * @brief IBIT 응답 구조체
 */
typedef struct PACKED {
    char chBitTotResult;
    char chPositionResult;
} RES_BIT;

typedef struct PACKED {
	char chAzimuthDeg[8];
	char chElevationDeg[8];
} REQ_POSITIONER_AZ_EL_SET;

typedef struct PACKED {
	char chResult;
} RES_POSITIONER_AZ_EL_SET;

typedef struct PACKED {
	char chTrackingSelect;
} REQ_TRACKING_SELECT;

typedef struct PACKED {
	char chResult;
} RES_TRACKING_SELECT;

typedef struct PACKED {
	char chTrackingStartPoint;
} REQ_TRACKING_START_POINT_SET;

typedef struct PACKED {
	char chResult;
} RES_TRACKING_START_POINT_SET;

typedef struct PACKED {
	void* pvData;
} REQ_CANNON_BALL_TRAJECTORY_INFO;

typedef struct PACKED {
	char chResult;
} RES_CANNON_BALL_TRAJECTORY_INFO;

typedef struct PACKED {
	char chLatitude[8];
	char chLongitude[8];
	char chHeight[8];
} REQ_SHELTER_COORDINATE_INFO;

typedef struct PACKED {
	char chResult;
} RES_SHELTER_COORDINATE_INFO;

typedef struct PACKED {
	char chLatitude[8];
	char chLongitude[8];
	char chHeight[8];
} REQ_EXTERN_DEV_COORDINATE_INFO;

typedef struct PACKED {
	char chResult;
} RES_EXTERN_DEV_COORDINATE_INFO;

typedef struct PACKED {
	char chLatitude[8];
	char chLongitude[8];
	char chHeight[8];
} REQ_CANNON_COORDINATE_INFO;

typedef struct PACKED {
	char chResult;
} RES_CANNON_COORDINATE_INFO;


typedef struct PACKED {
	char chStartStop;
} REQ_TRACKING_CONTROL;

typedef struct PACKED {
	char chResult;
} RES_TRACKING_CONTROL;

typedef struct PACKED {
	char chSendOnOff;
} REQ_POSITIONER_DEG_SEND;

typedef struct PACKED {
	char chResult;
} RES_POSITIONER_DEG_SEND;

typedef struct PACKED {
	char chAcuMode;
} REQ_ACU_MODE;

typedef struct PACKED {
	char chResult;
} RES_ACU_MODE;

typedef struct PACKED {
	char chTimeSynqCheck;
} REQ_TIME_SYNQ_CHECK;

typedef struct PACKED {
	char chResult;
} RES_TIME_SYNQ_CHECK;

typedef struct PACKED {
	short nYear;
	char chMon;
	char chDay;
	char chHour;
	char chMin;
	char chSec;
} REQ_TIME_SYNQ_SET;

typedef struct PACKED {
	short nYear;
	char chMon;
	char chDay;
	char chHour;
	char chMin;
	char chSec;
} RES_TIME_SYNQ_SET;

typedef struct PACKED {
	int iAzOffset;
	int iElOffset;
} REQ_AZ_EL_OFFSET_SET;

typedef struct PACKED {
	char chResult;
} RES_AZ_EL_OFFSET_SET;

typedef struct PACKED {
    int iAz;
    int iEl;
} REQ_KEYBOARD_AZ_EL;
typedef struct PACKED {
    char chResult;
} RES_KEYBOARD_AZ_EL;

typedef struct PACKED {
	char chSelect;
} REQ_EXTERN_DEV_SELECT;

typedef struct PACKED {
	char chResult;
} RES_EXTERN_DEV_SELECT;

typedef struct PACKED {
    double dLatitude;
    double dLongitude;
    double dAltitude;
} RES_LLA_DATA;

typedef struct PACKED {
    double 	dLatitude;
    double 	dLongitude;
    float  	fAltitude;
	float	fHeading;
} RES_GPS_DATA;

typedef struct PACKED {
    double dRoll;
    double dPitch;
    double dYaw;
} RES_RPY_DATA;

typedef struct PACKED {
    double dAz;
    double dEl;
	unsigned char uchTriggerFlag;
} RES_SP_DATA;

typedef struct PACKED {
    double dAz;
    double dEl;
} REQ_KEYBOARD_DATA;
typedef struct PACKED {
    double dAz;
    double dEl;
} RES_KEYBOARD_DATA;


typedef struct PACKED {
    double dAz;
    double dEl;
} RES_AZ_EL_DATA;

typedef struct PACKED {
	char chWaitOnOff;
	double dStandbyAz;
	double dStandbyEl;
} REQ_AUTO_TRACKING_WAIT;

typedef struct PACKED {
	char chResult;
} RES_AUTO_TRACKING_WAIT;

typedef struct __attribute__((__packed__)){
	int iTime;
	int iAz;
	int iEl;
	char chExtSerialState;
	char chTriggerState;
	int iRecvAz;
	int iRecvEl;
}SEND_CURR_AZ_EL;




/* ========================================================================== */
/* Public API                                                                 */
/* ========================================================================== */
double endianChange(char* i_chData);


/**
 * @brief Keep-Alive 명령 처리 (통신 활성 여부 확인)
 *
 * 장비 또는 소프트웨어 측에서 연결 유지 여부를 판단하기 위한 명령이며,
 * 응답은 항상 `chResult = 0x01` 로 설정된다.
 *
 * @param puchRecvData     수신 원본 요청 데이터 버퍼 (미사용)
 * @param puchCmdResult    응답 데이터가 기록될 버퍼
 *
 * @return 응답 데이터 크기 (sizeof(RES_KEEP_ALIVE))
 *
 * @see RES_KEEP_ALIVE
 */


int buildForwardReqIdInfo(const void* pvUserData, void* pvOutData);
int dispatchCmdIdInfo(const void* pvRecvData, void* pvOutData);
int buildResIdInfo(const void* pvUserData, void* pvOutData);

int buildForwardReqKeepAlive(const void* pvUserData, void* pvOutData);
int dispatchCmdKeepAlive(const void* pvRecvData, void* pvOutData);
int buildResKeepAlive(const void* pvUserData, void* pvOutData);

int buildForwardReqIbit(const void* pvUserData, void* pvOutData);
int dispatchCmdIbit(const void* pvRecvData, void* pvOutData);
int buildResIbit(const void* pvUserData, void* pvOutData);

int buildForwardReqRbit(const void* pvUserData, void* pvOutData);
int dispatchCmdRbit(const void* pvRecvData, void* pvOutData);
int buildResRbit(const void* pvUserData, void* pvOutData);

int buildForwardReqCbit(const void* pvUserData, void* pvOutData);
int dispatchCmdCbit(const void* pvRecvData, void* pvOutData);
int buildResCbit(const void* pvUserData, void* pvOutData);

int buildForwardReqPositionAzElSet(const void* pvUserData, void* pvOutData);
int dispatchCmdPositionAzElSet(const void* pvRecvData, void* pvOutData);
int buildResPositionAzElSet(const void* pvUserData, void* pvOutData);

int buildForwardReqTrackingSelect(const void* pvUserData, void* pvOutData);
int dispatchCmdTrackingSelect(const void* pvRecvData, void* pvOutData);
int buildResTrackingSelect(const void* pvUserData, void* pvOutData);

int buildForwardReqAcuModeSelect(const void* pvUserData, void* pvOutData);
int dispatchCmdAcuModeSelect(const void* pvRecvData, void* pvOutData);
int buildResAcuModeSelect(const void* pvUserData, void* pvOutData);

int buildForwardReqAutoTrackingWait(const void* pvUserData, void* pvOutData);
int dispatchCmdAutoTrackingWait(const void* pvRecvData, void* pvOutData);
int buildResAutoTrackingWait(const void* pvUserData, void* pvOutData);

int buildForwardReqKeyboardAzEl(const void* pvUserData, void* pvOutData);
int dispatchKeyboardAzEl(const void* pvRecvData, void* pvOutData);
int buildResKeyboardAzEl(const void* pvUserData, void* pvOutData);

int buildForwardCurrAzEl(const void* pvUserData, void* pvOutData);
int dispatchCurrAzEl(const void* pvRecvData, void* pvOutData);
int buildResCurrAzEl(const void* pvUserData, void* pvOutData);





int buildResImuData(const void* pvUserData, void* pvOutData);

int buildResGpsData(const void* pvUserData, void* pvOutData);

int buildResCtrlAzElData(const void* pvUserData, void* pvOutData);

int buildResSpData(const void* pvUserData, void* pvOutData);

int buildResExternData(const void* pvUserData, void* pvOutData);

int buildResKeyboardData(const void* pvUserData, void* pvOutData);


#endif /* ICD_COMMAND_H */
 