/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 */
/* *
 * @file pins_arduino.h
 * @brief Pin mapping for Arduino compatibility on ws63 (chip porting layer)
 * @version 3.0
 * @date 2026-04-21
 *
 * This header is chip-specific (ws63) and lives in the chip porting layer
 * (middleware/chips/ws63/arduino/). Arduino.h includes it to obtain the pin
 * count and pin map; another chip supplies its own copy at
 * middleware/chips/<chip>/arduino/pins_arduino.h.
 */

#ifndef PINS_ARDUINO_H
#define PINS_ARDUINO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Total Pin Count (ws63-specific)
 * ============================================================================ */

#define NUM_DIGITAL_PINS 32
#define NUM_ANALOG_INPUTS 8

// Alias for compatibility with some Arduino libraries
#define NUM_ANALOG_PINS NUM_ANALOG_INPUTS

// WS63 exposes 6 ADC channels (ADC0..ADC5); A6/A7 have no hardware channel.
#define NUM_ADC_CHANNELS 6

/* ============================================================================
 * Pin Number Definitions
 * ============================================================================ */

/* Digital Pins (D0-D31) */
#define D0 0
#define D1 1
#define D2 2
#define D3 3
#define D4 4
#define D5 5
#define D6 6
#define D7 7
#define D8 8
#define D9 9
#define D10 10
#define D11 11
#define D12 12
#define D13 13
#define D14 14
#define D15 15
#define D16 16
#define D17 17
#define D18 18
#define D19 19
#define D20 20
#define D21 21
#define D22 22
#define D23 23
#define D24 24
#define D25 25
#define D26 26
#define D27 27
#define D28 28
#define D29 29
#define D30 30
#define D31 31

/* Analog Pins (A0-A7) */
#define A0 24
#define A1 25
#define A2 26
#define A3 27
#define A4 28
#define A5 29
#define A6 30
#define A7 31

/* Special Pins */
#define PIN_SERIAL_TX D0
#define PIN_SERIAL_RX D1
#define PIN_WIRE_SDA A4
#define PIN_WIRE_SCL A5
#define PIN_SPI_SS D10
#define PIN_SPI_MOSI D11
#define PIN_SPI_MISO D12
#define PIN_SPI_SCK D13
#define LED_BUILTIN D13

/* Timer constants - use 0xFF for invalid (not 0 to avoid conflict with timer 0) */
#define NOT_ON_TIMER 0xFF

/* PWM support - ws63 specific pins */
#define HAS_PWM(pin) ((pin) == D3 || (pin) == D5 || (pin) == D6 || (pin) == D9 || (pin) == D10 || (pin) == D11)

/* Analog pin check */
#define IS_ANALOG_PIN(pin) ((pin) >= A0 && (pin) <= A7)

/* Invalid values - NOT_ON_TIMER defined above as 0xFF */
// NOT_A_PIN is unique, no conflict
#define NOT_A_PIN 0xFF

/* ============================================================================
 * Pin Mapping Tables (extern declarations)
 * These arrays are defined in a separate source file (if needed)
 * ============================================================================ */

extern const uint8_t arduino_pin_map[];
extern const uint8_t arduino_pwm_map[];
extern const uint8_t arduino_adc_map[];

/* ============================================================================
 * Pin Mapping Helper Functions
 * ============================================================================ */

static inline uint8_t digitalPinToPin(uint8_t arduino_pin)
{
    if (arduino_pin >= NUM_DIGITAL_PINS) {
        return NOT_A_PIN;
    }
    // For ws63, direct pin mapping (Arduino pin = ws63 pin)
    return arduino_pin;
}

static inline uint8_t digitalPinToPWMChannel(uint8_t arduino_pin)
{
    /* Real Arduino-pin -> PWM channel, from the WS63/HH-D01 pin-mux table:
     *   D3 =GPIO03=PWM3   D5 =GPIO05=PWM5   D6 =GPIO06=PWM6
     *   D9 =GPIO09=PWM1   D10=GPIO10=PWM2   D11=GPIO11=PWM3
     * NOTE: D3 and D11 both route to PWM channel 3 (hardware mux) and are
     * mutually exclusive at runtime — only one may drive that channel. */
    switch (arduino_pin) {
        case D3:  return 3;   /* PWM3 */
        case D5:  return 5;   /* PWM5 */
        case D6:  return 6;   /* PWM6 */
        case D9:  return 1;   /* PWM1 */
        case D10: return 2;   /* PWM2 */
        case D11: return 3;   /* PWM3 (shared with D3) */
        default:  return NOT_ON_TIMER;
    }
}

static inline uint8_t analogPinToChannel(uint8_t arduino_pin)
{
    if (!IS_ANALOG_PIN(arduino_pin)) {
        return NOT_A_PIN;
    }
    uint8_t ch = (uint8_t)(arduino_pin - A0);   /* A0..A7 -> 0..7 */
    if (ch >= NUM_ADC_CHANNELS) {               /* WS63 has only ADC0..ADC5 */
        return NOT_A_PIN;
    }
    return ch;
}

static inline bool pinSupportsPWM(uint8_t arduino_pin)
{
    if (arduino_pin >= NUM_DIGITAL_PINS) {
        return false;
    }
    return HAS_PWM(arduino_pin);
}

#ifdef __cplusplus
}
#endif

#endif /* PINS_ARDUINO_H */
