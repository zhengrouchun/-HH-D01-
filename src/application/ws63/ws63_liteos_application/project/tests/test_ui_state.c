#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include "lvgl.h"
lv_obj_t test_objects[32];
unsigned int test_object_count,test_label_writes;
uint32_t test_tick;
#include "../clearchain_ui.c"
int clearchain_lcd_init(void) { return 0; }
int clearchain_lcd_flush_rgb565(uint16_t x,uint16_t y,uint16_t w,uint16_t h,const uint8_t *p)
{ (void)x;(void)y;(void)w;(void)h;(void)p;return 0; }
int osal_printk(const char *fmt,...) { (void)fmt;return 0; }
int main(void)
{
    clearchain_display_state_t s={0};
    assert(clearchain_ui_init()==0);
    assert(g_message->wrap==LV_LABEL_LONG_WRAP);
    s.state_version=100;s.stage=1;s.phase=CLEARCHAIN_DISPLAY_FINISHED;
    s.tag_count=3;s.tags_expected=UINT16_MAX;s.result=CLEARCHAIN_DISPLAY_RESULT_APPROVED;
    s.color=1;s.risk_score=0;s.scan_direction=2;
    memset(s.message,'W',60);s.message[60]='\0';
    clearchain_ui_render(&s,CLEARCHAIN_LCD_CONNECTED,true);
    assert(!strcmp(g_status->text,"SCR_RESULT  OUT"));
    assert(!strcmp(g_message->text,s.message) && strlen(g_message->text)==60);
    assert(g_risk->color==0x22CC66);
    unsigned int writes=test_label_writes;
    clearchain_ui_render(&s,CLEARCHAIN_LCD_CONNECTED,true);assert(test_label_writes==writes);
    s.flags=CLEARCHAIN_DISPLAY_FLAG_BACKEND_OFFLINE;
    clearchain_ui_render(&s,CLEARCHAIN_LCD_CONNECTED,true);
    assert(strstr(g_status->text,"Server offline") && !g_count->text[0]);
    s.flags=0;
    clearchain_ui_render(&s,CLEARCHAIN_LCD_CONNECTED,true);
    assert(!strcmp(g_count->text,"Tags read: 3"));
    s.state_version=3;s.risk_score=CLEARCHAIN_RISK_SCORE_UNKNOWN;s.result=CLEARCHAIN_DISPLAY_RESULT_UNKNOWN;
    strcpy(s.message,"Backend returned no verdict");
    clearchain_ui_render(&s,CLEARCHAIN_LCD_CONNECTED,true);
    assert(!strcmp(g_risk->text,"RESULT UNKNOWN"));
    s.state_version++;strcpy(s.message,"LOCAL TEST - real tags; no backend verdict");
    clearchain_ui_render(&s,CLEARCHAIN_LCD_CONNECTED,true);
    assert(!strcmp(g_risk->text,"LOCAL READS - NO VERDICT"));
    s.state_version++;s.phase=CLEARCHAIN_DISPLAY_WAITING;s.armed=0;
    clearchain_ui_render(&s,CLEARCHAIN_LCD_CONNECTED,true);
    assert(strstr(g_status->text,"Waiting for Start"));
    clearchain_ui_render(&s,CLEARCHAIN_LCD_DISCONNECTED,false);
    assert(strstr(g_status->text,"SLE link unavailable"));
    puts("PASS: actual UI offline/recovery redraw with unchanged version, restart version, full 60-char message, OUT, disarmed READY and honest local/unknown verdicts");
    return 0;
}
