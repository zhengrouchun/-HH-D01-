#ifndef CLEARCHAIN_DEVICE_HTTP_H
#define CLEARCHAIN_DEVICE_HTTP_H

#include <stddef.h>
#include <stdint.h>
#include "clearchain_device_state.h"

typedef struct {
    const char *chip_uid;
    int8_t rssi_dbm;
} clearchain_device_reading_t;

/* Returns HTTP status, or -1 for transport/parse failure. */
int clearchain_device_http_get_state(clearchain_device_state_t *state);
int clearchain_device_http_select_mode(clearchain_device_mode_t mode,
                                       clearchain_device_state_t *returned_state);
int clearchain_device_http_post_scan(const clearchain_device_state_t *state,
                                     const clearchain_device_reading_t *readings,
                                     size_t count, int final);

/* Pure formatter for host tests; caller owns the returned cJSON allocation. */
char *clearchain_device_http_scan_json(const clearchain_device_state_t *state,
                                      const clearchain_device_reading_t *readings,
                                      size_t count, int final);

#endif
