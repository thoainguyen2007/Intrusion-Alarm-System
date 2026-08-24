#ifndef TIME_UTILS_H
#define TIME_UTILS_H

#include <stdbool.h>
#include <stdint.h>

/* Unsigned subtraction remains correct when a 32-bit millisecond tick wraps. */
static inline bool Time_HasElapsed(uint32_t now, uint32_t start, uint32_t duration)
{
    return (uint32_t)(now - start) >= duration;
}

/* Valid for deadlines less than INT32_MAX milliseconds into the future. */
static inline bool Time_DeadlineReached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

#endif
