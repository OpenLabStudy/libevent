/**
 * @file frame.h
 * @brief STX/ETX 기반 통신 프레임의 구조 정의 및 생성/파싱 API 제공
 *
 * 본 헤더는 요청(Request) 및 응답(Response) 프레임의 헤더, 페이로드, 테일 구조를 정의하고
 * 프레임 생성, 파싱, CRC 검증 등 프레임 처리 기능을 제공한다.
 *
 * Libevent 문서처럼 상세한 Doxygen 주석을 포함하며,
 * 변수 및 함수명은 헝거리안 표기법 + 카멜케이스 규칙을 따른다.
 */

#ifndef FRAME_H
#define FRAME_H

#include <stddef.h>

/* ========================================================================== */
/*  Constants                                                                 */
/* ========================================================================== */

/**
 * @name Frame Constants
 * @brief STX/ETX 값 및 최소 헤더 크기 정의
 * @{
 */
#define STX_CONST              0xAA55   /**< 프레임 시작(STX) 값 */
#define ETX_CONST              0x55AA   /**< 프레임 종료(ETX) 값 */
#define FRAME_HEADER_MIN_SIZE  6        /**< FRAME_HEADER의 최소 크기 */
/** @} */


/* ========================================================================== */
/*  Error Codes                                                               */
/* ========================================================================== */

/**
 * @enum FRAME_ERR
 * @brief 프레임 처리 과정에서 발생 가능한 오류 코드 정의
 *
 * 각 API의 반환값은 FRAME_ERR 타입이며, 정상 처리 시 FRAME_OK를 반환한다.
 */
typedef enum {
    FRAME_OK                    = 0,      /**< 정상 처리됨 */
    FRAME_ERR_NEED_MORE_DATA    = 1,      /**< 전체 프레임 구성에 필요한 데이터가 부족함 */

    FRAME_ERR_INVALID_STX       = -1,     /**< STX 값이 기대한 값과 다름 */
    FRAME_ERR_INVALID_ID        = -2,     /**< STX 값이 기대한 값과 다름 */
    FRAME_ERR_INVALID_ETX       = -3,     /**< ETX 값이 기대한 값과 다름 */
    FRAME_ERR_INVALID_CMD       = -4,     /**< 명령 코드가 유효하지 않음 */
    FRAME_ERR_INVALID_LENGTH    = -5,     /**< 페이로드 길이(DataLength)가 실제 데이터와 불일치 */
    FRAME_ERR_CRC_FAIL          = -6,     /**< CRC 값 불일치 */
    FRAME_ERR_NULL_PTR          = -7,     /**< NULL 포인터 전달됨 */
    FRAME_ERR_FRAME_TOO_SMALL   = -8,     /**< 프레임 길이가 최소 요구 크기보다 작음 */

    FRAME_ERR_UNKNOWN           = -100    /**< 정의되지 않은 알 수 없는 오류 */
} FRAME_ERR;


/* ========================================================================== */
/*  Structures                                                                */
/* ========================================================================== */

/**
 * @struct MSG_ID
 * @brief 송신자/수신자 ID 저장 구조체
 *
 * 메시지의 송신자(Source) 및 목적지(Destination) ID를 정의한다.
 */
typedef struct __attribute__((__packed__)) {
    unsigned char  uchSrcId;  /**< 송신자 ID */
    unsigned char  uchDstId;  /**< 수신자 ID */
} MSG_ID;


/**
 * @struct FRAME_HEADER
 * @brief 데이터 프레임 헤더 구조체
 *
 * STX, 데이터 길이, 송수신 ID, 서브모듈 ID, 명령(Command) 값을 포함한 기본 프레임 헤더.
 */
typedef struct __attribute__((__packed__)) {
    unsigned short  unStx;          /**< 프레임 시작(STX) 값 */
    int             iDataLength;    /**< 뒤따르는 페이로드(Data)의 바이트 길이 */
    MSG_ID          stMsgId;        /**< 송신자/수신자 ID */
    unsigned char   uchSubModule;   /**< 서브 모듈 ID */
    unsigned short  unCmd;          /**< 명령 코드 */
} FRAME_HEADER;


/**
 * @struct FRAME_TAIL
 * @brief CRC + ETX로 구성된 프레임 종료부 구조체
 */
