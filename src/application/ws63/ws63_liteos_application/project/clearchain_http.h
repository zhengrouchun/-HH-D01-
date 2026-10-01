#ifndef CLEARCHAIN_HTTP_H
#define CLEARCHAIN_HTTP_H

#include "clearchain_config.h"
#include "r200_reader.h"

#define CLEARCHAIN_HTTP_HOST SERVER_IP
#define CLEARCHAIN_HTTP_PORT SERVER_PORT
#define CLEARCHAIN_HTTP_PATH SERVER_PATH

typedef enum {
    CLEARCHAIN_SCAN_LED_GREEN = 0,
    CLEARCHAIN_SCAN_LED_ORANGE = 1,
    CLEARCHAIN_SCAN_LED_RED = 2,
    CLEARCHAIN_SCAN_LED_UNKNOWN = 3
} clearchain_scan_led_t;

/*
 * Send RFID scan data to the ClearChain /scan API.
 *
 * Smart-mode JSON:
 * {
 *   "chip_uid":"E28011704000021D35AFADD9",
 *   "scanner_id":"scanner_checkpoint",
 *   "scan_type":1,
 *   "stage_code":"PUB-c72m"
 * }
 *
 * Return CLEARCHAIN_SCAN_LED_* on success, -1 on failure.
 */
int clearchain_send_scan(const char *chip_uid);

typedef enum {
    CLEARCHAIN_VERIFY_AUTHORIZED = 0,
    CLEARCHAIN_VERIFY_MONITOR,
    CLEARCHAIN_VERIFY_ALERT,
    CLEARCHAIN_VERIFY_UNKNOWN
} clearchain_verify_result_t;

typedef struct {
    clearchain_verify_result_t result;
    int risk_score;
    int risk_score_valid;
} clearchain_verify_response_t;

typedef struct {
    unsigned int registered_tags;
    unsigned int total_samples;
} clearchain_register_response_t;

/* These known endpoints are available to future business orchestration. */
int clearchain_send_factory_scan(const char *chip_uid);
int clearchain_send_register_batch(const char *batch_id, const r200_batch_t *batch,
                                   clearchain_register_response_t *response);
int clearchain_send_factory_batch(const char *batch_id, const r200_batch_t *batch,
                                  clearchain_register_response_t *response);
int clearchain_send_verify_scan(const char *chip_uid, const char *location,
                                clearchain_verify_response_t *response);
clearchain_scan_led_t clearchain_verify_result_to_scan_led(clearchain_verify_result_t result);

/* /verify_batch is a manual/dashboard endpoint and is not used by the firmware loop. */
/* TODO: connect the agreed physical batch-label source before automatic upload. */

#endif
