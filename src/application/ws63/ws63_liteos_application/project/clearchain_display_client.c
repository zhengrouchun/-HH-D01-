#include "clearchain_display_client.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "clearchain_display_protocol.h"
#include "common_def.h"
#include "sle_connection_manager.h"
#include "sle_device_discovery.h"
#include "sle_errcode.h"
#include "sle_ssap_client.h"
#include "soc_osal.h"
#include "systick.h"
#include "clearchain_ui.h"

#define CLEAR_DISPLAY_NAME "clear_main"
#define CLEAR_DISPLAY_NAME_COMPLETE 0x0B
#define CLEAR_DISPLAY_NAME_SHORTENED 0x0A
#define CLEAR_DISPLAY_SERVICE_UUID 0x2222
#define CLEAR_DISPLAY_PROPERTY_UUID 0x2323
#define CLEAR_DISPLAY_RX_QUEUE_DEPTH 8

static clearchain_display_state_t g_latest = {
    .stage=0, .result=CLEARCHAIN_DISPLAY_RESULT_UNKNOWN, .risk_score=CLEARCHAIN_RISK_SCORE_UNKNOWN
};
static volatile bool g_has_sequence;
static uint32_t g_last_sequence;
static uint64_t g_last_received;
static volatile bool g_ever_connected;
static volatile uint32_t g_clearchain_display_client_epoch;
static volatile bool g_clearchain_display_client_connected;
static volatile bool g_clearchain_display_client_ready;
static bool g_clearchain_display_target_found;
static bool g_clearchain_display_service_found;
static uint16_t g_clearchain_display_service_start;
static uint16_t g_clearchain_display_service_end;
static uint8_t g_clearchain_display_find_type;
static uint16_t g_clearchain_display_client_conn_id;
static uint16_t g_clearchain_display_client_property_handle;
static sle_addr_t g_clearchain_display_remote_addr;
static bool g_clearchain_display_pair_cleanup_attempted;
static bool g_clearchain_display_pair_cleanup_pending;
static uint64_t g_clearchain_display_pair_cleanup_started;
static volatile bool g_clearchain_display_scan_retry_pending;
static uint64_t g_clearchain_display_scan_retry_at;
static sle_announce_seek_callbacks_t g_clearchain_display_seek_callbacks;
static sle_connection_callbacks_t g_clearchain_display_connection_callbacks;
static ssapc_callbacks_t g_clearchain_display_ssap_callbacks;

/* One bounded line per discovery result; never wait inside an SLE callback. */
static void clearchain_display_log_uuid(const char *kind, const sle_uuid_t *uuid)
{
    static const char hex[] = "0123456789abcdef";
    char text[sizeof(uuid->uuid) * 2U + 1U];
    for (size_t i = 0; i < sizeof(uuid->uuid); i++) {
        text[i * 2U] = hex[uuid->uuid[i] >> 4];
        text[i * 2U + 1U] = hex[uuid->uuid[i] & 0x0F];
    }
    text[sizeof(text) - 1U] = '\0';
    osal_printk("[CLEAR SLE DIAG] %s uuid_len=%u uuid=%s\r\n", kind, (unsigned int)uuid->len, text);
}

static bool clearchain_display_uuid_matches(const sle_uuid_t *uuid, uint16_t value)
{
    uint8_t low = (uint8_t)value;
    uint8_t high = (uint8_t)(value >> 8);
    if (uuid == NULL || uuid->len != 2) {
        return false;
    }
    return (uuid->uuid[14] == low && uuid->uuid[15] == high) ||
           (uuid->uuid[0] == low && uuid->uuid[1] == high);
}

static bool clearchain_display_has_name(const uint8_t *data, uint8_t data_length)
{
    size_t offset = 0;
    const size_t name_length = sizeof(CLEAR_DISPLAY_NAME) - 1;
    if (data == NULL) {
        return false;
    }
    while (offset < data_length) {
        uint8_t field_length = data[offset];
        if (field_length == 0) {
            break;
        }
        if (field_length > (size_t)data_length - offset - 1) {
            return false;
        }
        if ((data[offset + 1] == CLEAR_DISPLAY_NAME_COMPLETE ||
             data[offset + 1] == CLEAR_DISPLAY_NAME_SHORTENED) &&
            (size_t)field_length - 1 == name_length &&
            memcmp(&data[offset + 2], CLEAR_DISPLAY_NAME, name_length) == 0) {
            return true;
        }
        offset += (size_t)field_length + 1;
    }
    return false;
}

