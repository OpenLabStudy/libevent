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
	POSITION
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
    CDM_GPS_DATA = 0x7001,
    CDM_IMU_DATA,
    CDM_SP_DATA,
    CDM_EXTERN_DATA,
    CDM_KEYBOARD_DATA,
    CMD_COMMAND_FAIL = 0x00FF,
    CMD_ID_INFO     = 0x1000, /**< 장비 정보 요청 */
};
// #define CMD_KEEP_ALIVE							(0x0000)
// #define CMD_IBIT								(0x0001)
// #define CMD_RBIT								(0x0002)
// #define CMD_CBIT								(0x0003)
// #define CMD_POSITIONER_AZ_EL_SET			(0x0004)
// #define CMD_TRACKING_SELECT					(0x0005)
// #define CMD_TRACKING_START_POINT_SET		(0x0006)
// #define CMD_CANNON_BALL_TRAJECTORY_INFO	(0x0007)
// #define CMD_SHELTER_COORDINATE_INFO			(0x0008)
// #define CMD_MCC_COORDINATE_INFO				(0x0009)
// #define CMD_CANNON_COORDINATE_INFO			(0x000A)
// #define CMD_TRACKING_CONTROL					(0x000B)
// #define CMD_POSITIONER_DEG_SEND				(0x000C)
// #define CMD_ACU_MODE_SELECT					(0x000D)
// #define CMD_TIME_SYNQ_CHECK					(0x000E)
// #define CMD_TIME_SYNQ_SET						(0x000F)
// #define CMD_AZ_EL_OFFSET_SET					(0x0010)
// #define CMD_EXTERN_DEV_SAMPLE_COUNT			(0x0011)//ICD v3.0에서 삭제함
// #define CMD_GET_ACU_AZ_EL_DATA 				(0x0012)
// #define CMD_FPGA_TIME_SYNQ_SET 				(0x0013)
// #define CMD_FPGA_TIME_SYNQ_CHECK 			(0x0014)
// #define CMD_RADAR_COORDINATE_INFO			(0x0015)//ICD v3.0에서 삭제함
// #define CMD_PRE_PROGRAM_START_POINT			(0x0016)
// #define CMD_EL_CALIBRATION_CONFIG			(0x0017)
// #define CMD_TRUE_NORTH_SET					(0x0018)
// #define CMD_SCAN_START_STOP					(0x0019)//ICD v3.0에서 삭제함
// #define CMD_KALMAN_FILTER_SET				(0x001A)
// #define CMD_ALTITUDE_OFFSET_SET				(0x001B)
// #define CMD_EXTERN_PARAM_SET					(0x001C)
// #define CMD_IMU_OFFSET_SET					(0x001D)
// #define CMD_TARGET_LLA_SET					(0x001E)
// #define CMD_AUTO_TRACKING_WAIT				(0x001F)

// #define CMD_POSITIONER_AZ_EL					(0x0020)
// #define CMD_AN_HEUNG_TEST_SET				(0x0021)
// #define CMD_KEYBOARD_AZ_EL					(0x0030)
// #define CMD_SRIP_RING_SET						(0x0090)

// #define CMD_COMMAND_FAIL						(0x00FF)



/* ========================================================================== */
/* Struct Definition (PACKED)                                                 */
/* ========================================================================== */

/** @brief 구조체 byte alignment 강제 */
#define PACKED __attribute__((__packed__))

/**
 * @struct REQ_ID
 * @brief CMD_ID_INFO 요청 구조체
 */
typedef struct PACKED
{
    char chTmp;
} REQ_ID;

/**
 * @struct RES_ID
 * @brief CMD_ID_INFO 응답 구조체
 */
typedef struct PACKED
{
    char chResult;
} RES_ID;

/**
 * @struct REQ_KEEP_ALIVE
 * @brief Keep-Alive 요청 구조체
 */
typedef struct PACKED
{
    char chTmp;
} REQ_KEEP_ALIVE;

/**
 * @struct RES_KEEP_ALIVE
 * @brief Keep-Alive 응답 구조체
 */
typedef struct PACKED
{
    char chResult;
} RES_KEEP_ALIVE;

/**
 * @struct REQ_IBIT
 * @brief IBIT 요청 구조체
 */
typedef struct PACKED
{
    char chBit;
} REQ_BIT;

/**
 * @struct RES_IBIT
 * @brief IBIT 응답 구조체
 */
typedef struct PACKED
{
    char chBitTotResult;
    char chPositionResult;
} RES_BIT;


typedef struct PACKED{
	char chAzimuthDeg[8];
	char chElevationDeg[8];
}REQ_POSITIONER_AZ_EL_SET;

typedef struct PACKED{
	char chResult;
}RES_POSITIONER_AZ_EL_SET;


typedef struct PACKED{
	char chTrackingSelect;
}REQ_TRACKING_SELECT;

typedef struct PACKED{
	char chResult;
}RES_TRACKING_SELECT;


typedef struct PACKED{
	char chTrackingStartPoint;
}REQ_TRACKING_START_POINT_SET;

typedef struct PACKED{
	char chResult;
}RES_TRACKING_START_POINT_SET;


typedef struct PACKED{
	void* pvData;
}REQ_CANNON_BALL_TRAJECTORY_INFO;

