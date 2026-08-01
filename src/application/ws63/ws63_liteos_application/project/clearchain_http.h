#ifndef CLEARCHAIN_HTTP_H
#define CLEARCHAIN_HTTP_H

#define CLEARCHAIN_HTTP_HOST "shun-sternness-ranting.ngrok-free.dev"
#define CLEARCHAIN_HTTP_PORT 80
#define CLEARCHAIN_HTTP_PATH "/scan"

#define CARD_A_EPC "000000000000000000078843"
#define CARD_B_EPC "000000000000000000078842"
#define CARD_C_EPC "E28011704000021D35AFADD9"

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
 * Return 0 on success, -1 on failure.
 */
int clearchain_send_scan(const char *chip_uid);

#endif
