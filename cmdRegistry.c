#include "cmdRegistry.h"
#include "icdCommand.h"
#include <string.h>
#include <stdio.h>

/* ======================================================================
 *  프로젝트 기존 헤더 include
 *  - CMD_KEEP_ALIVE 같은 커맨드 ID 정의
 *  - REQ_xxx / RES_xxx 구조체 정의
 * ====================================================================== */

/* ============================================================
 * 4. 명령별 Request / Response 처리 함수
 * ============================================================ */
// static int buildKeepAlive(void* pvOutData, unsigned int iSize, const void* pvUserArg)
// {
//     (void)pvUserArg;
//     if (iSize < sizeof(REQ_KEEP_ALIVE))
//         return -1;

//     REQ_KEEP_ALIVE req = { .chTmp = 0 };
//     memcpy(pvOutData, &req, sizeof(req));
//     return sizeof(req);
// }


/* ----------------------------------------------------------------------
 *  커맨드 테이블: 여기만 수정하면 “명령 추가”가 끝
 * ---------------------------------------------------------------------- */
static CMD_DESC g_cmdTable[] = {
    {   
        CMD_KEEP_ALIVE, "KEEP_ALIVE",
        sizeof(REQ_KEEP_ALIVE), sizeof(RES_KEEP_ALIVE),
        buildForwardReqKeepAlive, dispatchCmdKeepAlive, buildResKeepAlive
    },
    {   
        CMD_IBIT, "IBIT",
        sizeof(REQ_BIT), sizeof(RES_BIT),        
        buildForwardReqIbit, dispatchCmdIbit, buildResIbit 
    },
    {   CMD_RBIT, "RBIT",
        sizeof(REQ_BIT), sizeof(RES_BIT),
        buildForwardReqRbit, dispatchCmdRbit, buildResRbit 
    },
    {   CMD_CBIT, "CBIT",
        sizeof(REQ_BIT), sizeof(RES_BIT),
        buildForwardReqCbit, dispatchCmdCbit, buildResCbit 
    },
    {   CMD_POSITIONER_AZ_EL_SET, "POSITIONER_AZ_EL_SET",
        sizeof(REQ_POSITIONER_AZ_EL_SET), sizeof(RES_POSITIONER_AZ_EL_SET),
        buildForwardReqPositionAzElSet, dispatchCmdPositionAzElSet, buildResPositionAzElSet 
    },
    {   CMD_TRACKING_SELECT, "TRACKING_SELECT",
        sizeof(REQ_TRACKING_SELECT), sizeof(RES_TRACKING_SELECT),
        buildForwardReqTrackingSelect, dispatchCmdTrackingSelect, buildResTrackingSelect 
    },
    
    {   CMD_ACU_MODE_SELECT, "ACU_MODE_SELECT",
        sizeof(REQ_ACU_MODE), sizeof(RES_ACU_MODE),        
        buildForwardReqAcuModeSelect, dispatchCmdAcuModeSelect, buildResAcuModeSelect 
    },
    {   CMD_AUTO_TRACKING_WAIT, "AUTO_TRACKING_WAIT",
        sizeof(REQ_AUTO_TRACKING_WAIT), sizeof(RES_AUTO_TRACKING_WAIT),        
        buildForwardReqAutoTrackingWait, dispatchCmdAutoTrackingWait, buildResAutoTrackingWait 
    },
    
    {   CMD_ID_INFO, "ID_INFO",
        sizeof(REQ_ID), sizeof(RES_ID),
        buildForwardReqIdInfo, dispatchCmdIdInfo, buildResIdInfo 
    },
};

static const size_t g_cmdCount = sizeof(g_cmdTable) / sizeof(g_cmdTable[0]);

/* ----------------------------------------------------------------------
 *  공통 Lookup
 * ---------------------------------------------------------------------- */
static const CMD_DESC* cmdFind(unsigned short unCmd)
{
    for (size_t i = 0; i < g_cmdCount; ++i) {
        if (g_cmdTable[i].unCmd == unCmd) 
            return &g_cmdTable[i];
    }
    return NULL;
}

FRAME_ERR cmdRegistryOverrideHandler( unsigned short unCmd,
    buildForwardReq fnBuildForwardReq, dispatchCommand fnDispatchCmd, buildResponse fnbuildRes)
{
    for (size_t i = 0; i < g_cmdCount; ++i) {
        if (g_cmdTable[i].unCmd == unCmd) {
            if (fnBuildForwardReq)
                g_cmdTable[i].fnBuildForwardReq = fnBuildForwardReq;
            if (fnDispatchCmd)
                g_cmdTable[i].fnDispatchCmd     = fnDispatchCmd;
            if (fnbuildRes)
                g_cmdTable[i].fnbuildRes        = fnbuildRes;
            return FRAME_OK;
        }
    }
    return FRAME_ERR_INVALID_CMD;
}

