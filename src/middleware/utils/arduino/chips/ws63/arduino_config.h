/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 */
/* *
 * @file arduino_config.h
 * @brief ws63 chip configuration for the Arduino layer (chip porting layer).
 *
 * Decides which Arduino features the ws63 build enables. Arduino.h includes this
 * (via middleware/chips/${CHIP}/arduino/ on the include path) instead of deriving
 * the config inline, so the chip-agnostic Arduino.h stays portable. Another chip
 * supplies its own copy at middleware/chips/<chip>/arduino/arduino_config.h.
 */

#ifndef ARDUINO_CONFIG_H
#define ARDUINO_CONFIG_H

/* ============================================================================
 * Hardware serial ports available (Arduino standard feature macros).
 * ws63 exposes UART0/1/2. (HardwareSerial.cpp gates Serial1/2 on the SDK's
 * UART_BUS_MAX_NUMBER in platform_core.h; these macros are informational.)
 * ============================================================================ */
#define HAVE_HWSERIAL0
#define HAVE_HWSERIAL1
#define HAVE_HWSERIAL2

/* ============================================================================
 * ADC / PWM / GPIO support — derived from ws63 menuconfig (CONFIG_*_USING_V*).
 * These gate the hardware code paths in wiring_analog.cpp / wiring_pulse.cpp /
 * Servo.cpp. Off => the API degrades to a stub / GPIO fallback.
 * ============================================================================ */
#if !defined(CONFIG_PWM_SUPPORT) && defined(CONFIG_PWM_USING_V151)
#define CONFIG_PWM_SUPPORT 1
#endif

/* ============================================================================
 * PWM period register width (wiring_analog.cpp / wiring_pulse.cpp). ws63 uses
 * the V151 PWM IP: the period register (high_time + low_time) is 16-bit.
 * Caps analogWrite()/tone() carrier cycle counts. Another chip sets its own.
 * ============================================================================ */
#if !defined(ARDUINO_PWM_PERIOD_MAX)
#define ARDUINO_PWM_PERIOD_MAX 65535U
#endif

#if !defined(CONFIG_ADC_SUPPORT) && defined(CONFIG_ADC_USING_V154)
#define CONFIG_ADC_SUPPORT 1
#endif

/* ============================================================================
 * ADC hardware resolution (wiring_analog.cpp). ws63 ADC is 12-bit (0-4095).
 * Drives the analogRead() range scaling. Another chip sets its own value.
 * ============================================================================ */
#if !defined(ARDUINO_ADC_HW_BITS)
#define ARDUINO_ADC_HW_BITS 12
#endif

#if !defined(CONFIG_GPIO_SUPPORT)
#define CONFIG_GPIO_SUPPORT 1
#endif

/* ============================================================================
 * Interrupt-capable pins. ws63: pins 0-15 are interrupt-capable; others return
 * NOT_AN_INTERRUPT. Used by wiring_interrupts.cpp and digitalPinToInterrupt().
 * ============================================================================ */
#define MAX_INTERRUPT_PINS 16

/* ============================================================================
 * UART (HardwareSerial) TX/RX support. Always on for ws63.
 * ============================================================================ */
#if !defined(CONFIG_UART_SUPPORT_TX)
#define CONFIG_UART_SUPPORT_TX 1
#endif
#if !defined(CONFIG_UART_SUPPORT_RX)
#define CONFIG_UART_SUPPORT_RX 1
#endif

/* ============================================================================
 * I2C master support (Wire). Default on for ws63.
 * ============================================================================ */
#if !defined(CONFIG_I2C_SUPPORT_MASTER)
#define CONFIG_I2C_SUPPORT_MASTER 1
#endif

/* ============================================================================
 * I2S/SIO support (I2SClass). CONFIG_I2S_SUPPORT is not a real SDK Kconfig;
 * derive it from the ws63 SIO driver version so the I2S implementation compiles
 * instead of an empty stub.
 * ============================================================================ */
#if !defined(CONFIG_I2S_SUPPORT) && defined(CONFIG_SIO_USING_V151)
#define CONFIG_I2S_SUPPORT 1
#endif

/* ============================================================================
 * I2S/SIO pin assignment (I2SClass.h). ws63 SIO0 default pinmux:
 *   SCK=GPIO00  WS=GPIO01  SD(out)=GPIO02  SD_IN(in)=GPIO03
 * ============================================================================ */
