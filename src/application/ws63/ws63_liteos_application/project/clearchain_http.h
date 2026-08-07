#ifndef CLEARCHAIN_HTTP_H
#define CLEARCHAIN_HTTP_H

#include "clearchain_config.h"

#define CLEARCHAIN_HTTP_HOST SERVER_IP
#define CLEARCHAIN_HTTP_PORT SERVER_PORT
#define CLEARCHAIN_HTTP_PATH SERVER_PATH

#define CARD_A_EPC "000000000000000000078843"
#define CARD_B_EPC "000000000000000000078842"
#define CARD_C_EPC "E28011704000021D35AFADD9"

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

#endif
