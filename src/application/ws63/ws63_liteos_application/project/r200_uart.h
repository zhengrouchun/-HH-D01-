#ifndef R200_UART_H
#define R200_UART_H

#include <stddef.h>
#include <stdint.h>

#define R200_UART_RING_BUFFER_SIZE 1024U

int r200_uart_init(void);
int r200_uart_write(const uint8_t *data, size_t length);

/*
 * Discard pending bytes before a new command. The RX callback only appends
 * bytes to the fixed ring buffer; complete protocol frames are extracted here
 * in task context.
 */
void r200_uart_prepare_receive(void);
int r200_uart_wait_frame(uint8_t *frame, size_t frame_size,
                         size_t *frame_length, uint32_t timeout_ms);
void r200_uart_flush(void);

#endif
