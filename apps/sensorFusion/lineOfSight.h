#include <math.h>

/* ===================== 매크로 ===================== */
#define DEGREE_TO_RADIAN(x) ((x) * M_PI / 180.0)
#define RADIAN_TO_DEGREE(x) ((x) * 180.0 / M_PI)

#define DEADBAND_AZ 0.05
#define DEADBAND_EL 0.05

typedef struct {
    double dAzimuth;    // deg
    double dElevation;  // deg
    double dRange;
} EAR;

typedef struct {
    double dEast;
    double dNorth;
    double dUp;
} ENU;

typedef struct {
    double dRoll;   // deg
    double dPitch;  // deg
    double dYaw;    // deg
} IMU_DATA;

void stabilizerCompute(const IMU_DATA *stImu,
                        double dStandbyAz, double dStandbyEl, double dYawOffset, 
                        double *dOutAz, double *dOutEl);