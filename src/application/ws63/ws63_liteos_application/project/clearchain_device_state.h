#ifndef CLEARCHAIN_DEVICE_STATE_H
#define CLEARCHAIN_DEVICE_STATE_H

#include <stddef.h>
#include <stdint.h>

#define CLEARCHAIN_BATCH_ID_SIZE 65U
#define CLEARCHAIN_MESSAGE_SIZE 64U
#define CLEARCHAIN_MODE_LABEL_SIZE 24U

typedef enum {
    CLEARCHAIN_DEVICE_MODE_NONE = 0,
    CLEARCHAIN_DEVICE_MODE_S1 = 1,
    CLEARCHAIN_DEVICE_MODE_S2,
    CLEARCHAIN_DEVICE_MODE_S3,
    CLEARCHAIN_DEVICE_MODE_S4,
    CLEARCHAIN_DEVICE_MODE_S5,
    CLEARCHAIN_DEVICE_MODE_CP
} clearchain_device_mode_t;

typedef enum {
    CLEARCHAIN_PHASE_IDLE = 0,
    CLEARCHAIN_PHASE_READY,
    CLEARCHAIN_PHASE_SCANNING,
    CLEARCHAIN_PHASE_DONE,
    CLEARCHAIN_PHASE_ERROR
} clearchain_device_phase_t;

typedef enum {
    CLEARCHAIN_STATUS_NONE = 0,
    CLEARCHAIN_STATUS_APPROVED,
    CLEARCHAIN_STATUS_MONITOR,
    CLEARCHAIN_STATUS_REJECT
} clearchain_device_status_t;

typedef struct {
    uint32_t state_version;
    int armed;
    char mode_label[CLEARCHAIN_MODE_LABEL_SIZE];
    uint8_t scan_direction; /* 0 null, 1 IN, 2 OUT, 3 MIXED */
    uint8_t color;          /* 0 null, 1 GREEN, 2 ORANGE, 3 RED */
    clearchain_device_mode_t mode;
    clearchain_device_phase_t phase;
    clearchain_device_status_t screen_status;
    int tags_read;
    int tags_expected;       /* -1 means JSON null. */
    int progress_percent;    /* -1 means JSON null. */
    int risk_percent;
    char batch_id[CLEARCHAIN_BATCH_ID_SIZE];
    char message[CLEARCHAIN_MESSAGE_SIZE];
} clearchain_device_state_t;

/* Parse only the documented state fields. A missing batch_id remains empty. */
int clearchain_device_state_parse(const char *json, clearchain_device_state_t *out);
const char *clearchain_device_mode_name(clearchain_device_mode_t mode);
const char *clearchain_device_access_code(clearchain_device_mode_t mode);
/* v2: stopped/DONE sessions never authorize reader posts. CP permits no batch. */
int clearchain_device_state_can_scan(const clearchain_device_state_t *state);

#endif
