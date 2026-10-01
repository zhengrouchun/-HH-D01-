#include "clearchain_display_link.h"

#include <string.h>

#include "app_init.h"
#include "common_def.h"
#include "sle_connection_manager.h"
#include "sle_device_discovery.h"
#include "sle_errcode.h"
#include "sle_ssap_server.h"
#include "soc_osal.h"
#include "systick.h"
#include "clearchain_runtime_config.h"

#define CLEAR_DISPLAY_TASK_PRIORITY 28
#define CLEAR_DISPLAY_TASK_STACK 0x1800
#define CLEAR_DISPLAY_QUEUE_DEPTH 8
#define CLEAR_DISPLAY_MTU 520
#define CLEAR_DISPLAY_ADV_HANDLE 1
#define CLEAR_DISPLAY_SERVICE_UUID 0x2222
#define CLEAR_DISPLAY_PROPERTY_UUID 0x2323
#define CLEAR_DISPLAY_ADV_NAME "clear_main"
#define CLEAR_DISPLAY_ADV_NAME_TYPE 0x0B
#define CLEAR_DISPLAY_ADV_LEVEL_TYPE 0x01
#define CLEAR_DISPLAY_ADV_ACCESS_TYPE 0x02
#define CLEAR_DISPLAY_ADV_POWER_TYPE 0x0C

