#ifndef TIME_UTIL_H
#define TIME_UTIL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <sys/time.h>

/* =========================================================================
 * Time Get (Absolute Time)
 * ========================================================================= */

/* 현재 시각 (ms, epoch 기준) */
long long timeNowMs(void);

/* 현재 시각 (us, epoch 기준) */
long long timeNowUs(void);

/* =========================================================================
 * Time Diff
 * ========================================================================= */

/* 두 timeval 간의 차이 (end - start), ms 단위 */
long timeDiffMs(const struct timeval *start,
                const struct timeval *end);

/* 두 timeval 간의 차이 (end - start), us 단위 */
long timeDiffUs(const struct timeval *start,
                const struct timeval *end);

/* =========================================================================
 * Time Pack / Unpack
 *
 * Bit layout (uint32_t)
 * [31:27] reserved
 * [26:22] hour (5 bits)
 * [21:16] min  (6 bits)
 * [15:10] sec  (6 bits)
 * [ 9: 0] ms   (10 bits)
 * ========================================================================= */

/* 현재 시각을 packed HMSms 포맷으로 반환 */
uint32_t timePackHMSms(void);

/* 개별 값으로 pack */
uint32_t timePackHMSmsEx(uint8_t hour,
                          uint8_t min,
                          uint8_t sec,
                          uint16_t ms);

/* packed HMSms 포맷 unpack */
void timeUnpackHMSms(uint32_t packed,
                     uint8_t *hour,
                     uint8_t *min,
                     uint8_t *sec,
                     uint16_t *ms);


#ifdef __cplusplus
}
#endif

#endif /* TIME_UTIL_H */
