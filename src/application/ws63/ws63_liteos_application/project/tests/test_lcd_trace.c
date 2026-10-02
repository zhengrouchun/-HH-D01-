#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include "clearchain_lcd.h"
#include "clearchain_lcd_config.h"
#include "spi.h"
static unsigned int dc=1,cs=1,command,pending,max_x,max_y,pixels,orientation;
static unsigned int xs,xe,ys,ye;
int osal_printk(const char *f,...) { (void)f;return 0; }
int osal_msleep(unsigned int ms) { (void)ms;return 0; }
void uapi_pin_init(void) { }
void uapi_gpio_init(void) { }
errcode_t uapi_pin_set_mode(unsigned int pin,unsigned int mode)
{ assert((pin==7 || pin==9)?mode==3:(pin==1 || pin==8 || pin==14) && mode==0);return 0; }
errcode_t uapi_gpio_set_dir(unsigned int pin,unsigned int mode)
{ assert((pin==1 || pin==8 || pin==14) && mode==1);return 0; }
errcode_t uapi_gpio_set_val(unsigned int pin,unsigned int value)
{ if(pin==1){dc=value;}else if(pin==8){cs=value;}else{assert(pin==14);}return 0; }
errcode_t uapi_spi_init(unsigned int bus,const spi_attr_t *attr,const spi_extra_attr_t *extra)
{ (void)extra;assert(bus==0 && !attr->is_slave && attr->freq_mhz==2 && !attr->clk_phase && !attr->clk_polarity);return 0; }
errcode_t uapi_spi_master_write(unsigned int bus,const spi_xfer_data_t *x,unsigned int timeout)
{
    assert(bus==0 && cs==0 && timeout==100 && x->tx_buff && x->tx_bytes>0 && x->tx_bytes<=960);
    if (!dc) {
        assert(x->tx_bytes==1 && pending==0);command=x->tx_buff[0];
        if(command==0x2C){pending=(xe-xs+1)*(ye-ys+1)*2;}
    } else if(command==0x2A || command==0x2B) {
        assert(x->tx_bytes==4);
        unsigned int start=((unsigned int)x->tx_buff[0]<<8)|x->tx_buff[1];
        unsigned int end=((unsigned int)x->tx_buff[2]<<8)|x->tx_buff[3];assert(end>=start);
        if(command==0x2A){xs=start;xe=end;assert(xe<480);if(xe>max_x){max_x=xe;}}
        else{ys=start;ye=end;assert(ye<320);if(ye>max_y){max_y=ye;}}
    } else if(command==0x2C) { assert(pending>=x->tx_bytes);pending-=x->tx_bytes;pixels+=x->tx_bytes/2; }
    else if(command==0x36) { assert(x->tx_bytes==1 && x->tx_buff[0]==0x28);orientation++; }
    else if(command==0x3A) { assert(x->tx_bytes==1 && x->tx_buff[0]==0x55); }
    return 0;
}
int main(void)
{
    assert(clearchain_lcd_init()==0 && max_x==479 && max_y==319 && orientation==1 && pixels>=480*320);
    clearchain_display_state_t s={.stage=1,.result=3,.risk_score=255,.flags=3};
    assert(clearchain_lcd_render(&s,CLEARCHAIN_LCD_CONNECTING,false)==0);
    assert(clearchain_lcd_render(&s,CLEARCHAIN_LCD_CONNECTED,true)==0);
    s.phase=1;s.percent=99;s.tag_count=11;s.total_samples=44;
    assert(clearchain_lcd_render(&s,CLEARCHAIN_LCD_CONNECTED,true)==0);
    s.phase=2;s.percent=100;s.result=4;
    assert(clearchain_lcd_render(&s,CLEARCHAIN_LCD_CONNECTED,true)==0);
    unsigned int saved=pixels;
    assert(clearchain_lcd_render(&s,CLEARCHAIN_LCD_CONNECTED,true)==0 && pixels==saved);
    assert(clearchain_lcd_render(&s,CLEARCHAIN_LCD_DISCONNECTED,false)==0);
    assert(clearchain_lcd_render(&s,CLEARCHAIN_LCD_STALE,false)==0);
    s.stage=5;s.phase=3;s.error=1;assert(clearchain_lcd_render(&s,CLEARCHAIN_LCD_CONNECTED,true)==0);
    assert(pending==0);
    puts("PASS: LCD landscape 480x320 address bounds, RGB565, MADCTL, 2MHz mode0, complete regions and unchanged-state no-op");
    return 0;
}