static FRAME_ERR frameCheckBasic(const unsigned char *puchData, int iFrameSize)
{
    if (!puchData)
        return FRAME_ERR_NULL_PTR;

    if (iFrameSize < (int)sizeof(FRAME_HEADER))
        return FRAME_ERR_NEED_MORE_DATA;

    return FRAME_OK;
}

static FRAME_TYPE checkCmd(unsigned short unCmd)
{
    const CMD_DESC* pstCmdDesc = cmdFind(unCmd);
    if(pstCmdDesc == NULL)
        return FRAME_ERR_INVALID_CMD;
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

    if (ntohl(pstHeader->iDataLength) != (getDataSize(unCmd, eFrameType)+8)){
        fprintf(stderr,"### %s():%d  %s %d %d ###\n", __func__,__LINE__, getCmdString(unCmd), 
            ntohl(pstHeader->iDataLength), getDataSize(unCmd, eFrameType)+8);
        return FRAME_ERR_INVALID_LENGTH;
    }

    return FRAME_OK;
}

static FRAME_ERR frameCheckCompleteSize(unsigned short unCmd,
                                        int iFrameSize, FRAME_TYPE eFrameType)
{
    int iNeedSize = getFrameSizeWithCmd(unCmd, eFrameType);

    if (iNeedSize <= 0)
        return FRAME_ERR_INVALID_LENGTH;

    if (iFrameSize < iNeedSize)
        return FRAME_ERR_NEED_MORE_DATA;

    return FRAME_OK;
}

static FRAME_ERR frameCheckHeader(unsigned short unCmd, unsigned char *puchData,
                                  int iFrameSize, FRAME_TYPE eFrameType)
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
    return (ntohs(pstTail->unEtx) == ETX_CONST) ? FRAME_OK : FRAME_ERR_INVALID_ETX;
}

static FRAME_TAIL* frameGetTailPtr(void *pvOutData, unsigned short unCmd, FRAME_TYPE eFrameType)
{
    return (FRAME_TAIL *)(pvOutData + sizeof(FRAME_HEADER) + getDataSize(unCmd, eFrameType));
}