#if !defined(ARDUINO_I2S_SCK_PIN)
#define ARDUINO_I2S_SCK_PIN 0
#endif
#if !defined(ARDUINO_I2S_WS_PIN)
#define ARDUINO_I2S_WS_PIN 1
#endif
#if !defined(ARDUINO_I2S_SD_PIN)
#define ARDUINO_I2S_SD_PIN 2
#endif
#if !defined(ARDUINO_I2S_SD_IN_PIN)
#define ARDUINO_I2S_SD_IN_PIN 3
#endif

/* ============================================================================
 * SPI master pin assignment (SPI.cpp). ws63 default pinmux on SPI0:
 *   DI(MISO)=GPIO11  DO(MOSI)=GPIO09  CLK=GPIO07  CS=GPIO10  pinmux mode=3
 * (Physical pin numbers; see SPI.cpp / ws63 IO复用关系表.)
 *
 * NOTE: CONFIG_SPI_SUPPORT_MASTER is intentionally NOT defaulted here. Unlike
 * PWM/ADC (driver-on => support-on), SPI master is a user-selectable menuconfig
 * option; ws63 ships with it "not set" (SPI.cpp compiles to its stub path).
 * Each chip's menuconfig decides. See porting_contract.md.
 * ============================================================================ */
#if !defined(CONFIG_SPI_DI_MASTER_PIN)
#define CONFIG_SPI_DI_MASTER_PIN 11
#endif
#if !defined(CONFIG_SPI_DO_MASTER_PIN)
#define CONFIG_SPI_DO_MASTER_PIN 9
#endif
#if !defined(CONFIG_SPI_CLK_MASTER_PIN)
#define CONFIG_SPI_CLK_MASTER_PIN 7
#endif
#if !defined(CONFIG_SPI_CS_MASTER_PIN)
#define CONFIG_SPI_CS_MASTER_PIN 10
#endif
#if !defined(CONFIG_SPI_MASTER_PIN_MODE)
#define CONFIG_SPI_MASTER_PIN_MODE 3
#endif

/* ============================================================================
 * SPI master bus clock (SPI.cpp). ws63 SSI source clock is 32MHz
 * (spi_porting.h: SPI_CLK_FREQ = 32000000). SDK uses bus_clk as the baud-rate
 * divider base: SCK = bus_clk / clk_div. Another chip sets its own value.
 * ============================================================================ */
#if !defined(ARDUINO_SPI_BUS_CLK_HZ)
#define ARDUINO_SPI_BUS_CLK_HZ 32000000UL
#endif
#if !defined(ARDUINO_SPI_MAX_CLOCK_HZ)
#define ARDUINO_SPI_MAX_CLOCK_HZ 32000000UL
#endif

/* ============================================================================
 * Timer (TimerClass). IRQ priority + which timer index Arduino reserves.
 * ARDUINO_TIMER_RESERVED expands to TIMER_INDEX_0 (enum from timer_porting.h,
 * resolved at point of use in TimerClass.cpp after timer_porting.h is included).
 * ============================================================================ */
#define ARDUINO_TIMER_IRQ_PRIORITY 1
#if !defined(ARDUINO_TIMER_RESERVED)
#define ARDUINO_TIMER_RESERVED TIMER_INDEX_0
#endif

// Timer instance mapping: Arduino timer number -> chip timer_index_t enum
// (defined in timer_porting.h). ws63 has TIMER_INDEX_0/1/2 (3 timers). Each
// TIMER_INSTANCE_N is consumed by the gated TimerN object in TimerClass.cpp.
// A chip with a different timer count defines its own set here.
#define TIMER_INSTANCE_0 ((uint8_t)TIMER_INDEX_0)
#define TIMER_INSTANCE_1 ((uint8_t)TIMER_INDEX_1)
#define TIMER_INSTANCE_2 ((uint8_t)TIMER_INDEX_2)

// Default timer for the C-style API (timerInit/timerStart/timerStop). Picks a
// TimerN object the system/RTOS does NOT use, so Arduino timers never clash
// with the system tick. ws63: Timer0 is the RTOS systick, Timer1 is the main.c
// test timebase, so Timer2 is free. Macro expansion is deferred to the point of
// use (inside TimerClass.cpp, after the TimerN globals are defined).
#define ARDUINO_DEFAULT_TIMER (&Timer2)

// USB macros (not applicable on this target, but kept for compatibility)
#define SERIAL_PORT_MONITOR Serial
#define SERIAL_PORT_HARDWARE Serial
#define SERIAL_PORT_HARDWARE1 Serial1
#define SERIAL_PORT_HARDWARE2 Serial2

#endif /* ARDUINO_CONFIG_H */
