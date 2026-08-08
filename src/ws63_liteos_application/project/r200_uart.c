#include "r200_uart.h"

#include "errcode.h"
#include "osal_debug.h"
#include "pinctrl.h"
#include "uart.h"

#define R200_UART_BUS       1
#define R200_UART_TX_PIN    S_MGPIO15
#define R200_UART_RX_PIN    S_MGPIO16
#define R200_UART_PIN_MODE  PIN_MODE_1
#define R200_UART_BAUDRATE  115200
#define R200_UART_RX_SIZE   512

static uint8_t g_r200_uart_rx_buffer[R200_UART_RX_SIZE];
static uart_buffer_config_t g_r200_uart_buffer_config = {
    .rx_buffer = g_r200_uart_rx_buffer,
    .rx_buffer_size = sizeof(g_r200_uart_rx_buffer)
};

int r200_uart_init(void)
{
    uart_attr_t attr = {
        .baud_rate = R200_UART_BAUDRATE,
        .data_bits = UART_DATA_BIT_8,
        .stop_bits = UART_STOP_BIT_1,
        .parity = UART_PARITY_NONE
    };
    uart_pin_config_t pin_config = {
        .tx_pin = R200_UART_TX_PIN,
        .rx_pin = R200_UART_RX_PIN,
        .cts_pin = PIN_NONE,
        .rts_pin = PIN_NONE
    };

    uapi_pin_init();

#if defined(CONFIG_PINCTRL_SUPPORT_IE)
    (void)uapi_pin_set_ie(R200_UART_RX_PIN, PIN_IE_ENABLE);
#endif

    (void)uapi_pin_set_mode(R200_UART_TX_PIN, R200_UART_PIN_MODE);
    (void)uapi_pin_set_mode(R200_UART_RX_PIN, R200_UART_PIN_MODE);
    (void)uapi_uart_deinit(R200_UART_BUS);

    if (uapi_uart_init(R200_UART_BUS, &pin_config, &attr, NULL,
                       &g_r200_uart_buffer_config) != ERRCODE_SUCC) {
        osal_printk("R200 UART init failed\r\n");
        return -1;
    }

    r200_uart_flush();
    return 0;
}

int r200_uart_write(const uint8_t *data, size_t length)
{
    if (data == NULL || length == 0) {
        return -1;
    }

    return (uapi_uart_write(R200_UART_BUS, data, (uint32_t)length, 100) ==
            (int32_t)length) ? 0 : -1;
}

int r200_uart_read_byte(uint8_t *value, uint32_t timeout_ms)
{
    if (value == NULL) {
        return -1;
    }

    return (uapi_uart_read(R200_UART_BUS, value, 1, timeout_ms) == 1) ? 0 : -1;
}

void r200_uart_flush(void)
{
    uint8_t value;

    while (uapi_uart_read(R200_UART_BUS, &value, 1, 1) == 1) {
    }
}
