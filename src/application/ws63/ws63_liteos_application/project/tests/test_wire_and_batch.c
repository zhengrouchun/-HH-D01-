#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "clearchain_display_protocol.h"
#include "r200_protocol.h"
#include "r200_reader.h"
#include "r200_uart.h"

static uint64_t now;
static unsigned int seen, tags=11, dropped, errors, progress_calls, commands;
static int io_fail, empty, bad;
int osal_printk(const char *fmt, ...) { (void)fmt; return 0; }
int osal_msleep(unsigned int ms) { now+=ms; return 0; }
uint64_t uapi_systick_get_ms(void) { return now; }
int r200_uart_init(void) { return 0; }
void r200_uart_prepare_receive(void) { }
void r200_uart_flush(void) { }
int r200_uart_write(const uint8_t *p,size_t n) { assert(p && n>=7); commands++; return io_fail?-1:0; }
void r200_uart_get_diagnostics(uint32_t *d,uint32_t *b,uint32_t *e)
{ *d=dropped; *b=0; *e=errors; }
int r200_uart_wait_frame(uint8_t *out,size_t cap,size_t *n,uint32_t timeout)
{
    if (empty) { now+=timeout; return -1; }
    now+=timeout<20?timeout:20;
    /* RSSI, PC, 2-byte EPC, CRC: payload 7, total 14. */
    uint8_t frame[]={0xAA,0x02,0x22,0x00,0x07,0xCC,0x08,0x00,0xE2,0,0x00,0x00,0,0xDD};
    frame[9]=(uint8_t)(seen++%tags);
    uint8_t sum=0; for (unsigned int i=1;i<12;i++) { sum=(uint8_t)(sum+frame[i]); }
    frame[12]=bad?(uint8_t)(sum^1):sum;
    assert(cap>=sizeof(frame)); memcpy(out,frame,sizeof(frame)); *n=sizeof(frame);
    if (seen==2U && dropped==0xFFFFFFFFU) { dropped=3; }
    return 0;
}
static void progress(const r200_batch_t *batch,uint32_t elapsed,uint32_t window)
{ assert(batch->tag_count<=32U && elapsed<=window); progress_calls++; }

static void test_display(void)
{
    uint8_t packet[24], type; uint32_t seq; clearchain_display_state_t decoded;
    clearchain_display_state_t state={.stage=4,.phase=1,.percent=67,.tag_count=11,
        .total_samples=44,.result=3,.risk_score=255,.flags=3};
    for (uint8_t cmd=1;cmd<=8;cmd++) {
        assert(clearchain_display_encode(packet,sizeof(packet),cmd,0x12345678,&state)==24);
        assert(clearchain_display_decode(packet,24,&type,&seq,&decoded)==0);
        assert(type==cmd && seq==0x12345678 && decoded.total_samples==44 && decoded.risk_score==255);
        for (size_t n=0;n<24;n++) { assert(clearchain_display_decode(packet,n,&type,&seq,&decoded)<0); }
        for (unsigned int i=0;i<24;i++) for (unsigned int bit=0;bit<8;bit++) {
            packet[i]^=(uint8_t)(1U<<bit);
            assert(clearchain_display_decode(packet,24,&type,&seq,&decoded)<0);
            packet[i]^=(uint8_t)(1U<<bit);
        }
    }
    state.percent=101; assert(clearchain_display_encode(packet,24,1,0,&state)<0);
    state.percent=0; state.risk_score=101; assert(clearchain_display_encode(packet,24,1,0,&state)<0);
    assert(clearchain_display_sequence_newer(1,0xFFFFFFFF));
    assert(!clearchain_display_sequence_newer(7,7));
    assert(!clearchain_display_sequence_newer(6,7));
    assert(!clearchain_display_sequence_newer(0x80000000,0));
}
static void test_r200(void)
{
    uint8_t frame[128]; size_t n;
    assert(r200_protocol_build_inventory(frame,128,&n)==0 && n==7 && frame[2]==0x22);
    const uint8_t multi[]={0xAA,0,0x27,0,3,0x22,0x27,0x10,0x83,0xDD};
    assert(r200_protocol_build_multi_inventory(frame,128,&n,10000)==0 && n==10);
    assert(memcmp(frame,multi,10)==0);
    assert(r200_protocol_build_multi_inventory(frame,128,&n,0)<0);
    assert(r200_protocol_build_stop_inventory(frame,128,&n)==0 && n==7 && frame[5]==0x28);
    uint8_t golden[]={0xAA,0x02,0x22,0,0x11,0xC9,0x34,0,0x30,0x75,0x1F,0xEB,0x70,0x5C,0x59,0x04,0xE3,0xD5,0x0D,0x70,0x3A,0x76,0xEF,0xDD};
    char epc[65]; int8_t rssi;
    assert(r200_protocol_parse_inventory(golden,sizeof(golden),epc,sizeof(epc),&rssi)==0);
    assert(!strcmp(epc,"30751FEB705C5904E3D50D70") && rssi==-55);
    golden[22]^=1; assert(r200_protocol_parse_inventory(golden,sizeof(golden),epc,sizeof(epc),&rssi)<0);
    uint8_t no_tag[]={0xAA,1,0xFF,0,1,0x15,0x16,0xDD};
    assert(r200_protocol_parse_inventory(no_tag,8,epc,sizeof(epc),&rssi)==R200_PARSE_NO_TAG);
}
static void reset(void)
{ now=0; seen=0; tags=11; dropped=0; errors=0; progress_calls=0; io_fail=empty=bad=0; commands=0; }
static void test_batch(void)
{
    r200_batch_t batch;
    reset(); assert(r200_reader_read_batch_progress(&batch,2500,progress)==0);
    assert(now==2500 && batch.tag_count==11 && batch.total_samples==44 && progress_calls<=27);
    assert(batch.sample_limit_drops>0 && commands==1);
    reset(); empty=1; assert(r200_reader_read_batch(&batch,2500)==0 && batch.tag_count==0 && now==2500);
    reset(); io_fail=1; assert(r200_reader_read_batch(&batch,2500)<0 && now==0);
    reset(); tags=33; assert(r200_reader_read_batch(&batch,2500)<0 && batch.tag_count==32 && batch.capacity_drops>0);
    reset(); bad=1; assert(r200_reader_read_batch(&batch,2500)<0 && batch.tag_count==0 && batch.rejected_frames>0);
    assert(r200_reader_read_batch(NULL,2500)<0 && r200_reader_read_batch(&batch,0)<0);
}
int main(void)
{
    test_display(); test_r200(); test_batch();
    puts("PASS: wire boundaries/corruption/sequence; R200 manual vectors; batch dedup/RSSI/capacity/timeouts");
    return 0;
}
