/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 */
/*
  Servo.cpp - Servo library for Arduino compatibility layer
  HiSilicon Adaptation: Uses PWM hardware to generate 50Hz servo signals

  Pulse range: 544-2400μs (standard hobby servo)
  Supports up to 4 independent servos
  APIs: uapi_pwm_init(), uapi_pwm_open(), uapi_pwm_start(), uapi_pwm_update_duty_ratio()
*/

#include "Servo.h"
#include "driver/pwm.h"

// Static channel allocation bitmap
static uint8_t g_channel_allocated = 0;

// PWM module initialization flag
static bool g_pwm_initialized = false;

// Constructor
Servo::Servo()
    : m_pin(0),
      m_channel(INVALID_CHANNEL),
      m_attached(false),
      m_angle(SERVO_MIN_ANGLE),  // Unattached read() returns 0, not 90
      m_pulseWidth(MIN_PULSE_WIDTH),
      m_minPulse(MIN_PULSE_WIDTH),
      m_maxPulse(MAX_PULSE_WIDTH),
      m_minAngle(SERVO_MIN_ANGLE),
      m_maxAngle(SERVO_MAX_ANGLE)
{
}

// Destructor - release PWM channel
Servo::~Servo()
{
    detach();
}

// Attach servo to a pin
bool Servo::attach(uint8_t pin)
{
    // Validate pin range
    if (pin >= NUM_DIGITAL_PINS) {
        return false;
    }

    if (m_attached) {
        // Already attached, return true
        return true;
    }

    // Find an available channel (0-3 for 4 servos)
    for (uint8_t ch = 0; ch < MAX_SERVOS; ch++) {
        if (!(g_channel_allocated & (1 << ch))) {
            // Channel is available
            m_channel = ch;
            m_pin = pin;

            // Mark channel as allocated
            g_channel_allocated |= (1 << ch);

            // Initialize PWM module (only needs to be called once)
            if (!g_pwm_initialized) {
                errcode_t init_ret = uapi_pwm_init();
                if (init_ret == ERRCODE_SUCC) {
                    g_pwm_initialized = true;
                }
                // If init failed, g_pwm_initialized stays false and will retry on next attach
            }

            // Configure PWM channel
            pwm_config_t cfg;
            cfg.repeat = true;   // Continuous output
            cfg.cycles = 0;      // No cycle limit (continuous)
            cfg.offset_time = 0; // No phase offset

            // Initial duty ratio at center position
            uint32_t pwm_freq = uapi_pwm_get_frequency(m_channel);
            if (pwm_freq == 0) {
                pwm_freq = DEFAULT_PWM_FREQ_HZ;
            }
            cfg.high_time = (uint32_t)(SERVO_CENTER_US * pwm_freq / MICROSECONDS_PER_SECOND);
            cfg.low_time = (uint32_t)((SERVO_PERIOD_US - SERVO_CENTER_US) * pwm_freq / MICROSECONDS_PER_SECOND);

            errcode_t ret = uapi_pwm_open(m_channel, &cfg);
            if (ret == ERRCODE_SUCC) {
#if defined(CONFIG_PWM_USING_V151)
                // On PWM V151, uapi_pwm_start() requires the channel
                // to be registered in a group. Register channel into group 0.
                uint8_t grp_ch = m_channel;
                ret = uapi_pwm_set_group(0, &grp_ch, 1);
                if (ret == ERRCODE_SUCC) {
                    ret = uapi_pwm_start(m_channel);
                }
#else
                ret = uapi_pwm_start(m_channel);
#endif
            }
            if (ret != ERRCODE_SUCC) {
                // Either open failed (already closed) or start failed (now open), close it
                uapi_pwm_close(m_channel);
#if defined(CONFIG_PWM_USING_V151)
                (void)uapi_pwm_clear_group(0);
#endif
                g_channel_allocated &= ~(1 << m_channel);
                m_channel = INVALID_CHANNEL;
                m_attached = false;
                m_pin = 0;
                m_angle = SERVO_CENTER_ANGLE;
                m_pulseWidth = MIN_PULSE_WIDTH;
                return false;
            }

            m_attached = true;

            // Set initial position (center)
            writeMicroseconds(SERVO_CENTER_US);
            return true;
        }
    }

    // No available channels
    return false;
}

