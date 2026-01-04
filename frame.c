#include "frame.h"
#include "icdCommand.h"

#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>

/* ========================================================================== */
/*  CRC Utilities                                                             */
/* ========================================================================== */

static unsigned char frameCalcCrc(const unsigned char *puchBuf, int iTotalSize)
{
    unsigned char uchCrc = 0x00;
    int iOffset =
        sizeof(((FRAME_HEADER *)0)->unStx) +
        sizeof(((FRAME_HEADER *)0)->iDataLength);
    for (int i = iOffset; i < iTotalSize - sizeof(FRAME_TAIL); i++) {
        uchCrc += puchBuf[i];
    }

    return ((uchCrc & 0xFF) == 0xFF) ? 0x00 : uchCrc;
}

static FRAME_ERR frameCheckCrc(const unsigned char *puchBuf,
                               int iTotalSize,
                               unsigned char uchRecvCrc)
{
    return (frameCalcCrc(puchBuf, iTotalSize) == uchRecvCrc) ?
            FRAME_OK : FRAME_ERR_CRC_FAIL;
}

/* ========================================================================== */
/*  Command Validation                                                        */
/* ========================================================================== */

static FRAME_ERR checkCmd(unsigned short unCmd)
{
    switch (unCmd) {
        case CMD_ID_INFO:
        case CMD_KEEP_ALIVE:
        case CMD_IBIT:
        
        case CMD_RBIT:
        case CMD_CBIT:
        case CMD_POSITIONER_AZ_EL_SET:
        case CMD_TRACKING_SELECT:
        case CMD_TRACKING_START_POINT_SET:
        case CMD_CANNON_BALL_TRAJECTORY_INFO:
        case CMD_SHELTER_COORDINATE_INFO:
        case CMD_MCC_COORDINATE_INFO:
        case CMD_CANNON_COORDINATE_INFO:
        case CMD_TRACKING_CONTROL:
        case CMD_POSITIONER_DEG_SEND:
        case CMD_ACU_MODE_SELECT:
        case CMD_TIME_SYNQ_CHECK:
        case CMD_TIME_SYNQ_SET:
        case CMD_AZ_EL_OFFSET_SET:
        case CDM_GPS_DATA:
        case CDM_IMU_DATA:
        case CDM_SP_DATA:
        case CDM_EXTERN_DATA:
        case CDM_KEYBOARD_DATA:        
            return FRAME_OK;
        default:
            return FRAME_ERR_INVALID_CMD;
    }
}



/* ========================================================================== */
/*  Frame Header / Tail Build                                                  */
/* ========================================================================== */

static void frameMakeHeader(unsigned short unCmd,
                            const MSG_ID *pstMsgId,
                            unsigned char *puchBuf,
                            FRAME_TYPE eFrameType)
{
    FRAME_HEADER *pstHeader = (FRAME_HEADER *)puchBuf;

    pstHeader->unStx        = htons(STX_CONST);
    pstHeader->iDataLength  = htonl(getDataSize(unCmd, eFrameType));
    pstHeader->stMsgId      = *pstMsgId;
    pstHeader->uchSubModule = 0x00;
    pstHeader->unCmd        = htons(unCmd);
}

static FRAME_TAIL* frameGetTailPtr(unsigned char *puchData,
                                   unsigned short unCmd,
                                   FRAME_TYPE eFrameType)
{
    return (FRAME_TAIL *)(puchData
            + sizeof(FRAME_HEADER)
            + getDataSize(unCmd, eFrameType));
}

static void frameMakeTail(unsigned short unCmd,
                          unsigned char *puchBuf,
                          FRAME_TYPE eFrameType)
{
    FRAME_TAIL *pstTail = frameGetTailPtr(puchBuf, unCmd, eFrameType);
    pstTail->uchCrc = frameCalcCrc(puchBuf, getFrameSizeWithCmd(unCmd, eFrameType));
    pstTail->unEtx = htons(ETX_CONST);
}


