/**
 * @file frame.c
 * @brief STX/ETX 기반 프레임 생성·파싱 및 CRC 검증 구현부
 *
 * 본 소스는 frame.h에 선언된 API의 실제 동작을 구현한다.
 * - STX/ETX 기반 프레임 구조 처리
 * - CRC 생성 및 검증
 * - 요청(Request) 및 응답(Response) 프레임 생성
 * - 파싱 및 오류 검출(FRAME_ERR 기반)
 *
 * 모든 내부 함수는 static으로 숨겨져 있으며,
 * Libevent 스타일의 상세한 Doxygen 주석을 포함한다.
 */

#include "frame.h"
#include "icdCommand.h"

#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
 
 
/* ========================================================================== */
/*  Internal Static Helpers                                                   */
/* ========================================================================== */
 
/**
 * @brief 프레임 전체 데이터에 대해 CRC 값을 계산한다.
 *
 * CRC는 FRAME_HEADER의 STX, Length를 제외한 페이로드 영역부터
 * FRAME_TAIL 직전까지의 모든 바이트의 합으로 생성된다.
 *
 * @param puchData        CRC 계산을 수행할 프레임 전체 버퍼
 * @param iTotalFrameSize 프레임 전체 크기 (헤더 + 페이로드 + 테일)
 *
 * @return 계산된 CRC 값 (1 byte)
 */
static unsigned char calculateCrc(unsigned char *puchData, int iTotalFrameSize)
{
    unsigned char uchCrc = 0x00;

    int i = sizeof(((FRAME_HEADER *)0)->unStx)
        + sizeof(((FRAME_HEADER *)0)->iDataLength);

    for (; i < iTotalFrameSize - sizeof(FRAME_TAIL); i++) {
        uchCrc += puchData[i];
    }

    if ((uchCrc & 0xFF) == 0xFF)
        uchCrc = 0x00;

    return uchCrc;
}
 
/**
 * @brief 계산된 CRC 값과 수신 CRC 값을 비교하여 일치 여부를 판단한다.
 *
 * @param puchData   프레임 전체 버퍼
 * @param iSize      CRC 계산 대상 크기
 * @param uchRecvCrc 수신된 CRC 값
 *
 * @return FRAME_OK(일치) 또는 FRAME_ERR_CRC_FAIL(불일치)
 */
static FRAME_ERR checkCrc(unsigned char *puchData, int iSize, unsigned char uchRecvCrc)
{
    return (calculateCrc(puchData, iSize) == uchRecvCrc)
            ? FRAME_OK : FRAME_ERR_CRC_FAIL;
}
 
/**
 * @brief 명령 코드가 유효한 CMD인지 확인한다.
 *
 * @param unCmd 검사할 CMD 값
 *
 * @return FRAME_OK(유효), FRAME_ERR_INVALID_CMD(지원되지 않는 CMD)
 */
static FRAME_ERR checkCmd(unsigned short unCmd)
{
    switch (unCmd & 0x3FFF) {
        case CMD_ID_INFO:
        case CMD_KEEP_ALIVE:
        case CMD_IBIT:
            return FRAME_OK;

        default:
            return FRAME_ERR_INVALID_CMD;
    }
}
 
/**
 * @brief 프레임 헤더(FRAME_HEADER 구조체)를 생성한다.
 *
 * @param unCmd     명령 코드
 * @param pstMsgId  송신자/수신자 ID 구조체
 * @param puchData  헤더가 기록될 프레임 시작 주소
 *
 * @return void
 */
static void makeFrameHeader(unsigned short unCmd, MSG_ID *pstMsgId,
                            unsigned char *puchData)
{
    FRAME_HEADER *pstHeader = (FRAME_HEADER *)puchData;

    pstHeader->unStx       = htons(STX_CONST);
    pstHeader->iDataLength = htonl(getDataSize(unCmd));
    pstHeader->stMsgId     = *pstMsgId;
    pstHeader->unCmd       = htons(unCmd & 0x3FFF);
}
 
/**
 * @brief 프레임 꼬리부(FRAME_TAIL 구조체)를 생성한다.
 *
 * CRC 계산 후 FRAME_TAIL(uchCrc, unEtx)를 버퍼에 기록한다.
 *
 * @param unCmd     명령 코드 (CRC 계산에 필요)
 * @param puchData  프레임 전체 버퍼 시작 주소
 *
 * @return void
 */