typedef struct PACKED{
	char chResult;
}RES_CANNON_BALL_TRAJECTORY_INFO;


typedef struct PACKED{
	char chLatitude[8];
	char chLongitude[8];
	char chHeight[8];
}REQ_SHELTER_COORDINATE_INFO;

typedef struct PACKED{
	char chResult;
}RES_SHELTER_COORDINATE_INFO;


typedef struct PACKED{
	char chLatitude[8];
	char chLongitude[8];
	char chHeight[8];
}REQ_EXTERN_DEV_COORDINATE_INFO;

typedef struct PACKED{
	char chResult;
}RES_EXTERN_DEV_COORDINATE_INFO;


typedef struct PACKED{
	char chLatitude[8];
	char chLongitude[8];
	char chHeight[8];
}REQ_CANNON_COORDINATE_INFO;

typedef struct PACKED{
	char chResult;
}RES_CANNON_COORDINATE_INFO;


typedef struct PACKED{
	char chStartStop;
}REQ_TRACKING_CONTROL;

typedef struct PACKED{
	char chResult;
}RES_TRACKING_CONTROL;


typedef struct PACKED{
	char chSendOnOff;
}REQ_POSITIONER_DEG_SEND;

typedef struct PACKED{
	char chResult;
}RES_POSITIONER_DEG_SEND;

typedef struct PACKED{
	char chAcuMode;
}REQ_ACU_MODE;

typedef struct PACKED{
	char chResult;
}RES_ACU_MODE;


typedef struct PACKED{
	char chTimeSynqCheck;
}REQ_TIME_SYNQ_CHECK;

typedef struct PACKED{
	char chResult;
}RES_TIME_SYNQ_CHECK;

typedef struct PACKED{
	short nYear;
	char chMon;
	char chDay;
	char chHour;
	char chMin;
	char chSec;
}REQ_TIME_SYNQ_SET;

typedef struct PACKED{
	short nYear;
	char chMon;
	char chDay;
	char chHour;
	char chMin;
	char chSec;
}RES_TIME_SYNQ_SET;

typedef struct __attribute__((__packed__)){
	int iAzOffset;
	int iElOffset;
}REQ_AZ_EL_OFFSET_SET;

typedef struct __attribute__((__packed__)){
	char chResult;
}RES_AZ_EL_OFFSET_SET;

typedef struct PACKED{
	char chSelect;
}REQ_EXTERN_DEV_SELECT;

typedef struct PACKED{
	char chResult;
}RES_EXTERN_DEV_SELECT;


typedef struct PACKED
{
    double dLatitude;
    double dLongitude;
    double dAltitude;
} RES_LLA_DATA;

typedef struct PACKED
{
    double dRoll;
    double dPitch;
    double dYaw;
} RES_RPY_DATA;

typedef struct PACKED
{
    double dAz;
    double dEl;
} RES_AZ_EL_DATA;


/* ========================================================================== */
/* Public API                                                                 */
/* ========================================================================== */

int idInfo(unsigned char* puchRecvData, unsigned char* puchCmdResult);


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
int keepAlive(unsigned char* puchRecvData, unsigned char* puchCmdResult);

/**
 * @brief 초기 Built-In-Test (IBIT) 명령 처리 함수
 *
 * 장비의 상태를 확인하기 위한 테스트 기능이며
 * 총 결과와 Position 결과를 모두 `0x01` 로 반환한다.
 *
 * @param puchRecvData     수신 원본 요청 데이터 버퍼(미사용)
 * @param puchCmdResult    응답 데이터 저장 버퍼
 *
 * @return 응답 데이터 크기(sizeof(RES_IBIT))
 *
 * @see RES_IBIT
 */
int iBit(unsigned char* puchRecvData, unsigned char* puchCmdResult);


int rBit(unsigned char* puchRecvData, unsigned char* puchCmdResult);
int cBit(unsigned char* puchRecvData, unsigned char* puchCmdResult);
int positionAzElSet(unsigned char* puchRecvData, unsigned char* puchCmdResult);
int trackingSelect(unsigned char* puchRecvData, unsigned char* puchCmdResult);
int trackingStartPointSet(unsigned char* puchRecvData, unsigned char* puchCmdResult);
int cannonBallTrajectoryInfo(unsigned char* puchRecvData, unsigned char* puchCmdResult);
int shelterCoordinateInfo(unsigned char* puchRecvData, unsigned char* puchCmdResult);
int mccCoordinateInfo(unsigned char* puchRecvData, unsigned char* puchCmdResult);
int cannonCoordinateInifo(unsigned char* puchRecvData, unsigned char* puchCmdResult);
int trackingControl(unsigned char* puchRecvData, unsigned char* puchCmdResult);
int positionDegCtrl(unsigned char* puchRecvData, unsigned char* puchCmdResult);
int acuModeSelect(unsigned char* puchRecvData, unsigned char* puchCmdResult);
int timeSynqCheck(unsigned char* puchRecvData, unsigned char* puchCmdResult);
int timeSynqSet(unsigned char* puchRecvData, unsigned char* puchCmdResult);
int azElOffset(unsigned char* puchRecvData, unsigned char* puchCmdResult);



#endif /* ICD_COMMAND_H */
 