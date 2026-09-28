#ifndef CLEARCHAIN_SOFT_I2C_H
#define CLEARCHAIN_SOFT_I2C_H

#include <stdint.h>

#include "errcode.h"

#define CLEARCHAIN_SOFT_I2C_ACK  1U
#define CLEARCHAIN_SOFT_I2C_NACK 0U

errcode_t clearchain_soft_i2c_init(void);
int clearchain_soft_i2c_lock(void);
void clearchain_soft_i2c_unlock(void);

void clearchain_soft_i2c_start(void);
void clearchain_soft_i2c_stop(void);
uint8_t clearchain_soft_i2c_write_byte(uint8_t value);
uint8_t clearchain_soft_i2c_read_byte(uint8_t acknowledge);

#endif
