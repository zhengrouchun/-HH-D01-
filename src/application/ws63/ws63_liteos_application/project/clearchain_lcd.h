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
/* Call from the display task only, never from an SLE callback. */
int clearchain_lcd_render(const clearchain_display_state_t *state,
                          clearchain_lcd_connection_t connection, bool fresh);
#endif
