/* Exercise the actual firmware owner, keys, posts and raw reader tick. */
#include <assert.h>
#include <stdarg.h>
#include "../clearchain_device_app.c"
enum {
    R200_TEST_COMMAND_INDEX = 2,
    R200_TEST_POWER_HIGH_INDEX = 5,
    R200_TEST_POWER_LOW_INDEX = 6,
    R200_TEST_POWER_BYTE_SHIFT = 8,
    R200_TEST_REPLY_CHECKSUM_OFFSET = 2,
    R200_TEST_REPLY_FIRST_CHECKSUM_BYTE = 1,
    POWER_BELOW_TARGET_CDBM = 1800,
    POWER_TARGET_CDBM = 2000,
    POWER_ABOVE_TARGET_CDBM = 2200
};
static uint64_t clock_ms;
static clearchain_device_state_t server;
static int key,posts,stop_commands,select_commands,fail_post;
static unsigned int fake_reads,uart_commands;
static uint16_t simulated_power_cdbm=POWER_BELOW_TARGET_CDBM;
static uint16_t requested_power_cdbm;
static unsigned int power_set_commands;
static int pending_power_command, power_reply_timeout;
static osal_task task;
uint64_t uapi_systick_get_ms(void) { return clock_ms; }
int osal_msleep(unsigned int ms) { clock_ms+=ms; return 0; }
int osal_printk(const char *f,...) { (void)f;return 0; }
osal_task *osal_kthread_create(osal_kthread_handler f,void *a,const char *n,uint32_t s)
{ (void)f;(void)a;(void)n;(void)s;return &task; }
void osal_kfree(void *p) { (void)p; }
void clearchain_feedback_init(void) { }
void clearchain_feedback_set_status(int n) { assert(n>=0 && n<=3); }
void clearchain_key_set_remote_mode(clearchain_mode_t m) { (void)m; }
void clearchain_key_start(void) { }
int clearchain_key_take_event(clearchain_key_event_t *e,clearchain_key_availability_t *a)
{ if (!key) { return 0; } *e=(clearchain_key_event_t)key;*a=0;key=0;return 1; }
errcode_t clearchain_tca9555_probe(void) { return 0; }
errcode_t wifi_connectTo_AP(const char *s,const char *p) { (void)s;(void)p;return 0; }
int wifi_station_connected(void) { return 1; }
int r200_reader_init(void) { return 0; }
void r200_uart_flush(void) { fake_reads=0; }
int r200_uart_write(const uint8_t *p,size_t n)
{
    if (p[R200_TEST_COMMAND_INDEX]==R200_GET_POWER_COMMAND) {
        assert(n==7);
        pending_power_command=R200_GET_POWER_COMMAND;
        return 0;
    }
    if (p[R200_TEST_COMMAND_INDEX]==R200_SET_POWER_COMMAND) {
        assert(n==9);
        pending_power_command=R200_SET_POWER_COMMAND;
        requested_power_cdbm=(uint16_t)(((uint16_t)p[R200_TEST_POWER_HIGH_INDEX]<<
                                         R200_TEST_POWER_BYTE_SHIFT)|p[R200_TEST_POWER_LOW_INDEX]);
        power_set_commands++;
        return 0;
    }
    assert(n==7 && p[2]==0x22);
    uart_commands++;
    return 0;
}
int r200_uart_wait_frame(uint8_t *p,size_t cap,size_t *n,uint32_t timeout)
{
    if (timeout==CC_R200_POWER_REPLY_TIMEOUT_MS) {
        if (power_reply_timeout) { return -1; }
        if (pending_power_command==R200_GET_POWER_COMMAND) {
            uint8_t reply[]={0xAA,0x01,R200_GET_POWER_COMMAND,0x00,0x02,
                             (uint8_t)(simulated_power_cdbm>>R200_TEST_POWER_BYTE_SHIFT),
                             (uint8_t)simulated_power_cdbm,0x00,0xDD};
            for (size_t i=R200_TEST_REPLY_FIRST_CHECKSUM_BYTE;
                 i<sizeof(reply)-R200_TEST_REPLY_CHECKSUM_OFFSET;i++) {
                reply[sizeof(reply)-R200_TEST_REPLY_CHECKSUM_OFFSET]=
                    (uint8_t)(reply[sizeof(reply)-R200_TEST_REPLY_CHECKSUM_OFFSET]+reply[i]);
            }
            assert(cap>=sizeof(reply));memcpy(p,reply,sizeof(reply));*n=sizeof(reply);
            pending_power_command=0;
            return 0;
        }
        assert(pending_power_command==R200_SET_POWER_COMMAND);
        uint8_t reply[]={0xAA,0x01,R200_SET_POWER_COMMAND,0x00,0x01,0x00,0xB8,0xDD};
        assert(cap>=sizeof(reply));memcpy(p,reply,sizeof(reply));*n=sizeof(reply);
        simulated_power_cdbm=requested_power_cdbm;
        pending_power_command=0;
        return 0;
    }
    assert(timeout==0);
    if (!fake_reads) { return -1; } --fake_reads;
    uint8_t frame[]={0xAA,2,0x22,0,7,0xCC,0,0,0x07,0x08,0,0,0,0xDD};
    for (unsigned int i=1;i<12;i++) { frame[12]=(uint8_t)(frame[12]+frame[i]); }
    assert(cap>=sizeof(frame)); memcpy(p,frame,sizeof(frame)); *n=sizeof(frame); return 0;
}
int clearchain_device_http_get_state(clearchain_device_state_t *s) { *s=server;return 200; }
int clearchain_device_http_command(const char *cmd,clearchain_device_mode_t mode,clearchain_device_state_t *s)
{
    if (!strcmp(cmd,"STOP_SCAN")) { stop_commands++;server.phase=CLEARCHAIN_PHASE_READY;server.armed=0; }
    else { assert(!strcmp(cmd,"SELECT_MODE"));select_commands++;server.mode=mode;server.phase=CLEARCHAIN_PHASE_READY;server.armed=1; }
    server.state_version++;*s=server;return 200;
}
int clearchain_device_http_post_scan(const clearchain_device_state_t *s,const clearchain_device_reading_t *r,
                                    size_t n,int final,clearchain_device_state_t *out)
{
    assert(s->armed);posts++;
    for (size_t i=0;i<n;i++) { assert(r[i].rssi_dbm==-52 && !strcmp(r[i].chip_uid,"0708")); }
    if (fail_post) { return fail_post; }
    server.phase=final?CLEARCHAIN_PHASE_DONE:CLEARCHAIN_PHASE_SCANNING;
    server.state_version++;*out=server;return 200;
}
static void reset(void)
{
    memset(&g_session,0,sizeof(g_session));memset(&server,0,sizeof(server));
    server.mode=CLEARCHAIN_DEVICE_MODE_S1;server.phase=CLEARCHAIN_PHASE_READY;
    server.armed=1;server.tags_expected=-1;server.state_version=100;
    strcpy(server.batch_id,"BATCH-TEST");g_offline=g_failures=key=posts=stop_commands=select_commands=fail_post=0;
    fake_reads=uart_commands=0;clock_ms=g_next_poll=g_next_inventory=0;
}
int main(void)
{
    assert(test_app_entry);reset();test_app_entry();
    assert(poll_state()==200 && cc_session_start(&g_session,0));
    fake_reads=2;reader_tick(0);assert(g_session.total_samples==2 && g_session.unique_count==1);
    assert(post_pending(0)==200 && g_session.active && g_session.state.phase==CLEARCHAIN_PHASE_SCANNING);
    fake_reads=1;reader_tick(20);assert(g_session.pending_count==1);
    server.armed=0;server.phase=CLEARCHAIN_PHASE_READY;server.state_version++;
    poll_state();assert(!g_session.active && !g_session.pending_count && !cc_session_start(&g_session,30));
    assert(post_pending(1)<0 && posts==1); /* Stop never emits final or stray samples. */
    server.armed=1;server.phase=CLEARCHAIN_PHASE_SCANNING;server.state_version++;
    poll_state();assert(cc_session_start(&g_session,40));fake_reads=1;reader_tick(40);
    key=CLEARCHAIN_KEY_STAGE_4;consume_keys();
    assert(stop_commands==1 && select_commands==1 && !g_session.pending_count);
    assert(g_session.state.mode==CLEARCHAIN_DEVICE_MODE_S4 && cc_session_start(&g_session,50));
    assert(post_pending(1)==200 && g_session.state.phase==CLEARCHAIN_PHASE_DONE);
    assert(!cc_session_start(&g_session,4000));poll_state();assert(!cc_session_start(&g_session,4500));
    /* A backend restart can LOWER its clock-based state version. */
    server.state_version=3;server.mode=CLEARCHAIN_DEVICE_MODE_NONE;server.phase=CLEARCHAIN_PHASE_IDLE;
    poll_state();assert(g_session.state.state_version==3 && g_session.state.mode==0);
    reset();poll_state();assert(cc_session_start(&g_session,0));
    fake_reads=1;reader_tick(0);fail_post=-1;assert(post_pending(0)<0);
    assert(!g_session.active && g_session.pending_count==0 && g_session.latched);
    poll_state();assert(!cc_session_start(&g_session,1000)); /* No ambiguous POST retry. */
    reset();server.mode=CLEARCHAIN_DEVICE_MODE_CP;server.batch_id[0]=0;
    poll_state();assert(cc_session_start(&g_session,0));
    assert(!cc_session_finished(&g_session,2999) && cc_session_finished(&g_session,3000));
    simulated_power_cdbm=POWER_BELOW_TARGET_CDBM;power_set_commands=0;power_reply_timeout=0;
    configure_r200_power();
    assert(simulated_power_cdbm==POWER_TARGET_CDBM && power_set_commands==1);
    simulated_power_cdbm=POWER_ABOVE_TARGET_CDBM;
    configure_r200_power();
    assert(simulated_power_cdbm==POWER_ABOVE_TARGET_CDBM && power_set_commands==1);
    power_reply_timeout=1;
    configure_r200_power();
    assert(simulated_power_cdbm==POWER_ABOVE_TARGET_CDBM && power_set_commands==1);
    puts("PASS: actual firmware Stop/disarm, key STOP-before-SELECT, raw repeated RSSI, empty final, DONE latch, lower restart version, no POST retry and CP");
    return 0;
}
