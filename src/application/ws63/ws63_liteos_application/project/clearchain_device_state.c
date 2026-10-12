#include "clearchain_device_state.h"
#include "clearchain_config.h"
#include "cJSON.h"
#include <limits.h>
#include <string.h>

static const char *const modes[] = {"NONE", "S1", "S2", "S3", "S4", "S5", "CP"};
static const char *const phases[] = {"IDLE", "READY", "SCANNING", "DONE", "ERROR"};
static const char *const statuses[] = {"NONE", "APPROVED", "MONITOR", "REJECT"};

static int name_index(const cJSON *root, const char *key, const char *const *names, size_t count)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsString(item) || item->valuestring == NULL) { return -1; }
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(item->valuestring, names[i]) == 0) { return (int)i; }
    }
    return -1;
}

static int number_field(const cJSON *root, const char *key, int *out, int nullable)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (nullable && cJSON_IsNull(item)) { *out = -1; return 0; }
    if (!cJSON_IsNumber(item) || item->valuedouble < 0.0 || item->valuedouble > INT_MAX ||
        item->valuedouble != (double)item->valueint) { return -1; }
    *out = item->valueint;
    return 0;
}

static int copy_optional(const cJSON *root, const char *key, char *out, size_t size)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (item == NULL || cJSON_IsNull(item)) { out[0] = '\0'; return 0; }
    if (!cJSON_IsString(item) || item->valuestring == NULL || strlen(item->valuestring) >= size) {
        return -1;
    }
    (void)strcpy(out, item->valuestring);
    return 0;
}

int clearchain_device_state_parse(const char *json, clearchain_device_state_t *out)
{
    clearchain_device_state_t parsed = {0};
    cJSON *root;
    const cJSON *ok;
    const cJSON *version, *armed, *direction, *color;
    static const char *const directions[] = {"IN", "OUT", "MIXED"};
    static const char *const colors[] = {"GREEN", "ORANGE", "RED"};
    int mode, phase, status;
    if (json == NULL || out == NULL || (root = cJSON_Parse(json)) == NULL) { return -1; }
    ok = cJSON_GetObjectItemCaseSensitive(root, "ok");
    version = cJSON_GetObjectItemCaseSensitive(root, "state_version");
    armed = cJSON_GetObjectItemCaseSensitive(root, "armed");
    direction = cJSON_GetObjectItemCaseSensitive(root, "scan_direction");
    color = cJSON_GetObjectItemCaseSensitive(root, "color");
    mode = name_index(root, "mode", modes, sizeof(modes) / sizeof(modes[0]));
    phase = name_index(root, "phase", phases, sizeof(phases) / sizeof(phases[0]));
    status = name_index(root, "screen_status", statuses, sizeof(statuses) / sizeof(statuses[0]));
    if (!cJSON_IsObject(root) || !cJSON_IsTrue(ok) || mode < 0 || phase < 0 || status < 0 ||
        !cJSON_IsNumber(version) || version->valuedouble < 0 ||
        version->valuedouble > UINT32_MAX ||
        version->valuedouble != (double)(uint32_t)version->valuedouble || !cJSON_IsBool(armed) ||
        (!cJSON_IsNull(direction) && !cJSON_IsString(direction)) ||
        (!cJSON_IsNull(color) && !cJSON_IsString(color)) ||
        number_field(root, "tags_read", &parsed.tags_read, 0) != 0 ||
        number_field(root, "tags_expected", &parsed.tags_expected, 1) != 0 ||
        number_field(root, "progress_percent", &parsed.progress_percent, 1) != 0 ||
        number_field(root, "risk_percent", &parsed.risk_percent, 0) != 0 ||
        parsed.progress_percent > 100 || parsed.risk_percent > 100 ||
        copy_optional(root, "batch_id", parsed.batch_id, sizeof(parsed.batch_id)) != 0 ||
        copy_optional(root, "message", parsed.message, sizeof(parsed.message)) != 0 ||
        strlen(parsed.message) > 60U ||
        copy_optional(root, "mode_label", parsed.mode_label, sizeof(parsed.mode_label)) != 0 ||
        parsed.mode_label[0] == '\0') {
        cJSON_Delete(root);
        return -1;
    }
    if (!cJSON_IsNull(direction)) {
        int index = name_index(root, "scan_direction", directions, 3);
        if (index < 0) { cJSON_Delete(root); return -1; }
        parsed.scan_direction = (uint8_t)(index + 1);
    }
    if (!cJSON_IsNull(color)) {
        int index = name_index(root, "color", colors, 3);
        if (index < 0) { cJSON_Delete(root); return -1; }
        parsed.color = (uint8_t)(index + 1);
    }
    parsed.state_version = (uint32_t)version->valuedouble;
    parsed.armed = cJSON_IsTrue(armed);
    parsed.mode = (clearchain_device_mode_t)mode;
    parsed.phase = (clearchain_device_phase_t)phase;
    parsed.screen_status = (clearchain_device_status_t)status;
    *out = parsed;
    cJSON_Delete(root);
    return 0;
}

int clearchain_device_state_can_scan(const clearchain_device_state_t *state)
{
    return state != NULL && state->armed && state->mode != CLEARCHAIN_DEVICE_MODE_NONE &&
        (state->phase == CLEARCHAIN_PHASE_READY || state->phase == CLEARCHAIN_PHASE_SCANNING) &&
        (state->mode == CLEARCHAIN_DEVICE_MODE_CP || state->batch_id[0] != '\0');
}

const char *clearchain_device_mode_name(clearchain_device_mode_t mode)
{
    return (unsigned int)mode < sizeof(modes) / sizeof(modes[0]) ? modes[mode] : NULL;
}

const char *clearchain_device_access_code(clearchain_device_mode_t mode)
{
    static const char *const codes[] = {NULL, CLEARCHAIN_ACCESS_S1, CLEARCHAIN_ACCESS_S2,
        CLEARCHAIN_ACCESS_S3, CLEARCHAIN_ACCESS_S4, CLEARCHAIN_ACCESS_S5, CLEARCHAIN_ACCESS_CP};
    return (unsigned int)mode < sizeof(codes) / sizeof(codes[0]) ? codes[mode] : NULL;
}