static void makeFrameTail(unsigned short unCmd, unsigned char *puchData)
{
    FRAME_TAIL *pstTail =
        (FRAME_TAIL *)(puchData + sizeof(FRAME_HEADER) + getDataSize(unCmd));

    int iTotalSize =
        sizeof(FRAME_HEADER) + getDataSize(unCmd) + sizeof(FRAME_TAIL);

    pstTail->uchCrc = calculateCrc(puchData, iTotalSize);
    pstTail->unEtx  = htons(ETX_CONST);
}
 
/**
 * @brief 수신 버퍼의 헤더(FRAME_HEADER)가 정상인지 검사한다.
 *
 * 검사항목:
 * - STX 값 확인
 * - 명령 코드 유효성 검사
 * - DataLength와 getDataSize() 값 비교
 * - 전체 프레임 크기 요구량이 충분한지 검사
 *
 * @param unCmd      검사할 명령 코드
 * @param pstMsgId   송수신 ID 구조체
 * @param puchData   수신 버퍼 주소
 * @param tDataLen   수신된 전체 데이터 길이
 *
 * @return FRAME_OK 또는 FRAME_ERR 계열 코드
 */
static FRAME_ERR checkFrameHeader(unsigned short unCmd, MSG_ID *pstMsgId,
                                unsigned char *puchData, size_t tDataLen)
{
    if (!puchData || !pstMsgId)
        return FRAME_ERR_NULL_PTR;
        
    if (tDataLen < sizeof(FRAME_HEADER))
        return FRAME_ERR_NEED_MORE_DATA;
        
    FRAME_HEADER *pstHeader = (FRAME_HEADER *)puchData;    
    if (ntohs(pstHeader->unStx) != STX_CONST)
        return FRAME_ERR_INVALID_STX;
    // if((pstHeader->stMsgId.uchDstId & pstMsgId->uchSrcId) != pstMsgId->uchSrcId)
    //     return FRAME_ERR_INVALID_ID;
    
    FRAME_ERR eErr = checkCmd(unCmd);
    if (eErr != FRAME_OK)
        return eErr;
        
    if (ntohl(pstHeader->iDataLength) != getDataSize(unCmd))
        return FRAME_ERR_INVALID_LENGTH;
        
    int iNeedSize =
        sizeof(FRAME_HEADER) + getDataSize(unCmd) + sizeof(FRAME_TAIL);
    if ((int)tDataLen < iNeedSize)
        return FRAME_ERR_NEED_MORE_DATA;
        
    return FRAME_OK;
}
 
/**
 * @brief 프레임 테일(FRAME_TAIL: CRC + ETX)을 검증한다.
 *
 * CRC 불일치, ETX 값 불일치 등 다양한 오류를 검출한다.
 *
 * @param unCmd     명령 코드
 * @param puchData  프레임 전체 버퍼
 *
 * @return FRAME_OK 또는 FRAME_ERR(CRC_FAIL, INVALID_ETX 등)
 */
static FRAME_ERR checkFrameTail(unsigned short unCmd, unsigned char *puchData)
{
    if (!puchData)
        return FRAME_ERR_NULL_PTR;

    FRAME_TAIL *pstTail =
        (FRAME_TAIL *)(puchData + sizeof(FRAME_HEADER) + getDataSize(unCmd));

    int iTotalSize =
        sizeof(FRAME_HEADER) + getDataSize(unCmd) + sizeof(FRAME_TAIL);

    if (ntohs(pstTail->unEtx) != ETX_CONST)
        return FRAME_ERR_INVALID_ETX;

    return checkCrc(puchData, iTotalSize, pstTail->uchCrc);
}
 
 
/* ========================================================================== */
/*  Public API Implementation                                                 */
/* ========================================================================== */
 
