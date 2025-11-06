/**
 * @file r632Gps.c
 * @brief Hemisphere R632 GNSS Binary Parser Implementation
 */

 #include "r632Gps.h"
 #include <string.h>
 #include <math.h>
 #include <time.h>
 #include <stdio.h>
 
 /* 내부 전용 정적 함수 선언 */
 static int FindNextHeader(const uint8_t* data, int len, int start);
 static int GetGpsDataLength(const char* frame);
 static uint16_t GetGpsDataCrc(const char* frame);
 static char VerifyChecksum(const uint8_t* frame, int size);
 static char ExtractNextFrame(const uint8_t* data, int len, int* offset,
                              uint8_t* out, int* outLen);
 
 /* ================================================================
  * 내부 유틸 함수
  * ================================================================ */
 
 static int FindNextHeader(const uint8_t* data, int len, int start)
 {
     for (int i = start; i + 3 < len; i++) {
         if (data[i]=='$' && data[i+1]=='B' && data[i+2]=='I' && data[i+3]=='N')
             return i;
     }
     return -1;
 }
  /**
  * @brief 데이터 길이 추출
  *
  * @param p_pchFrame    프레임 시작 주소
  * @return              데이터 길이 (바이트 단위)
  */
 static int GetGpsDataLength(const char* frame)
 {
     const SBinaryMsg3* msg = (const SBinaryMsg3*)frame;
     return msg->m_stHead.m_stBytes.m_wDataLength;
 }
  /**
  * @brief 체크섬 필드 추출
  *
  * @param p_pchFrame    프레임 시작 주소
  * @return              체크섬 값 (16비트)
  */
 static uint16_t GetGpsDataCrc(const char* frame)
 {
     const SBinaryMsg3* msg = (const SBinaryMsg3*)frame;
     return msg->m_wChecksum;
 }
  /**
  * @brief 체크섬 검증
  *
  * 체크섬은 헤더(8바이트) 뒤의 데이터 영역(길이 m_wDataLength)의 바이트 합을
  * 16비트로 마스킹한 값과 m_wChecksum을 비교하여 검증한다.
  *
  * @param p_pchFrame    프레임 데이터 (헤더 포함)
  * @param iSize         프레임의 가용 길이(바이트)
  * @return              1(유효) / 0(무효)
  */
 static char VerifyChecksum(const uint8_t* frame, int size)
 {
     if (size < 12) return 0;
     uint16_t len = (uint16_t)GetGpsDataLength((const char*)frame);
     int total = 8 + len + 2;
     if (size < total) return 0;
 
     uint16_t sum = 0;
     for (int i = 8; i < 8 + len; i++)
         sum += frame[i];
 
     uint16_t calc = (uint16_t)(sum & 0xFFFF);
     return (calc == GetGpsDataCrc((const char*)frame)) ? 1 : 0;
 }
  /**
  * @brief 버퍼에서 다음 유효 프레임 추출
  *
  * @param p_pchData     전체 버퍼
  * @param iLen          버퍼 크기
  * @param p_iOffset     시작 오프셋(입력) / 다음 검색 위치(출력)
  * @param p_pchOut      추출된 프레임 복사 대상 버퍼
  * @param p_iOutLen     추출된 프레임 길이(출력)
  * @return              1(성공) / 0(실패 또는 부족)
  */
 static char ExtractNextFrame(const uint8_t* data, int len, int* offset,
                              uint8_t* out, int* outLen)
 {
     int pos = *offset;
     while (1) {
         int start = FindNextHeader(data, len, pos);
         if (start < 0) { *offset = len; return 0; }
         if (start + 8 > len) { *offset = start; return 0; }
 
         uint16_t dlen = (uint16_t)GetGpsDataLength((const char*)(data + start));
         if (dlen < 4 || dlen > R632_MAX_BUFFER) { pos = start + 1; continue; }
 
         int frameSize = 8 + dlen + 4;
         if (start + frameSize > len) { *offset = start; return 0; }
 
         if (!VerifyChecksum(data + start, frameSize - 2)) {
             pos = start + 1;
             continue;
         }
 
         memcpy(out, data + start, frameSize);
         *outLen = frameSize;
         *offset = start + frameSize;
         return 1;
     }
 }
 
 /* ================================================================
  * 공개 API 함수
  * ================================================================ */
  
 /**
  * @brief 프레임 파싱 및 시간 문자열 변환
  *
  * - 구조체에 안전 복사
  * - GPS 주/주내초(Leap sec 보정) → UNIX epoch 변환
  * - UTC 문자열 포맷팅
  *
  * @param p_pchFrame    프레임 데이터(헤더 포함)
  * @param iSize         프레임 길이
  * @return              파싱 결과 구조체 (m_chOk=1이면 유효)
  */
 SGpsDataInfo R632ParseFrame(const uint8_t* frame, int size)
 {
     SGpsDataInfo info;
     memset(&info, 0, sizeof(info));
 
     if (size < 8) return info;
     if (!(frame[0]=='$' && frame[1]=='B' && frame[2]=='I' && frame[3]=='N'))
         return info;
 
     size_t copy = (size < (int)sizeof(SBinaryMsg3)) ? size : sizeof(SBinaryMsg3);
     memcpy(&info.m_stMsg3, frame, copy);
 
     const int secPerWeek = 604800;
     const int leapSec = 19;
     const int gpsEpoch = 315964800; // 1980-01-06
 
     double totalSec = (double)info.m_stMsg3.m_wGpsWeek * secPerWeek +
                       info.m_stMsg3.m_dGpsTow - leapSec;
 
     time_t tUnix = (time_t)(gpsEpoch + floor(totalSec));
     int milli = (int)((totalSec - floor(totalSec)) * 1000.0);
     struct tm* utc = gmtime(&tUnix);
     if (utc)
         snprintf(info.m_szTime, sizeof(info.m_szTime),
                  "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                  utc->tm_year + 1900, utc->tm_mon + 1, utc->tm_mday,
                  utc->tm_hour, utc->tm_min, utc->tm_sec, milli);
 
     info.m_chOk = 1;
     return info;
 }
  
 /**
  * @brief UART 스트림에서 R632 프레임을 추출/파싱
  *
  * 내부 누적 버퍼에 수신 바이트를 append한 뒤, 유효 프레임을 탐색/검증/파싱한다.
  * 성공하면 p_pstOut에 최종 레코드를 저장하고, 누적 버퍼에서 처리된 구간을 제거한다.
  *
  * @param p_pchData     새로 수신된 UART 데이터
  * @param iLen          수신된 데이터 길이
  * @param p_pstOut      결과 저장 구조체 (누적 버퍼 포함)
  * @return              1: 프레임 파싱 성공 / 0: 프레임 없음
  */
 char R632Feed(const uint8_t* data, int len, SGpsDataInfo* out)
 {
     uint8_t frame[R632_MAX_BUFFER];
     int frameLen = 0;
 
     if (out->m_iTotSize + len > R632_MAX_BUFFER) {
         out->m_iTotSize = 0;
         out->m_iOffset = 0;
     }
 
     memcpy(out->m_szGpsData + out->m_iTotSize, data, len);
     out->m_iTotSize += len;
 
     while (ExtractNextFrame((const uint8_t*)out->m_szGpsData, out->m_iTotSize,
                             &out->m_iOffset, frame, &frameLen)) {
         *out = R632ParseFrame(frame, frameLen);
         if (out->m_chOk) {
             int remain = out->m_iTotSize - out->m_iOffset;
             memmove(out->m_szGpsData, out->m_szGpsData + out->m_iOffset, remain);
             out->m_iTotSize = remain;
             out->m_iOffset = 0;
             return 1;
         }
     }
 
     if (out->m_iOffset > 0 && out->m_iOffset < out->m_iTotSize) {
         int remain = out->m_iTotSize - out->m_iOffset;
         memmove(out->m_szGpsData, out->m_szGpsData + out->m_iOffset, remain);
         out->m_iTotSize = remain;
         out->m_iOffset = 0;
     }
 
     return 0;
 }
 