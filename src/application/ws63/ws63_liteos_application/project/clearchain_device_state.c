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
    int version;
    int mode, phase, status;
    if (json == NULL || out == NULL || (root = cJSON_Parse(json)) == NULL) { return -1; }
    ok = cJSON_GetObjectItemCaseSensitive(root, "ok");
    mode = name_index(root, "mode", modes, sizeof(modes) / sizeof(modes[0]));
    phase = name_index(root, "phase", phases, sizeof(phases) / sizeof(phases[0]));
    status = name_index(root, "screen_status", statuses, sizeof(statuses) / sizeof(statuses[0]));
    if (!cJSON_IsObject(root) || !cJSON_IsTrue(ok) || mode < 0 || phase < 0 || status < 0 ||
        number_field(root, "state_version", &version, 0) != 0 ||
        number_field(root, "tags_read", &parsed.tags_read, 0) != 0 ||
        number_field(root, "tags_expected", &parsed.tags_expected, 1) != 0 ||
        number_field(root, "progress_percent", &parsed.progress_percent, 1) != 0 ||
        number_field(root, "risk_percent", &parsed.risk_percent, 0) != 0 ||
        parsed.progress_percent > 100 || parsed.risk_percent > 100 ||
        copy_optional(root, "batch_id", parsed.batch_id, sizeof(parsed.batch_id)) != 0 ||
        copy_optional(root, "message", parsed.message, sizeof(parsed.message)) != 0) {
        cJSON_Delete(root);
        return -1;
    }
    parsed.state_version = (uint32_t)version;
    parsed.mode = (clearchain_device_mode_t)mode;
    parsed.phase = (clearchain_device_phase_t)phase;
    parsed.screen_status = (clearchain_device_status_t)status;
    *out = parsed;
    cJSON_Delete(root);
    return 0;
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
