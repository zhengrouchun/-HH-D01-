#include "clearchain_device_session.h"
#include <string.h>

/* Named durations keep the stage-one registration window distinct from later scans. */
enum {
    CC_EMPTY_COUNT = 0,
    CC_S1_WINDOW_MS = 10000,
    CC_OTHER_WINDOW_MS = 3000
};

void cc_session_cancel(cc_device_session_t *s)
{ s->active = 0; s->pending_count = CC_EMPTY_COUNT; }
int cc_session_accept(cc_device_session_t *s, const clearchain_device_state_t *state)
{
    int changed = !s->valid || state->state_version != s->state.state_version;
    if (!changed) { return 0; }
    int identity_changed = !s->valid || state->mode != s->state.mode ||
        strcmp(state->batch_id, s->state.batch_id) != 0;
    if (identity_changed || !clearchain_device_state_can_scan(state)) {
        cc_session_cancel(s); s->latched = 0;
    }
    s->state = *state; s->valid = 1; return 1;
}
int cc_session_start(cc_device_session_t *s, uint64_t now_ms)
{
    if (!s->valid || s->active || s->latched || !clearchain_device_state_can_scan(&s->state)) { return 0; }
    s->active = s->latched = 1; s->start_ms = s->last_post_ms = now_ms;
    s->pending_count = s->unique_count = s->total_samples = CC_EMPTY_COUNT; return 1;
}
int cc_session_add(cc_device_session_t *s, const char *epc, int8_t rssi)
{
    if (!s->active || epc == NULL || epc[0] == '\0' || strlen(epc) >= CC_SESSION_EPC_SIZE ||
        s->pending_count >= CC_SESSION_MAX_PENDING) { return -1; }
    unsigned int i;
    for (i=CC_EMPTY_COUNT; i<s->unique_count; ++i) { if (!strcmp(s->unique[i],epc)) { break; } }
    if (i==s->unique_count) {
        if (i>=CC_SESSION_MAX_TAGS) { return -1; }
        (void)strcpy(s->unique[s->unique_count++],epc);
    }
    (void)strcpy(s->pending[s->pending_count].epc,epc);
    s->pending[s->pending_count++].rssi=rssi; s->total_samples++; return 0;
}
int cc_session_finished(const cc_device_session_t *s, uint64_t now_ms)
{
    return s->active && (now_ms-s->start_ms >=
        (s->state.mode==CLEARCHAIN_DEVICE_MODE_S1?CC_S1_WINDOW_MS:CC_OTHER_WINDOW_MS));
}
