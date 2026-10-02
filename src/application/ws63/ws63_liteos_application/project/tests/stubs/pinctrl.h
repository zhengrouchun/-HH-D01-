#ifndef TEST_PINCTRL_H
#define TEST_PINCTRL_H
#include "errcode.h"
#define PIN_MODE_0 0
#define PIN_MODE_3 3
void uapi_pin_init(void);
errcode_t uapi_pin_set_mode(unsigned int pin,unsigned int mode);
#endif
