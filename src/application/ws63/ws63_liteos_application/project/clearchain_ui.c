#include "clearchain_ui.h"
#include "clearchain_lcd_config.h"
#include "lvgl.h"
#include "src/draw/sw/lv_draw_sw_utils.h"
#include "soc_osal.h"
#include <stdio.h>
#include <string.h>

/* 32 lines, 30,720 bytes; one partial RGB565 draw buffer. */
static uint8_t g_draw_buffer[CLEARCHAIN_LCD_WIDTH * 32U * 2U];
static lv_display_t *g_display;
static lv_obj_t *g_mode[6];
static lv_obj_t *g_status;
static lv_obj_t *g_count;
static lv_obj_t *g_message;
static lv_obj_t *g_progress;
static lv_obj_t *g_risk;
static uint32_t g_last_version;
static uint8_t g_last_mode = 255U;
static clearchain_lcd_connection_t g_last_connection = CLEARCHAIN_LCD_STALE;
static bool g_last_fresh;
static uint32_t g_selected_until;
static bool g_rendered;

static void flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    uint16_t width = (uint16_t)(area->x2 - area->x1 + 1);
    uint16_t height = (uint16_t)(area->y2 - area->y1 + 1);
    /* LVGL stores RGB565 in CPU byte order; ST7796 receives high byte first. */
    lv_draw_sw_rgb565_swap(pixels, (uint32_t)width * height);
    if (clearchain_lcd_flush_rgb565((uint16_t)area->x1, (uint16_t)area->y1,
                                   width, height, pixels) != 0) {
        osal_printk("[CLEAR LCD] LVGL flush failed\r\n");
    }
    lv_display_flush_ready(display);
}

static lv_obj_t *label(lv_obj_t *parent, int x, int y, int width)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_width(obj, width);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(obj, lv_color_hex(0xFFFFFF), 0);
    return obj;
}