typedef struct __attribute__((__packed__)) {
    unsigned char   uchCrc;   /**< 데이터 CRC 검증 값 */
    unsigned short  unEtx;    /**< 프레임 종료(ETX) 값 */
} FRAME_TAIL;


/* ========================================================================== */
/*  Public API Functions                                                      */
/* ========================================================================== */

/**
 * @brief 명령에 따른 페이로드 크기를 반환한다.
 *
 * @param unCmd 명령 코드
 * @return 페이로드 크기(바이트)
 */
int getDataSize(unsigned short unCmd);


/**
 * @brief 요청(Request) 프레임을 파싱하고 내부 명령 처리 루틴 수행
 *
 * @param puchRecvData   수신된 원본 데이터 버퍼
 * @param pstMsgId       송신자/수신자 ID
 * @param tDataLen       수신된 전체 데이터 길이
 * @param puchCmdResult  명령 처리 결과 출력 버퍼(페이로드)
 * @param piSendDataSize 생성된 데이터의 프레임 크기 출력 포인터
 *
 * @return FRAME_ERR 코드 (성공 시 FRAME_OK)
 */
FRAME_ERR commandHandler(unsigned char *puchRecvData,
                        MSG_ID *pstMsgId,
                        size_t tDataLen,
                        unsigned char *puchCmdResult,
                        int *piSendDataSize);


/**
 * @brief 응답(Response) 프레임 생성
 *
 * @param unCmd          명령 코드
 * @param pstMsgId       송신자/수신자 ID
 * @param puchCmdResult  응답 페이로드 데이터
 * @param puchSendData   생성된 전체 프레임 저장 버퍼
 *
 * @return FRAME_ERR (성공 시 FRAME_OK)
 */
FRAME_ERR makeResFrame(unsigned short unCmd, MSG_ID *pstMsgId,
                    unsigned char *puchCmdResult, unsigned char *puchSendData);


/**
 * @brief 요청(Request) 프레임 생성
 *
 * @param unCmd            요청 명령 코드
 * @param pstMsgId         송신자/수신자 ID
 * @param puchSendData     생성된 프레임 저장 버퍼
 * @param piOutFrameSize   생성된 전체 프레임 크기 출력 포인터
 *
 * @return FRAME_ERR (성공 시 FRAME_OK)
 */
FRAME_ERR makeReqFrame(unsigned short unCmd, MSG_ID *pstMsgId,
                    unsigned char *puchSendData, int *piOutFrameSize);


/**
 * @brief 응답(Response) 프레임 파싱 및 검증
 *
 * @param puchRecvData 수신 데이터
 * @param pstMsgId     송신자/수신자 ID
 * @param tDataLen     데이터 길이
 *
 * @return FRAME_ERR (성공 시 FRAME_OK)
 */
FRAME_ERR responseFrame(unsigned char *puchRecvData,
                        MSG_ID *pstMsgId, size_t tDataLen);


FRAME_ERR udsResponseFrame(unsigned char *puchRecvData, MSG_ID *pstMsgId, 
                    size_t tDataLen, unsigned char *puchResult);

/**
 * @brief 요청 프레임 검증 후 명령 코드 추출
 *
 * @param puchRecvData  수신 데이터
 * @param pstMsgId      송신자/수신자 ID
 * @param tDataLen      수신 길이
 * @param punOutCmd     추출된 CMD 출력 포인터
 *
 * @return FRAME_ERR (성공 시 FRAME_OK)
 */
FRAME_ERR requestFrame(unsigned char *puchRecvData,
                    MSG_ID *pstMsgId, size_t tDataLen, unsigned short *punOutCmd);


/**
 * @brief 전체 프레임 크기 계산
 *
 * @param puchData       프레임 시작 주소
 *
 * @return 프레임 길이
 */
int getFrameSize(unsigned char *puchData);

/**
 * @brief FRAME_ERR 오류 코드를 사람이 읽을 수 있는 문자열로 변환한다.
 *
 * Libevent의 event_err_to_string() 과 유사한 기능을 수행한다.
 *
 * @param eErr  FRAME_ERR 열거형 오류 코드
 *
 * @return 오류 메시지 문자열 (정적 문자열, free() 불필요)
 */
const char* frameErrToStr(FRAME_ERR eErr);



unsigned char getSrcId(unsigned char *puchRecvData);
unsigned char getDstId(unsigned char *puchRecvData);

#endif /* FRAME_H */
 