/* ========================================================================== */
/*  Encode API                                                                */
/* ========================================================================== */

int findFrameHeader(unsigned char *puchData, int iSize)
{
    if (!puchData || iSize < 2)
        return -1;

    for (int i = 0; i <= iSize - 2; i++) {
        /* STX = 0xAA55 (network order) */
        if (puchData[i] == 0xAA && puchData[i + 1] == 0x55) {
            return i;
        }
    }

    /* 만약 마지막 바이트가 0xAA 라면,
       다음 recv에서 0x55가 올 가능성 있음 */
    if (puchData[iSize - 1] == 0xAA)
        return -2;

    return -1;
}


FRAME_ERR makeRequestFrame(unsigned short unCmd,
                            MSG_ID *pstMsgId,
                            unsigned char *puchSendData)
{
    if (!pstMsgId || !puchSendData)
        return FRAME_ERR_NULL_PTR;

    frameMakeHeader(unCmd, pstMsgId, puchSendData, FRAME_TYPE_REQUEST);

    switch (unCmd) {
        case CMD_ID_INFO:
            ((REQ_ID *)(puchSendData + sizeof(FRAME_HEADER)))->chTmp = 0x01;
            break;

        case CMD_KEEP_ALIVE:
            ((REQ_KEEP_ALIVE *)(puchSendData + sizeof(FRAME_HEADER)))->chTmp = 0x01;
            break;

        case CMD_IBIT:
            ((REQ_IBIT *)(puchSendData + sizeof(FRAME_HEADER)))->chIbit = 0x01;
            break;
        case CMD_RBIT:
            ((REQ_IBIT *)(puchSendData + sizeof(FRAME_HEADER)))->chIbit = 0x01;
            break;    
        case CMD_CBIT:
            ((REQ_ID *)(puchSendData + sizeof(FRAME_HEADER)))->chTmp = 0x01;
            break;
        case CMD_POSITIONER_AZ_EL_SET:
            ((REQ_POSITIONER_AZ_EL_SET *)(puchSendData + sizeof(FRAME_HEADER)))->chTmp = 0x01;
            break;
        case CMD_TRACKING_SELECT:
            ((REQ_TRACKING_SELECT *)(puchSendData + sizeof(FRAME_HEADER)))->chIbit = 0x01;
            break;
        case CMD_TRACKING_START_POINT_SET:
            ((REQ_IBIT *)(puchSendData + sizeof(FRAME_HEADER)))->chIbit = 0x01;
            break;
        case CMD_CANNON_BALL_TRAJECTORY_INFO:
            ((REQ_IBIT *)(puchSendData + sizeof(FRAME_HEADER)))->chIbit = 0x01;
            break;
        case CMD_SHELTER_COORDINATE_INFO:
            ((REQ_IBIT *)(puchSendData + sizeof(FRAME_HEADER)))->chIbit = 0x01;
            break;    
        case CMD_MCC_COORDINATE_INFO:
            ((REQ_ID *)(puchSendData + sizeof(FRAME_HEADER)))->chTmp = 0x01;
            break;
        case CMD_CANNON_COORDINATE_INFO:
            ((REQ_KEEP_ALIVE *)(puchSendData + sizeof(FRAME_HEADER)))->chTmp = 0x01;
            break;
        case CMD_TRACKING_CONTROL:
            ((REQ_IBIT *)(puchSendData + sizeof(FRAME_HEADER)))->chIbit = 0x01;
            break;
        case CMD_POSITIONER_DEG_SEND:
            ((REQ_IBIT *)(puchSendData + sizeof(FRAME_HEADER)))->chIbit = 0x01;
            break;          
            
        case CMD_ACU_MODE_SELECT:
            ((REQ_IBIT *)(puchSendData + sizeof(FRAME_HEADER)))->chIbit = 0x01;
            break;
        case CMD_TIME_SYNQ_CHECK:
            ((REQ_IBIT *)(puchSendData + sizeof(FRAME_HEADER)))->chIbit = 0x01;
            break;    
        case CMD_TIME_SYNQ_SET:
            ((REQ_ID *)(puchSendData + sizeof(FRAME_HEADER)))->chTmp = 0x01;
            break;
        case CMD_AZ_EL_OFFSET_SET:
            ((REQ_KEEP_ALIVE *)(puchSendData + sizeof(FRAME_HEADER)))->chTmp = 0x01;
            break;
        case CDM_GPS_DATA:
            ((REQ_IBIT *)(puchSendData + sizeof(FRAME_HEADER)))->chIbit = 0x01;
            break;
        case CDM_IMU_DATA:
            ((REQ_IBIT *)(puchSendData + sizeof(FRAME_HEADER)))->chIbit = 0x01;
            break;
        case CDM_SP_DATA:
            ((REQ_IBIT *)(puchSendData + sizeof(FRAME_HEADER)))->chIbit = 0x01;
            break;
        case CDM_EXTERN_DATA:
            ((REQ_IBIT *)(puchSendData + sizeof(FRAME_HEADER)))->chIbit = 0x01;
            break;    
        case CDM_KEYBOARD_DATA:
            ((REQ_ID *)(puchSendData + sizeof(FRAME_HEADER)))->chTmp = 0x01;
            break;
        case CMD_COMMAND_FAIL:
            ((REQ_KEEP_ALIVE *)(puchSendData + sizeof(FRAME_HEADER)))->chTmp = 0x01;
            break;        
        default:
            return FRAME_ERR_INVALID_CMD;
    }

    frameMakeTail(unCmd, puchSendData, FRAME_TYPE_REQUEST);
    return FRAME_OK;
}

