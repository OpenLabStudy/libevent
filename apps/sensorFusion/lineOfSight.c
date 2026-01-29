#include <stdio.h>
#include <stdbool.h>
#include "lineOfSight.h"

// static inline double wrapDeg180(double dValue)
// {
//     while (dValue > 180.0) 
//         dValue -= 360.0;

//     while (dValue < -180.0)
//         dValue += 360.0;
//     return dValue;
// }

// static void ear2enu(const EAR *pstEar, ENU *pstEnu)
// {
//     double dElevation = DEGREE_TO_RADIAN(pstEar->dElevation);
//     double dAzimuth   = DEGREE_TO_RADIAN(pstEar->dAzimuth);

//     pstEnu->dEast  = pstEar->dRange * cos(dElevation) * sin(dAzimuth);
//     pstEnu->dNorth = pstEar->dRange * cos(dElevation) * cos(dAzimuth);
//     pstEnu->dUp    = pstEar->dRange * sin(dElevation);
// }

// static void transpose3x3(const double dR[3][3], double dRt[3][3])
// {
//     for (int i=0;i<3;i++)
//         for (int j=0;j<3;j++)
//             dRt[i][j] = dR[j][i];
// }

// static void calculateDCM(double dRadRoll, double dRadPitch, double dRadYaw,
//                       double adDcm[3][3])
// {
//     double Rroll[3][3] = {
//         {1, 0,              0},
//         {0, cos(dRadRoll),  sin(dRadRoll)},
//         {0, -sin(dRadRoll), cos(dRadRoll)}
//     };

//     double Rpitch[3][3] = {
//         { cos(dRadPitch),   0,  -sin(dRadPitch)},
//         { 0,                1,  0},
//         { sin(dRadPitch),   0,  cos(dRadPitch)}
//     };

//     double Ryaw[3][3] = {
//         { cos(dRadYaw), -sin(dRadYaw),  0},
//         { sin(dRadYaw), cos(dRadYaw),   0},
//         { 0,            0,              1}
//     };

//     double temp[3][3] = {0};

//     /* Ryaw * Rpitch */
//     for (int i=0;i<3;i++)
//         for (int j=0;j<3;j++)
//             for (int k=0;k<3;k++)
//                 temp[i][j] += Ryaw[i][k] * Rpitch[k][j];

//     /* (Ryaw * Rpitch) * Rroll */
//     for (int i=0;i<3;i++)
//         for (int j=0;j<3;j++) {
//             adDcm[i][j] = 0;
//             for (int k=0;k<3;k++)
//                 adDcm[i][j] += temp[i][k] * Rroll[k][j];
//         }
// }


// static void calcAzElFromEnu2Body(double dE, double dN, double dU,
//                 const double adDcm[3][3],
//                 double *dOutAz, double *dOutEl)
// {
//     double dBodyX   = adDcm[0][0]*dE + adDcm[0][1]*dN + adDcm[0][2]*dU;
//     double dBodyY   = adDcm[1][0]*dE + adDcm[1][1]*dN + adDcm[1][2]*dU;
//     double dBodyZ   = adDcm[2][0]*dE + adDcm[2][1]*dN + adDcm[2][2]*dU;

//     *dOutAz     = RADIAN_TO_DEGREE(atan2(dBodyX, dBodyY));
//     *dOutEl     = RADIAN_TO_DEGREE(atan2(dBodyZ, sqrt(dBodyX*dBodyX + dBodyY*dBodyY)));
// }

// void stabilizerCompute(const IMU_DATA *stImu,
//                         double dStandbyAz, double dStandbyEl, double dYawOffset, 
//                         double *dOutAz, double *dOutEl)
// {
//     double dDcmBody2World[3][3];
//     double dDcmWorld2Body[3][3];

//     /* EAR */
//     EAR stEar = {
//         .dAzimuth   = dStandbyAz,
//         .dElevation = dStandbyEl,
//         .dRange     = 1.0
//     };

//     ENU stEnu;
//     ear2enu(&stEar, &stEnu);
//     double dUsedYaw   = wrapDeg180(stImu->dYaw - dYawOffset);

//     calculateDCM(DEGREE_TO_RADIAN(stImu->dRoll), DEGREE_TO_RADIAN(stImu->dPitch), 
//             DEGREE_TO_RADIAN(dUsedYaw),  dDcmBody2World);

//     transpose3x3(dDcmBody2World, dDcmWorld2Body);

//     calcAzElFromEnu2Body(stEnu.dEast, stEnu.dNorth, stEnu.dUp,
//                         dDcmWorld2Body, dOutAz, dOutEl);

