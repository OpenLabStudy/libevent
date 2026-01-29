#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include "lineOfSight.h"

/* ============================================================
 *  메인 테스트 루프
 * ============================================================ */
int main(void)
{
    double standbyAz, standbyEl, yawOffset;

    printf("=== Stabilizer Test Program ===\n");
    printf("Enter StandbyAz (deg): ");
    scanf("%lf", &standbyAz);
    printf("Enter StandbyEl (deg): ");
    scanf("%lf", &standbyEl);
    printf("Enter YawOffset (deg): ");
    scanf("%lf", &yawOffset);

    while (1) {
        char buf[64];
        IMU_DATA imu;
        double outAz, outEl;

        printf("\nEnter Roll Pitch Yaw (deg) or 'q' to quit: ");
        scanf("%63s", buf);

        if (strcmp(buf, "q") == 0 || strcmp(buf, "exit") == 0)
            break;

        imu.dRoll = atof(buf);
        scanf("%lf %lf", &imu.dPitch, &imu.dYaw);

        stabilizerCompute(
            &imu,
            standbyAz, standbyEl, yawOffset,
            &outAz, &outEl
        );

        printf("--------------------------------------------------\n");
        printf("Input  : Roll=%7.3f  Pitch=%7.3f  Yaw=%7.3f\n",
                imu.dRoll, imu.dPitch, imu.dYaw);
        printf("Output : AZ = %7.3f deg,  EL = %7.3f deg\n",
                outAz, outEl);
        printf("--------------------------------------------------\n");
    }

    printf("Program terminated.\n");
    return 0;
}