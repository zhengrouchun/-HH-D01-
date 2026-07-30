#include "clearchain_led.h"

#include "gpio.h"
#include "pinctrl.h"
#include "soc_osal.h"

#define LED_RED_PIN       S_MGPIO3
#define LED_GREEN_PIN     S_MGPIO2
#define LED_YELLOW_PIN    S_MGPIO0
#define LED_GPIO_MODE     PIN_MODE_0

#define LED_ACTIVE_LEVEL  GPIO_LEVEL_HIGH
#define LED_IDLE_LEVEL    GPIO_LEVEL_LOW

static pin_t clearchain_led_pin(clearchain_led_t led)
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

static void clearchain_led_prepare_pin(pin_t pin)
{
    uapi_pin_set_mode(pin, LED_GPIO_MODE);
    uapi_gpio_set_dir(pin, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_val(pin, LED_IDLE_LEVEL);
}

void clearchain_led_init(void)
{
    uapi_pin_init();
    uapi_gpio_init();

    clearchain_led_prepare_pin(LED_RED_PIN);
    clearchain_led_prepare_pin(LED_GREEN_PIN);
    clearchain_led_prepare_pin(LED_YELLOW_PIN);
}

void clearchain_led_all_off(void)
{
    uapi_gpio_set_val(LED_RED_PIN, LED_IDLE_LEVEL);
    uapi_gpio_set_val(LED_GREEN_PIN, LED_IDLE_LEVEL);
    uapi_gpio_set_val(LED_YELLOW_PIN, LED_IDLE_LEVEL);
}

void clearchain_led_on(clearchain_led_t led)
{
    uapi_gpio_set_val(clearchain_led_pin(led), LED_ACTIVE_LEVEL);
}

void clearchain_led_off(clearchain_led_t led)
{
    uapi_gpio_set_val(clearchain_led_pin(led), LED_IDLE_LEVEL);
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