FRAME_ERR makeResponseFrame(unsigned short unCmd,
                            MSG_ID *pstMsgId,
                            unsigned char *puchCmdResult,
                            unsigned char *puchSendData)
{
    if (!pstMsgId || !puchCmdResult || !puchSendData)
        return FRAME_ERR_NULL_PTR;
    
    frameMakeHeader(unCmd, pstMsgId, puchSendData, FRAME_TYPE_RESPONSE);
    memcpy(puchSendData + sizeof(FRAME_HEADER), puchCmdResult,
           getDataSize(unCmd, FRAME_TYPE_RESPONSE));
    frameMakeTail(unCmd, puchSendData, FRAME_TYPE_RESPONSE);
    return FRAME_OK;
}

/* ========================================================================== */
/*  Decode / Validate (NEW STRUCTURE)                                          */
/* ========================================================================== */

/* ---- Basic ---- */

static FRAME_ERR frameCheckBasic(const unsigned char *puchData, int iFrameSize)
{
    if (!puchData)
        return FRAME_ERR_NULL_PTR;

    if (iFrameSize < (int)sizeof(FRAME_HEADER))
        return FRAME_ERR_NEED_MORE_DATA;

    return FRAME_OK;
}

/* ---- Header ---- */

static FRAME_ERR frameCheckHeaderFields(const FRAME_HEADER *pstHeader,
                                        FRAME_TYPE eFrameType)
{
    if (ntohs(pstHeader->unStx) != STX_CONST)
        return FRAME_ERR_INVALID_STX;

    return checkCmd(ntohs(pstHeader->unCmd));
}

static FRAME_ERR frameCheckDataLength(const FRAME_HEADER *pstHeader,
                                      FRAME_TYPE eFrameType)
{
    unsigned short unCmd = ntohs(pstHeader->unCmd);

    if (ntohl(pstHeader->iDataLength) != getDataSize(unCmd, eFrameType)){
        fprintf(stderr,"### %s():%d  %d %d ###\n", __func__,__LINE__, ntohl(pstHeader->iDataLength), getDataSize(unCmd, eFrameType));
        return FRAME_ERR_INVALID_LENGTH;
    }

    return FRAME_OK;
}