//     *dOutAz = wrapDeg180(*dOutAz);
// }

#include "lineOfSight.h"
#include <stdio.h>
#include "lineOfSight.h"
#include <stdio.h>

/* ============================================================
 *  내부 유틸
 * ============================================================ */
static inline double wrapDeg180(double v)
{
    while (v > 180.0) v -= 360.0;
    while (v < -180.0) v += 360.0;
    return v;
}

/* ============================================================
 *  Standby Az/El → ENU LOS 벡터
 *  az : North 기준 시계방향
 *  el : 수평 기준 위쪽 +
 * ============================================================ */
static void azelToEnu(double azDeg, double elDeg, double v[3])
{
    double az = DEGREE_TO_RADIAN(azDeg);
    double el = DEGREE_TO_RADIAN(elDeg);

    v[0] = cos(el) * sin(az);  /* East  */
    v[1] = cos(el) * cos(az);  /* North */
    v[2] = sin(el);            /* Up    */
}

/* ============================================================
 *  Body(FRU) → ENU DCM
 *  Body 축 정의:
 *    X = Forward
 *    Y = Right
 *    Z = Up
 *
 *  회전 순서:
 *    Rz(yaw) · Ry(pitch) · Rx(roll)
 *
 *  ★ FRU + ENU 기준에 맞게 검증된 최종 수식
 * ============================================================ */
static void dcmBodyToEnu(double roll, double pitch, double yaw,
                         double C[3][3])
{
    double cr = cos(roll),  sr = sin(roll);
    double cp = cos(pitch), sp = sin(pitch);
    double cy = cos(yaw),   sy = sin(yaw);

    /* Row 0 : East */
    C[0][0] =  cy * cp;
    C[0][1] =  cy * sp * sr - sy * cr;
    C[0][2] =  cy * sp * cr + sy * sr;

    /* Row 1 : North */
    C[1][0] =  sy * cp;
    C[1][1] =  sy * sp * sr + cy * cr;
    C[1][2] =  sy * sp * cr - cy * sr;

    /* Row 2 : Up */
    C[2][0] =  sp;
    C[2][1] = -cp * sr;
    C[2][2] =  cp * cr;
}

/* ============================================================
 *  ENU → Body
 *  v_body = C_be^T · v_enu
 * ============================================================ */
static void enuToBody(const double C[3][3],
                      const double vEnu[3],
                      double vBody[3])
{
    vBody[0] = C[0][0]*vEnu[0] + C[1][0]*vEnu[1] + C[2][0]*vEnu[2];
    vBody[1] = C[0][1]*vEnu[0] + C[1][1]*vEnu[1] + C[2][1]*vEnu[2];
    vBody[2] = C[0][2]*vEnu[0] + C[1][2]*vEnu[1] + C[2][2]*vEnu[2];
}

/* ============================================================
 *  Body LOS 벡터 → 짐벌 Az / El
 * ============================================================ */
static void bodyVecToAzEl(const double vBody[3],
                          double *azDeg,
                          double *elDeg)
{
    double x = vBody[0]; /* Forward */
    double y = vBody[1]; /* Right   */
    double z = vBody[2]; /* Up      */

    *azDeg = RADIAN_TO_DEGREE(atan2(y, x));
    *elDeg = RADIAN_TO_DEGREE(atan2(z, sqrt(x*x + y*y)));
}

/* ============================================================
 *  외부 API
 * ============================================================ */
void stabilizerCompute(const IMU_DATA *stImu,
                       double dStandbyAz,
                       double dStandbyEl,
                       double dYawOffset,
                       double *dOutAz,
                       double *dOutEl)
{
    double vEnu[3];
    double vBody[3];
    double Cbe[3][3];

    /* 1) Standby LOS → ENU */
    azelToEnu(dStandbyAz, dStandbyEl, vEnu);

    /* 2) IMU Euler (ENU 기준) */
    double roll  = DEGREE_TO_RADIAN(stImu->dRoll);
    double pitch = DEGREE_TO_RADIAN(stImu->dPitch);
    double yaw   = DEGREE_TO_RADIAN(
                        wrapDeg180(stImu->dYaw - dYawOffset));

    /* 3) Body → ENU DCM */
    dcmBodyToEnu(roll, pitch, yaw, Cbe);

    /* 4) ENU → Body */
    enuToBody(Cbe, vEnu, vBody);

    /* 5) Body 기준 Az / El */
    bodyVecToAzEl(vBody, dOutAz, dOutEl);

    *dOutAz = wrapDeg180(*dOutAz);
}
