#ifndef R200_UART_H
#define R200_UART_H

#include <stddef.h>
#include <stdint.h>

int r200_uart_init(void);
int r200_uart_write(const uint8_t *data, size_t length);
int r200_uart_read_byte(uint8_t *value, uint32_t timeout_ms);
void r200_uart_flush(void);

#endif
