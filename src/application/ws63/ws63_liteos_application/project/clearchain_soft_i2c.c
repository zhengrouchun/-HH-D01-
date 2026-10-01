#include "clearchain_soft_i2c.h"

#include "gpio.h"
#include "pinctrl.h"
#include "soc_osal.h"
#include "tcxo.h"

#define CLEARCHAIN_SOFT_I2C_SCL_PIN      S_MGPIO13
#define CLEARCHAIN_SOFT_I2C_SDA_PIN      S_MGPIO14
#define CLEARCHAIN_SOFT_I2C_PIN_MODE     PIN_MODE_0
#define CLEARCHAIN_SOFT_I2C_DELAY_US     5U

#define CLEARCHAIN_SOFT_I2C_UNINITIALIZED 0U
#define CLEARCHAIN_SOFT_I2C_INITIALIZING  1U
#define CLEARCHAIN_SOFT_I2C_READY         2U

static osal_mutex g_clearchain_soft_i2c_mutex = { NULL };
static volatile uint8_t g_clearchain_soft_i2c_state = CLEARCHAIN_SOFT_I2C_UNINITIALIZED;
static int g_bus_failed;

static void clearchain_soft_i2c_delay(void)
{
    uapi_tcxo_delay_us(CLEARCHAIN_SOFT_I2C_DELAY_US);
}

static void clearchain_soft_i2c_release(pin_t pin)
{
    (void)uapi_gpio_set_dir(pin, GPIO_DIRECTION_INPUT);
}

static void clearchain_soft_i2c_drive_low(pin_t pin)
{
    (void)uapi_gpio_set_val(pin, GPIO_LEVEL_LOW);
    (void)uapi_gpio_set_dir(pin, GPIO_DIRECTION_OUTPUT);
}

static void clearchain_soft_i2c_scl_high(void)
{
    clearchain_soft_i2c_release(CLEARCHAIN_SOFT_I2C_SCL_PIN);
    for (unsigned int i = 0; i < 20U; i++) {
        if (uapi_gpio_get_val(CLEARCHAIN_SOFT_I2C_SCL_PIN) == GPIO_LEVEL_HIGH) {
            clearchain_soft_i2c_delay();
            return;
        }
        clearchain_soft_i2c_delay();
    }
    g_bus_failed = 1;
    clearchain_soft_i2c_delay();
}

static void clearchain_soft_i2c_scl_low(void)
{
    clearchain_soft_i2c_drive_low(CLEARCHAIN_SOFT_I2C_SCL_PIN);
    clearchain_soft_i2c_delay();
}

static void clearchain_soft_i2c_sda_high(void)
{
    clearchain_soft_i2c_release(CLEARCHAIN_SOFT_I2C_SDA_PIN);
    clearchain_soft_i2c_delay();
}

static void clearchain_soft_i2c_sda_low(void)
{
    clearchain_soft_i2c_drive_low(CLEARCHAIN_SOFT_I2C_SDA_PIN);
    clearchain_soft_i2c_delay();
}

static uint8_t clearchain_soft_i2c_sda_read(void)
{
    return (uapi_gpio_get_val(CLEARCHAIN_SOFT_I2C_SDA_PIN) == GPIO_LEVEL_HIGH) ? 1U : 0U;
}