// Attach servo to a pin with custom min/max pulse width
bool Servo::attach(uint8_t pin, int min, int max)
{
    // Accept custom pulse widths (Arduino allows values beyond default MIN/MAX_PULSE_WIDTH)
    if (min >= max || min < 0) {
        return false;
    }

    m_minPulse = min;
    m_maxPulse = max;
    return attach(pin);
}

// Attach servo to a pin with custom pulse and angle range
bool Servo::attach(uint8_t pin, int min, int max, int minAngle, int maxAngle)
{
    // Accept custom pulse widths (Arduino allows values beyond default MIN/MAX_PULSE_WIDTH)
    if (min >= max || min < 0) {
        return false;
    }

    // Validate angle range
    if (minAngle < SERVO_MIN_ANGLE || maxAngle > SERVO_MAX_ANGLE || minAngle >= maxAngle) {
        return false;
    }

    m_minPulse = min;
    m_maxPulse = max;
    m_minAngle = minAngle;
    m_maxAngle = maxAngle;
    return attach(pin);
}

// Detach servo from pin
void Servo::detach()
{
    if (!m_attached) {
        return;
    }

    // FIX: Ensure channel is valid before closing
    if (m_channel != INVALID_CHANNEL && m_channel < MAX_SERVOS) {
        // Close PWM channel (this should also stop it)
        uapi_pwm_close(m_channel);

#if defined(CONFIG_PWM_USING_V151)
        // Clear the group entry we added in attach() (PWM V151 requires
        // channels to be registered in a group before start()).
        (void)uapi_pwm_clear_group(0);
#endif

        // Mark channel as free
        g_channel_allocated &= ~(1 << m_channel);
    }

    m_channel = INVALID_CHANNEL;
    m_attached = false;
    m_pin = 0;
    m_angle = SERVO_CENTER_ANGLE;
    m_pulseWidth = MIN_PULSE_WIDTH;
}

// Deinitialize PWM module and release all resources
void Servo::end()
{
    uapi_pwm_deinit();
    g_pwm_initialized = false;
    g_channel_allocated = 0;
}

// Write angle (SERVO_MIN_ANGLE-SERVO_MAX_ANGLE degrees)
// Parameter is int to avoid uint8_t wrap-around of negative values.
void Servo::write(int angle)
{
    if (!m_attached) {
        return;
    }

    // Constrain angle to valid range
    if (angle > m_maxAngle) {
        angle = m_maxAngle;
    }
    if (angle < m_minAngle) {
        angle = m_minAngle;
    }

    // Store the angle (for read() to return)
    m_angle = angle;

    // Convert angle to pulse width
    uint16_t pulse = angleToPulse(angle);
    writeMicroseconds(pulse);
}

// Write pulse width directly (microseconds)
void Servo::writeMicroseconds(uint16_t pulse)
{
    if (!m_attached) {
        return;
    }

    // Constrain pulse width to user-defined range (not hardcoded limits)
    if (pulse < (uint16_t)m_minPulse) {
        pulse = (uint16_t)m_minPulse;
    } else if (pulse > (uint16_t)m_maxPulse) {
        pulse = (uint16_t)m_maxPulse;
    }

    m_pulseWidth = pulse;
    // Update angle based on pulse width for read() to return
    m_angle = pulseToAngle(pulse);
    updateDutyRatio(pulse);
}

// Read current angle
uint8_t Servo::read()
{
    // Return stored angle, not converted from pulse width
    // This ensures read() returns the last written angle
    return m_angle;
}

// Check if servo is attached
bool Servo::attached()
{
    return m_attached;
}

