#include "app_init.h"
#include "clearchain_config.h"
#include "clearchain_device_http.h"
#include "clearchain_device_session.h"
#include "clearchain_display_link.h"
#include "clearchain_feedback.h"
#include "clearchain_buzzer.h"
#include "clearchain_key.h"
#include "clearchain_runtime_config.h"
#include "clearchain_tca9555.h"
#include "my_wifi_api.h"
#include "r200_reader.h"
#include "r200_uart.h"
#include "r200_protocol.h"
#include "soc_osal.h"
#include "systick.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

/* A single owner serializes HTTP and state. UART extraction never blocks. */
enum {
    CC_NO_WAIT_MS = 0,                   /* Check the UART ring without blocking the device loop. */
    CC_MAX_FRAMES_PER_TICK = 16,        /* Bound work so keys and SLE keep running. */
    CC_ONLINE_STATE_POLL_MS = 500,      /* Refresh real backend state twice per second. */
    CC_OFFLINE_STATE_POLL_MS = 2000,    /* Space out retries while the backend is unreachable. */
    CC_SINGLE_INVENTORY_INTERVAL_MS = 300, /* Interval used by the non-bench firmware. */
    CC_PENDING_POST_INTERVAL_MS = 1000, /* Upload collected readings once per second. */
    CC_R200_POWER_REPLY_TIMEOUT_MS = 800, /* Wait for each power-command response. */
    CC_R200_SAFE_TARGET_POWER_CDBM = 2000, /* 20 dBm is within both documented R200 variants. */
    CC_R200_CDBM_PER_DBM = 100 /* The protocol represents one dBm as 100 units. */
};
static int read_r200_power(uint16_t *power_cdbm)
{
    uint8_t command[R200_MAX_FRAME_SIZE];
    uint8_t response[R200_MAX_FRAME_SIZE];
    size_t command_length, response_length;
    if (r200_protocol_build_get_power(command, sizeof(command), &command_length) != 0) { return -1; }
    r200_uart_flush();
    if (r200_uart_write(command, command_length) != 0 ||
        r200_uart_wait_frame(response, sizeof(response), &response_length,
                             CC_R200_POWER_REPLY_TIMEOUT_MS) != 0) {
        return -1;
    }
    return r200_protocol_parse_get_power(response, response_length, power_cdbm);
}

static int set_r200_power(uint16_t power_cdbm)
{
    uint8_t command[R200_MAX_FRAME_SIZE];
    uint8_t response[R200_MAX_FRAME_SIZE];
    size_t command_length, response_length;
    if (r200_protocol_build_set_power(command, sizeof(command), &command_length,
                                      power_cdbm) != 0) { return -1; }
    r200_uart_flush();
    if (r200_uart_write(command, command_length) != 0 ||
        r200_uart_wait_frame(response, sizeof(response), &response_length,
                             CC_R200_POWER_REPLY_TIMEOUT_MS) != 0) {
        return -1;
    }
    return r200_protocol_parse_set_power(response, response_length);
}

