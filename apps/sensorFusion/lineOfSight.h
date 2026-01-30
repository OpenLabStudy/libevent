#include <math.h>

/* ===================== 매크로 ===================== */
#define DEGREE_TO_RADIAN(x) ((x) * M_PI / 180.0)
#define RADIAN_TO_DEGREE(x) ((x) * 180.0 / M_PI)

#define DEADBAND_AZ 0.05
#define DEADBAND_EL 0.05

typedef struct {
	double dLatitude;	/**< 위도값 */
	double dLongitude;	/**< 경도값 */
	double dAltitude;	/**< 고도값 */
} LLA;

/**
 * @struct ECEF
 * @brief 지구 중심 좌표계의 X,Y,Z값이며, (0,0,0)이 지구 중심을 나타낸다.
 */
typedef struct {
	double dX;	/**< 위/경도(0, 0)을 통과하며 본초 자오선을 통과하는 값 */
	double dY;	/**< 위/경도(-90, 0)을 통과하며 적도를 통과하는 값 */
	double dZ;	/**< 남북극을 통과하는 값 */
} ECEF;


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

typedef struct
{
    /* IMU 기준 자세 (자세보정 시작 시점) */
    IMU_DATA stImuData;

    /* 페데스탈 기준 LOS (자세보정 시작 시점) */
    double dStandbyAz;
    double dStandbyEl;

} STABILIZER_REF;

void stabilizerInit(STABILIZER_REF *pstStabilizerRef,
                    const IMU_DATA *pstImuData,
                    double dStandbyAz, double dStandbyEl);
void stabilizerCompute(const IMU_DATA *pstImuData,
                       const STABILIZER_REF *pstStabilizerRef,
                       double *outAz, double *outEl);                    
// void stabilizerCompute(const IMU_DATA *stImu,
//                         double dStandbyAz, double dStandbyEl,
//                         double *dOutAz, double *dOutEl);
