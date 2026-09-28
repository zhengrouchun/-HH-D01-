#ifndef CLEARCHAIN_KEY_H
#define CLEARCHAIN_KEY_H

#include <stdint.h>

typedef enum {
    CLEARCHAIN_MODE_STAGE_1 = 1,
    CLEARCHAIN_MODE_STAGE_2,
    CLEARCHAIN_MODE_STAGE_3,
    CLEARCHAIN_MODE_STAGE_4,
    CLEARCHAIN_MODE_STAGE_5,
    CLEARCHAIN_MODE_CP
} clearchain_mode_t;

typedef struct {
    uint8_t stage;
    const char *name;
    const char *scanner_id;
    const char *stage_code;
} clearchain_stage_config_t;

/* Starts the polling task for five stage-selection buttons on TCA9555 P10-P14. */
void clearchain_key_start(void);

uint8_t clearchain_key_get_stage(void);
const clearchain_stage_config_t *clearchain_key_get_stage_config(void);
clearchain_mode_t clearchain_key_get_mode(void);

#endif