static volatile bool g_clearchain_display_started;
static volatile bool g_clearchain_display_connected;
static volatile bool g_clearchain_display_ready;
static volatile bool g_clearchain_display_replay_pending;
static volatile uint32_t g_clearchain_display_epoch;
static uint8_t g_clearchain_display_server_id;
static uint16_t g_clearchain_display_service_handle;
static uint16_t g_clearchain_display_property_handle;
static uint16_t g_clearchain_display_conn_id;
static uint32_t g_generation;
static uint32_t g_sequence;
static uint8_t g_command = CLEARCHAIN_DISPLAY_CMD_WAITING;
static clearchain_display_state_t g_state = {
    .stage = 1, .phase = CLEARCHAIN_DISPLAY_WAITING,
    .result = CLEARCHAIN_DISPLAY_RESULT_UNKNOWN,
    .risk_score = CLEARCHAIN_RISK_SCORE_UNKNOWN,
    .flags = CLEARCHAIN_UPLOAD_ALLOWED ? 0 : CLEARCHAIN_DISPLAY_FLAG_UPLOAD_DISABLED
};
static const uint8_t g_clearchain_display_uuid_base[16] = {
    0x37, 0xBE, 0xA8, 0x80, 0xFC, 0x70, 0x11, 0xEA,
    0xB7, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static void clearchain_display_set_uuid(uint16_t short_uuid, sle_uuid_t *uuid)
{
    (void)memcpy(uuid->uuid, g_clearchain_display_uuid_base, sizeof(g_clearchain_display_uuid_base));
    uuid->uuid[14] = (uint8_t)short_uuid;
    uuid->uuid[15] = (uint8_t)(short_uuid >> 8);
    uuid->len = 2;
}

static bool clearchain_display_valid_stage(uint8_t stage)
{
    return stage >= CLEARCHAIN_DISPLAY_MIN_STAGE && stage <= CLEARCHAIN_DISPLAY_MAX_STAGE;
}

/* One-slot coalescing mailbox. Producers only copy state under an IRQ lock. */
static int clearchain_display_update(uint8_t command, uint8_t stage, uint8_t percent,
                                    uint8_t tags, uint16_t samples, uint8_t result,
                                    uint8_t risk, uint16_t error)
{
    if (!clearchain_display_valid_stage(stage) || percent > 100U ||
        result > CLEARCHAIN_DISPLAY_RESULT_LOCAL_CAPTURE ||
        (risk > 100U && risk != CLEARCHAIN_RISK_SCORE_UNKNOWN)) { return -1; }
    unsigned int irq = osal_irq_lock();
    g_state.stage = stage;
    g_state.error = error;
    if (command == CLEARCHAIN_DISPLAY_CMD_WAITING || command == CLEARCHAIN_DISPLAY_CMD_STAGE_CHANGED ||
        command == CLEARCHAIN_DISPLAY_CMD_SCAN_STARTED) {
        g_state.phase = command == CLEARCHAIN_DISPLAY_CMD_SCAN_STARTED ? CLEARCHAIN_DISPLAY_SCANNING : CLEARCHAIN_DISPLAY_WAITING;
        g_state.percent = 0; g_state.tag_count = 0; g_state.total_samples = 0;
        g_state.result = CLEARCHAIN_DISPLAY_RESULT_UNKNOWN;
        g_state.risk_score = CLEARCHAIN_RISK_SCORE_UNKNOWN;
        g_state.flags = CLEARCHAIN_UPLOAD_ALLOWED ? 0 : CLEARCHAIN_DISPLAY_FLAG_UPLOAD_DISABLED;
    } else if (command == CLEARCHAIN_DISPLAY_CMD_SCAN_PROGRESS) {
        g_state.phase = CLEARCHAIN_DISPLAY_SCANNING;
        g_state.percent = percent; g_state.tag_count = tags; g_state.total_samples = samples;
        g_state.flags |= CLEARCHAIN_DISPLAY_FLAG_TIME_PROGRESS;
    } else if (command == CLEARCHAIN_DISPLAY_CMD_RESULT) {
        g_state.phase = CLEARCHAIN_DISPLAY_FINISHED;
        g_state.result = result; g_state.risk_score = risk;
        if (result == CLEARCHAIN_DISPLAY_RESULT_LOCAL_CAPTURE) {
            g_state.percent = 100; g_state.tag_count = tags; g_state.total_samples = samples;
        }
    } else if (command == CLEARCHAIN_DISPLAY_CMD_ERROR) {
        g_state.phase = CLEARCHAIN_DISPLAY_ERROR;
        g_state.result = CLEARCHAIN_DISPLAY_RESULT_UNKNOWN;
        g_state.risk_score = CLEARCHAIN_RISK_SCORE_UNKNOWN;
    }
    g_command = command;
    g_generation++;
    osal_irq_restore(irq);
    return 0;
}
int clearchain_display_show_waiting(uint8_t stage)
{ return clearchain_display_update(CLEARCHAIN_DISPLAY_CMD_WAITING, stage, 0, 0, 0, 3, 255, 0); }
int clearchain_display_stage_changed(uint8_t stage)
{ return clearchain_display_update(CLEARCHAIN_DISPLAY_CMD_STAGE_CHANGED, stage, 0, 0, 0, 3, 255, 0); }
int clearchain_display_scan_started(uint8_t stage)
{ return clearchain_display_update(CLEARCHAIN_DISPLAY_CMD_SCAN_STARTED, stage, 0, 0, 0, 3, 255, 0); }
int clearchain_display_scan_update(uint8_t stage, uint8_t percent, uint8_t tags, uint16_t samples)
{ return clearchain_display_update(CLEARCHAIN_DISPLAY_CMD_SCAN_PROGRESS, stage, percent, tags, samples, 3, 255, 0); }
int clearchain_display_show_progress(uint8_t stage, uint8_t percent)
{ return clearchain_display_scan_update(stage, percent, 0, 0); }
int clearchain_display_scan_complete(uint8_t stage, uint8_t tags, uint16_t samples)
{ return clearchain_display_update(CLEARCHAIN_DISPLAY_CMD_RESULT, stage, 100, tags, samples, CLEARCHAIN_DISPLAY_RESULT_LOCAL_CAPTURE, 255, 0); }
int clearchain_display_show_error(uint8_t stage, uint16_t error)
{ return clearchain_display_update(CLEARCHAIN_DISPLAY_CMD_ERROR, stage, 0, 0, 0, 3, 255, error); }
int clearchain_display_show_result(clearchain_display_result_t result, uint8_t risk_score)
{
    unsigned int irq = osal_irq_lock();
    uint8_t stage = g_state.stage;
    osal_irq_restore(irq);
    return clearchain_display_update(CLEARCHAIN_DISPLAY_CMD_RESULT, stage, 0, 0, 0, result, risk_score, 0);
}
bool clearchain_display_is_connected(void) { return g_clearchain_display_connected; }

static int clearchain_display_send(uint8_t command, const clearchain_display_state_t *state, uint32_t epoch)
{
    uint8_t bytes[CLEARCHAIN_DISPLAY_MAX_PACKET_SIZE];
    ssaps_ntf_ind_t notification = {0};
    if (!g_clearchain_display_ready || epoch != g_clearchain_display_epoch) { return -1; }
    int length = clearchain_display_encode(bytes, sizeof(bytes), command, ++g_sequence, state);
    if (length < 0) { return -1; }
    notification.handle = g_clearchain_display_property_handle;
    notification.type = SSAP_PROPERTY_TYPE_VALUE;
    notification.value = bytes;
    notification.value_len = (uint16_t)length;
    errcode_t status = ssaps_notify_indicate(g_clearchain_display_server_id, g_clearchain_display_conn_id, &notification);
    if (status != ERRCODE_SLE_SUCCESS) {
        osal_printk("[CLEAR SLE] notification failed: 0x%x\r\n", status);
        return -1;
    }
    return 0;
}

static void clearchain_display_mtu_changed(uint8_t server_id, uint16_t conn_id,
                                           ssap_exchange_info_t *info, errcode_t status)
{
    unused(server_id);
    unused(info);
    if (status == ERRCODE_SLE_SUCCESS && g_clearchain_display_connected &&
        conn_id == g_clearchain_display_conn_id) {
        g_clearchain_display_ready = true;
        g_clearchain_display_replay_pending = true;
    }
}

static void clearchain_display_set_mtu(void)
{
    ssap_exchange_info_t info = {0};
    info.mtu_size = CLEAR_DISPLAY_MTU;
    info.version = 1;
    if (ssaps_set_info(g_clearchain_display_server_id, &info) != ERRCODE_SLE_SUCCESS) {
        osal_printk("[CLEAR SLE] set MTU failed\r\n");
    }
}

static void clearchain_display_connection_changed(uint16_t conn_id, const sle_addr_t *addr,
    sle_acb_state_t state, sle_pair_state_t pair_state, sle_disc_reason_t reason)
{
    unused(addr);
    unused(reason);
    if (state == SLE_ACB_STATE_CONNECTED) {
        g_clearchain_display_conn_id = conn_id;
        g_clearchain_display_connected = true;
        g_clearchain_display_ready = false;
        osal_printk("[CLEAR SLE] display connected\r\n");
        if (pair_state != SLE_PAIR_NONE) {
            clearchain_display_set_mtu();
        }
    } else if (state == SLE_ACB_STATE_DISCONNECTED) {
        g_clearchain_display_connected = false;
        g_clearchain_display_ready = false;
        g_clearchain_display_conn_id = 0;
        g_clearchain_display_epoch++;
        osal_printk("[CLEAR SLE] display disconnected, advertising again\r\n");
        if (sle_start_announce(CLEAR_DISPLAY_ADV_HANDLE) != ERRCODE_SLE_SUCCESS) {
            osal_printk("[CLEAR SLE] restart advertising failed\r\n");
        }
    }
}

static void clearchain_display_pair_complete(uint16_t conn_id, const sle_addr_t *addr, errcode_t status)
{
    unused(addr);
    if (status == ERRCODE_SLE_SUCCESS && conn_id == g_clearchain_display_conn_id) {
        clearchain_display_set_mtu();
    } else {
        osal_printk("[CLEAR SLE] pairing failed: 0x%x\r\n", status);
    }
}

static void clearchain_display_write_request(uint8_t server_id, uint16_t conn_id,
                                              ssaps_req_write_cb_t *request, errcode_t status)
{
    unused(server_id);
    if (status == ERRCODE_SLE_SUCCESS && request != NULL && request->value != NULL &&
        conn_id == g_clearchain_display_conn_id && request->handle == g_clearchain_display_property_handle &&
        request->length == 2U && request->value[0] == 'R' && request->value[1] == CLEARCHAIN_DISPLAY_PROTOCOL_VERSION) {
        g_clearchain_display_replay_pending = true;
    }
}

static errcode_t clearchain_display_register_server(void)
{
    static sle_connection_callbacks_t connection_callbacks = {0};
    static ssaps_callbacks_t ssap_callbacks = {0};
    static sle_announce_seek_callbacks_t announce_callbacks = {0};
    sle_uuid_t app_uuid = {0};
    sle_uuid_t service_uuid = {0};
    ssaps_property_info_t property = {0};
    ssaps_desc_info_t descriptor = {0};
    uint8_t initial_value = 0;
    uint8_t description[] = {0x01, 0x00};
    errcode_t status;

    connection_callbacks.connect_state_changed_cb = clearchain_display_connection_changed;
    connection_callbacks.pair_complete_cb = clearchain_display_pair_complete;
    status = sle_connection_register_callbacks(&connection_callbacks);
    if (status != ERRCODE_SLE_SUCCESS) {
        return status;
    }
    status = sle_announce_seek_register_callbacks(&announce_callbacks);
    if (status != ERRCODE_SLE_SUCCESS) {
        return status;
    }
    ssap_callbacks.mtu_changed_cb = clearchain_display_mtu_changed;
    ssap_callbacks.write_request_cb = clearchain_display_write_request;
    status = ssaps_register_callbacks(&ssap_callbacks);
    if (status != ERRCODE_SLE_SUCCESS) {
        return status;
    }

    clearchain_display_set_uuid(0x1122, &app_uuid);
    app_uuid.len = sizeof(app_uuid.uuid);
    status = ssaps_register_server(&app_uuid, &g_clearchain_display_server_id);
    if (status != ERRCODE_SLE_SUCCESS) {
        return status;
    }
    clearchain_display_set_uuid(CLEAR_DISPLAY_SERVICE_UUID, &service_uuid);
    status = ssaps_add_service_sync(g_clearchain_display_server_id, &service_uuid, true,
                                    &g_clearchain_display_service_handle);
    if (status != ERRCODE_SLE_SUCCESS) {
        goto fail;
    }
    clearchain_display_set_uuid(CLEAR_DISPLAY_PROPERTY_UUID, &property.uuid);
    property.permissions = SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE;
    property.operate_indication = SSAP_OPERATE_INDICATION_BIT_READ |
                                  SSAP_OPERATE_INDICATION_BIT_WRITE |
                                  SSAP_OPERATE_INDICATION_BIT_NOTIFY;
    property.value = &initial_value;
    property.value_len = sizeof(initial_value);
    status = ssaps_add_property_sync(g_clearchain_display_server_id, g_clearchain_display_service_handle,
                                     &property, &g_clearchain_display_property_handle);
    if (status != ERRCODE_SLE_SUCCESS) {
        goto fail;
    }
    descriptor.permissions = SSAP_PERMISSION_READ;
    descriptor.type = SSAP_DESCRIPTOR_USER_DESCRIPTION;
    descriptor.operate_indication = SSAP_OPERATE_INDICATION_BIT_READ;
    descriptor.value = description;
    descriptor.value_len = sizeof(description);
    status = ssaps_add_descriptor_sync(g_clearchain_display_server_id, g_clearchain_display_service_handle,
                                       g_clearchain_display_property_handle, &descriptor);
    if (status != ERRCODE_SLE_SUCCESS) {
        goto fail;
    }
    status = ssaps_start_service(g_clearchain_display_server_id, g_clearchain_display_service_handle);
    if (status == ERRCODE_SLE_SUCCESS) {
        return status;
    }
fail:
    (void)ssaps_unregister_server(g_clearchain_display_server_id);
    return status;
}

static errcode_t clearchain_display_start_advertising(void)
{
    sle_announce_param_t params = {0};
    sle_announce_data_t data = {0};
    uint8_t announce_data[] = {2, CLEAR_DISPLAY_ADV_LEVEL_TYPE, SLE_ANNOUNCE_LEVEL_NORMAL,
                               2, CLEAR_DISPLAY_ADV_ACCESS_TYPE, 0};
    uint8_t response[5 + sizeof(CLEAR_DISPLAY_ADV_NAME) - 1] = {0};
    const uint8_t address[SLE_ADDR_LEN] = {1, 2, 3, 4, 5, 6};
    errcode_t status;

    params.announce_mode = SLE_ANNOUNCE_MODE_CONNECTABLE_SCANABLE;
    params.announce_handle = CLEAR_DISPLAY_ADV_HANDLE;
    params.announce_gt_role = SLE_ANNOUNCE_ROLE_T_CAN_NEGO;
    params.announce_level = SLE_ANNOUNCE_LEVEL_NORMAL;
    params.announce_channel_map = 0x07;
    params.announce_interval_min = 0xC8;
    params.announce_interval_max = 0xC8;
    params.conn_interval_min = 12;
    params.conn_interval_max = 12;
    params.conn_max_latency = 0x1F3;
    params.conn_supervision_timeout = 0x1F4;
    params.announce_tx_power = 18;
    params.own_addr.type = 0;
    (void)memcpy(params.own_addr.addr, address, sizeof(address));

    status = sle_set_announce_param(CLEAR_DISPLAY_ADV_HANDLE, &params);
    if (status != ERRCODE_SLE_SUCCESS) {
        return status;
    }
    response[0] = 2;
    response[1] = CLEAR_DISPLAY_ADV_POWER_TYPE;
    response[2] = 10;
    response[3] = sizeof(CLEAR_DISPLAY_ADV_NAME);
    response[4] = CLEAR_DISPLAY_ADV_NAME_TYPE;
    (void)memcpy(&response[5], CLEAR_DISPLAY_ADV_NAME, sizeof(CLEAR_DISPLAY_ADV_NAME) - 1);
    data.announce_data = announce_data;
    data.announce_data_len = sizeof(announce_data);
    data.seek_rsp_data = response;
    data.seek_rsp_data_len = sizeof(response);
    status = sle_set_announce_data(CLEAR_DISPLAY_ADV_HANDLE, &data);
    if (status != ERRCODE_SLE_SUCCESS) {
        return status;
    }
    status = sle_start_announce(CLEAR_DISPLAY_ADV_HANDLE);
    if (status == ERRCODE_SLE_SUCCESS) {
        osal_printk("[CLEAR SLE] server advertising\r\n");
    }
    return status;
}

static void *clearchain_display_task(void *arg)
{
    errcode_t status;
    uint32_t sent_generation = UINT32_MAX;
    uint64_t last_send = 0U;
    unused(arg);
    (void)osal_msleep(5000);
    status = enable_sle();
    if (status == ERRCODE_SLE_SUCCESS) { status = clearchain_display_register_server(); }
    if (status == ERRCODE_SLE_SUCCESS) { status = clearchain_display_start_advertising(); }
    if (status != ERRCODE_SLE_SUCCESS) {
        osal_printk("[CLEAR SLE] server startup failed: 0x%x\r\n", status);
        return NULL;
    }
    while (1) {
        uint64_t now = uapi_systick_get_ms();
        unsigned int irq = osal_irq_lock();
        uint32_t generation = g_generation, epoch = g_clearchain_display_epoch;
        bool replay = g_clearchain_display_replay_pending;
        bool ready = g_clearchain_display_ready;
        clearchain_display_state_t state = g_state;
        uint8_t command = replay ? CLEARCHAIN_DISPLAY_CMD_FULL_STATE_SNAPSHOT :
            (generation != sent_generation ? g_command : CLEARCHAIN_DISPLAY_CMD_HEARTBEAT);
        if (ready && (replay || generation != sent_generation || now - last_send >= 1000U)) {
            g_clearchain_display_replay_pending = false;
            osal_irq_restore(irq);
            if (clearchain_display_send(command, &state, epoch) == 0) {
                sent_generation = generation;
                last_send = now;
            } else {
                g_clearchain_display_replay_pending = true;
            }
        } else { osal_irq_restore(irq); }
        osal_msleep(CLEARCHAIN_PROGRESS_INTERVAL_MS);
    }
}

int clearchain_display_link_init(void)
{
    if (g_clearchain_display_started) { return 0; }
    osal_task *task = osal_kthread_create((osal_kthread_handler)clearchain_display_task, NULL,
                                         "CCDisplaySLE", CLEAR_DISPLAY_TASK_STACK);
    if (task == NULL) { return -1; }
    g_clearchain_display_started = true;
    (void)osal_kthread_set_priority(task, CLEAR_DISPLAY_TASK_PRIORITY);
    osal_kfree(task);
    return 0;
}
