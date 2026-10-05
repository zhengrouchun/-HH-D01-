#include "app_init.h"
#include "clearchain_config.h"
#include "clearchain_device_http.h"
#include "clearchain_display_link.h"
#include "clearchain_feedback.h"
#include "clearchain_key.h"
#include "clearchain_runtime_config.h"
#include "clearchain_tca9555.h"
#include "my_wifi_api.h"
#include "r200_reader.h"
#include "soc_osal.h"
#include "systick.h"
#include <string.h>

#define CC_POLL_MS 500U
#define CC_OFFLINE_RETRY_MS 2000U
#define CC_S1_WINDOW_MS 10000U
#define CC_OTHER_WINDOW_MS 3000U
#define CC_POST_INTERVAL_MS 1000U
#define CC_PENDING_MAX 32U
#define CC_TOTAL_SAMPLE_MAX 128U

typedef struct {
    char epc[R200_TAG_ID_MAX_LEN];
    int8_t rssi;
} captured_reading_t;

static clearchain_device_state_t g_state;
static volatile int g_state_valid;
static volatile int g_offline;
static captured_reading_t g_pending[CC_PENDING_MAX];
static char g_unique[R200_MAX_TAGS][R200_TAG_ID_MAX_LEN];

static int state_snapshot(clearchain_device_state_t *out)
{
    unsigned int irq = osal_irq_lock();
    int valid = g_state_valid;
    if (valid) { *out = g_state; }
    osal_irq_restore(irq);
    return valid;
}

static void accept_state(const clearchain_device_state_t *incoming)
{
    unsigned int irq = osal_irq_lock();
    int changed = !g_state_valid || incoming->state_version > g_state.state_version;
    if (changed) { g_state = *incoming; g_state_valid = 1; }
    osal_irq_restore(irq);
    if (changed) {
        clearchain_key_set_remote_mode((clearchain_mode_t)incoming->mode);
        (void)clearchain_display_publish_device_state(incoming);
        osal_printk("[DEVICE] state v%u mode=%s phase=%u batch=%s\r\n",
            (unsigned int)incoming->state_version, clearchain_device_mode_name(incoming->mode),
            (unsigned int)incoming->phase, incoming->batch_id[0] ? incoming->batch_id : "(none)");
    }
}

static void *wifi_task(void *unused)
{
    (void)unused;
    while (wifi_connectTo_AP(WIFI_SSID_NAME, WIFI_SSID_KEY) != ERRCODE_SUCC) {
        osal_printk("[DEVICE] Wi-Fi connect failed; retrying\r\n");
        osal_msleep(CC_OFFLINE_RETRY_MS);
    }
    osal_printk("[DEVICE] Wi-Fi ready\r\n");
    return NULL;
}

static void *state_poll_task(void *unused)
{
    unsigned int failures = 0;
    (void)unused;
    while (1) {
        clearchain_device_state_t next;
        int status = clearchain_device_http_get_state(&next);
        if (status == 200) {
            failures = 0;
            if (g_offline) {
                g_offline = 0;
                clearchain_display_set_backend_offline(0);
            }
            accept_state(&next);
            osal_msleep(CC_POLL_MS);
        } else {
            if (failures < 3U) { failures++; }
            if (failures == 3U && !g_offline) {
                g_offline = 1;
                clearchain_display_set_backend_offline(1);
                osal_printk("[DEVICE] backend offline after three failed polls\r\n");
            }
            osal_msleep(failures >= 3U ? CC_OFFLINE_RETRY_MS : CC_POLL_MS);
        }
    }
    return NULL;
}

static void consume_stage_keys(void)
{
    clearchain_key_event_t event;
    clearchain_key_availability_t availability;
    while (clearchain_key_take_event(&event, &availability)) {
        if (event >= CLEARCHAIN_KEY_STAGE_1 && event <= CLEARCHAIN_KEY_STAGE_5) {
            clearchain_device_state_t returned;
            clearchain_device_mode_t mode = (clearchain_device_mode_t)
                (CLEARCHAIN_DEVICE_MODE_S1 + event - CLEARCHAIN_KEY_STAGE_1);
            int status = clearchain_device_http_select_mode(mode, &returned);
            if (status == 200) { accept_state(&returned); }
            else { osal_printk("[DEVICE] SELECT_MODE failed: HTTP %d\r\n", status); }
        } else {
            /* CP remains phone-only; D keys and D-pad have no v1 command. */
            (void)availability;
        }
    }
}

