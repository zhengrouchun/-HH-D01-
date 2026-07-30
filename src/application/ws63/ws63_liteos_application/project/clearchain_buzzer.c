#include "clearchain_buzzer.h"

#include "gpio.h"
#include "pinctrl.h"
#include "soc_osal.h"


#define BUZZER_PIN       S_MGPIO6
#define BUZZER_MODE      PIN_MODE_0


// 你的模块：低电平触发
#define BUZZER_ON        GPIO_LEVEL_LOW
#define BUZZER_OFF       GPIO_LEVEL_HIGH


static int g_buzzer_ready = 0;



void clearchain_buzzer_init(void)
{

    uapi_pin_set_mode(
        BUZZER_PIN,
        BUZZER_MODE
    );


    uapi_gpio_init();


    uapi_gpio_set_dir(
        BUZZER_PIN,
        GPIO_DIRECTION_OUTPUT
    );


    // 上电后立即关闭蜂鸣器
    uapi_gpio_set_val(
        BUZZER_PIN,
        BUZZER_OFF
    );


    g_buzzer_ready = 1;

}



void clearchain_buzzer_beep(unsigned int duration_ms)
{

    if(!g_buzzer_ready)
    {
        clearchain_buzzer_init();
    }


    // 响
    uapi_gpio_set_val(
        BUZZER_PIN,
        BUZZER_ON
    );


    osal_msleep(duration_ms);


    // 停
    uapi_gpio_set_val(
        BUZZER_PIN,
        BUZZER_OFF
    );

}