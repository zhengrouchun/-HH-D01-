#ifndef TEST_LVGL_H
#define TEST_LVGL_H
#include <stdint.h>
#include <string.h>
typedef struct { char text[128]; int hidden, wrap; uint32_t color; } lv_obj_t;
typedef struct { int unused; } lv_display_t;
typedef struct { int x1,y1,x2,y2; } lv_area_t;
enum { LV_LABEL_LONG_DOT, LV_LABEL_LONG_WRAP, LV_OPA_COVER, LV_COLOR_FORMAT_RGB565,
       LV_DISPLAY_RENDER_MODE_PARTIAL, LV_OBJ_FLAG_HIDDEN, LV_ANIM_OFF };
extern lv_obj_t test_objects[32];
extern unsigned int test_object_count, test_label_writes;
extern uint32_t test_tick;
static inline void lv_init(void) { }
static inline lv_display_t *lv_display_create(int w,int h)
{ static lv_display_t d; (void)w;(void)h;return &d; }
static inline void lv_display_set_color_format(lv_display_t *d,int f) { (void)d;(void)f; }
static inline void lv_display_set_flush_cb(lv_display_t *d,void (*cb)(lv_display_t *,const lv_area_t *,uint8_t *))
{ (void)d;(void)cb; }
static inline void lv_display_set_buffers(lv_display_t *d,void *a,void *b,unsigned int n,int m)
{ (void)d;(void)a;(void)b;(void)n;(void)m; }
static inline lv_obj_t *lv_screen_active(void) { return &test_objects[0]; }
static inline lv_obj_t *lv_obj_create(lv_obj_t *p) { (void)p;return &test_objects[++test_object_count]; }
static inline lv_obj_t *lv_label_create(lv_obj_t *p) { return lv_obj_create(p); }
static inline lv_obj_t *lv_bar_create(lv_obj_t *p) { return lv_obj_create(p); }
static inline uint32_t lv_color_hex(uint32_t n) { return n; }
static inline void lv_obj_set_pos(lv_obj_t *o,int x,int y) { (void)o;(void)x;(void)y; }
static inline void lv_obj_set_width(lv_obj_t *o,int w) { (void)o;(void)w; }
static inline void lv_obj_set_size(lv_obj_t *o,int w,int h) { (void)o;(void)w;(void)h; }
static inline void lv_obj_set_style_radius(lv_obj_t *o,int n,int s) { (void)o;(void)n;(void)s; }
static inline void lv_obj_set_style_pad_all(lv_obj_t *o,int n,int s) { (void)o;(void)n;(void)s; }
static inline void lv_obj_set_style_bg_opa(lv_obj_t *o,int n,int s) { (void)o;(void)n;(void)s; }
static inline void lv_obj_set_style_bg_color(lv_obj_t *o,uint32_t c,int s) { (void)s;o->color=c; }
static inline void lv_obj_set_style_text_color(lv_obj_t *o,uint32_t c,int s) { (void)s;o->color=c; }
static inline void lv_obj_center(lv_obj_t *o) { (void)o; }
static inline void lv_label_set_long_mode(lv_obj_t *o,int m) { o->wrap=m; }
static inline void lv_label_set_text(lv_obj_t *o,const char *t)
{ ++test_label_writes;strncpy(o->text,t,sizeof(o->text)-1);o->text[sizeof(o->text)-1]='\0'; }
static inline void lv_bar_set_range(lv_obj_t *o,int a,int b) { (void)o;(void)a;(void)b; }
static inline void lv_bar_set_value(lv_obj_t *o,int n,int a) { (void)o;(void)n;(void)a; }
static inline void lv_obj_add_flag(lv_obj_t *o,int f) { (void)f;o->hidden=1; }
static inline void lv_obj_remove_flag(lv_obj_t *o,int f) { (void)f;o->hidden=0; }
static inline void lv_display_flush_ready(lv_display_t *d) { (void)d; }
static inline uint32_t lv_tick_get(void) { return test_tick; }
static inline void lv_tick_inc(unsigned int n) { test_tick+=n; }
static inline void lv_timer_handler(void) { }
#endif
