#include "clearchain_tca9555.h"

#include "gpio.h"
#include "pinctrl.h"
#include "soc_osal.h"
#include "tcxo.h"

#define TCA9555_I2C_ADDR         0x20

#define TCA9555_SCL_PIN          S_MGPIO13
#define TCA9555_SDA_PIN          S_MGPIO14
#define TCA9555_PIN_MODE         PIN_MODE_0
#define TCA9555_I2C_DELAY_US     5

#define TCA9555_REG_INPUT0       0x00
#define TCA9555_REG_INPUT1       0x01
#define TCA9555_REG_OUTPUT0      0x02
#define TCA9555_REG_OUTPUT1      0x03
#define TCA9555_REG_POLARITY0    0x04
#define TCA9555_REG_POLARITY1    0x05
#define TCA9555_REG_CONFIG0      0x06
#define TCA9555_REG_CONFIG1      0x07

#define TCA9555_PORT0_OUTPUT_MASK 0x0F

static uint8_t g_output_latch[2] = { 0xFF, 0xFF };
static int g_tca9555_ready = 0;

static void tca9555_delay(void)
{
    uapi_tcxo_delay_us(TCA9555_I2C_DELAY_US);
}

static void tca9555_gpio_release(pin_t pin)
{
    (void)uapi_gpio_set_dir(pin, GPIO_DIRECTION_INPUT);
}

static void tca9555_gpio_low(pin_t pin)
{
    (void)uapi_gpio_set_val(pin, GPIO_LEVEL_LOW);
    (void)uapi_gpio_set_dir(pin, GPIO_DIRECTION_OUTPUT);
}

static uint8_t tca9555_sda_read(void)
{
    return (uapi_gpio_get_val(TCA9555_SDA_PIN) == GPIO_LEVEL_HIGH) ? 1 : 0;
}

static void tca9555_scl_high(void)
{
    tca9555_gpio_release(TCA9555_SCL_PIN);
    tca9555_delay();
}

static void tca9555_scl_low(void)
{
    tca9555_gpio_low(TCA9555_SCL_PIN);
    tca9555_delay();
}

static void tca9555_sda_high(void)
{
    tca9555_gpio_release(TCA9555_SDA_PIN);
    tca9555_delay();
}

static void tca9555_sda_low(void)
{
    tca9555_gpio_low(TCA9555_SDA_PIN);
    tca9555_delay();
}

static void tca9555_i2c_start(void)
{
    tca9555_sda_high();
    tca9555_scl_high();
    tca9555_sda_low();
    tca9555_scl_low();
}

static void tca9555_i2c_stop(void)
{
    tca9555_sda_low();
    tca9555_scl_high();
    tca9555_sda_high();
}

static uint8_t tca9555_i2c_write_byte(uint8_t value)
{
    for (uint8_t mask = 0x80; mask != 0; mask >>= 1) {
        if ((value & mask) != 0) {
            tca9555_sda_high();
        } else {
            tca9555_sda_low();
        }
        tca9555_scl_high();
        tca9555_scl_low();
    }

    tca9555_sda_high();
    tca9555_scl_high();
    uint8_t ack = (uint8_t)(tca9555_sda_read() == 0);
    tca9555_scl_low();
    return ack;
}

static uint8_t tca9555_i2c_read_byte(uint8_t ack)
{
    uint8_t value = 0;

    tca9555_sda_high();
    for (uint8_t i = 0; i < 8; i++) {
        value <<= 1;
        tca9555_scl_high();
        if (tca9555_sda_read()) {
            value |= 1;
        }
        tca9555_scl_low();
    }

    if (ack) {
        tca9555_sda_low();
    } else {
        tca9555_sda_high();
    }
    tca9555_scl_high();
    tca9555_scl_low();
    tca9555_sda_high();

    return value;
}

static errcode_t tca9555_write_reg(uint8_t reg, uint8_t value)
{
    tca9555_i2c_start();
    if (!tca9555_i2c_write_byte((uint8_t)(TCA9555_I2C_ADDR << 1))) {
        tca9555_i2c_stop();
        return ERRCODE_I2C_ACK_ERR;
    }
    if (!tca9555_i2c_write_byte(reg)) {
        tca9555_i2c_stop();
        return ERRCODE_I2C_ACK_ERR;
    }
    if (!tca9555_i2c_write_byte(value)) {
        tca9555_i2c_stop();
        return ERRCODE_I2C_ACK_ERR;
    }
    tca9555_i2c_stop();

    return ERRCODE_SUCC;
}

static errcode_t tca9555_read_reg(uint8_t reg, uint8_t *value)
{
    tca9555_i2c_start();
    if (!tca9555_i2c_write_byte((uint8_t)(TCA9555_I2C_ADDR << 1))) {
        tca9555_i2c_stop();
        return ERRCODE_I2C_ACK_ERR;
    }
    if (!tca9555_i2c_write_byte(reg)) {
        tca9555_i2c_stop();
        return ERRCODE_I2C_ACK_ERR;
    }

    tca9555_i2c_start();
    if (!tca9555_i2c_write_byte((uint8_t)((TCA9555_I2C_ADDR << 1) | 1))) {
        tca9555_i2c_stop();
        return ERRCODE_I2C_ACK_ERR;
    }

    *value = tca9555_i2c_read_byte(0);
    tca9555_i2c_stop();

    return ERRCODE_SUCC;
}

