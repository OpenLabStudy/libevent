#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include "lineOfSight.h"

/* ============================================================
 *  메인 테스트 루프
 * ============================================================ */

int main(void)
{
    AUTO_TRACKING_WAIT stAutoTrackingWait = {0,};
    IMU_DATA stImuData = {0,};
    stAutoTrackingWait.chWaitOnOff = 1;
    stAutoTrackingWait.dStandbyAz = 20.0;
    stAutoTrackingWait.dStandbyEl = 5.0;
    stImuData.dRoll = 0.5;
    stImuData.dPitch = 0.8;
    stImuData.dYaw = 0.0;
    double randVal;

    /* ===============================
     * 기준 설정
     * =============================== */
    stabilizerSetReference(&stAutoTrackingWait, &stImuData);

    printf("===== Reference Set =====\n");
    printf("AZ=%.2f  EL=%.2f\n",
           stAutoTrackingWait.dStandbyAz, stAutoTrackingWait.dStandbyEl);

    printf("\n===== Yaw Sweep Test =====\n");

    for(int i=0; i<360; i++)
    {
        double dAzCmd, dElCmd;

        stabilizerUpdate(&stAutoTrackingWait.stStabilizerRef, &stImuData,
                         &dAzCmd, &dElCmd);

        printf("YAW=%6.2f deg  ->  AZ=%7.2f  EL=%7.2f\n",
               stImuData.dYaw,
               RADIAN_TO_DEGREE(dAzCmd),
               RADIAN_TO_DEGREE(dElCmd));

        stImuData.dRoll = 0.1 + (0.4 - 0.1) * ((double)rand() / RAND_MAX);
        stImuData.dPitch = 0.1 + (0.4 - 0.1) * ((double)rand() / RAND_MAX);
        stImuData.dYaw += 1.0;
    }

    return 0;
}
