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

#define CLEAR_DISPLAY_NAME "clear_main"
#define CLEAR_DISPLAY_NAME_COMPLETE 0x0B
#define CLEAR_DISPLAY_NAME_SHORTENED 0x0A
#define CLEAR_DISPLAY_SERVICE_UUID 0x2222
#define CLEAR_DISPLAY_PROPERTY_UUID 0x2323
#define CLEAR_DISPLAY_RX_QUEUE_DEPTH 8

typedef struct {
    uint32_t epoch;
    uint8_t command;
    uint8_t first;
    uint8_t second;
} clearchain_display_rx_message_t;

static unsigned long g_clearchain_display_rx_queue;
static volatile uint32_t g_clearchain_display_client_epoch;
static volatile bool g_clearchain_display_client_connected;
static volatile bool g_clearchain_display_client_ready;
static bool g_clearchain_display_target_found;
static bool g_clearchain_display_service_found;
static uint16_t g_clearchain_display_client_conn_id;
static uint16_t g_clearchain_display_client_property_handle;
static sle_addr_t g_clearchain_display_remote_addr;
static sle_announce_seek_callbacks_t g_clearchain_display_seek_callbacks;
static sle_connection_callbacks_t g_clearchain_display_connection_callbacks;
static ssapc_callbacks_t g_clearchain_display_ssap_callbacks;

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

static void clearchain_display_seek_stopped(errcode_t status)
{
    if (status == ERRCODE_SLE_SUCCESS && g_clearchain_display_target_found &&
        sle_connect_remote_device(&g_clearchain_display_remote_addr) != ERRCODE_SLE_SUCCESS) {
        osal_printk("[CLEAR SLE] connect request failed, scanning again\r\n");
        clearchain_display_start_scan();
    }
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
    unused(addr);
    unused(reason);
    if (state == SLE_ACB_STATE_CONNECTED) {
        g_clearchain_display_client_conn_id = conn_id;
        g_clearchain_display_client_connected = true;
        g_clearchain_display_client_ready = false;
        g_clearchain_display_service_found = false;
        g_clearchain_display_client_property_handle = 0;
        osal_printk("[CLEAR SLE] connected\r\n");
        if (pair_state == SLE_PAIR_NONE) {
            if (sle_pair_remote_device(&g_clearchain_display_remote_addr) != ERRCODE_SLE_SUCCESS) {
                osal_printk("[CLEAR SLE] pairing request failed\r\n");
            }
        } else {
            clearchain_display_exchange_info();
        }
    } else if (state == SLE_ACB_STATE_DISCONNECTED) {
        g_clearchain_display_client_connected = false;
        g_clearchain_display_client_ready = false;
        g_clearchain_display_client_property_handle = 0;
        g_clearchain_display_client_epoch++;
        osal_printk("[CLEAR SLE] disconnected, scanning again\r\n");
        clearchain_display_start_scan();
    }
}

static void clearchain_display_pair_complete(uint16_t conn_id, const sle_addr_t *addr, errcode_t status)
{
    unused(addr);
    if (status == ERRCODE_SLE_SUCCESS && conn_id == g_clearchain_display_client_conn_id) {
        clearchain_display_exchange_info();
    } else {
        osal_printk("[CLEAR SLE] pairing failed: 0x%x\r\n", status);
    }
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
    params.type = SSAP_FIND_TYPE_PROPERTY;
    params.start_hdl = 1;
    params.end_hdl = 0xFFFF;
    if (ssapc_find_structure(0, conn_id, &params) != ERRCODE_SLE_SUCCESS) {
        osal_printk("[CLEAR SLE] service discovery request failed\r\n");
    }
}

static void clearchain_display_service_found(uint8_t client_id, uint16_t conn_id,
                                              ssapc_find_service_result_t *service, errcode_t status)
{
    unused(client_id);
    if (status == ERRCODE_SLE_SUCCESS && conn_id == g_clearchain_display_client_conn_id &&
        service != NULL && clearchain_display_uuid_matches(&service->uuid, CLEAR_DISPLAY_SERVICE_UUID)) {
        g_clearchain_display_service_found = true;
    }
}

static void clearchain_display_property_found(uint8_t client_id, uint16_t conn_id,
                                               ssapc_find_property_result_t *property, errcode_t status)
{
    unused(client_id);
    if (status == ERRCODE_SLE_SUCCESS && conn_id == g_clearchain_display_client_conn_id &&
        property != NULL && clearchain_display_uuid_matches(&property->uuid, CLEAR_DISPLAY_PROPERTY_UUID) &&
        (property->operate_indication & SSAP_OPERATE_INDICATION_BIT_NOTIFY) != 0) {
        g_clearchain_display_client_property_handle = property->handle;
    }
}

static void clearchain_display_discovery_complete(uint8_t client_id, uint16_t conn_id,
                                                   ssapc_find_structure_result_t *result, errcode_t status)
{
    unused(client_id);
    unused(result);
    if (status == ERRCODE_SLE_SUCCESS && conn_id == g_clearchain_display_client_conn_id &&
        g_clearchain_display_service_found && g_clearchain_display_client_property_handle != 0) {
        g_clearchain_display_client_ready = true;
        osal_printk("[CLEAR SLE] service discovery complete\r\n");
    } else {
        osal_printk("[CLEAR SLE] display property not found\r\n");
    }
}