int getDataSize(unsigned short unCmd)
{
    switch (unCmd) {
        case REQ_CMD_ID_INFO:    return sizeof(REQ_ID);
        case RES_CMD_ID_INFO:    return sizeof(RES_ID);

        case REQ_CMD_KEEP_ALIVE: return sizeof(REQ_KEEP_ALIVE);
        case RES_CMD_KEEP_ALIVE: return sizeof(RES_KEEP_ALIVE);

        case REQ_CMD_IBIT:       return sizeof(REQ_IBIT);
        case RES_CMD_IBIT:       return sizeof(RES_IBIT);
    }
    return 0;
}
int getRequeestDataSize(unsigned short unCmd)
{
    switch (unCmd) {
        case CMD_ID_INFO:    return sizeof(REQ_ID);
        case CMD_KEEP_ALIVE: return sizeof(REQ_KEEP_ALIVE);
        case CMD_IBIT:       return sizeof(REQ_IBIT);
    }
    return 0;
}
int getResposeDataSize(unsigned short unCmd)
{
    switch (unCmd) {
        case CMD_ID_INFO:    return sizeof(RES_ID);
        case CMD_KEEP_ALIVE: return sizeof(RES_KEEP_ALIVE);
        case CMD_IBIT:       return sizeof(RES_IBIT);
    }
    return 0;
}
 
/**
 * @brief 전체 프레임 크기 반환
 */
int getFrameSize(unsigned char *puchData)
{
    if (!puchData)
        return -1;

    FRAME_HEADER *pstHeader = (FRAME_HEADER *)puchData;

    if (ntohs(pstHeader->unStx) != STX_CONST)
        return -1;

    return sizeof(FRAME_HEADER) + ntohl(pstHeader->iDataLength) + sizeof(FRAME_TAIL);
}
 
/**
 * @brief 요청 프레임 생성
 */
FRAME_ERR makeReqFrame(unsigned short unCmd, MSG_ID *pstMsgId,
                    unsigned char *puchSendData, int *piOutFrameSize)
{
    if (!puchSendData || !pstMsgId)
        return FRAME_ERR_NULL_PTR;
    
    makeFrameHeader(unCmd | REQ_CMD_OFFSET, pstMsgId, puchSendData);

    int iDataSize = getDataSize(unCmd | REQ_CMD_OFFSET);

    switch (unCmd) {
        case CMD_ID_INFO: {
            REQ_ID *pstReq =
                (REQ_ID *)(puchSendData + sizeof(FRAME_HEADER));
            pstReq->chTmp = 0x01;
            *piOutFrameSize = sizeof(REQ_ID);
            break;
        }
        case CMD_KEEP_ALIVE: {
            REQ_KEEP_ALIVE *pstReq =
                (REQ_KEEP_ALIVE *)(puchSendData + sizeof(FRAME_HEADER));
            pstReq->chTmp = 0x01;
            *piOutFrameSize = sizeof(REQ_KEEP_ALIVE);
            break;
        }
        case CMD_IBIT: {
            REQ_IBIT *pstReq =
                (REQ_IBIT *)(puchSendData + sizeof(FRAME_HEADER));
            pstReq->chIbit = 0x01;
            *piOutFrameSize = sizeof(REQ_IBIT);
            break;
        }
        default:
            return FRAME_ERR_INVALID_CMD;
    }

    makeFrameTail(unCmd | REQ_CMD_OFFSET, puchSendData);

    *piOutFrameSize =
        sizeof(FRAME_HEADER) + iDataSize + sizeof(FRAME_TAIL);

    return FRAME_OK;
}

/**
 * @brief 응답 프레임 생성
 */
FRAME_ERR makeResFrame(unsigned short unCmd, MSG_ID *pstMsgId,
                    unsigned char *puchCmdResult, unsigned char *puchSendData)
{
    if (!puchSendData || !pstMsgId)
        return FRAME_ERR_NULL_PTR;

    int iDataSize = getDataSize(unCmd | RES_CMD_OFFSET);

    makeFrameHeader(unCmd | RES_CMD_OFFSET, pstMsgId, puchSendData);

    memcpy(puchSendData + sizeof(FRAME_HEADER), puchCmdResult, iDataSize);

    makeFrameTail(unCmd | RES_CMD_OFFSET, puchSendData);

    return FRAME_OK;
}

/**
 * @brief 요청 프레임 검증 후 CMD 추출
 */
