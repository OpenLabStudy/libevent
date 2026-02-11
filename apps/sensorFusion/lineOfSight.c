#include <math.h>
#include <stdio.h>
#include <stdbool.h>
#include "lineOfSight.h"

static inline double wrapDeg180(double dValue)
{
    while (dValue > 180.0) dValue -= 360.0;
    while (dValue < -180.0) dValue += 360.0;
    return dValue;
}

static inline double wrapDeg360(double dValue)
{
    dValue = fmod(dValue, 360.0);
    if (dValue < 0) dValue += 360.0;
    return dValue;
}

static VECTOR3 calcLosVectorToBody(double dAzRad, double dElRad)
{
    VECTOR3 stVector3;

    stVector3.dX = cos(dElRad) * cos(dAzRad);
    stVector3.dY = cos(dElRad) * sin(dAzRad);
    stVector3.dZ = sin(dElRad);

    return stVector3;
}

/**
 * @brief  3x3 행렬 전치 함수
 *
 * @param src  입력 행렬 (3x3)
 * @param dst  출력 행렬 (src의 전치행렬)
 *
 * @note
 *  - 회전행렬(DCM)은 직교행렬이므로
 *    전치(transpose)는 역회전(inverse rotation)과 동일함
 *  - Active rotation 행렬 → Passive rotation 행렬 변환에 사용
 */
static void transposeDcm3x3(const double dSrcDcm[3][3],
                            double dDstDcm[3][3])
{
    dDstDcm[0][0] = dSrcDcm[0][0];
    dDstDcm[0][1] = dSrcDcm[1][0];
    dDstDcm[0][2] = dSrcDcm[2][0];

    dDstDcm[1][0] = dSrcDcm[0][1];
    dDstDcm[1][1] = dSrcDcm[1][1];
    dDstDcm[1][2] = dSrcDcm[2][1];

    dDstDcm[2][0] = dSrcDcm[0][2];
    dDstDcm[2][1] = dSrcDcm[1][2];
    dDstDcm[2][2] = dSrcDcm[2][2];
}


/**
 * @brief  Euler 각(Roll, Pitch, Yaw)으로부터 DCM 생성 (Active rotation)
 *
 * @param dRoll   Roll  angle [rad] (X-axis rotation)
 * @param dPitch  Pitch angle [rad] (Y-axis rotation)
 * @param dYaw    Yaw   angle [rad] (Z-axis rotation)
 * @param R       Output DCM (3x3)
 *
 * @note
 *  - R = Rz(dYaw) * Ry(dPitch) * Rx(dRoll)
 *  - 본 행렬은 벡터를 회전시키는 Active rotation 행렬임
 *  - 좌표계 변환(Passive rotation)에 사용할 경우 반드시 transpose 필요
 */
static void calcDcmFromEuler(double dRoll, double dPitch, double dYaw,
                             double adOutDcm[3][3])
{
    double dCosRoll  = cos(dRoll);
    double dSinRoll  = sin(dRoll);
    double dCosPitch = cos(dPitch);
    double dSinPitch = sin(dPitch);
    double dCosYaw   = cos(dYaw);
    double dSinYaw   = sin(dYaw);

    /* R = Rz(yaw) * Ry(pitch) * Rx(roll) */

    adOutDcm[0][0] =  dCosYaw * dCosPitch;
    adOutDcm[0][1] =  dCosYaw * dSinPitch * dSinRoll - dSinYaw * dCosRoll;
    adOutDcm[0][2] =  dCosYaw * dSinPitch * dCosRoll + dSinYaw * dSinRoll;

    adOutDcm[1][0] =  dSinYaw * dCosPitch;
    adOutDcm[1][1] =  dSinYaw * dSinPitch * dSinRoll + dCosYaw * dCosRoll;
    adOutDcm[1][2] =  dSinYaw * dSinPitch * dCosRoll - dCosYaw * dSinRoll;

    adOutDcm[2][0] = -dSinPitch;
    adOutDcm[2][1] =  dCosPitch * dSinRoll;
    adOutDcm[2][2] =  dCosPitch * dCosRoll;
}

static VECTOR3 rotateVector(const double adRotate[3][3], const VECTOR3* pstVector3)
{
    VECTOR3 stOutVector3;

    stOutVector3.dX = adRotate[0][0]*pstVector3->dX + adRotate[0][1]*pstVector3->dY + adRotate[0][2]*pstVector3->dZ;
    stOutVector3.dY = adRotate[1][0]*pstVector3->dX + adRotate[1][1]*pstVector3->dY + adRotate[1][2]*pstVector3->dZ;
    stOutVector3.dZ = adRotate[2][0]*pstVector3->dX + adRotate[2][1]*pstVector3->dY + adRotate[2][2]*pstVector3->dZ;

    return stOutVector3;
}

static void vectorToAzEl(const VECTOR3* pstVector3, double* pdAzRad, double* pdElRad)
{
    double dX = pstVector3->dX;
    double dY = pstVector3->dY;
    double dZ = pstVector3->dZ;

    *pdAzRad = atan2(dY, dX);
    *pdElRad = atan2(dZ, sqrt(dX*dX + dY*dY));
}

static void matMul3x3(const double adMatA[3][3],
                      const double adMatB[3][3],
                      double adOutMat[3][3])
{
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            adOutMat[i][j] = 0.0;
            for (int k = 0; k < 3; k++) {
                adOutMat[i][j] += adMatA[i][k] * adMatB[k][j];
            }
        }
    }
}

void stabilizerSetReference(AUTO_TRACKING_WAIT* pstAutoTrackingWait, IMU_DATA* pstImuData)
{
    // 1) 기준 Body LOS 저장
    pstAutoTrackingWait->stStabilizerRef.stVecRef = calcLosVectorToBody(
        DEGREE_TO_RADIAN(pstAutoTrackingWait->dStandbyAz),
        DEGREE_TO_RADIAN(pstAutoTrackingWait->dStandbyEl)
    );

    // 2) 기준 IMU DCM 저장 (Body→ENU)
    calcDcmFromEuler(DEGREE_TO_RADIAN(pstImuData->dRoll),
                     DEGREE_TO_RADIAN(pstImuData->dPitch),
                     DEGREE_TO_RADIAN(pstImuData->dYaw),
                     pstAutoTrackingWait->stStabilizerRef.adRotaionRef);
}



void stabilizerUpdate(const STABILIZER_REF* pstRef, IMU_DATA* pstImuData,
                      double* pdAzCmdRad, double* pdElCmdRad)
{
    double R_curr[3][3];
    double R_curr_T[3][3];
    double R_delta[3][3];

    VECTOR3 V_B_cmd;

    /* 1) 현재 IMU DCM (Body→ENU) */
    calcDcmFromEuler(DEGREE_TO_RADIAN(pstImuData->dRoll),DEGREE_TO_RADIAN(pstImuData->dPitch),
                     DEGREE_TO_RADIAN(pstImuData->dYaw), R_curr);

    /* 2) R_curr^T (ENU→Body) */
    transposeDcm3x3(R_curr, R_curr_T);

    /* 3) R_delta = R_curr^T * R_ref */
    matMul3x3(R_curr_T, pstRef->adRotaionRef, R_delta);

    /* 4) 기준 Body LOS에 상대회전 적용 */
    V_B_cmd = rotateVector(R_delta, &pstRef->stVecRef);

    /* 5) AZ / EL 계산 */
    vectorToAzEl(&V_B_cmd, pdAzCmdRad, pdElCmdRad);
}
