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

typedef enum {
    CLEARCHAIN_KEY_UP = 1,
    CLEARCHAIN_KEY_DOWN,
    CLEARCHAIN_KEY_LEFT,
    CLEARCHAIN_KEY_RIGHT,
    CLEARCHAIN_KEY_OK,
    CLEARCHAIN_KEY_D1_HISTORY,
    CLEARCHAIN_KEY_D2_VIEW_ORIGINAL,
    CLEARCHAIN_KEY_D3_VIEW_IMAGE,
    CLEARCHAIN_KEY_D4_BACK
} clearchain_key_event_t;

typedef enum {
    CLEARCHAIN_KEY_ENABLED = 0,
    CLEARCHAIN_KEY_DISABLED = 1
} clearchain_key_availability_t;

/* Starts polling S1-S5 on 0x20 and navigation/D1-D4 on 0x21. */
void clearchain_key_start(void);

uint8_t clearchain_key_get_stage(void);
uint32_t clearchain_key_get_stage_selection_epoch(void);
const clearchain_stage_config_t *clearchain_key_get_stage_config(void);
clearchain_mode_t clearchain_key_get_mode(void);
/* TODO: waiting for confirmed physical/UI entry into CP mode. */
clearchain_key_availability_t clearchain_key_availability(clearchain_key_event_t event,
                                                           clearchain_mode_t mode);
/* Returns 1 for an event, 0 if none. Disabled actions are still reported. */
int clearchain_key_take_event(clearchain_key_event_t *event,
                              clearchain_key_availability_t *availability);

#endif