// Calculate duty ratio from pulse width
void Servo::updateDutyRatio(uint16_t pulseWidth)
{
    // Get PWM frequency
    uint32_t pwm_freq = uapi_pwm_get_frequency(m_channel);
    if (pwm_freq == 0) {
        pwm_freq = DEFAULT_PWM_FREQ_HZ; // Default 1MHz (matches wiring_analog)
    }

    // For servo (50Hz), we need to calculate the correct high/low times
    // PWM clock cycles: high_time = pulse_width_us * pwm_freq / MICROSECONDS_PER_SECOND
    uint32_t high_time = ((uint32_t)pulseWidth * pwm_freq) / MICROSECONDS_PER_SECOND;
    // Use uint64_t to avoid overflow: at 1MHz clock, (20000-544)*1000000 exceeds uint32_t.
    uint64_t low_time = ((uint64_t)(SERVO_PERIOD_US - pulseWidth) * pwm_freq) / MICROSECONDS_PER_SECOND;

    // Use uapi_pwm_update_cfg() to update duty cycle in-place (V151 API).
    // uapi_pwm_update_cfg() atomically updates the running PWM config
    // without stopping/closing the channel, avoiding hardware state conflicts.
    pwm_config_t cfg;
    cfg.high_time = high_time;
    cfg.low_time = (uint32_t)low_time;
    cfg.offset_time = 0;
    cfg.cycles = 0;
    cfg.repeat = true;

    errcode_t ret = uapi_pwm_update_cfg(m_channel, &cfg);
    if (ret != ERRCODE_SUCC) {
        // update_cfg failed, try close+reopen on same channel as retry
        uapi_pwm_close(m_channel);
        ret = uapi_pwm_open(m_channel, &cfg);
        if (ret == ERRCODE_SUCC) {
#if defined(CONFIG_PWM_USING_V151)
            // BUG-033 fix: use group API for V151
            (void)uapi_pwm_clear_group(0);
            uint8_t grp_ch = m_channel;
            (void)uapi_pwm_set_group(0, &grp_ch, 1);
            ret = uapi_pwm_start_group(0);
#else
            ret = uapi_pwm_start(m_channel);
#endif
        }
        if (ret != ERRCODE_SUCC) {
            // Either open failed (already closed) or start failed (now open), close it
            uapi_pwm_close(m_channel);
            g_channel_allocated &= ~(1 << m_channel);
            m_channel = INVALID_CHANNEL;
            m_attached = false;
            m_pin = 0;
            m_angle = SERVO_CENTER_ANGLE;
            m_pulseWidth = MIN_PULSE_WIDTH;
            return; // Failed to update duty ratio, give up and detach servo
        }
        m_attached = true;
    }
}

// Convert angle to pulse width
uint16_t Servo::angleToPulse(uint8_t angle)
{
    // Guard against zero angle range (would cause division by zero in denominator)
    if (m_maxAngle == m_minAngle) {
        return (uint16_t)m_minPulse; // Degenerate case: return min pulse
    }

    // Map user-defined angle range (m_minAngle-m_maxAngle) to user-defined pulse range (m_minPulse-m_maxPulse)
    return (uint16_t)m_minPulse +
           ((uint32_t)(angle - m_minAngle) * (m_maxPulse - m_minPulse) / (m_maxAngle - m_minAngle));
}

// Convert pulse width to angle
uint8_t Servo::pulseToAngle(uint16_t pulse)
{
    // Constrain to user-defined pulse range first
    if (pulse < (uint16_t)m_minPulse) {
        pulse = (uint16_t)m_minPulse;
    } else if (pulse > (uint16_t)m_maxPulse) {
        pulse = (uint16_t)m_maxPulse;
    }

    // Guard against zero pulse range (would cause division by zero in denominator)
    if (m_maxPulse == m_minPulse) {
        return (uint8_t)m_minAngle; // Degenerate case: return min angle
    }

    // Map user-defined pulse range to user-defined angle range
    return (uint8_t)((uint32_t)(pulse - m_minPulse) * (m_maxAngle - m_minAngle) / (m_maxPulse - m_minPulse) +
                     m_minAngle);
}