static int post_pending(const clearchain_device_state_t *state, unsigned int count, int final)
{
    clearchain_device_reading_t readings[CC_PENDING_MAX];
    for (unsigned int i = 0; i < count; ++i) {
        readings[i].chip_uid = g_pending[i].epc;
        readings[i].rssi_dbm = g_pending[i].rssi;
    }
    return clearchain_device_http_post_scan(state, readings, count, final);
}

static int unique_add(const char *epc, unsigned int *unique_count)
{
    for (unsigned int i = 0; i < *unique_count; ++i) {
        if (strcmp(g_unique[i], epc) == 0) { return 0; }
    }
    if (*unique_count >= R200_MAX_TAGS) { return -1; }
    (void)strncpy(g_unique[*unique_count], epc, R200_TAG_ID_MAX_LEN - 1U);
    g_unique[*unique_count][R200_TAG_ID_MAX_LEN - 1U] = '\0';
    (*unique_count)++;
    return 0;
}

/* Hold the newest sample for the nonempty final:true request. */
static int flush_intermediate(const clearchain_device_state_t *selected,
                              unsigned int *pending_count)
{
    unsigned int to_send;
    int status;
    if (*pending_count <= 1U) { return 0; }
    to_send = *pending_count - 1U;
    status = post_pending(selected, to_send, 0);
    if (status < 200 || status >= 300) { return status == -1 ? -1 : status; }
    g_pending[0] = g_pending[to_send];
    *pending_count = 1U;
    return 0;
}

static void capture_window(const clearchain_device_state_t *selected)
{
    uint64_t start = uapi_systick_get_ms();
    uint64_t last_post = start;
    uint32_t window = selected->mode == CLEARCHAIN_DEVICE_MODE_S1 ? CC_S1_WINDOW_MS : CC_OTHER_WINDOW_MS;
    unsigned int pending_count = 0, total_samples = 0, unique_count = 0;
    int error = 0;
    while (uapi_systick_get_ms() - start < window) {
        clearchain_device_state_t current;
        char epc[R200_TAG_ID_MAX_LEN] = {0};
        int8_t rssi;
        uint64_t now;
        if (!state_snapshot(&current) || g_offline || current.mode != selected->mode ||
            strcmp(current.batch_id, selected->batch_id) != 0 ||
            (current.phase != CLEARCHAIN_PHASE_READY && current.phase != CLEARCHAIN_PHASE_SCANNING)) {
            /* A changed mode/batch or offline connection invalidates this capture. */
            error = -1; break;
        }
        if (current.phase == CLEARCHAIN_PHASE_READY &&
            current.state_version > selected->state_version) {
            /* The documented Stop returns to READY. Finish this window. */
            break;
        }
        if (r200_reader_read_one(epc, sizeof(epc), &rssi) == 0) {
            if (unique_add(epc, &unique_count) != 0 || total_samples >= CC_TOTAL_SAMPLE_MAX) {
                error = -1; break;
            }
            if (pending_count == CC_PENDING_MAX &&
                (error = flush_intermediate(selected, &pending_count)) != 0) { break; }
            (void)strcpy(g_pending[pending_count].epc, epc);
            g_pending[pending_count].rssi = rssi;
            pending_count++; total_samples++;
        }
        now = uapi_systick_get_ms();
        if (now - last_post >= CC_POST_INTERVAL_MS) {
            if ((error = flush_intermediate(selected, &pending_count)) != 0) { break; }
            last_post = now;
        }
        if (selected->mode == CLEARCHAIN_DEVICE_MODE_S1 && current.tags_expected > 0 &&
            unique_count >= (unsigned int)current.tags_expected) { break; }
        consume_stage_keys();
    }
    if (error == 0 && pending_count != 0U) {
        int status = post_pending(selected, pending_count, 1);
        if (status < 200 || status >= 300) { error = status == -1 ? -1 : status; }
    }
    if (error != 0) {
        osal_printk("[DEVICE] scan stopped; last HTTP status=%d, no ambiguous retry\r\n", error);
        (void)clearchain_display_show_error((uint8_t)selected->mode, CLEARCHAIN_ERROR_BACKEND);
    } else if (total_samples == 0U) {
        /* No empty readings; no invented backend completion. */
        osal_printk("[DEVICE] no tag in capture window\r\n");
        (void)clearchain_display_show_error((uint8_t)selected->mode, CLEARCHAIN_ERROR_NO_TAGS);
    } else {
        osal_printk("[DEVICE] final scan posted: unique=%u samples=%u\r\n", unique_count, total_samples);
    }
}