int clearchain_ui_init(void)
{
    static const char *const names[] = {"S1", "S2", "S3", "S4", "S5", "CP"};
    lv_obj_t *root;
    if (clearchain_lcd_init() != 0) {
        osal_printk("[CLEAR UI] LCD driver init failed before LVGL\r\n");
        return -1;
    }
    lv_init();
    g_display = lv_display_create(CLEARCHAIN_LCD_WIDTH, CLEARCHAIN_LCD_HEIGHT);
    if (g_display == NULL) {
        osal_printk("[CLEAR UI] LVGL display allocation failed\r\n");
        return -1;
    }
    lv_display_set_color_format(g_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(g_display, flush_cb);
    lv_display_set_buffers(g_display, g_draw_buffer, NULL, sizeof(g_draw_buffer),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    root = lv_screen_active();
    lv_obj_set_style_bg_color(root, lv_color_hex(0x10222B), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    for (unsigned int i = 0; i < 6U; ++i) {
        g_mode[i] = lv_obj_create(root);
        lv_obj_set_pos(g_mode[i], 8 + (int)i * 79, 8);
        lv_obj_set_size(g_mode[i], 72, 35);
        lv_obj_set_style_radius(g_mode[i], 16, 0);
        lv_obj_set_style_pad_all(g_mode[i], 8, 0);
        lv_obj_set_style_bg_color(g_mode[i], lv_color_hex(0x273A43), 0);
        lv_obj_t *caption = lv_label_create(g_mode[i]);
        lv_label_set_text(caption, names[i]);
        lv_obj_set_style_text_color(caption, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(caption);
    }
    g_status = label(root, 18, 62, 444);
    g_progress = lv_bar_create(root);
    lv_obj_set_pos(g_progress, 18, 121);
    lv_obj_set_size(g_progress, 444, 18);
    lv_bar_set_range(g_progress, 0, 100);
    g_count = label(root, 18, 158, 444);
    g_risk = label(root, 18, 191, 444);
    g_message = label(root, 18, 247, 444);
    lv_label_set_text(g_status, "SCR_BOOT  Connecting...");
    lv_label_set_text(g_count, "");
    lv_label_set_text(g_risk, "");
    lv_label_set_text(g_message, "");
    lv_obj_add_flag(g_progress, LV_OBJ_FLAG_HIDDEN);
    return 0;
}

void clearchain_ui_render(const clearchain_display_state_t *state,
                          clearchain_lcd_connection_t connection, bool fresh)
{
    char text[112];
    const char *status = "SCR_IDLE  Select a mode";
    uint32_t now = lv_tick_get();
    bool banner_expired = g_selected_until != 0U && now >= g_selected_until;
    if (state == NULL || g_display == NULL) { return; }
    if (g_rendered && !banner_expired && state->state_version == g_last_version &&
        state->stage == g_last_mode && connection == g_last_connection && fresh == g_last_fresh) {
        return;
    }
    if (fresh && state->stage != g_last_mode && g_last_mode != 255U) {
        g_selected_until = now + 3000U;
    }
    if (banner_expired) { g_selected_until = 0U; }
    for (unsigned int i = 0; i < 6U; ++i) {
        lv_obj_set_style_bg_color(g_mode[i],
            state->stage == i + 1U ? lv_color_hex(0x1AA879) : lv_color_hex(0x273A43), 0);
    }
    if (connection == CLEARCHAIN_LCD_CONNECTING) {
        status = "SCR_BOOT  Connecting...";
    } else if (connection != CLEARCHAIN_LCD_CONNECTED || !fresh) {
        status = "SCR_OFFLINE  SLE link unavailable";
    } else if (state->flags & CLEARCHAIN_DISPLAY_FLAG_BACKEND_OFFLINE) {
        status = "SCR_OFFLINE  Server offline";
    } else if (g_selected_until != 0U) {
        static const char *const selected[] = {
            "Select a mode", "S1 FACTORY selected", "S2 FDA selected",
            "S3 WAREHOUSE selected", "S4 CHECKPOINT selected",
            "S5 HOSPITAL selected", "CP MOBILE INSPECTION selected"
        };
        status = state->stage <= 6U ? selected[state->stage] : selected[0];
    } else if (state->phase == CLEARCHAIN_DISPLAY_WAITING) {
        status = "SCR_READY  Waiting for scan";
    } else if (state->phase == CLEARCHAIN_DISPLAY_SCANNING) {
        status = "SCR_SCANNING";
    } else if (state->phase == CLEARCHAIN_DISPLAY_FINISHED) {
        status = "SCR_RESULT";
    } else if (state->phase == CLEARCHAIN_DISPLAY_ERROR) {
        status = "SCR_ERROR";
    }
    lv_label_set_text(g_status, status);
    if (connection != CLEARCHAIN_LCD_CONNECTED || !fresh ||
        (state->flags & CLEARCHAIN_DISPLAY_FLAG_BACKEND_OFFLINE)) {
        lv_label_set_text(g_count, "");
        lv_label_set_text(g_risk, "");
        lv_label_set_text(g_message, "Retrying...");
        lv_obj_add_flag(g_progress, LV_OBJ_FLAG_HIDDEN);
    } else {
        if (state->tags_expected == UINT16_MAX) {
            (void)snprintf(text, sizeof(text), "Tags read: %u", state->tag_count);
        } else {
            (void)snprintf(text, sizeof(text), "Tags: %u / %u", state->tag_count, state->tags_expected);
        }
        lv_label_set_text(g_count, text);
        if (state->phase == CLEARCHAIN_DISPLAY_FINISHED) {
            const char *word = state->result == CLEARCHAIN_DISPLAY_RESULT_APPROVED ? "APPROVED" :
                (state->result == CLEARCHAIN_DISPLAY_RESULT_MONITOR ? "MONITOR" :
                (state->result == CLEARCHAIN_DISPLAY_RESULT_REJECT ? "REJECT" : "RESULT UNKNOWN"));
            (void)snprintf(text, sizeof(text), "%s  Risk %u%%", word, state->risk_score);
            lv_label_set_text(g_risk, text);
            lv_obj_set_style_text_color(g_risk,
                lv_color_hex(state->result == CLEARCHAIN_DISPLAY_RESULT_APPROVED ? 0x22CC66 :
                    (state->result == CLEARCHAIN_DISPLAY_RESULT_MONITOR ? 0xFFB020 : 0xFF5555)), 0);
        } else { lv_label_set_text(g_risk, ""); }
        lv_label_set_text(g_message, state->message[0] ? state->message : "Details on phone");
        if (state->phase == CLEARCHAIN_DISPLAY_SCANNING &&
            !(state->flags & CLEARCHAIN_DISPLAY_FLAG_PROGRESS_UNKNOWN)) {
            lv_bar_set_value(g_progress, state->percent, LV_ANIM_OFF);
            lv_obj_remove_flag(g_progress, LV_OBJ_FLAG_HIDDEN);
        } else { lv_obj_add_flag(g_progress, LV_OBJ_FLAG_HIDDEN); }
    }
    g_last_version = state->state_version;
    g_last_mode = state->stage;
    g_last_connection = connection;
    g_last_fresh = fresh;
    g_rendered = true;
}

void clearchain_ui_tick(unsigned int elapsed_ms)
{
    if (g_display == NULL) { return; }
    lv_tick_inc(elapsed_ms);
    (void)lv_timer_handler();
}
