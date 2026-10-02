#include "clearchain_lcd.h"
#include "clearchain_lcd_config.h"
#include "clearchain_font5x7.h"
#include "gpio.h"
#include "pinctrl.h"
#include "spi.h"
#include "soc_osal.h"
#include <stdio.h>
#include <string.h>

#define BG 0x0841U
#define WHITE 0xFFFFU
#define GREEN 0x07E0U
#define YELLOW 0xFFE0U
#define RED 0xF800U
#define BLUE 0x051FU
/* 960 bytes, one landscape RGB565 row. No full-screen or double framebuffer. */
static uint8_t g_line[CLEARCHAIN_LCD_WIDTH * 2U];
static bool g_ready;
static bool g_previous_valid;
static clearchain_display_state_t g_previous;
static clearchain_lcd_connection_t g_previous_connection;
static bool g_previous_fresh;

static int write_bytes(bool data, const uint8_t *bytes, uint32_t length)
{
    spi_xfer_data_t transfer = { .tx_buff = (uint8_t *)bytes, .tx_bytes = length };
    (void)uapi_gpio_set_val(CLEARCHAIN_LCD_DC, data ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW);
    (void)uapi_gpio_set_val(CLEARCHAIN_LCD_CS, GPIO_LEVEL_LOW);
    errcode_t ret = uapi_spi_master_write(SPI_BUS_0, &transfer, 100U);
    (void)uapi_gpio_set_val(CLEARCHAIN_LCD_CS, GPIO_LEVEL_HIGH);
    return ret == ERRCODE_SUCC ? 0 : -1;
}
static int command(uint8_t cmd, const uint8_t *data, uint32_t length)
{
    if (write_bytes(false, &cmd, 1U) != 0) { return -1; }
    return length ? write_bytes(true, data, length) : 0;
}
static int window(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    if (w == 0U || h == 0U || (uint32_t)x+w > CLEARCHAIN_LCD_WIDTH ||
        (uint32_t)y+h > CLEARCHAIN_LCD_HEIGHT) { return -1; }
    uint16_t xe = x+w-1U, ye = y+h-1U;
    uint8_t col[] = {(uint8_t)(x>>8), (uint8_t)x, (uint8_t)(xe>>8), (uint8_t)xe};
    uint8_t row[] = {(uint8_t)(y>>8), (uint8_t)y, (uint8_t)(ye>>8), (uint8_t)ye};
    return command(0x2A, col, 4) || command(0x2B, row, 4) || command(0x2C, NULL, 0) ? -1 : 0;
}
static int fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (window(x,y,w,h) != 0) { return -1; }
    for (uint16_t i=0; i<w; i++) { g_line[i*2] = (uint8_t)(color>>8); g_line[i*2+1]=(uint8_t)color; }
    for (uint16_t row=0; row<h; row++) {
        if (write_bytes(true,g_line,w*2U) != 0) { return -1; }
        if ((row & 7U) == 7U) { osal_msleep(1); }
    }
    return 0;
}
/* A fixed-width text band erases old longer text without clearing the screen. */
static int text_band(uint16_t y, const char *text, uint16_t color, uint8_t scale)
{
    if (text == NULL || scale < 1U || scale > 4U || window(8,y,CLEARCHAIN_LCD_WIDTH-16U,8U*scale) != 0) { return -1; }
    size_t length = strlen(text);
    for (uint16_t row=0; row<8U*scale; row++) {
        for (uint16_t x=0; x<CLEARCHAIN_LCD_WIDTH-16U; x++) {
            size_t ci = x/(6U*scale);
            uint8_t col=(x/scale)%6U, bit=row/scale;
            uint8_t c=ci<length ? (uint8_t)text[ci] : ' ';
            if (c<32U || c>126U) { c='?'; }
            uint16_t pixel = col<5U && (g_cc_font[c-32U][col] & (1U<<bit)) ? color : BG;
            g_line[x*2]=(uint8_t)(pixel>>8); g_line[x*2+1]=(uint8_t)pixel;
        }
        if (write_bytes(true,g_line,(CLEARCHAIN_LCD_WIDTH-16U)*2U) != 0) { return -1; }
    }
    return 0;
}
int clearchain_lcd_init(void)
{
    const uint8_t outputs[] = {CLEARCHAIN_LCD_CS,CLEARCHAIN_LCD_DC,CLEARCHAIN_LCD_RESET};
    uapi_pin_init(); uapi_gpio_init();
    if (uapi_pin_set_mode(CLEARCHAIN_LCD_SCK,PIN_MODE_3) != ERRCODE_SUCC ||
        uapi_pin_set_mode(CLEARCHAIN_LCD_MOSI,PIN_MODE_3) != ERRCODE_SUCC) { return -1; }
    for (unsigned int i=0; i<sizeof(outputs); i++) {
        if (uapi_pin_set_mode(outputs[i],PIN_MODE_0) != ERRCODE_SUCC ||
            uapi_gpio_set_dir(outputs[i],GPIO_DIRECTION_OUTPUT) != ERRCODE_SUCC) { return -1; }
        (void)uapi_gpio_set_val(outputs[i],GPIO_LEVEL_HIGH);
    }
    spi_attr_t attr = {0}; spi_extra_attr_t extra = {0};
    attr.is_slave=false; attr.slave_num=1; attr.bus_clk=96000000;
    attr.freq_mhz=CLEARCHAIN_LCD_SPI_MHZ; attr.clk_polarity=0; attr.clk_phase=0;
    attr.frame_format=0; attr.spi_frame_format=HAL_SPI_FRAME_FORMAT_STANDARD;
    attr.frame_size=HAL_SPI_FRAME_SIZE_8; attr.tmod=1;
    /* Polling writes are bounded to one row; no RX, touch, TF, MISO or DMA. */
    if (uapi_spi_init(SPI_BUS_0,&attr,&extra) != ERRCODE_SUCC) { return -1; }
    (void)uapi_gpio_set_val(CLEARCHAIN_LCD_RESET,GPIO_LEVEL_LOW); osal_msleep(100);
    (void)uapi_gpio_set_val(CLEARCHAIN_LCD_RESET,GPIO_LEVEL_HIGH); osal_msleep(120);
    if (command(0x11,NULL,0) != 0) { return -1; } osal_msleep(120);
    /* Module setup follows the local HiHope ST7796 reference; landscape RGB565. */
    static const uint8_t setup[][17] = {
        {0x36,1,CLEARCHAIN_LCD_MADCTL},{0x3A,1,0x55},{0xF0,1,0xC3},{0xF0,1,0x96},
        {0xB4,1,0x02},{0xB7,1,0xC6},{0xC0,2,0xC0,0x00},{0xC1,1,0x13},
        {0xC2,1,0xA7},{0xC5,1,0x21},{0xE8,8,0x40,0x8A,0x1B,0x1B,0x23,0x0A,0xAC,0x33},
        {0xE0,14,0xD2,0x05,0x08,0x06,0x05,0x02,0x2A,0x44,0x46,0x39,0x15,0x15,0x2D,0x32},
        {0xE1,14,0x96,0x08,0x0C,0x09,0x09,0x25,0x2E,0x43,0x42,0x35,0x11,0x11,0x28,0x2E},
        {0xF0,1,0x3C},{0xF0,1,0x69}
    };
    for (size_t i=0; i<sizeof(setup)/sizeof(setup[0]); i++) {
        if (command(setup[i][0],&setup[i][2],setup[i][1]) != 0) { return -1; }
    }
    if (command(0x21,NULL,0) != 0 || command(0x29,NULL,0) != 0) { return -1; }
    osal_msleep(20);
    if (fill(0,0,CLEARCHAIN_LCD_WIDTH,CLEARCHAIN_LCD_HEIGHT,BG) != 0 || text_band(12,"CLEARCHAIN",WHITE,3) != 0 ||
        text_band(42,"BOARD B / ST7796 LANDSCAPE",BLUE,2) != 0) { return -1; }
    g_ready=true; g_previous_valid=false;
    osal_printk("[CLEAR LCD] init 480x320 RGB565 SPI0 2MHz mode0; row_buffer=960\r\n");
    return 0;
}
int clearchain_lcd_render(const clearchain_display_state_t *s, clearchain_lcd_connection_t connection, bool fresh)
{
    static const char *const links[]={"CONNECTING","CONNECTED","DISCONNECTED","STATE TIMEOUT"};
    static const char *const stages[]={"","FACTORY","FDA","WAREHOUSE","CHECKPOINT","HOSPITAL"};
    static const char *const errors[]={"","NO TAGS","READER ERROR","TAG CAPACITY","TCA ERROR","BACKEND ERROR","NOT AVAILABLE","KEY DISABLED"};
    char line[48]; int ret=0;
    if (!g_ready || s==NULL || s->stage<1 || s->stage>5 || connection>CLEARCHAIN_LCD_STALE) { return -1; }
    bool all=!g_previous_valid || connection!=g_previous_connection || fresh!=g_previous_fresh;
    if (all) { ret |= text_band(70,links[connection],connection==CLEARCHAIN_LCD_CONNECTED?GREEN:YELLOW,2); }
    if (all || s->stage!=g_previous.stage) {
        snprintf(line,sizeof(line),"S%u  %s",s->stage,stages[s->stage]); ret |= text_band(98,line,WHITE,2);
    }
    if (all || s->phase!=g_previous.phase || s->error!=g_previous.error || s->result!=g_previous.result) {
        const char *label="WAITING FOR SCAN"; uint16_t color=WHITE;
        if (!fresh) { label="WAITING FOR STATE"; color=YELLOW; }
        else if (s->phase==CLEARCHAIN_DISPLAY_SCANNING) { label="SCANNING"; color=BLUE; }
        else if (s->phase==CLEARCHAIN_DISPLAY_ERROR) { label=s->error<8U?errors[s->error]:"ERROR"; color=RED; }
        else if (s->phase==CLEARCHAIN_DISPLAY_FINISHED) {
            if (s->result==CLEARCHAIN_DISPLAY_RESULT_LOCAL_CAPTURE) { label="CAPTURED LOCALLY"; color=BLUE; }
            else if (s->result==CLEARCHAIN_DISPLAY_RESULT_APPROVED) { label="APPROVED"; color=GREEN; }
            else if (s->result==CLEARCHAIN_DISPLAY_RESULT_MONITOR) { label="MONITOR"; color=YELLOW; }
            else if (s->result==CLEARCHAIN_DISPLAY_RESULT_REJECT) { label="REJECT"; color=RED; }
            else { label="RESULT UNKNOWN"; color=YELLOW; }
        }
        ret |= text_band(126,label,color,2);
    }
    if (all || s->percent!=g_previous.percent || s->phase!=g_previous.phase) {
        snprintf(line,sizeof(line),"WINDOW TIME: %u%%",fresh?s->percent:0U);
        ret |= text_band(156,line,WHITE,2);
        uint16_t w=(uint16_t)((fresh?s->percent:0U)*(CLEARCHAIN_LCD_WIDTH-20U)/100U);
        if (w) { ret |= fill(10,180,w,12,BLUE); }
        if (w<CLEARCHAIN_LCD_WIDTH-20U) { ret |= fill(10+w,180,CLEARCHAIN_LCD_WIDTH-20U-w,12,0x2104U); }
    }
    if (all || s->tag_count!=g_previous.tag_count || s->total_samples!=g_previous.total_samples) {
        snprintf(line,sizeof(line),"TAGS %u   SAMPLES %u",s->tag_count,s->total_samples);
        ret |= text_band(208,line,WHITE,2);
    }
    if (all || s->risk_score!=g_previous.risk_score || s->flags!=g_previous.flags) {
        if (!fresh || s->risk_score==CLEARCHAIN_RISK_SCORE_UNKNOWN) { snprintf(line,sizeof(line),"RISK: UNKNOWN"); }
        else { snprintf(line,sizeof(line),"RISK: %u%%",s->risk_score); }
        ret |= text_band(238,line,WHITE,2);
        ret |= text_band(264,(s->flags&CLEARCHAIN_DISPLAY_FLAG_UPLOAD_DISABLED)?"UPLOAD DISABLED":"UPLOAD CONFIGURED",YELLOW,2);
    }
    if (all) {
        ret |= text_band(292,"TAG COMPLETENESS UNKNOWN",YELLOW,1);
        ret |= text_band(308,"LCD LINK / NO PIXEL TRANSFER",BLUE,1);
    }
    if (ret==0) { g_previous=*s; g_previous_connection=connection; g_previous_fresh=fresh; g_previous_valid=true; }
    else { g_previous_valid=false; }
    return ret;
}
