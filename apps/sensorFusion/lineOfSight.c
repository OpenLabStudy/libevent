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
                      double adDcm[3][3])
{
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
            adDcm[i][j] = 0;
            for (int k=0;k<3;k++)
                adDcm[i][j] += temp[i][k] * Rroll[k][j];
        }
}

static void calcAzElFromEnu2Body(double dE, double dN, double dU,
                const double adDcm[3][3],
                double *dOutAz, double *dOutEl)
{
    double dBodyX   = adDcm[0][0]*dE + adDcm[0][1]*dN + adDcm[0][2]*dU;
    double dBodyY   = adDcm[1][0]*dE + adDcm[1][1]*dN + adDcm[1][2]*dU;
    double dBodyZ   = adDcm[2][0]*dE + adDcm[2][1]*dN + adDcm[2][2]*dU;

    // *dOutAz     = RADIAN_TO_DEGREE(atan2(dBodyX, dBodyY));
    *dOutAz     = RADIAN_TO_DEGREE(atan2(dBodyY, dBodyX));
    *dOutEl     = RADIAN_TO_DEGREE(atan2(dBodyZ, sqrt(dBodyX*dBodyX + dBodyY*dBodyY)));
}


double calOffsetForDesiredHeading(double dYaw, double dStandbyAz)
{
    return wrapDeg360(dStandbyAz + dYaw);
}


void stabilizerInit(STABILIZER_REF *pstStabilizerRef,
                    const IMU_DATA *pstImuData,
                    double dStandbyAz, double dStandbyEl)
{
    pstStabilizerRef->stImuData.dRoll  = pstImuData->dRoll;
    pstStabilizerRef->stImuData.dPitch = pstImuData->dPitch;
    pstStabilizerRef->stImuData.dYaw   = pstImuData->dYaw;

    pstStabilizerRef->dStandbyAz        = dStandbyAz;
    pstStabilizerRef->dStandbyEl        = dStandbyEl;
}

#if 0
void stabilizerCompute(const IMU_DATA *stImu,
                        double dStandbyAz, double dStandbyEl,
                        double *dOutAz, double *dOutEl)
{
    double dDcmBody2World[3][3];
    double dDcmWorld2Body[3][3];
    double dUsedAz;
    dUsedAz = dStandbyAz + (90.0 - stImu->dYaw);
    /* EAR */
    EAR stEar = {
        .dAzimuth   = dStandbyAz,
        .dElevation = dStandbyEl,
        .dRange     = 1.0
    };

    ENU stEnu;
    ear2enu(&stEar, &stEnu);

    calculateDCM(   DEGREE_TO_RADIAN(stImu->dRoll), 
                    DEGREE_TO_RADIAN(stImu->dPitch), 
                    DEGREE_TO_RADIAN(stImu->dYaw),
                    dDcmBody2World);

    transpose3x3(dDcmBody2World, dDcmWorld2Body);

    calcAzElFromEnu2Body(stEnu.dEast, stEnu.dNorth, stEnu.dUp,
                        dDcmWorld2Body, dOutAz, dOutEl);

    *dOutAz = wrapDeg180(*dOutAz);
}
    #else
static void calcRelativeDCM(const IMU_DATA *pstImuData,
                            const STABILIZER_REF *pstStabilizerRef,
                            double dcmRel[3][3])
{
    double dDcmRef[3][3];
    double dDcmRealData[3][3];
    double dDcmRefTransfer[3][3];

    /* 기준 자세 */
    calculateDCM(
        DEGREE_TO_RADIAN(pstStabilizerRef->stImuData.dRoll),
        DEGREE_TO_RADIAN(pstStabilizerRef->stImuData.dPitch),
        DEGREE_TO_RADIAN(pstStabilizerRef->stImuData.dYaw),
        dDcmRef
    );

    /* 현재 자세 */
    calculateDCM(
        DEGREE_TO_RADIAN(pstImuData->dRoll),
        DEGREE_TO_RADIAN(pstImuData->dPitch),
        DEGREE_TO_RADIAN(pstImuData->dYaw),
        dDcmRealData
    );

    /* 상대 회전: now * ref^T */
    transpose3x3(dDcmRef, dDcmRefTransfer);

    for (int i=0;i<3;i++){
        for (int j=0;j<3;j++) {
            dcmRel[i][j] = 0.0;
            for (int k=0;k<3;k++)
                dcmRel[i][j] += dDcmRealData[i][k] * dDcmRefTransfer[k][j];
        }
    }
}

void stabilizerCompute(const IMU_DATA *pstImuData,
                       const STABILIZER_REF *pstStabilizerRef,
                       double *outAz, double *outEl)
{
    double dcmRel[3][3];
    double dcmRelT[3][3];

    /* 1) 상대 회전 DCM */
    calcRelativeDCM(pstImuData, pstStabilizerRef, dcmRel);

    /* 2) 역회전 */
    transpose3x3(dcmRel, dcmRelT);

    /* 3) 기준 LOS 벡터 (Body 기준) */
    double az0 = DEGREE_TO_RADIAN(pstStabilizerRef->dStandbyAz);
    double el0 = DEGREE_TO_RADIAN(pstStabilizerRef->dStandbyEl);

    double v0[3] = {
        cos(el0) * sin(az0),  // X
        cos(el0) * cos(az0),  // Y
        sin(el0)              // Z
    };

    /* 4) 보정 적용 */
    double vc[3] = {
        dcmRelT[0][0]*v0[0] + dcmRelT[0][1]*v0[1] + dcmRelT[0][2]*v0[2],
        dcmRelT[1][0]*v0[0] + dcmRelT[1][1]*v0[1] + dcmRelT[1][2]*v0[2],
        dcmRelT[2][0]*v0[0] + dcmRelT[2][1]*v0[1] + dcmRelT[2][2]*v0[2]
    };

    /* 5) Az / El 변환 (Body 기준) */
    *outAz = wrapDeg180(
        RADIAN_TO_DEGREE(atan2(vc[0], vc[1]))
    );

    *outEl = RADIAN_TO_DEGREE(
        atan2(vc[2], sqrt(vc[0]*vc[0] + vc[1]*vc[1]))
    );
}

#endif