static void *capture_task(void *unused)
{
    int wait_for_done = 0;
    int saw_done = 0;
    clearchain_device_mode_t last_mode = CLEARCHAIN_DEVICE_MODE_NONE;
    char last_batch[CLEARCHAIN_BATCH_ID_SIZE] = {0};
    (void)unused;
    while (1) {
        clearchain_device_state_t selected;
        consume_stage_keys();
        if (!state_snapshot(&selected) || g_offline || selected.batch_id[0] == '\0' ||
            selected.mode == CLEARCHAIN_DEVICE_MODE_NONE) { osal_msleep(100); continue; }
        if (selected.mode != last_mode || strcmp(selected.batch_id, last_batch) != 0) {
            wait_for_done = 0;
            saw_done = 0;
        }
        if (wait_for_done) {
            if (selected.phase == CLEARCHAIN_PHASE_DONE || selected.phase == CLEARCHAIN_PHASE_IDLE ||
                selected.phase == CLEARCHAIN_PHASE_ERROR) { saw_done = 1; }
            if (!saw_done || selected.phase != CLEARCHAIN_PHASE_READY) {
                osal_msleep(100); continue;
            }
            wait_for_done = 0; saw_done = 0;
        }
        if (selected.phase == CLEARCHAIN_PHASE_READY || selected.phase == CLEARCHAIN_PHASE_SCANNING) {
            last_mode = selected.mode;
            (void)strcpy(last_batch, selected.batch_id);
            capture_window(&selected);
            wait_for_done = 1;
        } else { osal_msleep(100); }
    }
    return NULL;
}

static int start_task(osal_kthread_handler handler, const char *name, uint32_t stack, int priority)
{
    osal_task *task = osal_kthread_create(handler, NULL, name, stack);
    if (task == NULL) { return -1; }
    osal_kthread_lock();
    (void)osal_kthread_set_priority(task, priority);
    osal_kthread_unlock();
    osal_kfree(task);
    return 0;
}

static void device_app_entry(void)
{
    clearchain_key_set_remote_mode(CLEARCHAIN_MODE_NONE);
    if (clearchain_display_link_init() != 0) {
        osal_printk("[DEVICE] SLE display link failed\r\n");
    }
    if (clearchain_tca9555_probe() != ERRCODE_SUCC) {
        osal_printk("[DEVICE] TCA9555 probe failed; check GPIO13/14 and addresses\r\n");
        return;
    }
    clearchain_feedback_init();
    if (r200_reader_init() != 0) {
        osal_printk("[DEVICE] R200 init failed; check UART1 GPIO15/16\r\n");
        return;
    }
    clearchain_key_start();
    if (start_task((osal_kthread_handler)wifi_task, "CCWifi", 0x2000, 26) != 0 ||
        start_task((osal_kthread_handler)state_poll_task, "CCStatePoll", 0x2000, 27) != 0 ||
        start_task((osal_kthread_handler)capture_task, "CCCapture", 0x2800, 25) != 0) {
        osal_printk("[DEVICE] task startup failed\r\n");
    }
}

app_run(device_app_entry);