static FRAME_ERR frameCheckCompleteSize(unsigned short unCmd,
                                        int iFrameSize,
                                        FRAME_TYPE eFrameType)
{
    int iNeedSize = getFrameSizeWithCmd(unCmd, eFrameType);

    if (iNeedSize <= 0)
        return FRAME_ERR_INVALID_LENGTH;

    if (iFrameSize < iNeedSize)
        return FRAME_ERR_NEED_MORE_DATA;

    return FRAME_OK;
}

static FRAME_ERR frameCheckHeader(unsigned short unCmd,
                                  unsigned char *puchData,
                                  int iFrameSize,
                                  FRAME_TYPE eFrameType)
{
    FRAME_ERR eErr;

    eErr = frameCheckBasic(puchData, iFrameSize);
    if (eErr != FRAME_OK)
        return eErr;

    FRAME_HEADER *pstHeader = (FRAME_HEADER *)puchData;

    eErr = frameCheckHeaderFields(pstHeader, eFrameType);
    if (eErr != FRAME_OK)
        return eErr;

    eErr = frameCheckDataLength(pstHeader, eFrameType);
    if (eErr != FRAME_OK)
        return eErr;

    return frameCheckCompleteSize(unCmd, iFrameSize, eFrameType);
}

/* ---- Tail ---- */

static FRAME_ERR frameCheckEtx(const FRAME_TAIL *pstTail)
{
    return (ntohs(pstTail->unEtx) == ETX_CONST) ?
        FRAME_OK : FRAME_ERR_INVALID_ETX;
}

static FRAME_ERR frameCheckTail(unsigned short unCmd,
                                unsigned char *puchData,
                                FRAME_TYPE eFrameType)
{
    FRAME_TAIL *pstTail = frameGetTailPtr(puchData, unCmd, eFrameType);

    FRAME_ERR eErr = frameCheckEtx(pstTail);
    if (eErr != FRAME_OK) return eErr;

    return frameCheckCrc(puchData,
                          getFrameSizeWithCmd(unCmd, eFrameType),
                          pstTail->uchCrc);
}

/* ---- Public Decode API ---- */

FRAME_ERR frameDecode(unsigned char *puchBuf,
                      int iFrameSize,
                      FRAME_TYPE eFrameType,
                      unsigned short *punOutCmd)
{
    if (!puchBuf || !punOutCmd)
        return FRAME_ERR_NULL_PTR;

    FRAME_ERR eErr;
    eErr = frameCheckBasic(puchBuf, iFrameSize);
    if (eErr != FRAME_OK)
        return eErr;

    FRAME_HEADER *pstHeader = (FRAME_HEADER *)puchBuf;
    unsigned short unCmd = ntohs(pstHeader->unCmd);

    eErr = frameCheckHeader(unCmd, puchBuf, iFrameSize, eFrameType);
    if (eErr != FRAME_OK)
        return eErr;

    eErr = frameCheckTail(unCmd, puchBuf, eFrameType);
    if (eErr != FRAME_OK)
        return eErr;

    *punOutCmd = unCmd;
    return FRAME_OK;
}
/* ========================================================================== */
/*  Size Helpers                                                              */
/* ========================================================================== */

