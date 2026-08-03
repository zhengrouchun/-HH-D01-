#include "clearchain_led.h"

#include "clearchain_tca9555.h"
#include "soc_osal.h"

#define LED_RED_PIN       0
#define LED_GREEN_PIN     1
#define LED_YELLOW_PIN    2

#define LED_ACTIVE_LEVEL  CLEARCHAIN_TCA9555_LEVEL_LOW
#define LED_IDLE_LEVEL    CLEARCHAIN_TCA9555_LEVEL_HIGH

static uint8_t clearchain_led_pin(clearchain_led_t led)
{
    switch (led) {
        case CLEARCHAIN_LED_RED:
            return LED_RED_PIN;
        case CLEARCHAIN_LED_GREEN:
            return LED_GREEN_PIN;
        case CLEARCHAIN_LED_YELLOW:
        default:
            return LED_YELLOW_PIN;
    }
}

void clearchain_led_init(void)
{
    (void)clearchain_tca9555_init();
    clearchain_led_all_off();
}

void clearchain_led_all_off(void)
{
    (void)clearchain_tca9555_write_pin(CLEARCHAIN_TCA9555_PORT0, LED_RED_PIN, LED_IDLE_LEVEL);
    (void)clearchain_tca9555_write_pin(CLEARCHAIN_TCA9555_PORT0, LED_GREEN_PIN, LED_IDLE_LEVEL);
    (void)clearchain_tca9555_write_pin(CLEARCHAIN_TCA9555_PORT0, LED_YELLOW_PIN, LED_IDLE_LEVEL);
}

void clearchain_led_on(clearchain_led_t led)
{
    (void)clearchain_tca9555_write_pin(CLEARCHAIN_TCA9555_PORT0, clearchain_led_pin(led), LED_ACTIVE_LEVEL);
}

void clearchain_led_off(clearchain_led_t led)
{
    (void)clearchain_tca9555_write_pin(CLEARCHAIN_TCA9555_PORT0, clearchain_led_pin(led), LED_IDLE_LEVEL);
}

void clearchain_led_show_standby(void)
{
    clearchain_led_all_off();
    clearchain_led_on(CLEARCHAIN_LED_YELLOW);
}

void clearchain_led_blink(clearchain_led_t led, unsigned int times,
                          unsigned int on_ms, unsigned int off_ms)
{
    for (unsigned int i = 0; i < times; i++) {
        clearchain_led_on(led);
        osal_msleep(on_ms);
        clearchain_led_off(led);
        osal_msleep(off_ms);
    }
}