static void clearchain_display_start_scan(void)
{
    sle_seek_param_t params = {0};
    params.own_addr_type = 0;
    params.seek_phys = 1;
    params.seek_type[0] = 1;
    params.seek_interval[0] = 100;
    params.seek_window[0] = 100;
    g_clearchain_display_scan_retry_pending = false;
    g_clearchain_display_target_found = false;
    if (sle_set_seek_param(&params) != ERRCODE_SLE_SUCCESS ||
        sle_start_seek() != ERRCODE_SLE_SUCCESS) {
        osal_printk("[CLEAR SLE] start scan failed\r\n");
        return;
    }
    osal_printk("[CLEAR SLE] scanning clear_main\r\n");
}

static void clearchain_display_sle_enabled(errcode_t status)
{
    if (status == ERRCODE_SLE_SUCCESS) {
        clearchain_display_start_scan();
    } else {
        osal_printk("[CLEAR SLE] client enable failed: 0x%x\r\n", status);
    }
}

static void clearchain_display_seek_result(sle_seek_result_info_t *result)
{
    if (result == NULL || g_clearchain_display_target_found ||
        !clearchain_display_has_name(result->data, result->data_length)) {
        return;
    }
    g_clearchain_display_remote_addr = result->addr;
    g_clearchain_display_target_found = true;
    if (sle_stop_seek() != ERRCODE_SLE_SUCCESS) {
        g_clearchain_display_target_found = false;
        osal_printk("[CLEAR SLE] stop scan failed\r\n");
    }
}

static bool clearchain_display_is_target(const sle_addr_t *addr)
{
    return addr != NULL && addr->type == g_clearchain_display_remote_addr.type &&
        memcmp(addr->addr, g_clearchain_display_remote_addr.addr, SLE_ADDR_LEN) == 0;
}

static void clearchain_display_retry_scan(void)
{
    g_clearchain_display_scan_retry_at = uapi_systick_get_ms() + 2000U;
    g_clearchain_display_scan_retry_pending = true;
}

static void clearchain_display_connect_target(void)
{
    uint8_t pair_state = 0;
    errcode_t query_status = sle_get_pair_state(&g_clearchain_display_remote_addr, &pair_state);
    errcode_t status = sle_connect_remote_device(&g_clearchain_display_remote_addr);
    osal_printk("[CLEAR SLE DIAG] connect request status=0x%x local_pair=%u query=0x%x\r\n",
                status, pair_state, query_status);
    if (status != ERRCODE_SLE_SUCCESS) {
        clearchain_display_retry_scan();
    }
}

static void clearchain_display_pair_removed(const sle_addr_t *addr, errcode_t status)
{
    if (!g_clearchain_display_pair_cleanup_pending || !clearchain_display_is_target(addr)) {
        return;
    }
    g_clearchain_display_pair_cleanup_pending = false;
    osal_printk("[CLEAR SLE DIAG] client peer removal complete status=0x%x\r\n", status);
    /* The API accepts a request asynchronously. Do not connect until it completes. */
    clearchain_display_connect_target();
}

static void clearchain_display_seek_stopped(errcode_t status)
{
    if (status != ERRCODE_SLE_SUCCESS || !g_clearchain_display_target_found) {
        return;
    }
    if (!g_clearchain_display_pair_cleanup_attempted) {
        uint8_t pair_state = 0;
        errcode_t query_status = sle_get_pair_state(&g_clearchain_display_remote_addr, &pair_state);
        g_clearchain_display_pair_cleanup_attempted = true;
        g_clearchain_display_pair_cleanup_pending = true;
        g_clearchain_display_pair_cleanup_started = uapi_systick_get_ms();
        errcode_t cleanup_status = sle_remove_paired_remote_device(&g_clearchain_display_remote_addr);
        osal_printk("[CLEAR SLE DIAG] client peer cleanup request status=0x%x local_pair=%u query=0x%x\r\n",
                    cleanup_status, pair_state, query_status);
        if (cleanup_status == ERRCODE_SLE_SUCCESS) {
            return;
        }
        g_clearchain_display_pair_cleanup_pending = false;
    }
    clearchain_display_connect_target();
}

