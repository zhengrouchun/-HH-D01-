#include "r200_reader.h"

#include <stdint.h>
#include <string.h>

#include "osal_debug.h"
#include "r200_protocol.h"
#include "r200_uart.h"
#include "soc_osal.h"

#define R200_MAX_READ_MS             800U
#define R200_SCAN_WINDOW_MS          2500U
#define R200_BATCH_MIN_WINDOW_MS     2500U
#define R200_BATCH_MAX_WINDOW_MS     30000U
#define R200_SCAN_GAP_MS             300U
#define R200_DEBUG_EVERY_N           30U
#define R200_PARSE_ATTEMPTS          4
#define R200_LOG_EVERY_N             50U
#define R200_LOG_EMPTY_SCAN          0
#define R200_READER_VERSION          "r200-reader-rssi-20260926"

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

static size_t r200_reader_find_tag(const r200_rssi_batch_t *batch, const char *epc)
{
    for (size_t i = 0; i < batch->tag_count; i++) {
        if (strcmp(batch->tags[i].epc, epc) == 0) {
            return i;
        }
    }
    return R200_MAX_TAGS;
}

static int8_t r200_reader_median(const int8_t *samples, size_t sample_count)
{
    int8_t sorted[R200_MAX_RSSI_SAMPLES_PER_POSITION];

    for (size_t i = 0; i < sample_count; i++) {
        size_t insert_at = i;
        while (insert_at > 0U && sorted[insert_at - 1U] > samples[i]) {
            sorted[insert_at] = sorted[insert_at - 1U];
            insert_at--;
        }
        sorted[insert_at] = samples[i];
    }

    if ((sample_count & 1U) != 0U) {
        return sorted[sample_count / 2U];
    }

    return (int8_t)(((int16_t)sorted[(sample_count / 2U) - 1U] +
                     (int16_t)sorted[sample_count / 2U]) / 2);
}

static int r200_reader_store_sample(r200_rssi_batch_t *batch,
                                    r200_observation_position_t position,
                                    size_t samples_per_tag,
                                    const char *epc, int8_t rssi)
{
    size_t tag_index = r200_reader_find_tag(batch, epc);
    r200_rssi_observation_t *observation;

    if (tag_index == R200_MAX_TAGS) {
        if (batch->tag_count >= R200_MAX_TAGS) {
            batch->tag_overflow_count++;
            return -1;
        }
        tag_index = batch->tag_count;
        batch->tag_count++;
        (void)memset(&batch->tags[tag_index], 0, sizeof(batch->tags[tag_index]));
        (void)strncpy(batch->tags[tag_index].epc, epc, R200_TAG_ID_MAX_LEN - 1U);
        batch->tags[tag_index].epc[R200_TAG_ID_MAX_LEN - 1U] = '\0';
    }

    observation = &batch->tags[tag_index].observations[position];
    if (observation->sample_count >= samples_per_tag ||
        observation->sample_count >= R200_MAX_RSSI_SAMPLES_PER_POSITION) {
        batch->sample_overflow_count++;
        return 0;
    }

    observation->samples[observation->sample_count] = rssi;
    observation->sample_count++;
    observation->median = r200_reader_median(observation->samples,
                                              observation->sample_count);
    observation->median_valid = 1U;
    return 1;
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

void r200_reader_batch_reset(r200_rssi_batch_t *batch)
{
    if (batch != NULL) {
        (void)memset(batch, 0, sizeof(*batch));
    }
}

int r200_reader_collect(r200_rssi_batch_t *batch,
                        r200_observation_position_t position,
                        size_t samples_per_tag)
{
    uint8_t command[R200_MAX_FRAME_SIZE];
    uint8_t response[R200_MAX_FRAME_SIZE];
    char epc[R200_TAG_ID_MAX_LEN];
    size_t command_length;
    size_t response_length;
    uint32_t elapsed = 0;
    uint32_t scan_window_ms;
    size_t stored_samples = 0;

    if (batch == NULL || position >= R200_OBSERVATION_POSITION_COUNT ||
        samples_per_tag == 0U ||
        samples_per_tag > R200_MAX_RSSI_SAMPLES_PER_POSITION) {
        return -1;
    }
    if (batch->tag_count > R200_MAX_TAGS) {
        return -1;
    }
    if (r200_protocol_build_inventory(command, sizeof(command),
                                      &command_length) != 0) {
        return -1;
    }

    for (size_t i = 0; i < batch->tag_count; i++) {
        (void)memset(&batch->tags[i].observations[position], 0,
                     sizeof(batch->tags[i].observations[position]));
    }

    if (samples_per_tag > (R200_BATCH_MAX_WINDOW_MS /
                           (R200_MAX_READ_MS + R200_SCAN_GAP_MS))) {
        scan_window_ms = R200_BATCH_MAX_WINDOW_MS;
    } else {
        scan_window_ms = (uint32_t)samples_per_tag *
                         (R200_MAX_READ_MS + R200_SCAN_GAP_MS);
        if (scan_window_ms < R200_BATCH_MIN_WINDOW_MS) {
            scan_window_ms = R200_BATCH_MIN_WINDOW_MS;
        }
    }

    while (elapsed < scan_window_ms) {
        r200_uart_prepare_receive();
        if (r200_uart_write(command, command_length) != 0) {
            return -1;
        }

        while (r200_reader_read_frame(response, sizeof(response),
                                      &response_length) == 0) {
            int8_t rssi;
            int parse_ret = r200_protocol_parse_inventory(response, response_length,
                                                           epc, sizeof(epc), &rssi);

            if (parse_ret == 0) {
                int store_ret = r200_reader_store_sample(batch, position,
                                                         samples_per_tag,
                                                         epc, rssi);
                if (store_ret > 0) {
                    stored_samples++;
                }
                continue;
            }
            if (parse_ret == R200_PARSE_NO_TAG) {
                break;
            }
        }

        osal_msleep(R200_SCAN_GAP_MS);
        elapsed += R200_MAX_READ_MS + R200_SCAN_GAP_MS;
    }

    return stored_samples > 0U ? 0 : -1;
}