static void configure_r200_power(void)
{
    uint16_t before_cdbm, after_cdbm;
    if (read_r200_power(&before_cdbm) != 0) {
        osal_printk("[R200 POWER] read failed; power unchanged\r\n");
        return;
    }
    osal_printk("[R200 POWER] before=%d.%02d dBm\r\n",
                 before_cdbm / CC_R200_CDBM_PER_DBM,
                 before_cdbm % CC_R200_CDBM_PER_DBM);
    if (before_cdbm >= CC_R200_SAFE_TARGET_POWER_CDBM) {
        osal_printk("[R200 POWER] already at or above 20 dBm; unchanged\r\n");
        return;
    }
    if (set_r200_power(CC_R200_SAFE_TARGET_POWER_CDBM) != 0 ||
        read_r200_power(&after_cdbm) != 0 ||
        after_cdbm != CC_R200_SAFE_TARGET_POWER_CDBM) {
        osal_printk("[R200 POWER] set or verification failed; actual power unknown\r\n");
        return;
    }
    osal_printk("[R200 POWER] verified=%d.%02d dBm\r\n",
                 after_cdbm / CC_R200_CDBM_PER_DBM,
                 after_cdbm % CC_R200_CDBM_PER_DBM);
}
static cc_device_session_t g_session;
static int g_offline, g_failures;
static uint64_t g_next_poll, g_next_inventory;
#if defined(CONFIG_CLEARCHAIN_DEVICE_BENCH)
/* The offline 20-tag trial uses one native multi-poll command per window. */
static bool g_multi_inventory_started;
static clearchain_device_state_t g_bench = {
    .mode=CLEARCHAIN_DEVICE_MODE_NONE, .phase=CLEARCHAIN_PHASE_IDLE,
    .tags_expected=-1, .progress_percent=-1, .mode_label="LOCAL BENCH",
    .message="LOCAL TEST - select S1-S5; real tags only"
};
static struct {
    unsigned int tx, stop_tx, rx, ack, epc, no_tag, rejected;
    uint32_t dropped_at_start, bad_at_start, errors_at_start;
} g_reader_diag;
static int bench_start_inventory(void)
{
    uint8_t bytes[R200_MAX_FRAME_SIZE]; size_t length;
    if (r200_protocol_build_multi_inventory(bytes,sizeof(bytes),&length,UINT16_MAX)!=0 ||
        r200_uart_write(bytes,length)!=0) {
        osal_printk("[R200 MULTI] start failed\r\n");
        return -1;
    }
    g_multi_inventory_started=true;
    g_reader_diag.tx++;
    osal_printk("[R200 MULTI] native multi-poll started rounds=65535\r\n");
    return 0;
}
static void bench_stop_inventory(void)
{
    uint8_t bytes[R200_MAX_FRAME_SIZE]; size_t length;
    if (!g_multi_inventory_started) { return; }
    g_multi_inventory_started=false;
    if (r200_protocol_build_stop_inventory(bytes,sizeof(bytes),&length)!=0 ||
        r200_uart_write(bytes,length)!=0) {
        osal_printk("[R200 MULTI] stop failed; check reader before next window\r\n");
    } else {
        g_reader_diag.stop_tx++;
        osal_printk("[R200 MULTI] native multi-poll stopped\r\n");
    }
}
static void bench_reader_summary(void)
{
    uint32_t dropped, bad, errors;
    r200_uart_get_diagnostics(&dropped, &bad, &errors);
    osal_printk("[R200 BENCH MULTI] tx=%u stop_tx=%u rx=%u ack=%u epc=%u no_tag=%u rejected=%u dropped=%u bad_frames=%u uart_errors=%u\r\n",
        g_reader_diag.tx, g_reader_diag.stop_tx, g_reader_diag.rx, g_reader_diag.ack,
        g_reader_diag.epc, g_reader_diag.no_tag,
        g_reader_diag.rejected, dropped-g_reader_diag.dropped_at_start,
        bad-g_reader_diag.bad_at_start, errors-g_reader_diag.errors_at_start);
}
static int query_state(clearchain_device_state_t *out) { *out=g_bench; return 200; }
static int command(const char *cmd, clearchain_device_mode_t mode, clearchain_device_state_t *out)
{
    if (!strcmp(cmd,"STOP_SCAN")) { g_bench.armed=0; g_bench.phase=CLEARCHAIN_PHASE_READY; }
    else {
        g_bench.mode=mode; g_bench.armed=1; g_bench.phase=CLEARCHAIN_PHASE_READY;
        g_bench.tags_read=0; g_bench.tags_expected=-1; g_bench.progress_percent=-1;
        (void)strcpy(g_bench.batch_id,"LOCAL-BENCH");
        (void)snprintf(g_bench.mode_label,sizeof(g_bench.mode_label),"S%u LOCAL TEST",(unsigned int)mode);
        (void)strcpy(g_bench.message,"LOCAL TEST - real tags; no backend verdict");
    }
    g_bench.state_version++; *out=g_bench; return 200;
}
#else
static int query_state(clearchain_device_state_t *out) { return clearchain_device_http_get_state(out); }
static int command(const char *cmd, clearchain_device_mode_t mode, clearchain_device_state_t *out)
{ return clearchain_device_http_command(cmd,mode,out); }
#endif
static void accept_state(const clearchain_device_state_t *state)
{
    if (cc_session_accept(&g_session,state)) {
        uint16_t local_samples = 0;
#if defined(CONFIG_CLEARCHAIN_DEVICE_BENCH)
        if (state->phase == CLEARCHAIN_PHASE_SCANNING ||
            state->phase == CLEARCHAIN_PHASE_DONE) {
            local_samples = g_session.total_samples > UINT16_MAX ?
                UINT16_MAX : (uint16_t)g_session.total_samples;
        }
#endif
#if defined(CONFIG_CLEARCHAIN_DEVICE_BENCH)
        if (!g_session.active) { bench_stop_inventory(); }
#endif
        clearchain_key_set_remote_mode((clearchain_mode_t)state->mode);
        (void)clearchain_display_publish_device_state(state, local_samples);
        clearchain_feedback_set_status(state->phase==CLEARCHAIN_PHASE_DONE ? (int)state->screen_status : 0);
        osal_printk("[DEVICE V2] state v%u mode=%s phase=%u armed=%u tags=%u direction=%u\r\n",
            (unsigned int)state->state_version,clearchain_device_mode_name(state->mode),
            (unsigned int)state->phase,(unsigned int)state->armed,
            (unsigned int)state->tags_read,(unsigned int)state->scan_direction);
        if (!g_session.active) { r200_uart_flush(); }
    }
}
static void handle_status(int status)
{
    if (status==200) {
        g_failures=0; g_offline=0; clearchain_display_set_backend_offline(0);
    } else {
        if (++g_failures>=3) {
            if (!g_offline) { osal_printk("[DEVICE V2] offline; scan suspended\r\n"); }
#if defined(CONFIG_CLEARCHAIN_DEVICE_BENCH)
            bench_stop_inventory();
#endif
            g_offline=1; cc_session_cancel(&g_session); g_session.latched=1;
            clearchain_display_set_backend_offline(1);
        }
        if (status==409) { g_next_poll=0; }
    }
}
static int poll_state(void)
{
    clearchain_device_state_t state;
    int status=query_state(&state);
    handle_status(status);
    if (status==200) { accept_state(&state); }
    g_next_poll=uapi_systick_get_ms()+(g_offline?CC_OFFLINE_STATE_POLL_MS:CC_ONLINE_STATE_POLL_MS);
    return status;
}
static void consume_keys(void)
{
    clearchain_key_event_t event;
    clearchain_key_availability_t availability;
    while (clearchain_key_take_event(&event,&availability)) {
        (void)availability;
        if (event<CLEARCHAIN_KEY_STAGE_1 || event>CLEARCHAIN_KEY_STAGE_5) { continue; }
        clearchain_device_mode_t mode=(clearchain_device_mode_t)
            (CLEARCHAIN_DEVICE_MODE_S1+event-CLEARCHAIN_KEY_STAGE_1);
        clearchain_device_state_t state;
        /* Discard before Stop/select, including a failed request. */
#if defined(CONFIG_CLEARCHAIN_DEVICE_BENCH)
        bench_stop_inventory();
#endif
        cc_session_cancel(&g_session); g_session.latched=1; r200_uart_flush();
        if (poll_state()!=200) { continue; }
        if (g_session.state.phase==CLEARCHAIN_PHASE_SCANNING) {
            osal_printk("[DEVICE V2] STOP_SCAN before SELECT_MODE %s\r\n",
                clearchain_device_mode_name(mode));
            int status=command("STOP_SCAN",CLEARCHAIN_DEVICE_MODE_NONE,&state);
            handle_status(status);
            if (status!=200) { osal_printk("[DEVICE V2] STOP failed HTTP %d\r\n",status); continue; }
            accept_state(&state);
        }
        osal_printk("[DEVICE V2] SELECT_MODE request %s\r\n",
            clearchain_device_mode_name(mode));
        int status=command("SELECT_MODE",mode,&state);
        handle_status(status);
        if (status==200) { accept_state(&state); g_session.latched=0; }
        else { osal_printk("[DEVICE V2] SELECT_MODE failed HTTP %d\r\n",status); g_next_poll=0; }
    }
}
static int post_pending(int final)
{
    clearchain_device_reading_t readings[CC_SESSION_MAX_PENDING];
    clearchain_device_state_t returned;
    if (!g_session.active || !clearchain_device_state_can_scan(&g_session.state)) { return -1; }
    for (unsigned int i=0;i<g_session.pending_count;i++) {
        readings[i].chip_uid=g_session.pending[i].epc; readings[i].rssi_dbm=g_session.pending[i].rssi;
    }
#if defined(CONFIG_CLEARCHAIN_DEVICE_BENCH)
    (void)readings;
    g_bench.tags_read=(int)g_session.unique_count;
    g_bench.phase=final?CLEARCHAIN_PHASE_DONE:CLEARCHAIN_PHASE_SCANNING;
    g_bench.state_version++; returned=g_bench;
    int status=200;
#else
    int status=clearchain_device_http_post_scan(&g_session.state,readings,
                                               g_session.pending_count,final,&returned);
#endif
    g_session.pending_count=0;
    if (final || status!=200) {
#if defined(CONFIG_CLEARCHAIN_DEVICE_BENCH)
        bench_stop_inventory();
#endif
        cc_session_cancel(&g_session); g_session.latched=1; r200_uart_flush();
    }
    handle_status(status);
    if (status==200) {
        accept_state(&returned);
        if (final) {
            osal_printk("[DEVICE V2] final accepted unique=%u samples=%u\r\n",
                g_session.unique_count,g_session.total_samples);
#if defined(CONFIG_CLEARCHAIN_DEVICE_BENCH)
            bench_reader_summary();
#endif
        }
    } else {
        osal_printk("[DEVICE V2] POST failed HTTP %d; pending discarded, no retry\r\n",status);
        g_next_poll=0;
    }
    return status;
}
static void reader_tick(uint64_t now)
{
    uint8_t response[R200_MAX_FRAME_SIZE]; size_t length;
    char epc[CC_SESSION_EPC_SIZE]; int8_t rssi;
    for (unsigned int i=0;i<CC_MAX_FRAMES_PER_TICK && g_session.active;i++) {
        if (r200_uart_wait_frame(response,sizeof(response),&length,CC_NO_WAIT_MS)!=0) { break; }
#if defined(CONFIG_CLEARCHAIN_DEVICE_BENCH)
        g_reader_diag.rx++;
        if (response[1]==R200_RESPONSE_FRAME_TYPE &&
            (response[2]==R200_NATIVE_MULTI_COMMAND || response[2]==R200_NATIVE_STOP_COMMAND)) {
            g_reader_diag.ack++;
            continue;
        }
#endif
        int parsed=r200_protocol_parse_inventory(response,length,epc,sizeof(epc),&rssi);
        if (parsed!=0) {
#if defined(CONFIG_CLEARCHAIN_DEVICE_BENCH)
            if (parsed==R200_PARSE_NO_TAG) { g_reader_diag.no_tag++; }
            else { g_reader_diag.rejected++; }
#endif
            continue;
        }
#if defined(CONFIG_CLEARCHAIN_DEVICE_BENCH)
        g_reader_diag.epc++;
#endif
        if (g_session.pending_count==CC_SESSION_MAX_PENDING && post_pending(0)!=200) { return; }
        if (!g_session.active) { return; }
        if (cc_session_add(&g_session,epc,rssi)!=0) {
#if defined(CONFIG_CLEARCHAIN_DEVICE_BENCH)
            bench_stop_inventory();
#endif
            cc_session_cancel(&g_session); g_session.latched=1;
            (void)clearchain_display_show_error((uint8_t)g_session.state.mode,CLEARCHAIN_ERROR_CAPACITY);
            osal_printk("[DEVICE V2] reader capacity reached; scan suspended\r\n"); return;
        }
        osal_printk("[DEVICE V2] EPC=%s RSSI=%d dBm unique=%u samples=%u\r\n",
            epc,(int)rssi,g_session.unique_count,g_session.total_samples);
    }
#if defined(CONFIG_CLEARCHAIN_DEVICE_BENCH)
    (void)now;
#else
    if (g_session.active && now>=g_next_inventory) {
        uint8_t bytes[7]; size_t n;
        if (r200_protocol_build_inventory(bytes,sizeof(bytes),&n)==0 && r200_uart_write(bytes,n)==0) {
            g_next_inventory=now+CC_SINGLE_INVENTORY_INTERVAL_MS;
        } else {
            cc_session_cancel(&g_session); g_session.latched=1;
            (void)clearchain_display_show_error((uint8_t)g_session.state.mode,CLEARCHAIN_ERROR_READER);
        }
    }
#endif
}
static void *wifi_task(void *arg)
{
    (void)arg;
    while (1) {
        while (wifi_connectTo_AP(WIFI_SSID_NAME,WIFI_SSID_KEY)!=ERRCODE_SUCC) {
            osal_printk("[DEVICE V2] Wi-Fi connect failed; retry\r\n"); osal_msleep(2000);
        }
        osal_printk("[DEVICE V2] Wi-Fi ready\r\n");
        while (wifi_station_connected()) { osal_msleep(2000); }
        osal_printk("[DEVICE V2] Wi-Fi lost; reconnect\r\n");
    }
    return NULL;
}
static void *device_task(void *arg)
{
    (void)arg;
    osal_printk("[DEVICE V2] api-v2-armed-stop-20261006-v1\r\n");
#if defined(CONFIG_CLEARCHAIN_DEVICE_BENCH)
    osal_printk("[DEVICE V2] OFFLINE BENCH: native R200 multi-poll; real EPC/RSSI/counts; no backend verdict\r\n");
#else
    osal_printk("[DEVICE V2] HTTP enabled=%u destination=%s:%u\r\n",
        (unsigned int)CLEARCHAIN_UPLOAD_ALLOWED,CLEARCHAIN_DEVICE_HOST,(unsigned int)CLEARCHAIN_DEVICE_PORT);
#endif
    if (clearchain_tca9555_probe()!=ERRCODE_SUCC || r200_reader_init()!=0) {
        osal_printk("[DEVICE V2] peripheral init failed; task stops\r\n"); return NULL;
    }
    configure_r200_power();
    clearchain_feedback_init(); clearchain_key_start();
#if defined(CONFIG_CLEARCHAIN_DEVICE_BENCH)
    osal_printk("[BENCH] LED boot sequence red/green/yellow; buzzer test 250ms\r\n");
    clearchain_buzzer_beep(250);
#endif
    while (1) {
        uint64_t now=uapi_systick_get_ms();
        consume_keys();
        if (now>=g_next_poll) { (void)poll_state(); }
        if (!g_offline && cc_session_start(&g_session,uapi_systick_get_ms())) {
            g_next_inventory=0; r200_uart_flush();
#if defined(CONFIG_CLEARCHAIN_DEVICE_BENCH)
            (void)memset(&g_reader_diag,0,sizeof(g_reader_diag));
            r200_uart_get_diagnostics(&g_reader_diag.dropped_at_start,
                &g_reader_diag.bad_at_start,&g_reader_diag.errors_at_start);
            if (bench_start_inventory()!=0) {
                cc_session_cancel(&g_session); g_session.latched=1;
                (void)clearchain_display_show_error((uint8_t)g_session.state.mode,CLEARCHAIN_ERROR_READER);
                continue;
            }
#endif
            osal_printk("[DEVICE V2] capture begins mode=%s\r\n",clearchain_device_mode_name(g_session.state.mode));
        }
        if (g_session.active && !g_offline) {
            now=uapi_systick_get_ms(); reader_tick(now);
            if (cc_session_finished(&g_session,now)) {
#if defined(CONFIG_CLEARCHAIN_DEVICE_BENCH)
                bench_stop_inventory();
#endif
                (void)post_pending(1);
            }
            else if (g_session.active && g_session.pending_count &&
                     now-g_session.last_post_ms>=CC_PENDING_POST_INTERVAL_MS) {
                (void)post_pending(0); g_session.last_post_ms=uapi_systick_get_ms();
            }
        }
        osal_msleep(20);
    }
    return NULL;
}
static int start_task(osal_kthread_handler fn,const char *name,uint32_t stack)
{
    osal_task *task=osal_kthread_create(fn,NULL,name,stack);
    if (task==NULL) { return -1; }
    osal_kfree(task); return 0;
}
static void device_app_entry(void)
{
    clearchain_key_set_remote_mode(CLEARCHAIN_MODE_NONE);
    if (clearchain_display_link_init()!=0) { osal_printk("[DEVICE V2] SLE init failed\r\n"); }
#if !defined(CONFIG_CLEARCHAIN_DEVICE_BENCH)
    if (CLEARCHAIN_UPLOAD_ALLOWED && start_task((osal_kthread_handler)wifi_task,"CCWifi",0x2000)!=0) {
        osal_printk("[DEVICE V2] Wi-Fi task failed\r\n");
    }
#else
    (void)wifi_task;
#endif
    if (start_task((osal_kthread_handler)device_task,"CCDeviceV2",0x4000)!=0) {
        osal_printk("[DEVICE V2] task creation failed\r\n");
    }
}
app_run(device_app_entry);
