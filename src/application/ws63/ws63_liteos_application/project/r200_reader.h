#ifndef R200_READER_H
#define R200_READER_H

#include <stddef.h>
#include <stdint.h>
#include "clearchain_runtime_config.h"

#define R200_TAG_ID_MAX_LEN 65U
#define R200_MAX_TAGS CLEARCHAIN_MAX_TAGS
#define R200_MAX_SAMPLES_PER_TAG CLEARCHAIN_MAX_SAMPLES_PER_TAG

typedef struct {
    char chip_uid[R200_TAG_ID_MAX_LEN];
    int8_t rssi_dbm[R200_MAX_SAMPLES_PER_TAG];
    uint8_t sample_count;
} r200_tag_samples_t;

typedef struct {
    r200_tag_samples_t tags[R200_MAX_TAGS];
    size_t tag_count;
    size_t total_samples;
    uint32_t rejected_frames;
    uint32_t capacity_drops;
    uint32_t sample_limit_drops;
    uint32_t uart_dropped_bytes;
    uint32_t uart_error_events;
} r200_batch_t;

typedef void (*r200_batch_progress_fn)(const r200_batch_t *batch,
                                      uint32_t elapsed_ms, uint32_t window_ms);

int r200_reader_init(void);
int r200_reader_read_epc(char *epc, size_t epc_size);
int r200_reader_read_one(char *epc, size_t epc_size, int8_t *rssi_dbm);
void r200_reader_batch_reset(r200_batch_t *batch);

/* Caller supplies a test/configuration window pending hardware timing.
 * Returns 0 after the window (including an empty batch), -1 on I/O error.
 */
int r200_reader_read_batch(r200_batch_t *batch, uint32_t timeout_ms);
int r200_reader_read_batch_progress(r200_batch_t *batch, uint32_t timeout_ms,
                                    r200_batch_progress_fn progress);

#endif
