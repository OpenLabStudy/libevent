#include <math.h>

/* ===================== 매크로 ===================== */
#define DEGREE_TO_RADIAN(x) ((x) * M_PI / 180.0)
#define RADIAN_TO_DEGREE(x) ((x) * 180.0 / M_PI)

#define DEADBAND_AZ 0.05
#define DEADBAND_EL 0.05

typedef struct {
    double dX;
    double dY;
    double dZ;
} VECTOR3;

typedef struct
{
    double adRotaionRef[3][3];   // 기준 IMU 자세 (Body→ENU)
    VECTOR3 stVecRef;      // 기준 Body LOS (AZ0, EL0)
} STABILIZER_REF;


typedef struct {
    double dRoll;   // deg
    double dPitch;  // deg
    double dYaw;    // deg
} IMU_DATA;

typedef struct{
    char    chWaitOnOff;
    double  dStandbyAz;
    double  dStandbyEl;
    STABILIZER_REF stStabilizerRef;
}AUTO_TRACKING_WAIT;

void stabilizerSetReference(AUTO_TRACKING_WAIT* pstAutoTrackingWait, IMU_DATA* pstImuData);

void stabilizerUpdate(const STABILIZER_REF* pstRef, IMU_DATA* pstImuData,
                      double* pdAzCmdRad, double* pdElCmdRad);