FRAME_ERR chkRequestFrame(unsigned char *puchRecvData, MSG_ID *pstMsgId,
                    size_t tDataLen, unsigned short *punOutCmd)
{
    if (!puchRecvData || !pstMsgId || !punOutCmd)
        return FRAME_ERR_NULL_PTR;
        
    FRAME_HEADER *pstHeader = (FRAME_HEADER *)puchRecvData;

    unsigned short unCmd = ntohs(pstHeader->unCmd) | REQ_CMD_OFFSET;
    
    FRAME_ERR eErr =
        checkFrameHeader(unCmd, pstMsgId, puchRecvData, tDataLen);
    if (eErr != FRAME_OK)
        return eErr;

    eErr = checkFrameTail(unCmd, puchRecvData);
    if (eErr != FRAME_OK)
        return eErr;
    
    *punOutCmd = ntohs(pstHeader->unCmd);
    return FRAME_OK;
}

/**
 * @brief 요청 프레임 처리 + 응답 페이로드 생성
 */
FRAME_ERR commandHandler(unsigned char *puchRecvData, MSG_ID *pstMsgId,
                        size_t tDataLen, unsigned char *puchCmdResult, int *piSendDataSize)
{
    *piSendDataSize = 0;
    if (!puchRecvData || !pstMsgId)
        return FRAME_ERR_NULL_PTR;

    FRAME_HEADER *pstHeader = (FRAME_HEADER *)puchRecvData;

    unsigned short unReqCmd = ntohs(pstHeader->unCmd) | REQ_CMD_OFFSET;
    unsigned short unResCmd = ntohs(pstHeader->unCmd) | RES_CMD_OFFSET;    
    
    switch (unReqCmd) {
        case REQ_CMD_ID_INFO:
            idInfo(puchRecvData, puchCmdResult);
            break;

        case REQ_CMD_KEEP_ALIVE:
            keepAlive(puchRecvData, puchCmdResult);
            break;

        case REQ_CMD_IBIT:
            iBit(puchRecvData, puchCmdResult);
            break;

        default:
            return FRAME_ERR_INVALID_CMD;
    }
    *piSendDataSize = sizeof(FRAME_HEADER)+sizeof(FRAME_TAIL)+getDataSize(unResCmd);
    return FRAME_OK;
}

/**
 * @brief 응답(Response) 프레임 검증
 */
FRAME_ERR responseFrame(unsigned char *puchRecvData, MSG_ID *pstMsgId, 
    size_t tDataLen, unsigned short *punCmd, unsigned char *uchResult)
{
    if (!puchRecvData || !pstMsgId)
        return FRAME_ERR_NULL_PTR;

    FRAME_HEADER *pstHeader = (FRAME_HEADER *)puchRecvData;

    unsigned short unCmd = ntohs(pstHeader->unCmd) | RES_CMD_OFFSET;

    FRAME_ERR eErr = checkFrameHeader(unCmd, pstMsgId, puchRecvData, tDataLen);
    if (eErr != FRAME_OK)
        return eErr; 
    eErr = checkFrameTail(unCmd, puchRecvData);
    if (eErr != FRAME_OK)
        return eErr;
    switch (unCmd) {
        case RES_CMD_ID_INFO:
        {
            RES_ID* pstResIdInfo = (RES_ID *)(puchRecvData+sizeof(FRAME_HEADER));
            *punCmd = CMD_ID_INFO;
            uchResult[0] = pstResIdInfo->chResult;
            fprintf(stderr,"Client Id  0x%02x\n", pstResIdInfo->chResult);
            break;
        }

        case RES_CMD_KEEP_ALIVE:
        {
            RES_KEEP_ALIVE* pstResKeepAlive = (RES_KEEP_ALIVE *)(puchRecvData+sizeof(FRAME_HEADER));
            *punCmd = CMD_KEEP_ALIVE;
            uchResult[0] = pstResKeepAlive->chResult;
            fprintf(stderr,"keepalive %02x\n", pstResKeepAlive->chResult);
            break;
        }

        case RES_CMD_IBIT:
        {
            RES_IBIT* pstResIBit = (RES_IBIT *)(puchRecvData+sizeof(FRAME_HEADER));
            *punCmd = CMD_IBIT;
            uchResult[0] = pstResIBit->chBitTotResult;
            fprintf(stderr,"iBit %02x %02x\n", pstResIBit->chBitTotResult, pstResIBit->chPositionResult);
            break;
        }

        default:
            fprintf(stderr,"ICD %02x\n", pstHeader->unCmd);
            return FRAME_ERR_INVALID_CMD;
    }

    return FRAME_OK;
}


/**
 * @brief 응답(Response) 프레임 검증
 */
