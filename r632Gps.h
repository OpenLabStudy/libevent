/**
 * @file r632Gps.h
 * @brief Hemisphere R632 GNSS Binary Message Parser API
 *
 * Hemisphere R632 GNSS 수신기의 `$BIN` 이진 메시지를 파싱하는 라이브러리입니다.
 * UART 스트림 형태의 연속적인 입력 데이터를 프레임 단위로 분석하고,
 * 메시지 #3(BinaryMsg3) 구조체 기반으로 위치·자세·품질 정보를 제공합니다.
 */

#ifndef R632_GPS_H
#define R632_GPS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/* Defines                                                                    */
/* ========================================================================== */

#define R632_MAX_BUFFER     1024   /**< 내부 누적 버퍼 최대 크기 */


/* ========================================================================== */
/* Type Definitions                                                           */
/* ========================================================================== */

/**
 * @struct SBinaryMsgHeader
 * @brief R632 메시지 기본 헤더 (`"$BIN"` + BlockID + DataLength)
 */
typedef struct
{
    char      m_szSOH[4];      /**< 항상 `"$BIN"` */
    uint16_t  m_wBlockID;      /**< 메시지 ID */
    uint16_t  m_wDataLength;   /**< 뒤에 이어지는 데이터 길이 */
} SBinaryMsgHeader;


/**
 * @union SUnionMsgHeader
 * @brief 구조 확장을 대비한 헤더 래핑 구조
 */
typedef union
{
    SBinaryMsgHeader m_stBytes;
} SUnionMsgHeader;


/**
 * @struct SBinaryMsg3
 * @brief Hemisphere R632 Binary Message #3 구조체
 *
 * 구성: Header(8 bytes) + Payload + Checksum(2B) + CRLF(0x0D 0x0A)
 */
typedef struct
{
SUnionMsgHeader m_stHead;            /**< 메시지 헤더 */

double         m_dGpsTow;           /**< GPS Time of Week */
uint16_t       m_wGpsWeek;          /**< GPS Week */
uint16_t       m_wNumSatsTracked;   /**< 추적 중인 위성 수 */
uint16_t       m_wNumSatsUsed;      /**< 사용 중인 위성 수 */
unsigned char  m_byNavMode;         /**< 항법 모드 */
unsigned char  m_bySpare00;         /**< 예비 필드 */

double         m_dLatitude;         /**< 위도 (deg) */
double         m_dLongitude;        /**< 경도 (deg) */
float          m_fHeight;           /**< 고도 (m) */
float          m_fSpeed;            /**< 수평 속도 (m/s) */
float          m_fVUp;              /**< 수직 속도 (m/s, +up) */
float          m_fCog;              /**< 지면방향 (deg) */
float          m_fHeading;          /**< 헤딩 (deg) */
float          m_fPitch;            /**< 피치 (deg) */
float          m_fRoll;             /**< 롤 (deg) */

uint16_t       m_wAgeOfDiff;        /**< 보정 데이터 연령 */
uint16_t       m_wAttitudeStatus;   /**< 자세 상태 플래그 */

float          m_fStdevHeading;     /**< Heading 표준편차 */
float          m_fStdevPitch;       /**< Pitch 표준편차 */
float          m_fHrms;             /**< 수평 RMS */
float          m_fVrms;             /**< 수직 RMS */
float          m_fHdop;             /**< HDOP */
float          m_fVdop;             /**< VDOP */
float          m_fTdop;             /**< TDOP */

float          m_fCovNN;            /**< Covariance North-North */
float          m_fCovNE;
float          m_fCovNU;
float          m_fCovEE;
float          m_fCovEU;
float          m_fCovUU;

uint16_t       m_wChecksum;         /**< Checksum (헤더+데이터의 합 16비트) */
uint16_t       m_wCrlf;             /**< CRLF (0x0D0A) */
} SBinaryMsg3;

/**
 * @struct SGpsDataInfo
 * @brief 내부 누적 수신 버퍼 및 파싱 상태 구조체
 */
typedef struct
{
    SBinaryMsg3    m_stMsg3;                            /**< 마지막 파싱된 프레임 */
    int            m_iTotSize;                          /**< 누적 버퍼 크기 */
    int            m_iOffset;                           /**< 파싱된 오프셋 위치 */
    char           m_szGpsData[R632_MAX_BUFFER];        /**< 누적 데이터 버퍼 */
    char           m_szTime[64];                        /**< 변환된 UTC 문자열 */
    char           m_chOk;                              /**< 파싱 성공 여부 (1=성공) */
} SGpsDataInfo;
 

/* ========================================================================== */
/* Public API                                                                 */
/* ========================================================================== */

/**
 * @brief UART 수신 스트림 기반 프레임 파싱 처리
 *
 * @param pData  신규 수신 UART 데이터
 * @param len    수신 길이
 * @param pOut   파싱 결과 저장 구조체
 * @return       1: 유효 프레임 파싱 성공, 0: 프레임 없음
 */
char R632Feed(const uint8_t* pData, int len, SGpsDataInfo* pOut);

/**
 * @brief 단일 R632 프레임($BIN 포함)을 파싱하여 구조체 변환
 *
 * @param pFrame   프레임 시작 주소
 * @param size     프레임 길이
 * @return         SGpsDataInfo (m_chOk=1이면 유효)
 */
SGpsDataInfo R632ParseFrame(const uint8_t* pFrame, int size);


#ifdef __cplusplus
}
#endif

#endif /* R632_GPS_H */
 