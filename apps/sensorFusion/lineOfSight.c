#include <stdio.h>
#include <stdbool.h>
#include "lineOfSight.h"

static inline double wrapDeg180(double dValue)
{
    while (dValue > 180.0) 
        dValue -= 360.0;

    while (dValue < -180.0)
        dValue += 360.0;
    return dValue;
}

static inline double wrapDeg360(double dValue)
{
    dValue = fmod(dValue, 360.0);
    if (dValue < 0) 
        dValue += 360.0;
    return dValue;
}

static void ear2enu(const EAR *pstEar, ENU *pstEnu)
{
    double dElevation = DEGREE_TO_RADIAN(pstEar->dElevation);
    double dAzimuth   = DEGREE_TO_RADIAN(pstEar->dAzimuth);

    pstEnu->dEast  = pstEar->dRange * cos(dElevation) * sin(dAzimuth);
    pstEnu->dNorth = pstEar->dRange * cos(dElevation) * cos(dAzimuth);
    pstEnu->dUp    = pstEar->dRange * sin(dElevation);
}

static void transpose3x3(const double dR[3][3], double dRt[3][3])
{
    for (int i=0;i<3;i++)
        for (int j=0;j<3;j++)
            dRt[i][j] = dR[j][i];
}

static void calculateDCM(double dRadRoll, double dRadPitch, double dRadYaw,
                      double dDcm[3][3])
{
    #if 0
    double dCosRoll     = cos(dRadRoll);
    double dSinRoll     = sin(dRadRoll);
    double dCosPitch    = cos(dRadPitch);
    double dSinPitch    = sin(dRadPitch);
    double dCosYaw      = cos(dRadYaw);
    double dSinYaw      = sin(dRadYaw);

    dDcm[0][0] =  dCosYaw * dCosPitch;
    dDcm[0][1] =  dCosYaw * dSinPitch * dSinRoll - dSinYaw * dCosRoll;
    dDcm[0][2] =  dCosYaw * dSinPitch * dCosRoll + dSinYaw * dSinRoll;

    dDcm[1][0] =  dSinYaw * dCosPitch;
    dDcm[1][1] =  dSinYaw * dSinPitch * dSinRoll + dCosYaw * dCosRoll;
    dDcm[1][2] =  dSinYaw * dSinPitch * dCosRoll - dCosYaw * dSinRoll;

    // dDcm[2][0] =  -dSinPitch;
    // dDcm[2][1] =  dCosPitch * dSinRoll;
    // dDcm[2][2] =  dCosPitch * dCosRoll;
    dDcm[2][0] =  dSinPitch;
    dDcm[2][1] = -dCosPitch * dSinRoll;
    dDcm[2][2] =  dCosPitch * dCosRoll;

    #else 
    double Rroll[3][3] = {
        {1, 0,              0},
        {0, cos(dRadRoll),  sin(dRadRoll)},
        {0, -sin(dRadRoll), cos(dRadRoll)}
    };

    double Rpitch[3][3] = {
        { cos(dRadPitch),   0,  -sin(dRadPitch)},
        { 0,                1,  0},
        { sin(dRadPitch),   0,  cos(dRadPitch)}
    };

    double Ryaw[3][3] = {
        { cos(dRadYaw), -sin(dRadYaw),  0},
        { sin(dRadYaw), cos(dRadYaw),   0},
        { 0,            0,              1}
    };

    double temp[3][3] = {0};

    /* Ryaw * Rpitch */
    for (int i=0;i<3;i++)
        for (int j=0;j<3;j++)
            for (int k=0;k<3;k++)
                temp[i][j] += Ryaw[i][k] * Rpitch[k][j];

    /* (Ryaw * Rpitch) * Rroll */
    for (int i=0;i<3;i++)
        for (int j=0;j<3;j++) {
            dDcm[i][j] = 0;
            for (int k=0;k<3;k++)
                dDcm[i][j] += temp[i][k] * Rroll[k][j];
        }
#endif
    // for (int i=0;i<3;i++)
    //     for (int j=0;j<3;j++)
    //         fprintf(stderr,"dDcm[%d][%d] = %lf\n", i, j, dDcm[i][j]);
}


double calOffsetForDesiredHeading(double dYaw, double dStandbyAz)
{
    return wrapDeg360(dStandbyAz + dYaw);
}


void calcRefDCM(const IMU_DATA *pstImuData, double dDcmRefTransfer[3][3])
{
    double dDcmRef[3][3];

    /* 기준 자세 */
    calculateDCM(   DEGREE_TO_RADIAN(pstImuData->dRoll),
                    DEGREE_TO_RADIAN(pstImuData->dPitch),
                    DEGREE_TO_RADIAN(pstImuData->dYaw),
                    dDcmRef );

    /* 상대 회전: now * ref^T */
    transpose3x3(dDcmRef, dDcmRefTransfer);
}

static void calcRelativeDCM(const IMU_DATA *pstImuData,
                            double dDcmRefTransfer[3][3],
                            double dcmRel[3][3])
{
    double dDcmRealData[3][3];

    /* 현재 자세 */
    calculateDCM(
        DEGREE_TO_RADIAN(pstImuData->dRoll),
        DEGREE_TO_RADIAN(pstImuData->dPitch),
        DEGREE_TO_RADIAN(pstImuData->dYaw),
        dDcmRealData
    );

    for (int i=0;i<3;i++){
        for (int j=0;j<3;j++) {
            dcmRel[i][j] = 0.0;
            for (int k=0;k<3;k++)
                dcmRel[i][j] += dDcmRealData[i][k] * dDcmRefTransfer[k][j];
        }
    }
}

void stabilizerCompute(const IMU_DATA *pstImuData,
                        AUTO_TRACKING_WAIT *pstAutoTrackingWait,
                        double *outAz, double *outEl)
{
    double dcmRel[3][3];
    double dcmRelT[3][3];

    /* 1) 상대 회전 DCM */
    calcRelativeDCM(pstImuData, pstAutoTrackingWait->dRefDcm, dcmRel);

    /* 2) 역회전 */
    transpose3x3(dcmRel, dcmRelT);

    /* 3) 기준 LOS 벡터 (Body 기준) */
    double dStandbyAz = DEGREE_TO_RADIAN(pstAutoTrackingWait->dStandbyAz);
    double dStandbyEl = DEGREE_TO_RADIAN(pstAutoTrackingWait->dStandbyEl);

    double vRefLos[3] = {
        cos(dStandbyEl) * cos(dStandbyAz),  // X
        cos(dStandbyEl) * sin(dStandbyAz),  // Y
        sin(dStandbyEl)                     // Z
    };

    /* 4) 보정 적용 */
    double vCorrectedLos[3] = {
        dcmRelT[0][0]*vRefLos[0] + dcmRelT[0][1]*vRefLos[1] + dcmRelT[0][2]*vRefLos[2],
        dcmRelT[1][0]*vRefLos[0] + dcmRelT[1][1]*vRefLos[1] + dcmRelT[1][2]*vRefLos[2],
        dcmRelT[2][0]*vRefLos[0] + dcmRelT[2][1]*vRefLos[1] + dcmRelT[2][2]*vRefLos[2]
    };

    /* 5) Az / El 변환 (Body 기준) */
    *outAz = wrapDeg180(
        RADIAN_TO_DEGREE(atan2(vCorrectedLos[1], vCorrectedLos[0]))
    );

    *outEl = RADIAN_TO_DEGREE(
        atan2(vCorrectedLos[2], sqrt(vCorrectedLos[0]*vCorrectedLos[0] + vCorrectedLos[1]*vCorrectedLos[1]))
    ) * -1.0;
}
