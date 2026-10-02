#ifndef CLEARCHAIN_LCD_CONFIG_H
#define CLEARCHAIN_LCD_CONFIG_H
#define CLEARCHAIN_LCD_WIDTH 480U
#define CLEARCHAIN_LCD_HEIGHT 320U
/* User-specified landscape dimensions, in 0.1 mm; mechanical metadata only. */
#define CLEARCHAIN_LCD_PHYSICAL_WIDTH_MM_X10 980U
#define CLEARCHAIN_LCD_PHYSICAL_HEIGHT_MM_X10 555U
#define CLEARCHAIN_LCD_SPI_MHZ 2U
#define CLEARCHAIN_LCD_SCK 7U
#define CLEARCHAIN_LCD_MOSI 9U
#define CLEARCHAIN_LCD_CS 8U
#define CLEARCHAIN_LCD_DC 1U
#define CLEARCHAIN_LCD_RESET 14U
#define CLEARCHAIN_LCD_MADCTL 0x28U /* MV=1, BGR=1: landscape; verify colors on panel. */
#if CLEARCHAIN_LCD_SPI_MHZ > 15 || CLEARCHAIN_LCD_SPI_MHZ < 1
#error "ST7796 SPI must be between 1 and 15 MHz"
#endif
#endif