static void clearchain_display_exchange_info(void)
{
    ssap_exchange_info_t info = {0};
    info.mtu_size = CONFIG_CLEARCHAIN_DISPLAY_CLIENT_MTU_SIZE;
    info.version = 1;
    if (ssapc_exchange_info_req(0, g_clearchain_display_client_conn_id, &info) != ERRCODE_SLE_SUCCESS) {
        osal_printk("[CLEAR SLE] MTU exchange request failed\r\n");
    }
}

static void clearchain_display_connection_changed(uint16_t conn_id, const sle_addr_t *addr,
    sle_acb_state_t state, sle_pair_state_t pair_state, sle_disc_reason_t reason)
{
    osal_printk("[CLEAR SLE DIAG] client link conn=%u state=%u pair_state=%u reason=0x%x\r\n",
                conn_id, state, pair_state, reason);
    if (state == SLE_ACB_STATE_CONNECTED) {
        if (!clearchain_display_is_target(addr)) {
            return;
        }
        g_clearchain_display_scan_retry_pending = false;
        g_clearchain_display_client_conn_id = conn_id;
        g_clearchain_display_client_connected = true;
        g_ever_connected = true;
        g_has_sequence = false;
        g_clearchain_display_client_ready = false;
        g_clearchain_display_service_found = false;
        g_clearchain_display_service_start = 0;
        g_clearchain_display_service_end = 0;
        g_clearchain_display_find_type = 0;
        g_clearchain_display_client_property_handle = 0;
        osal_printk("[CLEAR SLE] connected\r\n");
        if (pair_state == SLE_PAIR_NONE) {
            errcode_t pair_status = sle_pair_remote_device(addr);
            osal_printk("[CLEAR SLE DIAG] pairing request conn=%u status=0x%x\r\n", conn_id, pair_status);
        } else if (pair_state == SLE_PAIR_PAIRED) {
            clearchain_display_exchange_info();
        }
    } else if (state == SLE_ACB_STATE_DISCONNECTED) {
        if (!clearchain_display_is_target(addr)) {
            return;
        }
        g_clearchain_display_client_connected = false;
        g_has_sequence = false;
        g_clearchain_display_client_ready = false;
        g_clearchain_display_service_found = false;
        g_clearchain_display_service_start = 0;
        g_clearchain_display_service_end = 0;
        g_clearchain_display_find_type = 0;
        g_clearchain_display_client_property_handle = 0;
        g_clearchain_display_client_epoch++;
        osal_printk("[CLEAR SLE] disconnected, retry scan in 2 seconds\r\n");
        clearchain_display_retry_scan();
    }
}

static void clearchain_display_pair_complete(uint16_t conn_id, const sle_addr_t *addr, errcode_t status)
{
    osal_printk("[CLEAR SLE DIAG] pairing complete conn=%u status=0x%x connected=%u\r\n",
                conn_id, status, g_clearchain_display_client_connected);
    if (!clearchain_display_is_target(addr) || conn_id != g_clearchain_display_client_conn_id ||
        !g_clearchain_display_client_connected) {
        return;
    }
    if (status == ERRCODE_SLE_SUCCESS) {
        clearchain_display_exchange_info();
    } else {
        osal_printk("[CLEAR SLE] pairing failed: 0x%x\r\n", status);
    }
}

static void clearchain_display_auth_complete(uint16_t conn_id, const sle_addr_t *addr, errcode_t status,
                                            const sle_auth_info_evt_t *evt)
{
    unused(addr);
    unused(evt); /* Do not print authentication keys. */
    osal_printk("[CLEAR SLE DIAG] authentication complete conn=%u status=0x%x\r\n", conn_id, status);
}

