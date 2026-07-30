/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 */
/* *
 * @file wiring.cpp
 * @brief Time functions implementation for Arduino compatibility layer
 * @version 3.0
 * @date 2026-04-21
 */

#include "Arduino.h"
#include "los_tick.h"
#include "los_task.h"
#include "hal_timer.h"
#include "los_config.h"
#include "systick.h"
#include "chip_io.h"
#include "driver/security_unified/trng.h"
#include <stdlib.h>

#define BITS_PER_BYTE 8

/* *
 * @brief Get milliseconds since system startup
 * @return Unsigned long - milliseconds
 */
unsigned long millis()
{
    return (unsigned long)uapi_systick_get_ms();
}

/* *
 * @brief Get microseconds since system startup
 * @return Unsigned long - microseconds
 */
unsigned long micros()
{
    return (unsigned long)uapi_systick_get_us();
}

/* *
 * @brief Delay for specified milliseconds
 * @param ms - Milliseconds to delay
 */
void delay(unsigned long ms)
{
    if (ms == 0) {
        return;
    }

    uapi_systick_delay_ms(ms);
}

/* *
 * @brief Delay for specified microseconds
 * @param us - Microseconds to delay
 */
void delayMicroseconds(unsigned int us)
{
    if (us == 0) {
        return;
    }

    uapi_systick_delay_us(us);
}

/* *
 * @brief Yield function for cooperative multitasking
 */
void yield(void)
{
    // In LiteOS, this can trigger a task switch if needed
    // For now, it's a no-op as LiteOS handles scheduling automatically
}

/* *
 * @brief Map a value from one range to another
 * @param value - Value to map
 * @param from_low/from_high - Source range
 * @param to_low/to_high - Target range
 * @return Mapped value (integer arithmetic, truncated)
 */
long map(long value, long from_low, long from_high, long to_low, long to_high)
{
    if (from_high == from_low) {
        return to_low;  // Guard against divide-by-zero
    }
    return (value - from_low) * (to_high - to_low) / (from_high - from_low) + to_low;
}

/* *
 * @brief Seed the pseudo-random number generator
 * @param seed - Seed value (0 is ignored per Arduino spec)
 */
void randomSeed(unsigned long seed)
{
    if (seed != 0) {
        srand((unsigned int)seed);
    }
}

/* *
 * @brief Generate a random number using hardware TRNG
 * @param max - Exclusive upper bound
 * @return Random value in [0, max)
 */
long random(long max)
{
    if (max <= 0) {
        return 0;
    }
    uint32_t rnd = 0;
    if (uapi_drv_cipher_trng_get_random(&rnd) == ERRCODE_SUCC) {
        return (long)(rnd % (unsigned int)max);
    }
    // Fallback to rand() if TRNG fails
    return (long)(rand() % (unsigned int)max);
}

/* *
 * @brief Generate a random number in a range using hardware TRNG
 * @param min - Inclusive lower bound
 * @param max - Exclusive upper bound
 * @return Random value in [min, max)
 */
long random(long min, long max)
{
    if (min >= max) {
        return min;
    }
    unsigned long range = (unsigned long)(max - min);
    uint32_t rnd = 0;
    if (uapi_drv_cipher_trng_get_random(&rnd) == ERRCODE_SUCC) {
        return (long)(rnd % range) + min;
    }
    // Fallback to rand() if TRNG fails
    return (long)(rand() % range) + min;
}

/* *
 * @brief Construct a 16-bit word from a single value
 */
unsigned int makeWord(unsigned int w)
{
    return w;
}

/* *
 * @brief Construct a 16-bit word from high and low bytes
 */
unsigned int makeWord(unsigned char h, unsigned char l)
{
    return ((unsigned int)h << BITS_PER_BYTE) | l;
}
