/* Board B bench firmware: exercises the real ST7796/LVGL path without SLE or HTTP. */
#include "app_init.h"
#include "clearchain_ui.h"
#include "clearchain_lcd.h"
#include "lvgl.h"
#include "soc_osal.h"
#include "systick.h"
#include <stdint.h>
#include <string.h>

#define LCD_TEST_STEP_MS 6000U
#define LCD_TEST_COLOR_MS 8000U

static void hold_screen(uint32_t duration_ms, const clearchain_display_state_t *state)
{
    uint64_t start = uapi_systick_get_ms();
    uint64_t last = start;
    while (uapi_systick_get_ms() - start < duration_ms) {
        uint64_t now = uapi_systick_get_ms();
        clearchain_ui_tick((unsigned int)(now - last));
        /* The normal client renders every tick. Do the same here so a mode
         * banner expires and the simulated result actually becomes visible. */
        if (state != NULL) {
            clearchain_ui_render(state, CLEARCHAIN_LCD_CONNECTED, true);
        }
        last = now;
        osal_msleep(30);
    }
}

static void show_color_bars(bool inverted)
{
    static const uint32_t colors[5] = {0x000000U, 0xFFFFFFU, 0xFF0000U, 0x00FF00U, 0x0000FFU};
    static const char *const names[5] = {"BLACK", "WHITE", "RED", "GREEN", "BLUE"};
    lv_obj_t *bars[5] = {NULL, NULL, NULL, NULL, NULL};
    if (clearchain_lcd_set_inversion(inverted) != 0) {
        osal_printk("[LCD TEST] inversion command failed\r\n");
        return;
    }
    for (unsigned int i = 0; i < 5U; ++i) {
        bars[i] = lv_obj_create(lv_screen_active());
        if (bars[i] == NULL) { continue; }
        lv_obj_set_pos(bars[i], (int)(i * 96U), 0);
        lv_obj_set_size(bars[i], 96, 320);
        lv_obj_set_style_radius(bars[i], 0, 0);
        lv_obj_set_style_border_width(bars[i], 0, 0);
        lv_obj_set_style_pad_all(bars[i], 0, 0);
        lv_obj_set_style_bg_color(bars[i], lv_color_hex(colors[i]), 0);
        lv_obj_set_style_bg_opa(bars[i], LV_OPA_COVER, 0);
        lv_obj_t *label = lv_label_create(bars[i]);
        if (label != NULL) {
            lv_label_set_text(label, names[i]);
            lv_obj_set_style_text_color(label, lv_color_hex(i == 0U || i == 4U ? 0xFFFFFFU : 0x000000U), 0);
            lv_obj_center(label);
        }
    }
    osal_printk("[LCD TEST] INVERSION=%s command=0x%02x; LEFT->RIGHT BLACK WHITE RED GREEN BLUE; hold=8s\r\n",
                inverted ? "ON" : "OFF", inverted ? 0x21U : 0x20U);
    hold_screen(LCD_TEST_COLOR_MS, NULL);
    for (unsigned int i = 0; i < 5U; ++i) {
        if (bars[i] != NULL) { lv_obj_delete(bars[i]); }
    }
}

