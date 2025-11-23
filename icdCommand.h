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
enum COMMAND_ID
{
    CMD_ID_INFO     = 0x0001, /**< 장비 정보 요청 */
    CMD_KEEP_ALIVE,           /**< 통신 상태 유지 */
    CMD_IBIT                  /**< 초기 Built-In-Test */
};

/**
 * @enum REQUEST_CMD_ID
 * @brief 요청(Request) 프레임에서 사용되는 명령 ID (REQ_CMD_OFFSET 적용)
 */
enum REQUEST_CMD_ID
{
    REQ_CMD_OFFSET       = 0x8000, /**< REQ Bit Mask */

    REQ_CMD_ID_INFO,              /**< 장비 정보 Request */
    REQ_CMD_KEEP_ALIVE,           /**< Keep-Alive Request */
    REQ_CMD_IBIT                  /**< IBIT Request */
};

/**
 * @enum RESPONSE_CMD_ID
 * @brief 응답(Response) 프레임에서 사용되는 명령 ID (RES_CMD_OFFSET 적용)
 */
enum RESPONSE_CMD_ID
{
    RES_CMD_OFFSET       = 0x4000, /**< RES Bit Mask */

    RES_CMD_ID_INFO,              /**< 장비 정보 Response */
    RES_CMD_KEEP_ALIVE,           /**< Keep-Alive Response */
    RES_CMD_IBIT                  /**< IBIT Response */
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
    char chIbit;
} REQ_IBIT;

/**
 * @struct RES_IBIT
 * @brief IBIT 응답 구조체
 */
typedef struct PACKED
{
    char chBitTotResult;
    char chPositionResult;
} RES_IBIT;


/* ========================================================================== */
/* Public API                                                                 */
/* ========================================================================== */

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

#endif /* ICD_COMMAND_H */
 