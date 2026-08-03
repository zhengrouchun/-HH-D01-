#ifndef CLEARCHAIN_TCA9555_H
#define CLEARCHAIN_TCA9555_H

#include <stdint.h>
#include "errcode.h"

#define CLEARCHAIN_TCA9555_PORT0 0
#define CLEARCHAIN_TCA9555_PORT1 1

#define CLEARCHAIN_TCA9555_LEVEL_LOW  0
#define CLEARCHAIN_TCA9555_LEVEL_HIGH 1

errcode_t clearchain_tca9555_init(void);
errcode_t clearchain_tca9555_probe(void);
errcode_t clearchain_tca9555_write_pin(uint8_t port, uint8_t pin, uint8_t level);
errcode_t clearchain_tca9555_read_pin(uint8_t port, uint8_t pin, uint8_t *level);

#endif
