#include "r200_uart.h"

#include "errcode.h"
#include "osal_debug.h"
#include "pinctrl.h"
#include "soc_osal.h"
#include "uart.h"

#define R200_UART_BUS          1
#define R200_UART_TX_PIN       S_MGPIO15
#define R200_UART_RX_PIN       S_MGPIO16
#define R200_UART_PIN_MODE     PIN_MODE_1
#define R200_UART_BAUDRATE     115200
#define R200_UART_RX_BLOCK_SIZE 512U
#define R200_UART_RX_EVENT     (1U << 0)

#define R200_FRAME_HEADER      0xAAU
#define R200_FRAME_END         0xDDU
#define R200_FRAME_BASE_SIZE   7U

static uint8_t g_r200_uart_rx_block[R200_UART_RX_BLOCK_SIZE];
static uint8_t g_r200_uart_ring[R200_UART_RING_BUFFER_SIZE];
static volatile uint16_t g_r200_uart_ring_head = 0U;
static volatile uint16_t g_r200_uart_ring_tail = 0U;
static volatile uint32_t g_r200_uart_dropped_bytes = 0U;
static osal_event g_r200_uart_rx_event = { 0 };
static int g_r200_uart_rx_event_ready = 0;
static uart_buffer_config_t g_r200_uart_buffer_config = {
    .rx_buffer = g_r200_uart_rx_block,
    .rx_buffer_size = sizeof(g_r200_uart_rx_block)
};

static uint16_t r200_uart_ring_next(uint16_t index)
{
    index++;
    return (index >= R200_UART_RING_BUFFER_SIZE) ? 0U : index;
}

static size_t r200_uart_ring_count_locked(void)
{
    uint16_t head = g_r200_uart_ring_head;
    uint16_t tail = g_r200_uart_ring_tail;

    if (head >= tail) {
        return (size_t)(head - tail);
    }
    return (size_t)(R200_UART_RING_BUFFER_SIZE - tail + head);
}

static uint8_t r200_uart_ring_peek_locked(size_t offset)
{
    size_t index = (size_t)g_r200_uart_ring_tail + offset;
    if (index >= R200_UART_RING_BUFFER_SIZE) {
        index %= R200_UART_RING_BUFFER_SIZE;
    }
    return g_r200_uart_ring[index];
}

static void r200_uart_ring_drop_locked(size_t length)
{
    while (length > 0U && g_r200_uart_ring_tail != g_r200_uart_ring_head) {
        g_r200_uart_ring_tail = r200_uart_ring_next(g_r200_uart_ring_tail);
        length--;
    }
}

/* UART RX interrupt context: append bytes and wake the reader task only. */
static void r200_uart_rx_isr(const void *buffer, uint16_t length, bool error)
{
    const uint8_t *bytes = (const uint8_t *)buffer;
    uint16_t written = 0U;

    if (bytes == NULL || length == 0U || error) {
        return;
    }

    for (uint16_t i = 0U; i < length; i++) {
        uint16_t next = r200_uart_ring_next(g_r200_uart_ring_head);
        if (next == g_r200_uart_ring_tail) {
            g_r200_uart_dropped_bytes += (uint32_t)(length - i);
            break;
        }
        g_r200_uart_ring[g_r200_uart_ring_head] = bytes[i];
        g_r200_uart_ring_head = next;
        written++;
    }

    if (written > 0U && g_r200_uart_rx_event_ready) {
        (void)osal_event_write(&g_r200_uart_rx_event, R200_UART_RX_EVENT);
    }
}

static int r200_uart_register_rx_callback(void)
{
    return (uapi_uart_register_rx_callback(R200_UART_BUS,
        UART_RX_CONDITION_FULL_OR_IDLE, R200_UART_RX_BLOCK_SIZE,
        r200_uart_rx_isr) == ERRCODE_SUCC) ? 0 : -1;
}

static uint8_t r200_uart_frame_checksum(const uint8_t *frame, size_t payload_length)
{
    uint16_t sum = 0U;
    size_t checksum_length = 4U + payload_length;

    for (size_t i = 0U; i < checksum_length; i++) {
        sum = (uint16_t)(sum + frame[1U + i]);
    }
    return (uint8_t)sum;
}

