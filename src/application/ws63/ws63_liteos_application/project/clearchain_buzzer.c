#include "clearchain_buzzer.h"

#include "pinctrl.h"
#include "pwm.h"
#include "soc_osal.h"

#define BUZZER_PWM_PIN        S_MGPIO6
#define BUZZER_PWM_PIN_MODE   PIN_MODE_1
#define BUZZER_PWM_CHANNEL    6
#define BUZZER_PWM_GROUP_ID   6

#define BUZZER_PWM_LOW_CYC    8000
#define BUZZER_PWM_HIGH_CYC   8000

static int g_buzzer_ready = 0;

void clearchain_buzzer_init(void)
{
    uapi_pin_set_mode(BUZZER_PWM_PIN, BUZZER_PWM_PIN_MODE);
    uapi_pwm_init();
    g_buzzer_ready = 1;
}

void clearchain_buzzer_beep(unsigned int duration_ms)
{
    pwm_config_t cfg = {
        BUZZER_PWM_LOW_CYC,
        BUZZER_PWM_HIGH_CYC,
        0,
        0x7FFF,
        true
    };
    uint8_t channel_id = BUZZER_PWM_CHANNEL;

    if (!g_buzzer_ready) {
        clearchain_buzzer_init();
    }

    if (uapi_pwm_open(BUZZER_PWM_CHANNEL, &cfg) != ERRCODE_SUCC) {
        return;
    }

#ifdef CONFIG_PWM_USING_V151
    uapi_pwm_set_group(BUZZER_PWM_GROUP_ID, &channel_id, 1);
    uapi_pwm_start_group(BUZZER_PWM_GROUP_ID);
    osal_msleep(duration_ms);
    uapi_pwm_stop_group(BUZZER_PWM_GROUP_ID);
    uapi_pwm_clear_group(BUZZER_PWM_GROUP_ID);
#else
    uapi_pwm_start(BUZZER_PWM_CHANNEL);
    osal_msleep(duration_ms);
#endif

    uapi_pwm_close(BUZZER_PWM_CHANNEL);
}
