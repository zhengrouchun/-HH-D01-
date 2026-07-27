#include "clearchain_feedback.h"

#include "soc_osal.h"

#include "clearchain_buzzer.h"
#include "clearchain_led.h"

void clearchain_feedback_init(void)
{
    clearchain_led_init();
    clearchain_buzzer_init();
    clearchain_buzzer_beep(300);
    clearchain_feedback_standby();
}

void clearchain_feedback_standby(void)
{
    clearchain_led_show_standby();
}

void clearchain_feedback_tag_read(void)
{
    clearchain_led_all_off();
    clearchain_led_blink(CLEARCHAIN_LED_GREEN, 1, 180, 80);
    clearchain_feedback_standby();
}

void clearchain_feedback_post_success(void)
{
    clearchain_led_all_off();
    clearchain_led_blink(CLEARCHAIN_LED_GREEN, 2, 120, 120);
    clearchain_feedback_standby();
}

void clearchain_feedback_post_failed(void)
{
    clearchain_led_all_off();
    clearchain_led_on(CLEARCHAIN_LED_RED);
    osal_msleep(1000);
    clearchain_feedback_standby();
}

void clearchain_feedback_risk_alert(void)
{
    clearchain_led_all_off();
    clearchain_led_on(CLEARCHAIN_LED_RED);
    clearchain_buzzer_beep(500);
    osal_msleep(300);
    clearchain_feedback_standby();
}
