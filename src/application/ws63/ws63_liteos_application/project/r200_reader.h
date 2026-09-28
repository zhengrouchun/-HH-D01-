#ifndef R200_READER_H
#define R200_READER_H

#include <stddef.h>
#include <stdint.h>

#define R200_TAG_ID_MAX_LEN                    65U
#define R200_MAX_TAGS                          9U
#define R200_OBSERVATION_POSITION_COUNT        3U
#define R200_MAX_RSSI_SAMPLES_PER_POSITION     32U

typedef enum {
    R200_OBSERVATION_POSITION_A = 0,
    R200_OBSERVATION_POSITION_B,
    R200_OBSERVATION_POSITION_C
} r200_observation_position_t;

typedef struct {
    int8_t samples[R200_MAX_RSSI_SAMPLES_PER_POSITION];
    size_t sample_count;
    int8_t median;
    uint8_t median_valid;
} r200_rssi_observation_t;

typedef struct {
    char epc[R200_TAG_ID_MAX_LEN];
    r200_rssi_observation_t observations[R200_OBSERVATION_POSITION_COUNT];
} r200_tag_rssi_t;

typedef struct {
    r200_tag_rssi_t tags[R200_MAX_TAGS];
    size_t tag_count;
    uint32_t tag_overflow_count;
    uint32_t sample_overflow_count;
} r200_rssi_batch_t;

int r200_reader_init(void);
int r200_reader_read_epc(char *epc, size_t epc_size);

void r200_reader_batch_reset(r200_rssi_batch_t *batch);
int r200_reader_collect(r200_rssi_batch_t *batch,
                        r200_observation_position_t position,
                        size_t samples_per_tag);

#endif
