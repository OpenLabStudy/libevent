#include "timeUtil.h"

#include <time.h>
#include <string.h>

/* =========================================================================
 * Time Get (Absolute)
 * ========================================================================= */

long long timeNowMs(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    return (long long)tv.tv_sec * 1000LL + ((long long)tv.tv_usec / 1000LL);
}

long long timeNowUs(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    return (long long)tv.tv_sec * 1000000LL + (long long)tv.tv_usec;
}

/* =========================================================================
 * Time Diff
 * ========================================================================= */

long timeDiffMs(const struct timeval *start,
                const struct timeval *end)
{
    return (end->tv_sec  - start->tv_sec)  * 1000L +
           (end->tv_usec - start->tv_usec) / 1000L;
}

long timeDiffUs(const struct timeval *start,
                const struct timeval *end)
{
    return (end->tv_sec  - start->tv_sec)  * 1000000L +
           (end->tv_usec - start->tv_usec);
}

/* =========================================================================
 * Time Pack / Unpack
 * ========================================================================= */

uint32_t timePackHMSmsEx(uint8_t hour,
                          uint8_t min,
                          uint8_t sec,
                          uint16_t ms)
{
    return  ((uint32_t)(hour & 0x1F) << 22) |
            ((uint32_t)(min  & 0x3F) << 16) |
            ((uint32_t)(sec  & 0x3F) << 10) |
            ((uint32_t)(ms   & 0x3FF));
}

uint32_t timePackHMSms(void)
{
    struct timeval tv;
    struct tm tm_local;
    uint16_t ms;

    gettimeofday(&tv, NULL);
    localtime_r(&tv.tv_sec, &tm_local);

    ms = (uint16_t)(tv.tv_usec / 1000);

    return timePackHMSmsEx(
        (uint8_t)tm_local.tm_hour,
        (uint8_t)tm_local.tm_min,
        (uint8_t)tm_local.tm_sec,
        ms
    );
}

void timeUnpackHMSms(uint32_t packed,
                     uint8_t *hour,
                     uint8_t *min,
                     uint8_t *sec,
                     uint16_t *ms)
{
    if (hour)
        *hour = (packed >> 22) & 0x1F;
    if (min)
        *min  = (packed >> 16) & 0x3F;
    if (sec)
        *sec  = (packed >> 10) & 0x3F;
    if (ms)
        *ms   =  packed        & 0x3FF;
}

