#ifndef CLEARCHAIN_LCD_H
#define CLEARCHAIN_LCD_H
#include <stdbool.h>
#include "clearchain_display_protocol.h"
typedef enum {
    CLEARCHAIN_LCD_CONNECTING = 0,
    CLEARCHAIN_LCD_CONNECTED = 1,
    CLEARCHAIN_LCD_DISCONNECTED = 2,
    CLEARCHAIN_LCD_STALE = 3
} clearchain_lcd_connection_t;
int clearchain_lcd_init(void);
/* Returns the last failing init step string and error code (0 if none yet). */
void clearchain_lcd_get_last_error(const char **step, uint32_t *code);
/* LVGL flush writes a bounded RGB565 rectangle in row-major order. */
int clearchain_lcd_flush_rgb565(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                               const uint8_t *pixels);
/* Call from the display task only, never from an SLE callback. */
int clearchain_lcd_render(const clearchain_display_state_t *state,
                          clearchain_lcd_connection_t connection, bool fresh);
#endif