int getDataSize(unsigned short unCmd, FRAME_TYPE eFrameType)
{
    switch (unCmd) {
        case CMD_ID_INFO:
            return (eFrameType == FRAME_TYPE_REQUEST) ? sizeof(REQ_ID) : sizeof(RES_ID);

        case CMD_KEEP_ALIVE:
            return (eFrameType == FRAME_TYPE_REQUEST) ? sizeof(REQ_KEEP_ALIVE) : sizeof(RES_KEEP_ALIVE);

        case CMD_IBIT:
            return (eFrameType == FRAME_TYPE_REQUEST) ? sizeof(REQ_BIT) : sizeof(RES_BIT);

        case CMD_RBIT:
            return (eFrameType == FRAME_TYPE_REQUEST) ? sizeof(REQ_BIT) : sizeof(RES_BIT);

        case CMD_CBIT:
            return (eFrameType == FRAME_TYPE_REQUEST) ? sizeof(REQ_BIT) : sizeof(RES_BIT);

        case CMD_POSITIONER_AZ_EL_SET:
            return (eFrameType == FRAME_TYPE_REQUEST) ? sizeof(REQ_POSITIONER_AZ_EL_SET) : sizeof(RES_POSITIONER_AZ_EL_SET);

        case CMD_TRACKING_SELECT:
            return (eFrameType == FRAME_TYPE_REQUEST) ? sizeof(REQ_TRACKING_SELECT) : sizeof(RES_TRACKING_SELECT);

        case CMD_TRACKING_START_POINT_SET:
            return (eFrameType == FRAME_TYPE_REQUEST) ? sizeof(REQ_TRACKING_START_POINT_SET) : sizeof(RES_TRACKING_START_POINT_SET);

        case CMD_CANNON_BALL_TRAJECTORY_INFO:
            return (eFrameType == FRAME_TYPE_REQUEST) ? sizeof(REQ_CANNON_BALL_TRAJECTORY_INFO) : sizeof(RES_CANNON_BALL_TRAJECTORY_INFO);

        case CMD_SHELTER_COORDINATE_INFO:
            return (eFrameType == FRAME_TYPE_REQUEST) ? sizeof(REQ_SHELTER_COORDINATE_INFO) : sizeof(RES_SHELTER_COORDINATE_INFO);

        // case CMD_MCC_COORDINATE_INFO:
        //     return (eFrameType == FRAME_TYPE_REQUEST) ? sizeof(REQ_ID) : sizeof(RES_ID);

        case CMD_CANNON_COORDINATE_INFO:
            return (eFrameType == FRAME_TYPE_REQUEST) ? sizeof(REQ_CANNON_COORDINATE_INFO) : sizeof(RES_CANNON_COORDINATE_INFO);

        case CMD_TRACKING_CONTROL:
            return (eFrameType == FRAME_TYPE_REQUEST) ? sizeof(REQ_TRACKING_CONTROL) : sizeof(RES_TRACKING_CONTROL);

        case CMD_POSITIONER_DEG_SEND:
            return (eFrameType == FRAME_TYPE_REQUEST) ? sizeof(REQ_POSITIONER_DEG_SEND) : sizeof(RES_POSITIONER_DEG_SEND);

        case CMD_ACU_MODE_SELECT:
            return (eFrameType == FRAME_TYPE_REQUEST) ? sizeof(REQ_ACU_MODE) : sizeof(RES_ACU_MODE);

        case CMD_TIME_SYNQ_CHECK:
            return (eFrameType == FRAME_TYPE_REQUEST) ? sizeof(REQ_TIME_SYNQ_CHECK) : sizeof(RES_TIME_SYNQ_CHECK);

        case CMD_TIME_SYNQ_SET:
            return (eFrameType == FRAME_TYPE_REQUEST) ? sizeof(REQ_KEEP_ALIVE) : sizeof(RES_KEEP_ALIVE);

        // case CMD_AZ_EL_OFFSET_SET:
        //     return (eFrameType == FRAME_TYPE_REQUEST) ? sizeof(REQ_IBIT) : sizeof(RES_IBIT);

        case CDM_GPS_DATA:
            return (eFrameType == FRAME_TYPE_REQUEST) ? 0 : sizeof(RES_LLA_DATA);

        case CDM_IMU_DATA:
            return (eFrameType == FRAME_TYPE_REQUEST) ? 0 : sizeof(RES_RPY_DATA); 

        case CDM_SP_DATA:
            return (eFrameType == FRAME_TYPE_REQUEST) ? 0 : sizeof(RES_LLA_DATA);

        case CDM_EXTERN_DATA:
            return (eFrameType == FRAME_TYPE_REQUEST) ? 0 : sizeof(RES_RPY_DATA); 

        case CDM_KEYBOARD_DATA:
            return (eFrameType == FRAME_TYPE_REQUEST) ? 0 : sizeof(RES_LLA_DATA);

        case CMD_COMMAND_FAIL:
            return (eFrameType == FRAME_TYPE_REQUEST) ? 0 : sizeof(RES_RPY_DATA); 
        default:
            return 0;
    }
}
int getFrameSizeWithCmd(unsigned short unCmd, FRAME_TYPE eFrameType)
{
    int iDataSize = getDataSize(unCmd, eFrameType);
    if (iDataSize <= 0)
        return -1;

    return sizeof(FRAME_HEADER) + iDataSize + sizeof(FRAME_TAIL);
}