static void clearchain_display_info_exchanged(uint8_t client_id, uint16_t conn_id,
                                              ssap_exchange_info_t *info, errcode_t status)
{
    ssapc_find_structure_param_t params = {0};
    unused(client_id);
    unused(info);
    if (status != ERRCODE_SLE_SUCCESS || conn_id != g_clearchain_display_client_conn_id) {
        osal_printk("[CLEAR SLE] MTU exchange failed: 0x%x\r\n", status);
        return;
    }
    params.type = SSAP_FIND_TYPE_PRIMARY_SERVICE;
    params.start_hdl = 1;
    params.end_hdl = 0xFFFF;
    g_clearchain_display_find_type = params.type;
    errcode_t discovery_status = ssapc_find_structure(0, conn_id, &params);
    osal_printk("[CLEAR SLE DIAG] discovery request conn=%u type=%u status=0x%x\r\n",
                conn_id, params.type, discovery_status);
    if (discovery_status != ERRCODE_SLE_SUCCESS) {
        g_clearchain_display_find_type = 0;
        osal_printk("[CLEAR SLE] service discovery request failed\r\n");
    }
}

static void clearchain_display_service_found(uint8_t client_id, uint16_t conn_id,
                                              ssapc_find_service_result_t *service, errcode_t status)
{
    unused(client_id);
    osal_printk("[CLEAR SLE DIAG] service callback conn=%u status=0x%x present=%u\r\n",
                conn_id, status, (unsigned int)(service != NULL));
    if (status == ERRCODE_SLE_SUCCESS && service != NULL) {
        clearchain_display_log_uuid("service", &service->uuid);
        osal_printk("[CLEAR SLE DIAG] service range=%u..%u match=%u\r\n",
                    service->start_hdl, service->end_hdl,
                    (unsigned int)clearchain_display_uuid_matches(&service->uuid, CLEAR_DISPLAY_SERVICE_UUID));
    }
    if (status == ERRCODE_SLE_SUCCESS && conn_id == g_clearchain_display_client_conn_id &&
        g_clearchain_display_find_type == SSAP_FIND_TYPE_PRIMARY_SERVICE && service != NULL &&
        service->start_hdl != 0 && service->start_hdl <= service->end_hdl &&
        clearchain_display_uuid_matches(&service->uuid, CLEAR_DISPLAY_SERVICE_UUID)) {
        g_clearchain_display_service_found = true;
        g_clearchain_display_service_start = service->start_hdl;
        g_clearchain_display_service_end = service->end_hdl;
    }
}

static void clearchain_display_property_found(uint8_t client_id, uint16_t conn_id,
                                               ssapc_find_property_result_t *property, errcode_t status)
{
    unused(client_id);
    osal_printk("[CLEAR SLE DIAG] property callback conn=%u status=0x%x present=%u\r\n",
                conn_id, status, (unsigned int)(property != NULL));
    if (status == ERRCODE_SLE_SUCCESS && property != NULL) {
        clearchain_display_log_uuid("property", &property->uuid);
        osal_printk("[CLEAR SLE DIAG] property handle=%u operations=0x%x match=%u notify=%u\r\n",
                    property->handle, (unsigned int)property->operate_indication,
                    (unsigned int)clearchain_display_uuid_matches(&property->uuid, CLEAR_DISPLAY_PROPERTY_UUID),
                    (unsigned int)((property->operate_indication & SSAP_OPERATE_INDICATION_BIT_NOTIFY) != 0));
    }
    if (status == ERRCODE_SLE_SUCCESS && conn_id == g_clearchain_display_client_conn_id &&
        g_clearchain_display_find_type == SSAP_FIND_TYPE_PROPERTY &&
        property != NULL && clearchain_display_uuid_matches(&property->uuid, CLEAR_DISPLAY_PROPERTY_UUID) &&
        property->handle >= g_clearchain_display_service_start &&
        property->handle <= g_clearchain_display_service_end &&
        (property->operate_indication & SSAP_OPERATE_INDICATION_BIT_NOTIFY) != 0) {
        g_clearchain_display_client_property_handle = property->handle;
    }
}

