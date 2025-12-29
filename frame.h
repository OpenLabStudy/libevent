#ifndef FRAME_H
#define FRAME_H

#include <stddef.h>

/* ========================================================================== */
/*  Constants                                                                 */
/* ========================================================================== */

#define STX_CONST   0xAA55
#define ETX_CONST   0x55AA

/* ========================================================================== */
/*  Enums                                                                     */
/* ========================================================================== */

typedef enum {
    FRAME_OK                    = 0,
    FRAME_ERR_NEED_MORE_DATA    = 1,

    FRAME_ERR_INVALID_STX       = -1,
    FRAME_ERR_INVALID_ID        = -2,
    FRAME_ERR_INVALID_ETX       = -3,
    FRAME_ERR_INVALID_CMD       = -4,
    FRAME_ERR_INVALID_LENGTH    = -5,
    FRAME_ERR_CRC_FAIL          = -6,
    FRAME_ERR_NULL_PTR          = -7,
    FRAME_ERR_FRAME_TOO_SMALL   = -8,

    FRAME_ERR_UNKNOWN           = -100
} FRAME_ERR;

typedef enum {
    FRAME_TYPE_NONE = 0,
    FRAME_TYPE_REQUEST,
    FRAME_TYPE_RESPONSE
} FRAME_TYPE;

typedef enum {
    PROCESS_UNKNOWN=0,
    PROCESS_LOCAL,
    PROCESS_VIA_IPC
} PROCESS_PATH;
/* ========================================================================== */
/*  Structures                                                                */
/* ========================================================================== */

typedef struct __attribute__((__packed__)) {
    unsigned char  uchSrcId;
    unsigned char  uchDstId;
} MSG_ID;

typedef struct __attribute__((__packed__)) {
    unsigned short  unStx;
    int             iDataLength;
    MSG_ID          stMsgId;
    unsigned char   uchSubModule;
    unsigned short  unCmd;
} FRAME_HEADER;

typedef struct __attribute__((__packed__)) {
    unsigned char   uchCrc;
    unsigned short  unEtx;
} FRAME_TAIL;

/* ========================================================================== */
/*  Size Helpers                                                              */
/* ========================================================================== */

/**
 * @brief CMD + FrameType 기준 Payload 크기 반환
 */
int getDataSize(unsigned short unCmd, FRAME_TYPE eFrameType);

/**
 * @brief CMD + FrameType 기준 전체 Frame 크기 반환
 */
int getFrameSizeWithCmd(unsigned short unCmd, FRAME_TYPE eFrameType);

/**
 * @brief Frame 데이터에서 CMD를 찾아 전체 Frame 크기 반환
 *
 * @return >0 : frame size
 *         <0 : invalid / insufficient data
 */
int getFrameSizeWithData(unsigned char *puchData, FRAME_TYPE eFrameType);

/* ========================================================================== */
/*  Encode API                                                                */
/* ========================================================================== */

/**
 * @brief Request Frame 생성
 */
FRAME_ERR makeRequestFrame(unsigned short unCmd,
                           MSG_ID *pstMsgId,
                           unsigned char *puchSendData);

/**
 * @brief Response Frame 생성
 */
FRAME_ERR makeResponseFrame(unsigned short unCmd,
                            MSG_ID *pstMsgId,
                            unsigned char *puchCmdResult,
                            unsigned char *puchSendData);

/* ========================================================================== */
/*  Decode / Validation                                                       */
/* ========================================================================== */

/**
 * @brief Frame 검증 및 CMD 추출
 */
FRAME_ERR frameDecode(unsigned char *puchBuf,
                      int iFrameSize,
                      FRAME_TYPE eFrameType,
                      unsigned short *punOutCmd);

/**
 * @brief 최소 검증 후 CMD만 추출
 */
FRAME_ERR getCmdFromFrame(unsigned char *puchData,
                          int iDataSize,
                          unsigned short *punOutCmd);

/* ========================================================================== */
/*  Utilities                                                                 */
/* ========================================================================== */

/**
 * @brief 수신 Frame에서 Source ID 추출
 */
unsigned char getSrcId(unsigned char *puchRecvData);

/**
 * @brief 수신 Frame에서 Destination ID 추출
 */
unsigned char getDstId(unsigned char *puchRecvData);

/**
 * @brief Frame Error Code 문자열 변환
 */
const char* frameErrToStr(FRAME_ERR eErr);

/* ========================================================================== */
/*  Processing Decision                                                       */
/* ========================================================================== */

/**
 * @brief CMD 기반 처리 경로 결정
 */
PROCESS_PATH decideProcessingPath(unsigned char *puchRecvData);

/* ========================================================================== */
/*  Command Handler                                                           */
/* ========================================================================== */

/**
 * @brief 요청 프레임 처리 후 응답 Payload 생성
 *
 * @param puchRecvData   수신 Frame (Header 포함)
 * @param puchCmdResult  응답 Payload 출력 버퍼
 * @param piSendDataSize 응답 Payload 크기 반환
 */
FRAME_ERR commandHandler(unsigned char *puchRecvData,
                         unsigned char *puchCmdResult,
                         int *piSendDataSize);

FRAME_ERR parseAndDumpResponse(unsigned char *puchRecvData, unsigned char *puchResult);


char getIdInfo(unsigned char *puchData);

#endif /* FRAME_H */
