#include "clearchain_feedback.h"

#include "soc_osal.h"

#include "clearchain_buzzer.h"
#include "clearchain_led.h"

static int g_feedback_ready = 0;

void clearchain_feedback_init(void)
{
    if (g_feedback_ready) {
        return;
    }

    clearchain_led_init();
    clearchain_buzzer_init();
    clearchain_led_blink(CLEARCHAIN_LED_RED, 1, 120, 80);
    clearchain_led_blink(CLEARCHAIN_LED_GREEN, 1, 120, 80);
    clearchain_led_blink(CLEARCHAIN_LED_YELLOW, 1, 120, 80);
    clearchain_feedback_standby();

    g_feedback_ready = 1;
}

void clearchain_feedback_standby(void)
{
    clearchain_led_show_standby();
}

void clearchain_feedback_tag_read(void)
{
    osal_printk("Feedback: tag read\r\n");
    clearchain_led_all_off();
    clearchain_feedback_standby();
}

void clearchain_feedback_post_success(void)
{
    osal_printk("Feedback: approved GREEN\r\n");
    clearchain_led_all_off();
    clearchain_led_on(CLEARCHAIN_LED_GREEN);
    osal_msleep(2000);
    clearchain_feedback_standby();
}

void clearchain_feedback_post_failed(void)
{
    osal_printk("Feedback: post failed RED\r\n");
    clearchain_led_all_off();
    clearchain_led_on(CLEARCHAIN_LED_RED);
    clearchain_buzzer_beep(300);
    osal_msleep(1700);
    clearchain_feedback_standby();
}

void clearchain_feedback_verify(void)
{
    osal_printk("Feedback: verify YELLOW\r\n");
    clearchain_led_all_off();
    clearchain_led_on(CLEARCHAIN_LED_YELLOW);
    clearchain_buzzer_beep(250);
    osal_msleep(1750);
    clearchain_feedback_standby();
}

void clearchain_feedback_risk_alert(void)
{
    osal_printk("Feedback: inspection RED\r\n");
    clearchain_led_all_off();
    clearchain_led_on(CLEARCHAIN_LED_RED);
    clearchain_buzzer_beep(800);
    osal_msleep(3000);
    clearchain_feedback_standby();
}
