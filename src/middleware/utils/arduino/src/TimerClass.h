/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 */
/* *
 * @file TimerClass.h
 * @brief Hardware Timer class for Arduino compatibility layer
 * @version 1.0
 * @date 2026-04-22
 *
 * Hardware timer implementation using SDK Timer API
 * Supports multiple timer instances with interrupt callbacks
 */

#ifndef TIMERCLASS_H
#define TIMERCLASS_H

#include <stdint.h>

// Timer instance IDs — chip-specific. TIMER_INSTANCE_* map Arduino timer
// numbers to the chip's timer_index_t (defined in timer_porting.h). The count
// varies per chip (e.g. 3 timers on one chip, 4 on another), so the macro set
// + which TimerN objects exist are decided in the chip porting layer's
#include "timer_porting.h"

#include "arduino_config.h"

/* *
 * @class TimerClass
 * @brief Hardware timer class for periodic interrupts
 *
 * Provides Arduino TimerOne-style interface for hardware timers
 * Uses the chip SDK uapi_timer_* APIs
 */
class TimerClass {
private:
    void *m_timer_handle;      // /< Timer handle from SDK
    uint8_t m_instance;        // /< Timer instance number
    uint32_t m_period_us;      // /< Current period in microseconds
    bool m_running;            // /< Timer running state
    void (*m_callback)();      // /< User callback function
    uintptr_t m_callback_data; // /< Data passed to callback

    /* *
     * @brief Internal callback wrapper
     * @param data User data passed to callback
     */
    static void callbackWrapper(uintptr_t data);

public:
    /* *
     * @brief Construct a TimerClass object
     * @param instance Timer instance number (0, 1, 2)
     */
    TimerClass(uint8_t instance = TIMER_INSTANCE_0) noexcept;

    /* *
     * @brief Destroy the TimerClass object
     */
    ~TimerClass();

    /* *
     * @brief Initialize timer with specified period
     * @param microseconds Timer period in microseconds
     * @return true if initialization successful
     */
    bool initialize(uint32_t microseconds);

    /* *
     * @brief Set timer period
     * @param microseconds New period in microseconds
     * @return true if period updated successfully
     */
    bool setPeriod(uint32_t microseconds);

    /* *
     * @brief Get current period
     * @return Current period in microseconds
     */
    uint32_t getPeriod() const
    {
        return m_period_us;
    }

    /* *
     * @brief Start the timer
     * @return true if started successfully
     */
    bool start();

    /* *
     * @brief Stop the timer
     * @return true if stopped successfully
     */
    bool stop();

    /* *
     * @brief Check if timer is running
     * @return true if timer is running
     */
    bool isRunning() const
    {
        return m_running;
    }

    /* *
     * @brief Attach interrupt callback
     * @param callback Function to call on timer interrupt
     * @return true if callback attached successfully
     */
    bool attachInterrupt(void (*callback)());

    /* *
     * @brief Detach interrupt callback
     * @return true if callback detached successfully
     */
    bool detachInterrupt();

    /* *
     * @brief Check if callback is attached
     * @return true if callback is attached
     */
    bool hasCallback() const
    {
        return m_callback != nullptr;
    }

    /* *
     * @brief Get timer instance number
     * @return Instance number
     */
    uint8_t getInstance() const
    {
        return m_instance;
    }

    /* *
     * @brief Get maximum supported period
     * @return Maximum period in microseconds
     */
    static uint32_t getMaxPeriod();
};

// Pre-defined timer instances
#if CONFIG_TIMER_MAX_NUM > 0
extern TimerClass Timer0; // /< Timer instance 0
#endif

#if CONFIG_TIMER_MAX_NUM > 1
extern TimerClass Timer1; // /< Timer instance 1
#endif

#if CONFIG_TIMER_MAX_NUM > 2
extern TimerClass Timer2; // /< Timer instance 2
#endif

// C-style API declarations are in Arduino.h (timerInit, timerStart, timerStop)

#endif // TIMERCLASS_H