static void show_sample(unsigned int index, uint32_t version)
{
    clearchain_display_state_t state = {0};
    state.stage = 0U;
    state.phase = CLEARCHAIN_DISPLAY_IDLE;
    state.result = CLEARCHAIN_DISPLAY_RESULT_UNKNOWN;
    state.risk_score = CLEARCHAIN_RISK_SCORE_UNKNOWN;
    state.tags_expected = UINT16_MAX;
    state.state_version = version;
    (void)strcpy(state.message, "LCD TEST - no backend/RFID required");
    switch (index) {
        case 1U:
            state.stage = 1U;
            state.phase = CLEARCHAIN_DISPLAY_WAITING;
            break;
        case 2U:
            state.stage = 1U;
            state.phase = CLEARCHAIN_DISPLAY_SCANNING;
            state.percent = 60U;
            state.tag_count = 3U;
            state.tags_expected = 5U;
            break;
        case 3U:
            state.stage = 1U;
            state.phase = CLEARCHAIN_DISPLAY_FINISHED;
            state.result = CLEARCHAIN_DISPLAY_RESULT_APPROVED;
            state.risk_score = 0U;
            break;
        case 4U:
            state.stage = 4U;
            state.phase = CLEARCHAIN_DISPLAY_FINISHED;
            state.result = CLEARCHAIN_DISPLAY_RESULT_MONITOR;
            state.risk_score = 40U;
            break;
        case 5U:
            state.stage = 5U;
            state.phase = CLEARCHAIN_DISPLAY_FINISHED;
            state.result = CLEARCHAIN_DISPLAY_RESULT_REJECT;
            state.risk_score = 80U;
            break;
        case 6U:
            state.stage = 5U;
            state.phase = CLEARCHAIN_DISPLAY_ERROR;
            (void)strcpy(state.message, "LCD TEST - simulated error");
            break;
        case 7U:
            state.stage = 5U;
            state.flags = CLEARCHAIN_DISPLAY_FLAG_BACKEND_OFFLINE;
            (void)strcpy(state.message, "LCD TEST - simulated offline");
            break;
        default:
            break;
    }
    osal_printk("[LCD TEST] sample=%u version=%u\r\n", index, version);
    clearchain_ui_render(&state, CLEARCHAIN_LCD_CONNECTED, true);
    hold_screen(LCD_TEST_STEP_MS, &state);
}

static void *lcd_test_task(void *arg)
{
    uint32_t version = 1U;
    (void)arg;
    osal_printk("[LCD TEST] lcd-colors-inversion-20261006-v1; no SLE/backend required\r\n");
    if (clearchain_ui_init() != 0) {
        /* Controlled abort, NOT a crash: the [CLEAR LCD]/[CLEAR UI] lines above
         * name the exact failing step. Bump stack / heap or fix that step. */
        osal_printk("[LCD TEST] LCD/LVGL init failed -> task stops (controlled).\r\n");
        osal_printk("[LCD TEST] White screen is expected when init fails; panel only has backlight.\r\n");
        const char *step = NULL;
        uint32_t code = 0U;
        clearchain_lcd_get_last_error(&step, &code);
        while (1) {
            if (step != NULL) {
                osal_printk("[LCD TEST] LAST LCD ERROR step=<%s> code=0x%x (repeats every 3s)\r\n",
                            step, (unsigned int)code);
            } else {
                osal_printk("[LCD TEST] LAST LCD ERROR: UI failed at LVGL stage (LCD driver itself returned OK)\r\n");
            }
            osal_msleep(3000);
        }
        return NULL;
    }
    while (1) {
        show_color_bars(false);
        show_color_bars(true);
        if (clearchain_lcd_set_inversion(false) != 0) {
            osal_printk("[LCD TEST] could not restore INVERSION=OFF for simulated screens\r\n");
        }
        for (unsigned int i = 0; i < 8U; ++i) { show_sample(i, version++); }
    }
    return NULL;
}

static void lcd_test_entry(void)
{
    /* 0x4000 (16 KB): LVGL render path (flush_cb -> SPI poll loop) can overflow
     * an 8 KB stack and hard-fault; 16 KB removes that risk. */
    osal_task *task = osal_kthread_create((osal_kthread_handler)lcd_test_task,
                                          NULL, "CCLCDTest", 0x4000);
    if (task == NULL) {
        osal_printk("[LCD TEST] task creation failed\r\n");
        return;
    }
    /* Preserve the default OSAL priority: explicit priority changes during
     * app initialization previously faulted in LOS_TaskPriSet on this board. */
    osal_kfree(task);
}

app_run(lcd_test_entry);
