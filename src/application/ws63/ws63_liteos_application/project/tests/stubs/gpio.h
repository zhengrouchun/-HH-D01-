#ifndef TEST_GPIO_H
#define TEST_GPIO_H
#include "errcode.h"
#define GPIO_LEVEL_HIGH 1
#define GPIO_LEVEL_LOW 0
#define GPIO_DIRECTION_OUTPUT 1
void uapi_gpio_init(void);
errcode_t uapi_gpio_set_val(unsigned int pin,unsigned int value);
errcode_t uapi_gpio_set_dir(unsigned int pin,unsigned int direction);
#endif
