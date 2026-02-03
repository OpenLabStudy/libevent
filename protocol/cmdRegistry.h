#include <stddef.h>
#include <stdint.h>

typedef enum {
    COMMAND_PATH_FAIL = 0x00,
    COMMAND_PATH_NONE,
    CTRL_PC,
    TC_RCV_CMD_FROM_CTRL_PC,    
    ACU_CTRL_UART,
    GPS_RCV_UART,
    IMU_RCV_UART,
    EXTERN_RCV_NET,
    AC_SND_AZ_EL_TO_TC,
    TC_RCV_AZ_EL_FROM_AC,

    AC_RCV_AZ_EL_FROM_SF,
    SF_SND_AZ_EL_TO_AC,

    TC_SND_CMD_TO_CLN = 0x10,
    SF_RCV_CMD_FROM_TC,
    AC_RCV_CMD_FROM_TC,

    SF_RCV_SENSOR_DATA = 0x20,
    IMU_SND_TO_SF,
    GPS_SND_TO_SF

    
} COMMAND_PATH;

typedef enum {
    FRAME_TYPE_REQUEST = 0,
    FRAME_TYPE_RESPONSE
} FRAME_TYPE;

/* ========================================================================== */
/*  Constants                                                                 */
/* ========================================================================== */

#define STX_CONST   0xF0F0
#define ETX_CONST   0xFFFF

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
    FRAME_NOK                   = -9,

    FRAME_ERR_UNKNOWN           = -100
} FRAME_ERR;

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

/* 요청/응답 빌더/파서 공통 시그니처 */
typedef int (*buildForwardReq)(const void* pvUserData, void* pvOutData);
typedef int (*dispatchCommand)(const void* pvRecvData, void* pvOutData);
typedef int (*buildResponse)(const void* pvRecvData, void* pvOutData);

/* Command Descriptor */
typedef struct {
    unsigned short      unCmd;
    const char*         chCmdName;
    unsigned int        uiReqSize;
    unsigned int        uiResSize;    
    buildForwardReq     fnBuildForwardReq;
    dispatchCommand     fnDispatchCmd;//todo renaming
    buildResponse       fnbuildRes;
} CMD_DESC;

const char*     getWorkerName(int iId);
const char*     getCmdString(unsigned short unCmd);
unsigned int    getDataSize(unsigned short unCmd, FRAME_TYPE frameType);
unsigned int    getFrameSizeWithCmd(unsigned short unCmd, FRAME_TYPE eFrameType);
char            getIdInfo(char *puchData);
/* makeRequestFrame / parseAndDumpResponse 쪽에서 쓰기 좋게 제공 */
FRAME_ERR       createCmdRequest(unsigned short unCmd, MSG_ID *pstMsgId, void* uchUserData, void* pvOutData);
FRAME_ERR       cmdDispatch(const void* pvRecvData, int iFrameSize, void* pvOutData);
FRAME_ERR       createCmdResponse(unsigned short unCmd, const void* pvUserData, MSG_ID* pstMsgId, void* pvOutData);
const char*     frameErrToStr(FRAME_ERR eErr);
int             findFrameHeader(char *puchData, int iSize);
FRAME_ERR       cmdRegistryOverrideHandler( unsigned short unCmd,
                    buildForwardReq fnBuildForwardReq, dispatchCommand fnDispatchCmd, buildResponse fnbuildRes);
FRAME_ERR       repackageResponse(void* pvData, MSG_ID* pstMsgId, int iFrameSize);

FRAME_ERR       frameDecode(char *puchBuf, int iFrameSize,
                    FRAME_TYPE eFrameType, unsigned short *punOutCmd);