static void clearchain_display_notification(uint8_t client_id, uint16_t conn_id,
                                            ssapc_handle_value_t *value, errcode_t status)
{
    clearchain_display_rx_message_t message = {0};
    const uint8_t *packet;
    uint8_t command;
    uint8_t payload_length;
    unused(client_id);

    if (status != ERRCODE_SLE_SUCCESS || !g_clearchain_display_client_ready || value == NULL ||
        value->data == NULL || value->handle != g_clearchain_display_client_property_handle ||
        conn_id != g_clearchain_display_client_conn_id || value->data_len < CLEARCHAIN_DISPLAY_HEADER_SIZE ||
        value->data_len > CLEARCHAIN_DISPLAY_MAX_PACKET_SIZE) {
        return;
    }
    packet = value->data;
    command = packet[1];
    payload_length = packet[2];
    if (packet[0] != CLEARCHAIN_DISPLAY_PROTOCOL_VERSION ||
        value->data_len != (uint16_t)(CLEARCHAIN_DISPLAY_HEADER_SIZE + payload_length)) {
        return;
    }
    if (command == CLEARCHAIN_DISPLAY_CMD_WAITING || command == CLEARCHAIN_DISPLAY_CMD_STAGE_CHANGED) {
        if (payload_length != 1 || packet[3] < CLEARCHAIN_DISPLAY_MIN_STAGE ||
            packet[3] > CLEARCHAIN_DISPLAY_MAX_STAGE) {
            return;
        }
    } else if (command == CLEARCHAIN_DISPLAY_CMD_SCAN_PROGRESS) {
        if (payload_length != 2 || packet[3] < CLEARCHAIN_DISPLAY_MIN_STAGE ||
            packet[3] > CLEARCHAIN_DISPLAY_MAX_STAGE || packet[4] > 100) {
            return;
        }
    } else if (command == CLEARCHAIN_DISPLAY_CMD_RESULT) {
        if (payload_length != 2 || packet[3] > CLEARCHAIN_DISPLAY_RESULT_UNKNOWN ||
            (packet[4] > 100 && packet[4] != CLEARCHAIN_RISK_SCORE_UNKNOWN)) {
            return;
        }
    } else {
        return;
    }

    message.epoch = g_clearchain_display_client_epoch;
    message.command = command;
    message.first = packet[3];
    message.second = payload_length == 2 ? packet[4] : 0;
    if (osal_msg_queue_write_copy(g_clearchain_display_rx_queue, &message, sizeof(message), 0) != OSAL_SUCCESS) {
        osal_printk("[CLEAR DISPLAY] receive queue full\r\n");
    }
}

static void clearchain_display_print(const clearchain_display_rx_message_t *message)
{
    static const char *const result_names[] = {"APPROVED", "MONITOR", "REJECT", "UNKNOWN"};
    if (message->epoch != g_clearchain_display_client_epoch || !g_clearchain_display_client_connected) {
        return;
    }
    switch (message->command) {
        case CLEARCHAIN_DISPLAY_CMD_WAITING:
            osal_printk("[CLEAR DISPLAY] cmd=WAITING stage=%u\r\n", message->first);
            break;
        case CLEARCHAIN_DISPLAY_CMD_STAGE_CHANGED:
            osal_printk("[CLEAR DISPLAY] cmd=STAGE_CHANGED stage=%u\r\n", message->first);
            break;
        case CLEARCHAIN_DISPLAY_CMD_SCAN_PROGRESS:
            osal_printk("[CLEAR DISPLAY] cmd=SCAN_PROGRESS stage=%u percent=%u\r\n",
                        message->first, message->second);
            break;
        case CLEARCHAIN_DISPLAY_CMD_RESULT:
            if (message->second == CLEARCHAIN_RISK_SCORE_UNKNOWN) {
                osal_printk("[CLEAR DISPLAY] cmd=RESULT result=%s risk=UNKNOWN\r\n",
                            result_names[message->first]);
            } else {
                osal_printk("[CLEAR DISPLAY] cmd=RESULT result=%s risk=%u\r\n",
                            result_names[message->first], message->second);
            }
            break;
        default:
            break;
    }
}

void *clearchain_display_client_run(void *arg)
{
    clearchain_display_rx_message_t message;
    unsigned int read_length;
    errcode_t status;
    unused(arg);

    if (osal_msg_queue_create("cc_disp_rx", CLEAR_DISPLAY_RX_QUEUE_DEPTH, &g_clearchain_display_rx_queue,
                              0, sizeof(message)) != OSAL_SUCCESS) {
        osal_printk("[CLEAR SLE] receive queue creation failed\r\n");
        return NULL;
    }
    (void)osal_msleep(5000);
    g_clearchain_display_seek_callbacks.sle_enable_cb = clearchain_display_sle_enabled;
    g_clearchain_display_seek_callbacks.seek_result_cb = clearchain_display_seek_result;
    g_clearchain_display_seek_callbacks.seek_disable_cb = clearchain_display_seek_stopped;
    g_clearchain_display_connection_callbacks.connect_state_changed_cb = clearchain_display_connection_changed;
    g_clearchain_display_connection_callbacks.pair_complete_cb = clearchain_display_pair_complete;
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
        (void)osal_msg_queue_delete(g_clearchain_display_rx_queue);
        return NULL;
    }
    while (1) {
        read_length = sizeof(message);
        if (osal_msg_queue_read_copy(g_clearchain_display_rx_queue, &message, &read_length,
                                     OSAL_WAIT_FOREVER) == OSAL_SUCCESS && read_length == sizeof(message)) {
            clearchain_display_print(&message);
        }
    }
}
