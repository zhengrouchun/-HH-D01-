#ifndef CLEARCHAIN_DISPLAY_PROTOCOL_H
#define CLEARCHAIN_DISPLAY_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define CLEARCHAIN_DISPLAY_PROTOCOL_VERSION 3U
#define CLEARCHAIN_DISPLAY_HEADER_SIZE 10U
#define CLEARCHAIN_DISPLAY_MESSAGE_SIZE 48U
#define CLEARCHAIN_DISPLAY_PAYLOAD_SIZE 66U
#define CLEARCHAIN_DISPLAY_MAX_PACKET_SIZE 78U
#define CLEARCHAIN_DISPLAY_MIN_STAGE 0U
#define CLEARCHAIN_DISPLAY_MAX_STAGE 6U
#define CLEARCHAIN_RISK_SCORE_UNKNOWN 0xFFU
#define CLEARCHAIN_DISPLAY_FLAG_TIME_PROGRESS 1U
#define CLEARCHAIN_DISPLAY_FLAG_UPLOAD_DISABLED 2U
#define CLEARCHAIN_DISPLAY_FLAG_INCOMPLETE 4U
#define CLEARCHAIN_DISPLAY_FLAG_PROGRESS_UNKNOWN 8U
#define CLEARCHAIN_DISPLAY_FLAG_BACKEND_OFFLINE 16U

/* CC magic, version, type, seq LE32, length LE16, full state, CRC16 LE. */
typedef enum {
    CLEARCHAIN_DISPLAY_CMD_WAITING = 1,
    CLEARCHAIN_DISPLAY_CMD_STAGE_CHANGED = 2,
    CLEARCHAIN_DISPLAY_CMD_SCAN_PROGRESS = 3,
    CLEARCHAIN_DISPLAY_CMD_RESULT = 4,
    CLEARCHAIN_DISPLAY_CMD_SCAN_STARTED = 5,
    CLEARCHAIN_DISPLAY_CMD_ERROR = 6,
    CLEARCHAIN_DISPLAY_CMD_HEARTBEAT = 7,
    CLEARCHAIN_DISPLAY_CMD_FULL_STATE_SNAPSHOT = 8
} clearchain_display_command_t;

typedef enum {
    CLEARCHAIN_DISPLAY_RESULT_APPROVED = 0,
    CLEARCHAIN_DISPLAY_RESULT_MONITOR = 1,
    CLEARCHAIN_DISPLAY_RESULT_REJECT = 2,
    CLEARCHAIN_DISPLAY_RESULT_UNKNOWN = 3,
    CLEARCHAIN_DISPLAY_RESULT_LOCAL_CAPTURE = 4
} clearchain_display_result_t;

typedef enum {
    CLEARCHAIN_DISPLAY_IDLE = 0,
    CLEARCHAIN_DISPLAY_WAITING = 1,
    CLEARCHAIN_DISPLAY_SCANNING = 2,
    CLEARCHAIN_DISPLAY_FINISHED = 3,
    CLEARCHAIN_DISPLAY_ERROR = 4
} clearchain_display_phase_t;
typedef enum {
    CLEARCHAIN_ERROR_NONE = 0,
    CLEARCHAIN_ERROR_NO_TAGS = 1,
    CLEARCHAIN_ERROR_READER = 2,
    CLEARCHAIN_ERROR_CAPACITY = 3,
    CLEARCHAIN_ERROR_TCA = 4,
    CLEARCHAIN_ERROR_BACKEND = 5,
    CLEARCHAIN_ERROR_NOT_AVAILABLE = 6,
    CLEARCHAIN_ERROR_DISABLED_KEY = 7
} clearchain_display_error_t;
typedef struct {
    uint8_t stage, phase, percent, tag_count;
    uint16_t total_samples, error;
    uint8_t result, risk_score, flags;
    uint32_t state_version;
    uint16_t tags_expected; /* UINT16_MAX means JSON null. */
    char message[CLEARCHAIN_DISPLAY_MESSAGE_SIZE];
} clearchain_display_state_t;

/* Every event carries full state: coalescing cannot lose a required delta.
 * No native structs, strings, EPCs or pixels are transmitted. */
int clearchain_display_encode(uint8_t *out, size_t capacity, uint8_t command,
                              uint32_t sequence, const clearchain_display_state_t *state);
int clearchain_display_decode(const uint8_t *packet, size_t length, uint8_t *command,
                              uint32_t *sequence, clearchain_display_state_t *state);
int clearchain_display_sequence_newer(uint32_t candidate, uint32_t previous);

#endif