static void clearchain_display_discovery_complete(uint8_t client_id, uint16_t conn_id,
                                                   ssapc_find_structure_result_t *result, errcode_t status)
{
    unused(client_id);
    osal_printk("[CLEAR SLE DIAG] discovery complete conn=%u status=0x%x type=%u service_match=%u property_handle=%u\r\n",
                conn_id, status, result != NULL ? (unsigned int)result->type : 0xFFU,
                (unsigned int)g_clearchain_display_service_found, g_clearchain_display_client_property_handle);
    if (conn_id != g_clearchain_display_client_conn_id || !g_clearchain_display_client_connected ||
        result == NULL || status != ERRCODE_SLE_SUCCESS || result->type != g_clearchain_display_find_type) {
        osal_printk("[CLEAR SLE] discovery failed or stale\r\n");
        return;
    }
    if (result->type == SSAP_FIND_TYPE_PRIMARY_SERVICE) {
        if (!g_clearchain_display_service_found) {
            osal_printk("[CLEAR SLE] display service not found\r\n");
            return;
        }
        ssapc_find_structure_param_t params = {0};
        params.type = SSAP_FIND_TYPE_PROPERTY;
        params.start_hdl = g_clearchain_display_service_start;
        params.end_hdl = g_clearchain_display_service_end;
        g_clearchain_display_find_type = params.type;
        errcode_t discovery_status = ssapc_find_structure(0, conn_id, &params);
        osal_printk("[CLEAR SLE DIAG] property request conn=%u range=%u..%u status=0x%x\r\n",
                    conn_id, params.start_hdl, params.end_hdl, discovery_status);
        if (discovery_status != ERRCODE_SLE_SUCCESS) {
            g_clearchain_display_find_type = 0;
            osal_printk("[CLEAR SLE] property discovery request failed\r\n");
        }
        return;
    }
    if (result->type == SSAP_FIND_TYPE_PROPERTY && g_clearchain_display_service_found &&
        g_clearchain_display_client_property_handle != 0) {
        g_clearchain_display_client_ready = true;
        osal_printk("[CLEAR SLE] service discovery complete\r\n");
        static uint8_t ready[] = {'R', CLEARCHAIN_DISPLAY_PROTOCOL_VERSION};
        ssapc_write_param_t request = {0};
        request.handle = g_clearchain_display_client_property_handle;
        request.type = SSAP_PROPERTY_TYPE_VALUE;
        request.data = ready; request.data_len = sizeof(ready);
        errcode_t write_status = ssapc_write_cmd(0, conn_id, &request);
        osal_printk("[CLEAR SLE DIAG] ready write handle=%u status=0x%x\r\n", request.handle, write_status);
    } else {
        osal_printk("[CLEAR SLE] display property not found\r\n");
    }
}

static void clearchain_display_notification(uint8_t client_id, uint16_t conn_id,
                                            ssapc_handle_value_t *value, errcode_t status)
{
    clearchain_display_state_t state;
    uint32_t sequence;
    uint8_t command;
    unused(client_id);
    if (status != ERRCODE_SLE_SUCCESS || !g_clearchain_display_client_ready || value == NULL ||
        value->handle != g_clearchain_display_client_property_handle ||
        conn_id != g_clearchain_display_client_conn_id ||
        clearchain_display_decode(value->data,value->data_len,&command,&sequence,&state) != 0) { return; }
    unsigned int irq = osal_irq_lock();
    if (g_has_sequence && !clearchain_display_sequence_newer(sequence,g_last_sequence)) {
        osal_irq_restore(irq); return;
    }
    g_latest=state; g_last_sequence=sequence; g_has_sequence=true;
    g_last_received=uapi_systick_get_ms();
    osal_irq_restore(irq);
    if (command!=CLEARCHAIN_DISPLAY_CMD_HEARTBEAT) {
        osal_printk("[CLEAR DISPLAY] seq=%u cmd=%u stage=%u phase=%u percent=%u tags=%u samples=%u error=%u\r\n",
                    sequence,command,state.stage,state.phase,state.percent,state.tag_count,state.total_samples,state.error);
    }
}

