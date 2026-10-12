#ifndef CLEARCHAIN_DEVICE_SESSION_H
#define CLEARCHAIN_DEVICE_SESSION_H
#include "clearchain_device_state.h"
#define CC_SESSION_MAX_TAGS 32U
#define CC_SESSION_MAX_PENDING 32U
#define CC_SESSION_EPC_SIZE 65U
typedef struct { char epc[CC_SESSION_EPC_SIZE]; int8_t rssi; } cc_session_reading_t;
typedef struct {
    clearchain_device_state_t state;
    int valid, active, latched;
    uint64_t start_ms, last_post_ms;
    unsigned int pending_count, unique_count, total_samples;
    cc_session_reading_t pending[CC_SESSION_MAX_PENDING];
    char unique[CC_SESSION_MAX_TAGS][CC_SESSION_EPC_SIZE];
} cc_device_session_t;
int cc_session_accept(cc_device_session_t *session, const clearchain_device_state_t *state);
int cc_session_start(cc_device_session_t *session, uint64_t now_ms);
void cc_session_cancel(cc_device_session_t *session);
int cc_session_add(cc_device_session_t *session, const char *epc, int8_t rssi);
int cc_session_finished(const cc_device_session_t *session, uint64_t now_ms);
#endif
