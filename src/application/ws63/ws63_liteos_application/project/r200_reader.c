#include "r200_reader.h"

#include <stdint.h>
#include <string.h>

#include "osal_debug.h"
#include "r200_protocol.h"
#include "r200_uart.h"
#include "soc_osal.h"
#include "systick.h"

#define R200_MAX_READ_MS             800U
#define R200_SCAN_WINDOW_MS          2500U
#define R200_SCAN_GAP_MS             300U
#define R200_DEBUG_EVERY_N           30U
#define R200_PARSE_ATTEMPTS          4
#define R200_LOG_EVERY_N             50U
#define R200_LOG_EMPTY_SCAN          0
#define R200_READER_VERSION          "r200-reader-multi-rssi-batch-20260930"

static void r200_print_hex(const char *prefix, const uint8_t *data, size_t length)
{
    osal_printk("%s", prefix);
    for (size_t i = 0; i < length; i++) {
        osal_printk("%02X", data[i]);
        if (i + 1U < length) {
            osal_printk(" ");
        }
    }
    osal_printk("\r\n");
}

static int r200_reader_read_frame(uint8_t *frame, size_t frame_size,
                                  size_t *frame_length)
{
    static uint32_t no_response_logs = 0;

    if (frame == NULL || frame_length == NULL || frame_size < 7U) {
        return -1;
    }

    if (r200_uart_wait_frame(frame, frame_size, frame_length,
                             R200_MAX_READ_MS) == 0) {
        return 0;
    }

    no_response_logs++;
    if (R200_LOG_EMPTY_SCAN && (no_response_logs % R200_LOG_EVERY_N) == 1U) {
        osal_printk("R200 RX interrupt wait timeout\r\n");
    }
    return -1;
}

static int r200_reader_store_sample(r200_batch_t *batch, const char *epc, int8_t rssi_dbm)
{
    for (size_t i = 0; i < batch->tag_count; i++) {
        if (strcmp(batch->tags[i].chip_uid, epc) == 0) {
            r200_tag_samples_t *tag = &batch->tags[i];
            if (tag->sample_count >= R200_MAX_SAMPLES_PER_TAG) {
                batch->sample_limit_drops++;
                return 0;
            }
            tag->rssi_dbm[tag->sample_count++] = rssi_dbm;
            batch->total_samples++;
            return 1;
        }
    }
    if (batch->tag_count >= R200_MAX_TAGS) {
        batch->capacity_drops++;
        return 0;
    }

    r200_tag_samples_t *tag = &batch->tags[batch->tag_count++];
    (void)strncpy(tag->chip_uid, epc, sizeof(tag->chip_uid) - 1U);
    tag->chip_uid[sizeof(tag->chip_uid) - 1U] = '\0';
    tag->rssi_dbm[0] = rssi_dbm;
    tag->sample_count = 1U;
    batch->total_samples++;
    return 2;
}