FRAME_ERR udsResponseFrame(unsigned char *puchRecvData,
    MSG_ID *pstMsgId, size_t tDataLen, unsigned char *puchResult)
{
    FRAME_HEADER *pstHeader = (FRAME_HEADER *)puchRecvData;

    unsigned short unCmd = ntohs(pstHeader->unCmd);

    FRAME_ERR eErr = checkFrameHeader(unCmd, pstMsgId, puchRecvData, tDataLen);
    if (eErr != FRAME_OK)
        return eErr;

    eErr = checkFrameTail(unCmd, puchRecvData);
    if (eErr != FRAME_OK)
        return eErr;

    switch (unCmd) {
        case RES_CMD_ID_INFO:
        {
            RES_ID* pstResIdInfo = (RES_ID *)(puchRecvData+sizeof(FRAME_HEADER));
            fprintf(stderr,"Client Id  0x%02x\n", pstResIdInfo->chResult);
            *puchResult = pstResIdInfo->chResult;
            break;
        }

        case RES_CMD_KEEP_ALIVE:
        {
            RES_KEEP_ALIVE* pstResKeepAlive = (RES_KEEP_ALIVE *)(puchRecvData+sizeof(FRAME_HEADER));
            fprintf(stderr,"keepalive %02x\n", pstResKeepAlive->chResult);
            break;
        }

        case RES_CMD_IBIT:
        {
            RES_IBIT* pstResIBit = (RES_IBIT *)(puchRecvData+sizeof(FRAME_HEADER));
            fprintf(stderr,"iBit %02x %02x\n", pstResIBit->chBitTotResult, pstResIBit->chPositionResult);
            break;
        }

        default:
            return FRAME_ERR_INVALID_CMD;
    }

    return FRAME_OK;
}


const char* frameErrToStr(FRAME_ERR eErr)
{
    switch (eErr) {

        case FRAME_OK:
            return "FRAME_OK: No error";

        case FRAME_ERR_NEED_MORE_DATA:
            return "FRAME_ERR_NEED_MORE_DATA: More data required to complete frame";

        case FRAME_ERR_INVALID_STX:
            return "FRAME_ERR_INVALID_STX: Invalid STX value";

        case FRAME_ERR_INVALID_ETX:
            return "FRAME_ERR_INVALID_ETX: Invalid ETX value";

        case FRAME_ERR_INVALID_CMD:
            return "FRAME_ERR_INVALID_CMD: Unsupported or invalid command";

        case FRAME_ERR_INVALID_LENGTH:
            return "FRAME_ERR_INVALID_LENGTH: Payload length mismatch";

        case FRAME_ERR_CRC_FAIL:
            return "FRAME_ERR_CRC_FAIL: CRC verification failed";

        case FRAME_ERR_NULL_PTR:
            return "FRAME_ERR_NULL_PTR: Null pointer passed to function";

        case FRAME_ERR_FRAME_TOO_SMALL:
            return "FRAME_ERR_FRAME_TOO_SMALL: Frame shorter than minimal size";

        case FRAME_ERR_UNKNOWN:
            return "FRAME_ERR_UNKNOWN: Unknown error";

        default:
            return "FRAME_ERR: Unrecognized error code";
    }
}

unsigned char getSrcId(unsigned char *puchRecvData)
{
    if (!puchRecvData)
        return 0xFF;

    FRAME_HEADER *pstHeader = (FRAME_HEADER *)puchRecvData;
    return pstHeader->stMsgId.uchSrcId;
}

unsigned char getDstId(unsigned char *puchRecvData)
{
    if (!puchRecvData)
        return 0xFF;

    FRAME_HEADER *pstHeader = (FRAME_HEADER *)puchRecvData;
    return pstHeader->stMsgId.uchDstId;
}

PROCESS_PATH decideProcessingPath(unsigned char *puchRecvData)
{
    FRAME_HEADER *pstHeader = (FRAME_HEADER *)puchRecvData;
    unsigned short unCmd = ntohs(pstHeader->unCmd);
    switch (unCmd) {
        case CMD_ID_INFO:
            return PROCESS_LOCAL;
        case CMD_KEEP_ALIVE:
            return PROCESS_LOCAL;
        case CMD_IBIT:
            return PROCESS_VIA_IPC;;

        default:
            return PROCESS_UNKNOWN;
    }
}