/* Returns 1 for a frame, 0 when more bytes are needed. */
static int r200_uart_extract_frame(uint8_t *frame, size_t frame_size, size_t *frame_length)
{
    for (;;) {
        size_t available;
        size_t payload_length;
        size_t total_length;
        unsigned int irq_status = osal_irq_lock();

        available = r200_uart_ring_count_locked();
        while (available > 0U && r200_uart_ring_peek_locked(0U) != R200_FRAME_HEADER) {
            r200_uart_ring_drop_locked(1U);
            available--;
        }

        if (available < 5U) {
            osal_irq_restore(irq_status);
            return 0;
        }

        payload_length = ((size_t)r200_uart_ring_peek_locked(3U) << 8) |
                         (size_t)r200_uart_ring_peek_locked(4U);
        total_length = payload_length + R200_FRAME_BASE_SIZE;
        if (total_length > frame_size || total_length >= R200_UART_RING_BUFFER_SIZE) {
            r200_uart_ring_drop_locked(1U);
            osal_irq_restore(irq_status);
            continue;
        }

        if (available < total_length) {
            osal_irq_restore(irq_status);
            return 0;
        }

        for (size_t i = 0U; i < total_length; i++) {
            frame[i] = r200_uart_ring_peek_locked(i);
        }
        osal_irq_restore(irq_status);

        if (r200_uart_frame_checksum(frame, payload_length) == frame[total_length - 2U] &&
            frame[total_length - 1U] == R200_FRAME_END) {
            irq_status = osal_irq_lock();
            r200_uart_ring_drop_locked(total_length);
            osal_irq_restore(irq_status);
            *frame_length = total_length;
            return 1;
        }

        irq_status = osal_irq_lock();
        r200_uart_ring_drop_locked(1U);
        osal_irq_restore(irq_status);
    }
}

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

    if (!g_r200_uart_rx_event_ready) {
        if (osal_event_init(&g_r200_uart_rx_event) != OSAL_SUCCESS) {
            osal_printk("R200 UART RX event init failed\r\n");
            return -1;
        }
        g_r200_uart_rx_event_ready = 1;
    }

    if (r200_uart_register_rx_callback() != 0) {
        osal_printk("R200 UART RX interrupt register failed\r\n");
        return -1;
    }

    r200_uart_flush();
    osal_printk("R200 UART RX ring buffer ready\r\n");
    return 0;
}

int r200_uart_write(const uint8_t *data, size_t length)
{
    if (data == NULL || length == 0U) {
        return -1;
    }

    return (uapi_uart_write(R200_UART_BUS, data, (uint32_t)length, 100U) ==
            (int32_t)length) ? 0 : -1;
}

void r200_uart_prepare_receive(void)
{
    unsigned int irq_status;

    uapi_uart_unregister_rx_callback(R200_UART_BUS);
    irq_status = osal_irq_lock();
    g_r200_uart_ring_head = 0U;
    g_r200_uart_ring_tail = 0U;
    g_r200_uart_dropped_bytes = 0U;
    osal_irq_restore(irq_status);
    if (g_r200_uart_rx_event_ready) {
        (void)osal_event_clear(&g_r200_uart_rx_event, R200_UART_RX_EVENT);
    }
    if (r200_uart_register_rx_callback() != 0) {
        osal_printk("R200 UART RX interrupt re-register failed\r\n");
    }
}

int r200_uart_wait_frame(uint8_t *frame, size_t frame_size,
                         size_t *frame_length, uint32_t timeout_ms)
{
    if (frame == NULL || frame_length == NULL || frame_size < R200_FRAME_BASE_SIZE ||
        !g_r200_uart_rx_event_ready) {
        return -1;
    }

    for (;;) {
        if (r200_uart_extract_frame(frame, frame_size, frame_length) == 1) {
            return 0;
        }

        if (osal_event_read(&g_r200_uart_rx_event, R200_UART_RX_EVENT, timeout_ms,
                            OSAL_WAITMODE_AND | OSAL_WAITMODE_CLR) == OSAL_FAILURE) {
            return -1;
        }
    }
}

void r200_uart_flush(void)
{
    r200_uart_prepare_receive();
}