int r200_reader_init(void)
{
    int ret = r200_uart_init();

    if (ret == 0) {
        osal_printk("R200 uart ready: uart1 115200 8N1\r\n");
        osal_printk("R200 reader version: %s\r\n", R200_READER_VERSION);
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
    static uint32_t no_response_count = 0;
    int8_t rssi;
    int debug_this_time;
    uint32_t elapsed = 0;

    if (epc == NULL || epc_size < R200_TAG_ID_MAX_LEN) {
        return -1;
    }
    if (r200_protocol_build_inventory(command, sizeof(command),
                                      &command_length) != 0) {
        return -1;
    }

    while (elapsed < R200_SCAN_WINDOW_MS) {
        debug_count++;
        debug_this_time = (debug_count == 1U ||
                           (debug_count % R200_DEBUG_EVERY_N) == 0U);
        if (debug_this_time) {
            r200_print_hex("R200 TX: ", command, command_length);
        }

        r200_uart_prepare_receive();
        if (r200_uart_write(command, command_length) != 0) {
            osal_printk("R200 write failed\r\n");
            return -1;
        }

        for (int attempt = 0; attempt < R200_PARSE_ATTEMPTS; attempt++) {
            int parse_ret;

            if (r200_reader_read_frame(response, sizeof(response),
                                       &response_length) != 0) {
                break;
            }
            if (debug_this_time) {
                r200_print_hex("R200 RX: ", response, response_length);
            }

            parse_ret = r200_protocol_parse_inventory(response, response_length,
                                                       epc, epc_size, &rssi);
            if (parse_ret == 0) {
                osal_printk("R200 EPC: %s\r\n", epc);
                no_response_count = 0;
                return 0;
            }
            if (parse_ret == R200_PARSE_NO_TAG) {
                break;
            }
            if (parse_ret != R200_PARSE_COMMAND_ERROR) {
                osal_printk("R200 parse failed, wait next frame\r\n");
            }
        }

        osal_msleep(R200_SCAN_GAP_MS);
        elapsed += R200_MAX_READ_MS + R200_SCAN_GAP_MS;
    }

    no_response_count++;
    if (R200_LOG_EMPTY_SCAN && (no_response_count % 5U) == 1U) {
        osal_printk("R200 scan window timeout\r\n");
    }
    return -1;
}

void r200_reader_batch_reset(r200_batch_t *batch)
{
    if (batch != NULL) {
        (void)memset(batch, 0, sizeof(*batch));
    }
}

int r200_reader_read_batch(r200_batch_t *batch, uint32_t timeout_ms)
{
    return r200_reader_read_batch_progress(batch, timeout_ms, NULL);
}

int r200_reader_read_batch_progress(r200_batch_t *batch, uint32_t timeout_ms,
                                    r200_batch_progress_fn progress)
{
    uint8_t command[R200_MAX_FRAME_SIZE], response[R200_MAX_FRAME_SIZE];
    char epc[R200_TAG_ID_MAX_LEN];
    size_t command_length, response_length;
    uint32_t dropped_before, bad_before, errors_before;
    uint32_t dropped_after, bad_after, errors_after;
    uint64_t start_ms, command_ms = 0U, last_progress_ms = 0U;
    uint32_t reply_window = CLEARCHAIN_R200_FRAME_TIMEOUT_MS;
    int status = 0;
    int need_command = 1;
    int native_started = 0;

    if (batch == NULL || timeout_ms == 0U) { return -1; }
    r200_reader_batch_reset(batch);
    r200_uart_get_diagnostics(&dropped_before, &bad_before, &errors_before);
#if CLEARCHAIN_R200_NATIVE_MULTI
    if (r200_protocol_build_multi_inventory(command, sizeof(command), &command_length, 65535U) != 0) {
        return -1;
    }
#else
    if (r200_protocol_build_inventory(command, sizeof(command), &command_length) != 0) { return -1; }
#endif
    start_ms = uapi_systick_get_ms();
    if (progress != NULL) { progress(batch, 0U, timeout_ms); }
    osal_printk("[BATCH] start window_ms=%u native_multi=%u\r\n", timeout_ms, CLEARCHAIN_R200_NATIVE_MULTI);
    while (uapi_systick_get_ms() - start_ms < timeout_ms) {
        if (need_command) {
            r200_uart_prepare_receive();
            if (r200_uart_write(command, command_length) != 0) { status = -1; break; }
            native_started = CLEARCHAIN_R200_NATIVE_MULTI;
            command_ms = uapi_systick_get_ms();
            reply_window = CLEARCHAIN_R200_FRAME_TIMEOUT_MS;
            need_command = 0;
        }
        uint64_t now = uapi_systick_get_ms();
        if (now - start_ms >= timeout_ms) { break; }
        uint32_t remaining = timeout_ms - (uint32_t)(now - start_ms);
        uint32_t slice = remaining < 50U ? remaining : 50U;
        if (r200_uart_wait_frame(response, sizeof(response), &response_length, slice) == 0) {
            int8_t rssi_dbm;
            int parsed = r200_protocol_parse_inventory(response, response_length, epc, sizeof(epc), &rssi_dbm);
            if (parsed == 0) {
                (void)r200_reader_store_sample(batch, epc, rssi_dbm);
                command_ms = uapi_systick_get_ms();
                reply_window = CLEARCHAIN_R200_DRAIN_TIMEOUT_MS;
            } else if (parsed == R200_PARSE_COMMAND_ERROR) {
                batch->rejected_frames++;
                status = -1;
                break;
            } else if (parsed != R200_PARSE_NO_TAG) {
                batch->rejected_frames++;
            } else if (!CLEARCHAIN_R200_NATIVE_MULTI) {
                need_command = 1;
            }
        } else if (!CLEARCHAIN_R200_NATIVE_MULTI && uapi_systick_get_ms() - command_ms >= reply_window) {
            need_command = 1;
        }
        now = uapi_systick_get_ms();
        if (progress != NULL && now - start_ms - last_progress_ms >= CLEARCHAIN_PROGRESS_INTERVAL_MS) {
            uint32_t elapsed = (uint32_t)(now - start_ms);
            progress(batch, elapsed < timeout_ms ? elapsed : timeout_ms, timeout_ms);
            last_progress_ms = elapsed;
        }
        if (need_command && now - start_ms < timeout_ms) {
            remaining = timeout_ms - (uint32_t)(now - start_ms);
            osal_msleep(remaining < CLEARCHAIN_R200_SCAN_GAP_MS ? remaining : CLEARCHAIN_R200_SCAN_GAP_MS);
        }
    }
    if (native_started) {
        if (r200_protocol_build_stop_inventory(command, sizeof(command), &command_length) != 0 ||
            r200_uart_write(command, command_length) != 0) {
            status = -1;
            osal_printk("[BATCH] native stop failed; check module before next scan\r\n");
        }
    }
    r200_uart_get_diagnostics(&dropped_after, &bad_after, &errors_after);
    batch->uart_dropped_bytes = dropped_after - dropped_before;
    batch->uart_error_events = errors_after - errors_before;
    batch->rejected_frames += bad_after - bad_before;
    if (batch->capacity_drops || batch->uart_dropped_bytes || batch->uart_error_events || batch->rejected_frames) { status = -1; }
    if (progress != NULL) {
        uint32_t elapsed = (uint32_t)(uapi_systick_get_ms() - start_ms);
        progress(batch, elapsed < timeout_ms ? elapsed : timeout_ms, timeout_ms);
    }
    osal_printk("[BATCH] done tags=%u samples=%u elapsed=%ums status=%d bad=%u capacity=%u dropped=%u errors=%u\r\n",
                (unsigned int)batch->tag_count, (unsigned int)batch->total_samples,
                (uint32_t)(uapi_systick_get_ms() - start_ms), status, batch->rejected_frames,
                batch->capacity_drops, batch->uart_dropped_bytes, batch->uart_error_events);
    return status;
}