void *clearchain_display_client_run(void *arg)
{
    errcode_t status;
    uint64_t last_ui_tick = uapi_systick_get_ms();
    unused(arg);

    osal_printk("[CLEAR SLE DIAG] client peer-cleanup-two-stage-20261005-v3\r\n");
    bool lcd_ready = clearchain_ui_init() == 0;
    if (!lcd_ready) { osal_printk("[CLEAR LCD] init failed; SLE diagnostics continue\r\n"); }
    clearchain_ui_render(&g_latest,CLEARCHAIN_LCD_CONNECTING,false);
    (void)osal_msleep(5000);
    g_clearchain_display_seek_callbacks.sle_enable_cb = clearchain_display_sle_enabled;
    g_clearchain_display_seek_callbacks.seek_result_cb = clearchain_display_seek_result;
    g_clearchain_display_seek_callbacks.seek_disable_cb = clearchain_display_seek_stopped;
    g_clearchain_display_connection_callbacks.connect_state_changed_cb = clearchain_display_connection_changed;
    g_clearchain_display_connection_callbacks.pair_complete_cb = clearchain_display_pair_complete;
    g_clearchain_display_connection_callbacks.pair_remove_cb = clearchain_display_pair_removed;
    g_clearchain_display_connection_callbacks.auth_complete_cb = clearchain_display_auth_complete;
    g_clearchain_display_ssap_callbacks.exchange_info_cb = clearchain_display_info_exchanged;
    g_clearchain_display_ssap_callbacks.find_structure_cb = clearchain_display_service_found;
    g_clearchain_display_ssap_callbacks.ssapc_find_property_cbk = clearchain_display_property_found;
    g_clearchain_display_ssap_callbacks.find_structure_cmp_cb = clearchain_display_discovery_complete;
    g_clearchain_display_ssap_callbacks.notification_cb = clearchain_display_notification;

    status = sle_announce_seek_register_callbacks(&g_clearchain_display_seek_callbacks);
    if (status == ERRCODE_SLE_SUCCESS) {
        status = sle_connection_register_callbacks(&g_clearchain_display_connection_callbacks);
    }
    if (status == ERRCODE_SLE_SUCCESS) {
        status = ssapc_register_callbacks(&g_clearchain_display_ssap_callbacks);
    }
    if (status == ERRCODE_SLE_SUCCESS) {
        status = enable_sle();
    }
    if (status != ERRCODE_SLE_SUCCESS) {
        osal_printk("[CLEAR SLE] client startup failed: 0x%x\r\n", status);
        return NULL;
    }
    while (1) {
        uint64_t retry_now = uapi_systick_get_ms();
        if (g_clearchain_display_pair_cleanup_pending &&
            retry_now - g_clearchain_display_pair_cleanup_started >= 5000U) {
            g_clearchain_display_pair_cleanup_pending = false;
            osal_printk("[CLEAR SLE DIAG] client peer removal timeout; reset board to retry\r\n");
        }
        if (g_clearchain_display_scan_retry_pending && !g_clearchain_display_client_connected &&
            !g_clearchain_display_pair_cleanup_pending && retry_now >= g_clearchain_display_scan_retry_at) {
            clearchain_display_start_scan();
        }
        unsigned int irq=osal_irq_lock();
        clearchain_display_state_t state=g_latest;
        bool connected=g_clearchain_display_client_connected;
        bool fresh=connected && g_has_sequence && uapi_systick_get_ms()-g_last_received < 3500U;
        clearchain_lcd_connection_t connection=connected ?
            (fresh?CLEARCHAIN_LCD_CONNECTED:(g_has_sequence?CLEARCHAIN_LCD_STALE:CLEARCHAIN_LCD_CONNECTING)) :
            (g_ever_connected?CLEARCHAIN_LCD_DISCONNECTED:CLEARCHAIN_LCD_CONNECTING);
        osal_irq_restore(irq);
        if (lcd_ready) {
            clearchain_ui_render(&state,connection,fresh);
            uint64_t now = uapi_systick_get_ms();
            clearchain_ui_tick((unsigned int)(now - last_ui_tick));
            last_ui_tick = now;
        }
        osal_msleep(50);
    }
}