int getFrameSizeWithData(unsigned char *puchData, FRAME_TYPE eFrameType)
{
    if (!puchData)
        return -1;

    /* 최소 HEADER 크기 확보 필요 */
    FRAME_ERR eErr = frameCheckBasic(puchData, sizeof(FRAME_HEADER));
    if (eErr != FRAME_OK)
        return -1;

    FRAME_HEADER *pstHeader = (FRAME_HEADER *)puchData;

    /* STX 검증 (garbage 데이터 방지) */
    if (ntohs(pstHeader->unStx) != STX_CONST)
        return -1;

    unsigned short unCmd = ntohs(pstHeader->unCmd);

    /* CMD 유효성 확인 */
    eErr = checkCmd(unCmd);
    if (eErr != FRAME_OK)
        return -1;

    /* CMD + TYPE 기반 전체 프레임 크기 계산 */
    int iFrameSize = getFrameSizeWithCmd(unCmd, eFrameType);
    if (iFrameSize <= 0)
        return -1;

    return iFrameSize;
}

/* ========================================================================== */
/*  Utilities                                                                 */
/* ========================================================================== */

unsigned char getSrcId(unsigned char *puchRecvData)
{
    return puchRecvData ?
        ((FRAME_HEADER *)puchRecvData)->stMsgId.uchSrcId : 0xFF;
}

unsigned char getDstId(unsigned char *puchRecvData)
{
    return puchRecvData ?
        ((FRAME_HEADER *)puchRecvData)->stMsgId.uchDstId : 0xFF;
}

FRAME_ERR getCmdFromFrame(unsigned char *puchData,
                          int iDataSize,
                          unsigned short *punOutCmd)
{
    FRAME_ERR eErr;

    if (!puchData || !punOutCmd)
        return FRAME_ERR_NULL_PTR;

    eErr = frameCheckBasic(puchData, iDataSize);
    if (eErr != FRAME_OK)
        return eErr;

    FRAME_HEADER *pstHeader = (FRAME_HEADER *)puchData;

    if (ntohs(pstHeader->unStx) != STX_CONST)
        return FRAME_ERR_INVALID_STX;

    *punOutCmd = ntohs(pstHeader->unCmd);
    return FRAME_OK;
}

/* ========================================================================== */
/*  Processing Path Decision                                                  */
/* ========================================================================== */

PROCESS_PATH decideProcessingPath(unsigned char *puchRecvData)
{
    FRAME_HEADER *pstHeader = (FRAME_HEADER *)puchRecvData;
    unsigned short unCmd = ntohs(pstHeader->unCmd);

    switch (unCmd) {
        case CMD_IBIT:
        case CMD_RBIT:
        case CMD_CBIT:
        case CMD_TIME_SYNQ_CHECK:
        case CMD_TIME_SYNQ_SET:
        case CMD_KEEP_ALIVE:
            return PROCESS_LOCAL;

        case CMD_POSITIONER_AZ_EL_SET:
        case CMD_TRACKING_SELECT:
        case CMD_POSITIONER_DEG_SEND:
        case CMD_ACU_MODE_SELECT:
        case CMD_AZ_EL_OFFSET_SET:
            return PROCESS_VIA_IPC;

        default:
            return PROCESS_UNKNOWN;
    }
}

/* ========================================================================== */
/*  Command Handler                                                           */
/* ========================================================================== */

