/* Standalone bench firmware: the task actively polls after the scheduler starts. */
#include "app_init.h"
#include "r200_protocol.h"
#include "r200_uart.h"
#include "soc_osal.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define R200_TEST_EPC_SIZE 65U
#define R200_TEST_MAX_TAGS 32U
#define R200_TEST_POLLS_PER_WINDOW 5U
#define R200_TEST_MAX_REPLIES_PER_POLL 16U

static char g_epcs[R200_TEST_MAX_TAGS][R200_TEST_EPC_SIZE];
static unsigned int g_unique;
static unsigned int g_samples;

static void print_frame(const char *kind, const uint8_t *frame, size_t length)
{
    char hex[R200_MAX_FRAME_SIZE * 3U + 1U];
    size_t used = 0U;
    for (size_t i = 0U; i < length && i < R200_MAX_FRAME_SIZE; ++i) {
        int n = snprintf(hex + used, sizeof(hex) - used, "%02X ", frame[i]);
        if (n != 3) { break; }
        used += 3U;
    }
    hex[used] = '\0';
    osal_printk("[R200 TEST] %s bytes=%u %s\r\n", kind, (unsigned int)length, hex);
}

static void record_epc(const char *epc)
{
    for (unsigned int i = 0U; i < g_unique; ++i) {
        if (strcmp(g_epcs[i], epc) == 0) { return; }
    }
    if (g_unique < R200_TEST_MAX_TAGS) {
        (void)strcpy(g_epcs[g_unique++], epc);
    } else {
        osal_printk("[R200 TEST] unique table full (32); reduce tags for bench test\r\n");
    }
}

static void *r200_test_task(void *arg)
{
    uint8_t command[R200_MAX_FRAME_SIZE], response[R200_MAX_FRAME_SIZE];
    char epc[R200_TEST_EPC_SIZE];
    size_t command_length, response_length;
    unsigned int poll = 0U, window = 1U, replies = 0U, no_tag = 0U, timeouts = 0U;
    unsigned int parse_errors = 0U;
    (void)arg;
    osal_printk("[R200 TEST] r200-standalone-20261006-v1; no backend/TCA/SLE required\r\n");
    if (r200_uart_init() != 0 ||
        r200_protocol_build_inventory(command, sizeof(command), &command_length) != 0) {
        osal_printk("[R200 TEST] UART/command initialization failed; task stops\r\n");
        return NULL;
    }
    osal_printk("[R200 TEST] UART1 TX=GPIO15 RX=GPIO16 115200 8N1; command=0x22\r\n");
    osal_printk("[R200 TEST] attach antenna, then present one tag; summary every 5 polls\r\n");
    osal_msleep(1500);
    while (1) {
        ++poll;
        osal_printk("[R200 TEST] window=%u poll=%u\r\n", window, poll);
        print_frame("TX", command, command_length);
        r200_uart_prepare_receive();
        if (r200_uart_write(command, command_length) != 0) {
            osal_printk("[R200 TEST] UART write failed\r\n");
        } else {
            unsigned int received = 0U;
            for (unsigned int i = 0U; i < R200_TEST_MAX_REPLIES_PER_POLL; ++i) {
                int8_t rssi = 0;
                uint32_t timeout = i == 0U ? 800U : 50U;
                if (r200_uart_wait_frame(response, sizeof(response), &response_length, timeout) != 0) {
                    break;
                }
                ++received;
                ++replies;
                print_frame("RX", response, response_length);
                int status = r200_protocol_parse_inventory(response, response_length,
                                                           epc, sizeof(epc), &rssi);
                if (status == 0) {
                    record_epc(epc);
                    ++g_samples;
                    osal_printk("[R200 TEST] EPC=%s RSSI=%d dBm unique=%u samples=%u\r\n",
                                epc, (int)rssi, g_unique, g_samples);
                } else if (status == R200_PARSE_NO_TAG) {
                    ++no_tag;
                    osal_printk("[R200 TEST] reader replied NO_TAG (UART reply received)\r\n");
                } else {
                    ++parse_errors;
                    osal_printk("[R200 TEST] reply rejected status=%d; retain RX hex for diagnosis\r\n",
                                status);
                }
            }
            if (received == 0U) {
                ++timeouts;
                osal_printk("[R200 TEST] RX timeout: no valid protocol frame; check power/TX/RX/baud\r\n");
            }
        }
        if (poll % R200_TEST_POLLS_PER_WINDOW == 0U) {
            uint32_t dropped, bad_frames, uart_errors;
            r200_uart_get_diagnostics(&dropped, &bad_frames, &uart_errors);
            osal_printk("[R200 TEST] SUMMARY window=%u unique=%u samples=%u replies=%u no_tag=%u timeouts=%u parse_errors=%u dropped=%u bad_frames=%u uart_errors=%u\r\n",
                        window, g_unique, g_samples, replies, no_tag, timeouts,
                        parse_errors, dropped, bad_frames, uart_errors);
            ++window;
            g_unique = 0U;
            g_samples = 0U;
            replies = no_tag = timeouts = parse_errors = 0U;
        }
        osal_msleep(600);
    }
    return NULL;
}

static void r200_test_entry(void)
{
    osal_task *task = osal_kthread_create((osal_kthread_handler)r200_test_task,
                                         NULL, "CCR200Test", 0x2000);
    if (task == NULL) {
        osal_printk("[R200 TEST] task creation failed\r\n");
        return;
    }
    /* Leave the default priority; do not schedule or sleep in app init. */
    osal_kfree(task);
}

app_run(r200_test_entry);