errcode_t clearchain_tca9555_init(void)
{
    errcode_t ret;

    if (g_tca9555_ready) {
        return ERRCODE_SUCC;
    }

    uapi_pin_init();
    uapi_gpio_init();
    (void)uapi_tcxo_init();
    (void)uapi_pin_set_mode(TCA9555_SCL_PIN, TCA9555_PIN_MODE);
    (void)uapi_pin_set_mode(TCA9555_SDA_PIN, TCA9555_PIN_MODE);
    tca9555_gpio_low(TCA9555_SCL_PIN);
    tca9555_gpio_low(TCA9555_SDA_PIN);
    tca9555_scl_high();
    tca9555_sda_high();

    g_output_latch[0] = 0xFF;
    g_output_latch[1] = 0xFF;

    ret = tca9555_write_reg(TCA9555_REG_OUTPUT0, g_output_latch[0]);
    if (ret != ERRCODE_SUCC) {
        osal_printk("TCA9555 output0 init failed: 0x%x\r\n", ret);
        return ret;
    }

    ret = tca9555_write_reg(TCA9555_REG_OUTPUT1, g_output_latch[1]);
    if (ret != ERRCODE_SUCC) {
        osal_printk("TCA9555 output1 init failed: 0x%x\r\n", ret);
        return ret;
    }

    (void)tca9555_write_reg(TCA9555_REG_POLARITY0, 0x00);
    (void)tca9555_write_reg(TCA9555_REG_POLARITY1, 0x00);

    ret = tca9555_write_reg(TCA9555_REG_CONFIG0, (uint8_t)~TCA9555_PORT0_OUTPUT_MASK);
    if (ret != ERRCODE_SUCC) {
        osal_printk("TCA9555 config0 failed: 0x%x\r\n", ret);
        return ret;
    }

    ret = tca9555_write_reg(TCA9555_REG_CONFIG1, 0xFF);
    if (ret != ERRCODE_SUCC) {
        osal_printk("TCA9555 config1 failed: 0x%x\r\n", ret);
        return ret;
    }

    g_tca9555_ready = 1;
    return ERRCODE_SUCC;
}

errcode_t clearchain_tca9555_probe(void)
{
    uint8_t input0 = 0;
    uint8_t input1 = 0;
    errcode_t ret = clearchain_tca9555_init();

    if (ret != ERRCODE_SUCC) {
        osal_printk("TCA9555 probe failed: init ret=0x%x, addr=0x%x, software i2c scl=%d, sda=%d\r\n",
                    ret, TCA9555_I2C_ADDR, TCA9555_SCL_PIN, TCA9555_SDA_PIN);
        return ret;
    }

    ret = tca9555_read_reg(TCA9555_REG_INPUT0, &input0);
    if (ret != ERRCODE_SUCC) {
        osal_printk("TCA9555 probe failed: read input0 ret=0x%x, addr=0x%x\r\n", ret, TCA9555_I2C_ADDR);
        return ret;
    }

    ret = tca9555_read_reg(TCA9555_REG_INPUT1, &input1);
    if (ret != ERRCODE_SUCC) {
        osal_printk("TCA9555 probe failed: read input1 ret=0x%x, addr=0x%x\r\n", ret, TCA9555_I2C_ADDR);
        return ret;
    }

    osal_printk("TCA9555 probe ok: addr=0x%x, software i2c scl=%d, sda=%d, input0=0x%02x, input1=0x%02x\r\n",
                TCA9555_I2C_ADDR, TCA9555_SCL_PIN, TCA9555_SDA_PIN, input0, input1);
    return ERRCODE_SUCC;
}

errcode_t clearchain_tca9555_write_pin(uint8_t port, uint8_t pin, uint8_t level)
{
    uint8_t reg;
    uint8_t bit;

    if (port > CLEARCHAIN_TCA9555_PORT1 || pin > 7) {
        return ERRCODE_INVALID_PARAM;
    }

    if (!g_tca9555_ready) {
        errcode_t ret = clearchain_tca9555_init();
        if (ret != ERRCODE_SUCC) {
            return ret;
        }
    }

    reg = (port == CLEARCHAIN_TCA9555_PORT0) ? TCA9555_REG_OUTPUT0 : TCA9555_REG_OUTPUT1;
    bit = (uint8_t)(1U << pin);

    if (level == CLEARCHAIN_TCA9555_LEVEL_HIGH) {
        g_output_latch[port] |= bit;
    } else {
        g_output_latch[port] &= (uint8_t)~bit;
    }

    return tca9555_write_reg(reg, g_output_latch[port]);
}

errcode_t clearchain_tca9555_read_pin(uint8_t port, uint8_t pin, uint8_t *level)
{
    uint8_t reg;
    uint8_t value = 0;
    errcode_t ret;

    if (port > CLEARCHAIN_TCA9555_PORT1 || pin > 7 || level == 0) {
        return ERRCODE_INVALID_PARAM;
    }

    if (!g_tca9555_ready) {
        ret = clearchain_tca9555_init();
        if (ret != ERRCODE_SUCC) {
            return ret;
        }
    }

    reg = (port == CLEARCHAIN_TCA9555_PORT0) ? TCA9555_REG_INPUT0 : TCA9555_REG_INPUT1;
    ret = tca9555_read_reg(reg, &value);
    if (ret != ERRCODE_SUCC) {
        return ret;
    }

    *level = (value & (uint8_t)(1U << pin)) ? CLEARCHAIN_TCA9555_LEVEL_HIGH : CLEARCHAIN_TCA9555_LEVEL_LOW;
    return ERRCODE_SUCC;
}