FRAME_ERR commandHandler(unsigned char *puchRecvData,
                         unsigned char *puchCmdResult,
                         int *piSendDataSize)
{
    if (!puchRecvData || !puchCmdResult || !piSendDataSize)
        return FRAME_ERR_NULL_PTR;

    *piSendDataSize = 0;

    FRAME_HEADER *pstHeader = (FRAME_HEADER *)puchRecvData;
    unsigned short unCmd = ntohs(pstHeader->unCmd);

    switch (unCmd) {        
        case CMD_ID_INFO:
            idInfo(puchRecvData, puchCmdResult);
            *piSendDataSize = getDataSize(unCmd, FRAME_TYPE_RESPONSE);
            break;

        case CMD_KEEP_ALIVE:
            keepAlive(puchRecvData, puchCmdResult);
            *piSendDataSize = getDataSize(unCmd, FRAME_TYPE_RESPONSE);
            break;

        case CMD_IBIT:
            iBit(puchRecvData, puchCmdResult);
            *piSendDataSize = getDataSize(unCmd, FRAME_TYPE_RESPONSE);
            break;

        default:
            return FRAME_ERR_INVALID_CMD;
    }

    return FRAME_OK;
}

const char* frameErrToStr(FRAME_ERR eErr)
{
    switch (eErr) {
        case FRAME_OK:                 return "FRAME_OK";
        case FRAME_ERR_NEED_MORE_DATA: return "FRAME_ERR_NEED_MORE_DATA";
        case FRAME_ERR_INVALID_STX:    return "FRAME_ERR_INVALID_STX";
        case FRAME_ERR_INVALID_ETX:    return "FRAME_ERR_INVALID_ETX";
        case FRAME_ERR_INVALID_CMD:    return "FRAME_ERR_INVALID_CMD";
        case FRAME_ERR_INVALID_LENGTH: return "FRAME_ERR_INVALID_LENGTH";
        case FRAME_ERR_CRC_FAIL:       return "FRAME_ERR_CRC_FAIL";
        case FRAME_ERR_NULL_PTR:       return "FRAME_ERR_NULL_PTR";
        default:                       return "FRAME_ERR_UNKNOWN";
    }
}


FRAME_ERR parseAndDumpResponse(unsigned char *puchRecvData, unsigned char *puchResult)
{
    if (!puchRecvData)
        return FRAME_ERR_NULL_PTR;
    int iFrameSize = getFrameSizeWithData(puchRecvData, FRAME_TYPE_RESPONSE);

    unsigned short unCmd;
    getCmdFromFrame(puchRecvData, iFrameSize, &unCmd);
    switch (unCmd) {
        case CMD_ID_INFO:
        {
            RES_ID* pstResIdInfo = (RES_ID *)(puchRecvData+sizeof(FRAME_HEADER));
            puchResult[0] = pstResIdInfo->chResult;
            fprintf(stderr,"Client Id  0x%02x\n", pstResIdInfo->chResult);
            break;
        }

        case CMD_KEEP_ALIVE:
        {
            RES_KEEP_ALIVE* pstResKeepAlive = (RES_KEEP_ALIVE *)(puchRecvData+sizeof(FRAME_HEADER));
            puchResult[0] = pstResKeepAlive->chResult;
            fprintf(stderr,"keepalive %02x\n", pstResKeepAlive->chResult);
            break;
        }

        case CMD_IBIT:
        {
            RES_BIT* pstResIBit = (RES_BIT *)(puchRecvData+sizeof(FRAME_HEADER));
            puchResult[0] = pstResIBit->chBitTotResult;
            fprintf(stderr,"iBit %02x %02x\n", pstResIBit->chBitTotResult, pstResIBit->chPositionResult);
            break;
        }

        default:
            fprintf(stderr,"ICD %02x\n", unCmd);
            return FRAME_ERR_INVALID_CMD;
    }


    return FRAME_OK;
}

char getIdInfo(unsigned char *puchData)
{
    RES_ID *pstResId = (RES_ID *)(puchData+sizeof(FRAME_HEADER));
    return pstResId->chResult;
}