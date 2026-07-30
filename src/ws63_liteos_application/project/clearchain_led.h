#ifndef CLEARCHAIN_LED_H
#define CLEARCHAIN_LED_H

typedef enum {
    CLEARCHAIN_LED_RED = 0,
    CLEARCHAIN_LED_GREEN,
    CLEARCHAIN_LED_YELLOW
} clearchain_led_t;

void clearchain_led_init(void);
void clearchain_led_all_off(void);
void clearchain_led_on(clearchain_led_t led);
void clearchain_led_off(clearchain_led_t led);
void clearchain_led_show_standby(void);
void clearchain_led_blink(clearchain_led_t led, unsigned int times,
                          unsigned int on_ms, unsigned int off_ms);

#endif
