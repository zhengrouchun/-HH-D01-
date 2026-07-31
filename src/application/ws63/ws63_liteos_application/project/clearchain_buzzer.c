#include "clearchain_buzzer.h"

#include "gpio.h"
#include "pinctrl.h"
#include "soc_osal.h"

#define BUZZER_PIN       S_MGPIO6
#define BUZZER_MODE      PIN_MODE_0

/* The buzzer module is low-level triggered. */
#define BUZZER_ON        GPIO_LEVEL_LOW
#define BUZZER_OFF       GPIO_LEVEL_HIGH

static int g_buzzer_ready = 0;

void clearchain_buzzer_init(void)
{
    uapi_pin_init();
    uapi_gpio_init();

    (void)uapi_pin_set_mode(BUZZER_PIN, BUZZER_MODE);
    (void)uapi_pin_set_pull(BUZZER_PIN, PIN_PULL_TYPE_UP);

    /*
     * Load the output latch with OFF before enabling output. This reduces the
     * chance of a short low pulse when the pin switches from reset state to GPIO.
     */
    (void)uapi_gpio_set_val(BUZZER_PIN, BUZZER_OFF);
    (void)uapi_gpio_set_dir(BUZZER_PIN, GPIO_DIRECTION_OUTPUT);
    (void)uapi_gpio_set_val(BUZZER_PIN, BUZZER_OFF);

    g_buzzer_ready = 1;
}

void clearchain_buzzer_beep(unsigned int duration_ms)
{
    if (!g_buzzer_ready) {
        clearchain_buzzer_init();
    }

    (void)uapi_gpio_set_val(BUZZER_PIN, BUZZER_ON);
    osal_msleep(duration_ms);
    (void)uapi_gpio_set_val(BUZZER_PIN, BUZZER_OFF);
}
