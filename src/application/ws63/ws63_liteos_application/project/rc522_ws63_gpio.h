#ifndef RC522_WS63_GPIO_H
#define RC522_WS63_GPIO_H

#include <stdint.h>

void rc522_gpio_init(void);
void rc522_gpio_reset(void);
uint8_t rc522_spi_read_reg(uint8_t address);
void rc522_spi_write_reg(uint8_t address, uint8_t value);

#endif
