#include "clearchain_buzzer.h"

#include "clearchain_tca9555.h"
#include "soc_osal.h"

#define BUZZER_PIN       3

/* The buzzer module is low-level triggered. */
#define BUZZER_ON        CLEARCHAIN_TCA9555_LEVEL_LOW
#define BUZZER_OFF       CLEARCHAIN_TCA9555_LEVEL_HIGH

static int g_buzzer_ready = 0;

void clearchain_buzzer_init(void)
{
    (void)clearchain_tca9555_init();
    (void)clearchain_tca9555_write_pin(CLEARCHAIN_TCA9555_PORT0, BUZZER_PIN, BUZZER_OFF);

    g_buzzer_ready = 1;
}

void clearchain_buzzer_beep(unsigned int duration_ms)
{
    if (!g_buzzer_ready) {
        clearchain_buzzer_init();
    }

    (void)clearchain_tca9555_write_pin(CLEARCHAIN_TCA9555_PORT0, BUZZER_PIN, BUZZER_ON);
    osal_msleep(duration_ms);
    (void)clearchain_tca9555_write_pin(CLEARCHAIN_TCA9555_PORT0, BUZZER_PIN, BUZZER_OFF);
}