errcode_t clearchain_soft_i2c_init(void)
{
    uint8_t initialize_bus = 0U;
    unsigned int attempts = 0U;

    while (!initialize_bus) {
        unsigned int irq_status = osal_irq_lock();

        if (g_clearchain_soft_i2c_state == CLEARCHAIN_SOFT_I2C_READY) {
            osal_irq_restore(irq_status);
            return ERRCODE_SUCC;
        }

        if (g_clearchain_soft_i2c_state == CLEARCHAIN_SOFT_I2C_UNINITIALIZED) {
            g_clearchain_soft_i2c_state = CLEARCHAIN_SOFT_I2C_INITIALIZING;
            initialize_bus = 1U;
        }
        osal_irq_restore(irq_status);

        if (!initialize_bus) {
            if (++attempts >= 100U) { return ERRCODE_FAIL; }
            osal_msleep(1);
        }
    }

    uapi_pin_init();
    uapi_gpio_init();
    (void)uapi_tcxo_init();
    (void)uapi_pin_set_mode(CLEARCHAIN_SOFT_I2C_SCL_PIN, CLEARCHAIN_SOFT_I2C_PIN_MODE);
    (void)uapi_pin_set_mode(CLEARCHAIN_SOFT_I2C_SDA_PIN, CLEARCHAIN_SOFT_I2C_PIN_MODE);
    clearchain_soft_i2c_scl_high();
    clearchain_soft_i2c_sda_high();

    if (osal_mutex_init(&g_clearchain_soft_i2c_mutex) != OSAL_SUCCESS) {
        unsigned int irq_status = osal_irq_lock();
        g_clearchain_soft_i2c_state = CLEARCHAIN_SOFT_I2C_UNINITIALIZED;
        osal_irq_restore(irq_status);
        return ERRCODE_FAIL;
    }

    {
        unsigned int irq_status = osal_irq_lock();
        g_clearchain_soft_i2c_state = CLEARCHAIN_SOFT_I2C_READY;
        osal_irq_restore(irq_status);
    }
    return ERRCODE_SUCC;
}

int clearchain_soft_i2c_lock(void)
{
    if (clearchain_soft_i2c_init() != ERRCODE_SUCC) {
        return OSAL_FAILURE;
    }

    int ret = osal_mutex_lock_timeout(&g_clearchain_soft_i2c_mutex, 100U);
    if (ret == OSAL_SUCCESS) { g_bus_failed = 0; }
    return ret;
}

void clearchain_soft_i2c_unlock(void)
{
    osal_mutex_unlock(&g_clearchain_soft_i2c_mutex);
}

void clearchain_soft_i2c_start(void)
{
    clearchain_soft_i2c_sda_high();
    clearchain_soft_i2c_scl_high();
    if (clearchain_soft_i2c_sda_read() == 0U) { g_bus_failed = 1; }
    clearchain_soft_i2c_sda_low();
    clearchain_soft_i2c_scl_low();
}

void clearchain_soft_i2c_stop(void)
{
    clearchain_soft_i2c_sda_low();
    clearchain_soft_i2c_scl_high();
    clearchain_soft_i2c_sda_high();
}

uint8_t clearchain_soft_i2c_write_byte(uint8_t value)
{
    uint8_t mask;
    uint8_t acknowledged;

    if (g_bus_failed) { return CLEARCHAIN_SOFT_I2C_NACK; }

    for (mask = 0x80U; mask != 0U; mask >>= 1) {
        if ((value & mask) != 0U) {
            clearchain_soft_i2c_sda_high();
        } else {
            clearchain_soft_i2c_sda_low();
        }
        clearchain_soft_i2c_scl_high();
        clearchain_soft_i2c_scl_low();
    }

    clearchain_soft_i2c_sda_high();
    clearchain_soft_i2c_scl_high();
    acknowledged = (uint8_t)(clearchain_soft_i2c_sda_read() == 0U);
    clearchain_soft_i2c_scl_low();
    return g_bus_failed ? CLEARCHAIN_SOFT_I2C_NACK : acknowledged;
}

uint8_t clearchain_soft_i2c_read_byte(uint8_t acknowledge)
{
    uint8_t value = 0U;
    uint8_t bit;

    clearchain_soft_i2c_sda_high();
    for (bit = 0U; bit < 8U; bit++) {
        value <<= 1;
        clearchain_soft_i2c_scl_high();
        if (clearchain_soft_i2c_sda_read() != 0U) {
            value |= 1U;
        }
        clearchain_soft_i2c_scl_low();
    }

    if (acknowledge == CLEARCHAIN_SOFT_I2C_ACK) {
        clearchain_soft_i2c_sda_low();
    } else {
        clearchain_soft_i2c_sda_high();
    }
    clearchain_soft_i2c_scl_high();
    clearchain_soft_i2c_scl_low();
    clearchain_soft_i2c_sda_high();

    return value;
}

int clearchain_soft_i2c_failed(void) { return g_bus_failed; }
