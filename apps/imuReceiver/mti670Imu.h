#ifndef MTI670_IMU_H
#define MTI670_IMU_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/* ========================================================================= */
/* MTi-670 Frame Constants                                                   */
/* ========================================================================= */
#define MTI_PREAMBLE       0xFA
#define MTI_BID            0xFF
#define MTI_MID_MTDATA2    0x36

#define MTI_RX_BUF_SIZE    2048

/* ========================================================================= */
/* PACKET INFO                                                               */
/* ========================================================================= */
typedef struct __attribute__((__packed__)){
    unsigned short unDataId;
    unsigned char  uchDataLen;
} PACKET_INFO;

/* ========================================================================= */
/* XDI PACKETS                                                               */
/* ========================================================================= */
typedef struct __attribute__((__packed__)){
    PACKET_INFO    stPacketInfo;
    unsigned char  uchPacketCount[2];
} XDI_PacketCounter;

typedef struct __attribute__((__packed__)){
    PACKET_INFO    stPacketInfo;
    unsigned char  uchSampleTimeFine[4];
} XDI_SampleTimeFine;

typedef struct __attribute__((__packed__)){
    PACKET_INFO    stPacketInfo;
    unsigned char  uchSampleTimeCoarse[4];
} XDI_SampleTimeCoarse;

typedef struct __attribute__((__packed__)){
    PACKET_INFO    stPacketInfo;
    unsigned char  uchTimestampGroup[12];
} XDI_TimestampGroup;

typedef struct __attribute__((__packed__)){
    PACKET_INFO    stPacketInfo;
    char           chRoll[4];
    char           chPitch[4];
    char           chYaw[4];
} XDI_EulerAngles;

//가속도계 값(Accelerometer) Raw 가속도
typedef struct __attribute__((__packed__)){
    PACKET_INFO    stPacketInfo;
    float          fAccX;
    float          fAccY;
    float          fAccZ;
} XDI_Acceleration;

//가속도계 값(Accelerometer) 적분된 가속도
typedef struct __attribute__((__packed__)){
    PACKET_INFO    stPacketInfo;
    float          fDeltaX;
    float          fDeltaY;
    float          fDeltaZ;
} XDI_DeltaV;

//가속도계 값(Accelerometer) 중력 제거 가속도
typedef struct __attribute__((__packed__)){
    PACKET_INFO    stPacketInfo;
    float          fFreeAccX;
    float          fFreeAccY;
    float          fFreeAccZ;
} XDI_FreeAcceleration;

// Gyroscope
typedef struct __attribute__((__packed__)){
    PACKET_INFO    stPacketInfo;
    float          fGyrX;
    float          fGyrY;
    float          fGyrZ;
} XDI_RateOfTurn;

typedef struct __attribute__((__packed__)){
    PACKET_INFO    stPacketInfo;
    float          fDeltaQ0;
    float          fDeltaQ1;
    float          fDeltaQ2;
    float          fDeltaQ3;
} XDI_DeltaQ;

typedef struct __attribute__((__packed__)){
    PACKET_INFO    stPacketInfo;
    unsigned char  uchStatusWord[4];
} XDI_StatusWord;

/* ========================================================================= */
/* IMU FORMAT                                                                */
/* ========================================================================= */
typedef struct __attribute__((__packed__)){
    unsigned char         uchPreamble;
    unsigned char         uchBid;
    unsigned char         uchMid;
    unsigned char         uchLen;

    XDI_PacketCounter     stPacketCount;
    XDI_SampleTimeFine    stSampleTimeFine;
    XDI_SampleTimeCoarse  stSampleTimeCoarse;
    XDI_TimestampGroup    stTimestampGroup;
    XDI_EulerAngles       stEulerAngles;
    XDI_Acceleration      stAcceleration;
    XDI_DeltaV            stDeltaV;
    XDI_FreeAcceleration  stFreeAcceleration;
    XDI_RateOfTurn        stRateOfTurn;
    XDI_DeltaQ            stDeltaQ;
    XDI_StatusWord        stStatusWord;

    unsigned char         ucCrc;
} IMU_FORMAT;

/* ========================================================================= */
/* Parser Context                                                            */
/* ========================================================================= */
typedef struct {
    unsigned char  auchRxBuf[MTI_RX_BUF_SIZE];
    int            iRxLen;
} MTI670_PARSER_CTX;

/* ========================================================================= */
/* API                                                                       */
/* ========================================================================= */
void mti670ParserInit(MTI670_PARSER_CTX* pstCtx);

int  mti670Feed(MTI670_PARSER_CTX* pstCtx,
                const unsigned char* puchData,
                int iDataLen,
                IMU_FORMAT* pstImuFormat);

/* ========================================================================= */
/* Utilities                                                                 */
/* ========================================================================= */
unsigned short  mtiBe16(const unsigned char* puch);
unsigned int    mtiBe32(const unsigned char* puch);
float           mtiBeFloat(const unsigned char* puch);
float           mtiSwapFloat(float fIn);

#ifdef __cplusplus
}
#endif

#endif /* MTI670_IMU_H */
