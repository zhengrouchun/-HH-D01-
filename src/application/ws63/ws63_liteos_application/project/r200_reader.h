#ifndef R200_READER_H
#define R200_READER_H

#include <stddef.h>
#include <stdint.h>

#define R200_TAG_ID_MAX_LEN 65U
#define R200_MAX_TAGS 32U

typedef struct {
    char chip_uid[R200_TAG_ID_MAX_LEN];
    int8_t rssi_dbm;
} r200_tag_reading_t;

typedef struct {
    r200_tag_reading_t tags[R200_MAX_TAGS];
    size_t tag_count;
} r200_batch_t;

int r200_reader_init(void);
int r200_reader_read_epc(char *epc, size_t epc_size);
void r200_reader_batch_reset(r200_batch_t *batch);

/* Caller supplies a test/configuration window pending hardware timing.
 * Returns 0 after the window (including an empty batch), -1 on I/O error.
 */
int r200_reader_read_batch(r200_batch_t *batch, uint32_t timeout_ms);

#endif