/**
 * @file r632Gps.h
 * @brief Hemisphere R632 GNSS Binary Parser API
 *
 * R632 GNSS 수신기의 $BIN 이진 포맷을 파싱하기 위한 인터페이스를 제공합니다.
 * UART 스트림 또는 파일 데이터 버퍼로부터 프레임 단위 파싱이 가능합니다.
 */

 #ifndef R632_GPS_H
 #define R632_GPS_H
 
 #include <stdint.h>
 
 #ifdef __cplusplus
 extern "C" {
 #endif
 
 #define R632_MAX_BUFFER 1024
 
 /* ================================================================
  * Structures
  * ================================================================ */
 
  /* ================================================================
  *  Hemisphere R632 Binary Message Structures
  * ================================================================ */
 
 /**
  * @struct SBinaryMsgHeader
  * @brief R632 메시지 헤더 구조체 ($BIN + ID + DataLength)
  */
 typedef struct
 {
     char           m_szSOH[4];          /**< "$BIN" */
     uint16_t       m_wBlockID;          /**< 메시지 ID */
     uint16_t       m_wDataLength;       /**< 데이터 길이 */
 } SBinaryMsgHeader;
 
 
 /**
  * @union SUnionMsgHeader
  * @brief 메시지 헤더 통합 구조 (향후 확장 대비)
  */
 typedef union
 {
     SBinaryMsgHeader m_stBytes;          /**< 8바이트 헤더 구조체 */
 } SUnionMsgHeader;
 
 /**
  * @struct SBinaryMsg3
  * @brief Hemisphere R632 Binary Message #3 전체 프레임 구조
  *
  * 헤더(8) + 데이터(가변) + 체크섬(2) + CRLF(2)
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
  * @brief R632 GPS 수신 데이터 버퍼 및 파싱 결과
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
 
 /* ================================================================
  * API 함수 선언
  * ================================================================ */
 
 /**
  * @brief UART로부터 들어온 R632 데이터 스트림 파싱
  *
  * @param pData   새로 수신된 데이터 버퍼
  * @param len     데이터 길이
  * @param pOut    파싱 결과 저장 구조체 (버퍼 포함)
  * @return        1: 유효 프레임 파싱 성공 / 0: 프레임 없음
  */
 char R632Feed(const uint8_t* pData, int len, SGpsDataInfo* pOut);
 
 /**
  * @brief 단일 프레임($BIN 헤더 포함)을 파싱하고 UTC 변환
  *
  * @param pFrame  프레임 데이터 ($BIN 헤더 포함)
  * @param size    프레임 길이
  * @return        SGpsDataInfo (m_chOk=1이면 유효)
  */
 SGpsDataInfo R632ParseFrame(const uint8_t* pFrame, int size);
 
 #ifdef __cplusplus
 }
 #endif
 
 #endif /* R632_GPS_H */
 