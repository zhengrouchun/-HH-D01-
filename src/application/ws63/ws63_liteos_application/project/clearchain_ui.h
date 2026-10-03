#ifndef CLEARCHAIN_UI_H
#define CLEARCHAIN_UI_H

#include "clearchain_lcd.h"

int clearchain_ui_init(void);
void clearchain_ui_render(const clearchain_display_state_t *state,
                          clearchain_lcd_connection_t connection, bool fresh);
void clearchain_ui_tick(unsigned int elapsed_ms);

#endif
