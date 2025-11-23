/**
 * @file r632Gps.c
 * @brief Hemisphere R632 GNSS Binary Message Parser Implementation
 */

#include "r632Gps.h"
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdio.h>


/* ========================================================================== */
/* Internal Static Function Prototypes                                        */
/* ========================================================================== */

/**
 * @brief 버퍼에서 다음 `$BIN` 헤더 검색
 */
static int FindNextHeader(const uint8_t* pData, int len, int start);

/**
 * @brief 메시지 데이터 길이 추출
 */
static int GetGpsDataLength(const char* pFrame);

/**
 * @brief 메시지의 체크섬 필드 추출
 */
static uint16_t GetGpsDataCrc(const char* pFrame);

/**
 * @brief 헤더+Payload의 체크섬 검증
 */
static char VerifyChecksum(const uint8_t* pFrame, int size);

/**
 * @brief 버퍼에서 다음 완전한 프레임을 추출
 */
static char ExtractNextFrame(
        const uint8_t* pData, int len, int* pOffset,
        uint8_t* pOut, int* pOutLen);


/* ========================================================================== */
/* Internal Functions                                                         */
/* ========================================================================== */

static int FindNextHeader(const uint8_t* pData, int len, int start)
{
    for (int i = start; i + 3 < len; i++)
    {
        if (pData[i]=='$' && pData[i+1]=='B' && pData[i+2]=='I' && pData[i+3]=='N')
            return i;
    }
    return -1;
}


static int GetGpsDataLength(const char* pFrame)
{
    const SBinaryMsg3* pstMsg = (const SBinaryMsg3*)pFrame;
    return pstMsg->m_stHead.m_stBytes.m_wDataLength;
}


static uint16_t GetGpsDataCrc(const char* pFrame)
{
    const SBinaryMsg3* pstMsg = (const SBinaryMsg3*)pFrame;
    return pstMsg->m_wChecksum;
}


static char VerifyChecksum(const uint8_t* pFrame, int size)
{
    if (size < 12) return 0;

    uint16_t length = (uint16_t)GetGpsDataLength((const char*)pFrame);
    int expectedTotal = 8 + length + 2; // header + data + checksum

    if (size < expectedTotal)
        return 0;

    uint16_t u16Sum = 0;
    for (int i = 8; i < 8 + length; i++)
        u16Sum += pFrame[i];

    return ((u16Sum & 0xFFFF) == GetGpsDataCrc((const char*)pFrame)) ? 1 : 0;
}


static char ExtractNextFrame(
        const uint8_t* pData, int len, int* pOffset,
        uint8_t* pOut, int* pOutLen)
{
    int pos = *pOffset;

    while (1)
    {
        int start = FindNextHeader(pData, len, pos);
        if (start < 0) { *pOffset = len; return 0; }

        if (start + 8 > len) { *pOffset = start; return 0; }

        uint16_t dlen = (uint16_t)GetGpsDataLength((const char*)(pData + start));
        if (dlen < 4 || dlen > R632_MAX_BUFFER) { pos = start + 1; continue; }

        int frameSize = 8 + dlen + 4; // including tail CRLF

        if (start + frameSize > len) { *pOffset = start; return 0; }

        if (!VerifyChecksum(pData + start, frameSize - 2)) {
            pos = start + 1;
            continue;
        }

        memcpy(pOut, pData + start, frameSize);
        *pOutLen = frameSize;
        *pOffset = start + frameSize;
        return 1;
    }
}


/* ========================================================================== */
/* Public API Implementations                                                 */
/* ========================================================================== */

SGpsDataInfo R632ParseFrame(const uint8_t* pFrame, int size)
{
    SGpsDataInfo stInfo;
    memset(&stInfo, 0, sizeof(stInfo));

    if (size < 8) return stInfo;
    if (!(pFrame[0]=='$' && pFrame[1]=='B' && pFrame[2]=='I' && pFrame[3]=='N'))
        return stInfo;

    int copyLen = (size < (int)sizeof(SBinaryMsg3)) ? size : sizeof(SBinaryMsg3);
    memcpy(&stInfo.m_stMsg3, pFrame, copyLen);

    const int secPerWeek = 604800;
    const int leapSec = 19;
    const int gpsEpoch = 315964800; // Unix time for 1980-01-06

    double totalSec = (double)stInfo.m_stMsg3.m_wGpsWeek * secPerWeek +
                    stInfo.m_stMsg3.m_dGpsTow - leapSec;

    time_t unixSec = (time_t)(gpsEpoch + floor(totalSec));
    int milli = (int)((totalSec - floor(totalSec)) * 1000.0);

    struct tm* pstUtc = gmtime(&unixSec);
    if (pstUtc)
    {
        snprintf(stInfo.m_szTime, sizeof(stInfo.m_szTime),
                "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                pstUtc->tm_year + 1900, pstUtc->tm_mon + 1, pstUtc->tm_mday,
                pstUtc->tm_hour, pstUtc->tm_min, pstUtc->tm_sec, milli);
    }

    stInfo.m_chOk = 1;
    return stInfo;
}


char R632Feed(const uint8_t* pData, int len, SGpsDataInfo* pOut)
{
    uint8_t frame[R632_MAX_BUFFER];
    int frameLen = 0;

    /* overflow protection: drop buffer if full */
    if (pOut->m_iTotSize + len > R632_MAX_BUFFER) {
        pOut->m_iTotSize = 0;
        pOut->m_iOffset  = 0;
    }

    memcpy(pOut->m_szGpsData + pOut->m_iTotSize, pData, len);
    pOut->m_iTotSize += len;

    while (ExtractNextFrame(
                (const uint8_t*)pOut->m_szGpsData, pOut->m_iTotSize,
                &pOut->m_iOffset, frame, &frameLen))
    {
        *pOut = R632ParseFrame(frame, frameLen);

        if (pOut->m_chOk)
        {
            int remain = pOut->m_iTotSize - pOut->m_iOffset;
            memmove(pOut->m_szGpsData, pOut->m_szGpsData + pOut->m_iOffset, remain);
            pOut->m_iTotSize = remain;
            pOut->m_iOffset  = 0;
            return 1;
        }
    }

    if (pOut->m_iOffset > 0 && pOut->m_iOffset < pOut->m_iTotSize) {
        int remain = pOut->m_iTotSize - pOut->m_iOffset;
        memmove(pOut->m_szGpsData, pOut->m_szGpsData + pOut->m_iOffset, remain);
        pOut->m_iTotSize = remain;
        pOut->m_iOffset  = 0;
    }

    return 0;
}
 