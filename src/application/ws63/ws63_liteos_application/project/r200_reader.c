#include "r200_reader.h"

#include <stdint.h>

#include "osal_debug.h"
#include "r200_protocol.h"
#include "r200_uart.h"
#include "soc_osal.h"

#define R200_FRAME_TIMEOUT_MS 60
#define R200_MAX_READ_MS      350
#define R200_SCAN_WINDOW_MS   1800
#define R200_SCAN_GAP_MS      120
#define R200_DEBUG_EVERY_N    5
#define R200_PARSE_ATTEMPTS   8

static void r200_print_hex(const char *prefix, const uint8_t *data, size_t length)
{
    osal_printk("%s", prefix);
    for (size_t i = 0; i < length; i++) {
        osal_printk("%02X", data[i]);
        if (i + 1 < length) {
            osal_printk(" ");
        }
    }
    osal_printk("\r\n");
}

static int r200_reader_read_frame(uint8_t *frame, size_t frame_size,
                                  size_t *frame_length)
{
    uint8_t byte;
    uint16_t payload_length = 0;
    size_t length = 0;
    uint32_t elapsed = 0;

    if (frame == NULL || frame_length == NULL || frame_size < 7) {
        return -1;
    }

    while (elapsed < R200_MAX_READ_MS) {
        if (r200_uart_read_byte(&byte, R200_FRAME_TIMEOUT_MS) != 0) {
            elapsed += R200_FRAME_TIMEOUT_MS;
            continue;
        }

        elapsed = 0;

        if (length == 0 && byte != 0xAA) {
            osal_printk("R200 skip byte: %02X\r\n", byte);
            continue;
        }

        if (length >= frame_size) {
            osal_printk("R200 frame too long\r\n");
            return -1;
        }

        frame[length++] = byte;

        if (length == 5) {
            payload_length = (uint16_t)(((uint16_t)frame[3] << 8) | frame[4]);
            if ((size_t)payload_length + 7 > frame_size) {
                osal_printk("R200 bad payload len: %u\r\n", payload_length);
                length = 0;
                payload_length = 0;
                continue;
            }
        }

        if (length >= 7 && length == (size_t)payload_length + 7) {
            if (frame[length - 1] == 0xDD) {
                *frame_length = length;
                return 0;
            }

            r200_print_hex("R200 malformed frame: ", frame, length);

            if (frame[length - 1] == 0xAA) {
                frame[0] = 0xAA;
                length = 1;
            } else {
                length = 0;
            }
            payload_length = 0;
        }
    }

    if (length > 0) {
        r200_print_hex("R200 partial frame: ", frame, length);
    } else {
        osal_printk("R200 no response\r\n");
    }

    return -1;
}

int r200_reader_init(void)
{
    int ret = r200_uart_init();

    if (ret == 0) {
        osal_printk("R200 uart ready: uart1 115200 8N1\r\n");
    }

    return ret;
}

int r200_reader_read_epc(char *epc, size_t epc_size)
{
    uint8_t command[R200_MAX_FRAME_SIZE];
    uint8_t response[R200_MAX_FRAME_SIZE];
    size_t command_length;
    size_t response_length;
    static uint32_t debug_count = 0;
    int debug_this_time;
    uint32_t elapsed = 0;

    if (epc == NULL || epc_size < R200_TAG_ID_MAX_LEN) {
        return -1;
    }

    if (r200_protocol_build_inventory(command, sizeof(command),
                                      &command_length) != 0) {
        return -1;
    }

    r200_uart_flush();

    while (elapsed < R200_SCAN_WINDOW_MS) {
        debug_count++;
        debug_this_time = (debug_count == 1 || (debug_count % R200_DEBUG_EVERY_N) == 0);
        if (debug_this_time) {
            r200_print_hex("R200 TX: ", command, command_length);
        }

        if (r200_uart_write(command, command_length) != 0) {
            osal_printk("R200 write failed\r\n");
            return -1;
        }

        for (int attempt = 0; attempt < R200_PARSE_ATTEMPTS; attempt++) {
            if (r200_reader_read_frame(response, sizeof(response),
                                       &response_length) != 0) {
                break;
            }

            if (debug_this_time) {
                r200_print_hex("R200 RX: ", response, response_length);
            }

            if (r200_protocol_parse_inventory(response, response_length,
                                              epc, epc_size) == 0) {
                osal_printk("R200 EPC: %s\r\n", epc);
                return 0;
            }

            osal_printk("R200 parse failed, wait next frame\r\n");
        }

        osal_msleep(R200_SCAN_GAP_MS);
        elapsed += R200_MAX_READ_MS + R200_SCAN_GAP_MS;
    }

    osal_printk("R200 scan window timeout\r\n");

    return -1;
}
