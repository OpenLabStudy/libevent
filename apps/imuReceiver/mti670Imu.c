#include "mti670Imu.h"
#include <string.h>

/* ========================================================================= */
/* Big Endian Utilities                                                      */
/* ========================================================================= */
unsigned short mtiBe16(const unsigned char* puch)
{
    return ((unsigned short)puch[0] << 8) | puch[1];
}

unsigned int mtiBe32(const unsigned char* puch)
{
    return ((unsigned int)puch[0] << 24) |
           ((unsigned int)puch[1] << 16) |
           ((unsigned int)puch[2] << 8)  |
           puch[3];
}

float mtiBeFloat(const unsigned char* puch)
{
    unsigned int uiVal = mtiBe32(puch);
    float fVal;
    memcpy(&fVal, &uiVal, sizeof(float));
    return fVal;
}

/* ========================================================================= */
/* Checksum                                                                  */
/* ========================================================================= */
static int mtiVerifyChecksum(const unsigned char* puchFrame, int iFrameSize)
{
    unsigned char uchSum = 0;
    int i;

    for (i = 1; i < iFrameSize - 1; i++)
        uchSum += puchFrame[i];

    return ((unsigned char)(0x00 - uchSum) == puchFrame[iFrameSize - 1]);
}

/* ========================================================================= */
/* Parser Init                                                               */
/* ========================================================================= */
void mti670ParserInit(MTI670_PARSER_CTX* pstCtx)
{
    pstCtx->iRxLen = 0;
}

/* ========================================================================= */
/* Feed & Parse                                                              */
/* ========================================================================= */
int mti670Feed(MTI670_PARSER_CTX* pstCtx,
               const unsigned char* puchData,
               int iDataLen,
               IMU_FORMAT* pstImuFormat)
{
    int i;

    if (iDataLen <= 0)
        return 0;

    if (pstCtx->iRxLen + iDataLen > MTI_RX_BUF_SIZE)
        pstCtx->iRxLen = 0;

    memcpy(&pstCtx->auchRxBuf[pstCtx->iRxLen], puchData, iDataLen);
    pstCtx->iRxLen += iDataLen;

    for (i = 0; i + 4 < pstCtx->iRxLen; i++) {
        if (pstCtx->auchRxBuf[i]     != MTI_PREAMBLE ||
            pstCtx->auchRxBuf[i + 1] != MTI_BID ||
            pstCtx->auchRxBuf[i + 2] != MTI_MID_MTDATA2)
            continue;

        unsigned char uchLen = pstCtx->auchRxBuf[i + 3];
        int iFrameSize = 4 + uchLen + 1;

        if (i + iFrameSize > pstCtx->iRxLen)
            return 0;

        if (!mtiVerifyChecksum(&pstCtx->auchRxBuf[i], iFrameSize)) {
            continue;
        }

        memcpy(pstImuFormat, &pstCtx->auchRxBuf[i], iFrameSize);

        memmove(pstCtx->auchRxBuf,
                &pstCtx->auchRxBuf[i + iFrameSize],
                pstCtx->iRxLen - (i + iFrameSize));

        pstCtx->iRxLen -= (i + iFrameSize);
        return 1;
    }

    return 0;
}
