#ifndef CLEARCHAIN_OLED_H
#define CLEARCHAIN_OLED_H

#include <stddef.h>
#include <stdint.h>

#define CLEARCHAIN_OLED_I2C_ADDRESS     0x3CU
#define CLEARCHAIN_OLED_WIDTH           128U
#define CLEARCHAIN_OLED_HEIGHT          64U
#define CLEARCHAIN_OLED_PAGE_COUNT      (CLEARCHAIN_OLED_HEIGHT / 8U)
#define CLEARCHAIN_OLED_BUFFER_SIZE     (CLEARCHAIN_OLED_WIDTH * CLEARCHAIN_OLED_PAGE_COUNT)

int oled_init(void);
int oled_clear(void);
int oled_display_on(void);
int oled_display_off(void);
int oled_flush(void);

int oled_set_pixel(uint8_t x, uint8_t y, uint8_t on);
int oled_draw_char(uint8_t x, uint8_t y, char character);
int oled_draw_string(uint8_t x, uint8_t y, const char *text);
int oled_draw_rect(uint8_t x, uint8_t y, uint8_t width, uint8_t height,
                   uint8_t filled);
int oled_draw_progress_bar(uint8_t x, uint8_t y, uint8_t width, uint8_t height,
                           uint8_t percent);
int oled_test_page(void);

#endif