static unsigned char frameCalcCrc(const unsigned char *puchBuf, int iTotalSize)
{
    unsigned char uchCrc = 0x00;
    int iOffset =
        sizeof(((FRAME_HEADER *)0)->unStx) + sizeof(((FRAME_HEADER *)0)->iDataLength);
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

static FRAME_ERR frameCheckTail(unsigned short unCmd, void *pvOutData, FRAME_TYPE eFrameType)
{
    FRAME_TAIL *pstTail = frameGetTailPtr(pvOutData, unCmd, eFrameType);
    FRAME_ERR eErr = frameCheckEtx(pstTail);
    if (eErr != FRAME_OK) 
        return eErr;
        
    return frameCheckCrc(pvOutData, getFrameSizeWithCmd(unCmd, eFrameType), pstTail->uchCrc);
}

static void frameMakeHeader(unsigned short unCmd, const MSG_ID *pstMsgId,
                            void* pvOutData, FRAME_TYPE eFrameType)
{
    FRAME_HEADER *pstHeader = (FRAME_HEADER *)pvOutData;

    pstHeader->unStx        = htons(STX_CONST);
    pstHeader->iDataLength  = htonl(getDataSize(unCmd, eFrameType)+8); // MSG_ID(2) + SubModule(1) + CMD(2) + Tail(3)
    pstHeader->stMsgId      = *pstMsgId;
    pstHeader->uchSubModule = 0x00;
    pstHeader->unCmd        = htons(unCmd);
}

static void frameMakeTail(unsigned short unCmd,
                          void* pvOutData,
                          FRAME_TYPE eFrameType)
{
    FRAME_TAIL *pstTail = frameGetTailPtr(pvOutData, unCmd, eFrameType);
    pstTail->uchCrc = frameCalcCrc(pvOutData, getFrameSizeWithCmd(unCmd, eFrameType));
    pstTail->unEtx = htons(ETX_CONST);
}

static unsigned short getCmdCode(unsigned char *puchBuf, int iFrameSize, unsigned short *punOutCmd)
{
    FRAME_ERR eErr;

    if (!puchBuf || !punOutCmd)
        return FRAME_ERR_NULL_PTR;
    
    eErr = frameCheckBasic(puchBuf, iFrameSize);
    if (eErr != FRAME_OK)
        return eErr;
        
    FRAME_HEADER *pstHeader = (FRAME_HEADER *)puchBuf;
    *punOutCmd = ntohs(pstHeader->unCmd);
    return FRAME_OK;
}

const char* getCmdString(unsigned short unCmd)
{
    const CMD_DESC* pstCmdDesc = cmdFind(unCmd);
    return pstCmdDesc ? pstCmdDesc->chCmdName : "UNKNOWN";
}

unsigned int getDataSize(unsigned short unCmd, FRAME_TYPE eFrameType)
{
    const CMD_DESC* pstCmdDesc = cmdFind(unCmd);
    if (!pstCmdDesc)
        return 0;
    return (eFrameType == FRAME_TYPE_REQUEST) ? pstCmdDesc->uiReqSize : pstCmdDesc->uiResSize;
}

FRAME_ERR createCmdRequest(unsigned short unCmd, MSG_ID *pstMsgId, void* uchUserData, void* pvOutData)
{
    const CMD_DESC* pstCmdDesc = cmdFind(unCmd);
    frameMakeHeader(unCmd, pstMsgId, pvOutData, FRAME_TYPE_REQUEST);
    if (!pstCmdDesc || !pstCmdDesc->fnBuildForwardReq)
        return -1;

    if(pstCmdDesc->fnBuildForwardReq(uchUserData, pvOutData+sizeof(FRAME_HEADER))){

    }
    frameMakeTail(unCmd, pvOutData, FRAME_TYPE_REQUEST);
    return FRAME_OK;
}

FRAME_ERR cmdDispatch(const void* pvRecvData, int iFrameSize, void* pvOutData)
{
    unsigned short unCmd;
    FRAME_ERR eErr = getCmdCode(pvRecvData, iFrameSize, &unCmd);
    if(eErr != FRAME_OK)
        return eErr;
    
    const CMD_DESC* pstCmdDesc = cmdFind(unCmd);    
    if (!pstCmdDesc || !pstCmdDesc->fnDispatchCmd)
        return FRAME_ERR_INVALID_CMD;
        
    return (pstCmdDesc->fnDispatchCmd(pvRecvData+sizeof(FRAME_HEADER), pvOutData)==0)?FRAME_NOK : FRAME_OK;
}

FRAME_ERR createCmdResponse(unsigned short unCmd, const void* pvUserData, MSG_ID* pstMsgId, void* pvOutData)
{
    FRAME_ERR eErr;
    const CMD_DESC* pstCmdDesc = cmdFind(unCmd);
    if (!pstCmdDesc)
        return FRAME_ERR_INVALID_CMD;
        
    frameMakeHeader(unCmd, pstMsgId, pvOutData, FRAME_TYPE_RESPONSE);
    pstCmdDesc->fnbuildRes(pvUserData, pvOutData+sizeof(FRAME_HEADER));
    frameMakeTail(unCmd, pvOutData, FRAME_TYPE_RESPONSE);
    return FRAME_OK;
}

FRAME_ERR repackageResponse(void* pvData, MSG_ID* pstMsgId, int iFrameSize)
{
    FRAME_ERR eErr;
    unsigned short unCmd;

    FRAME_HEADER *pstFrameHeader = (FRAME_HEADER *)pvData;
    eErr = getCmdCode(pvData, iFrameSize, &unCmd);
    FRAME_TAIL *pstTail = frameGetTailPtr(pvData, unCmd, FRAME_TYPE_RESPONSE);

    fprintf(stderr,"### %s():%d Len:%d ###\n",__func__,__LINE__, getDataSize(unCmd, FRAME_TYPE_RESPONSE));    
    pstFrameHeader->stMsgId.uchSrcId = pstMsgId->uchSrcId;
    pstFrameHeader->stMsgId.uchDstId = pstMsgId->uchDstId;
    fprintf(stderr,"SRC:%02x, DST:%02x\n", pstFrameHeader->stMsgId.uchSrcId, pstFrameHeader->stMsgId.uchDstId);
    pstTail->uchCrc = frameCalcCrc(pvData, getFrameSizeWithCmd(unCmd, FRAME_TYPE_RESPONSE));
    return FRAME_OK;
}

unsigned int getFrameSizeWithCmd(unsigned short unCmd, FRAME_TYPE eFrameType)
{
    int iDataSize = getDataSize(unCmd, eFrameType);
    if (iDataSize <= 0)
        return -1;
    
    return sizeof(FRAME_HEADER) + iDataSize + sizeof(FRAME_TAIL);
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
        case FRAME_NOK:                return "FRAME_NOK";
        default:                       return "FRAME_ERR_UNKNOWN";
    }
}

/* ---- Public Decode API ---- */
FRAME_ERR frameDecode(unsigned char *puchBuf, int iFrameSize,
                      FRAME_TYPE eFrameType, unsigned short *punOutCmd)
{
    unsigned short unCmd;
    FRAME_ERR eErr;

    if (!puchBuf || !punOutCmd)
        return FRAME_ERR_NULL_PTR;
        
    eErr = frameCheckBasic(puchBuf, iFrameSize);
    if (eErr != FRAME_OK)
        return eErr;
        
    eErr = getCmdCode(puchBuf, iFrameSize, &unCmd);
    if(eErr != FRAME_OK)
        return eErr;
        
    eErr = frameCheckHeader(unCmd, puchBuf, iFrameSize, eFrameType);
    if (eErr != FRAME_OK)
        return eErr;
        
    eErr = frameCheckTail(unCmd, puchBuf, eFrameType);
    if (eErr != FRAME_OK)
        return eErr;
        
    *punOutCmd = unCmd;
    return FRAME_OK;
}

char getIdInfo(unsigned char *puchData)
{
    RES_ID *pstResId = (RES_ID *)(puchData);
    return pstResId->chResult;
}

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

