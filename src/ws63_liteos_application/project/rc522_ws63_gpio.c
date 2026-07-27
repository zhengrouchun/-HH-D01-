#include "rc522_ws63_gpio.h"

#include "gpio.h"
#include "pinctrl.h"
#include "soc_osal.h"

#define RC522_PIN_NSS S_MGPIO8
#define RC522_PIN_SCK S_MGPIO7
#define RC522_PIN_MOSI S_MGPIO9
#define RC522_PIN_MISO S_MGPIO11
#define RC522_PIN_RST S_MGPIO10

static void rc522_delay_us(uint32_t us)
{
    osal_udelay(us);
}

static void rc522_set(pin_t pin, gpio_level_t level)
{
    uapi_gpio_set_val(pin, level);
    rc522_delay_us(2);
}

static uint8_t rc522_get(pin_t pin)
{
    return (uapi_gpio_get_val(pin) == GPIO_LEVEL_HIGH) ? 1 : 0;
}

void rc522_gpio_init(void)
{
    uapi_pin_init();
    uapi_gpio_init();

    uapi_pin_set_mode(RC522_PIN_NSS, PIN_MODE_0);
    uapi_pin_set_mode(RC522_PIN_SCK, PIN_MODE_0);
    uapi_pin_set_mode(RC522_PIN_MOSI, PIN_MODE_0);
    uapi_pin_set_mode(RC522_PIN_MISO, PIN_MODE_0);
    uapi_pin_set_mode(RC522_PIN_RST, PIN_MODE_0);

    uapi_pin_set_pull(RC522_PIN_MISO, PIN_PULL_TYPE_UP);

    uapi_gpio_set_dir(RC522_PIN_NSS, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_dir(RC522_PIN_SCK, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_dir(RC522_PIN_MOSI, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_dir(RC522_PIN_MISO, GPIO_DIRECTION_INPUT);
    uapi_gpio_set_dir(RC522_PIN_RST, GPIO_DIRECTION_OUTPUT);

    rc522_set(RC522_PIN_NSS, GPIO_LEVEL_HIGH);
    rc522_set(RC522_PIN_SCK, GPIO_LEVEL_HIGH);
    rc522_set(RC522_PIN_MOSI, GPIO_LEVEL_LOW);
    rc522_set(RC522_PIN_RST, GPIO_LEVEL_HIGH);
}

void rc522_gpio_reset(void)
{
    rc522_set(RC522_PIN_RST, GPIO_LEVEL_HIGH);
    osal_msleep(5);
    rc522_set(RC522_PIN_RST, GPIO_LEVEL_LOW);
    osal_msleep(5);
    rc522_set(RC522_PIN_RST, GPIO_LEVEL_HIGH);
    osal_msleep(5);
}

uint8_t rc522_spi_read_reg(uint8_t address)
{
    uint8_t result = 0;
    uint8_t command = ((address << 1) & 0x7E) | 0x80;

    rc522_set(RC522_PIN_SCK, GPIO_LEVEL_LOW);
    rc522_set(RC522_PIN_NSS, GPIO_LEVEL_LOW);

    for(uint8_t i = 0; i < 8; i++)
    {
        rc522_set(RC522_PIN_MOSI, (command & 0x80) ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW);
        rc522_set(RC522_PIN_SCK, GPIO_LEVEL_HIGH);
        command <<= 1;
        rc522_set(RC522_PIN_SCK, GPIO_LEVEL_LOW);
    }

    for(uint8_t i = 0; i < 8; i++)
    {
        rc522_set(RC522_PIN_SCK, GPIO_LEVEL_HIGH);
        result <<= 1;
        result |= rc522_get(RC522_PIN_MISO);
        rc522_set(RC522_PIN_SCK, GPIO_LEVEL_LOW);
    }

    rc522_set(RC522_PIN_NSS, GPIO_LEVEL_HIGH);
    rc522_set(RC522_PIN_SCK, GPIO_LEVEL_HIGH);

    return result;
}

void rc522_spi_write_reg(uint8_t address, uint8_t value)
{
    uint8_t command = (address << 1) & 0x7E;

    rc522_set(RC522_PIN_SCK, GPIO_LEVEL_LOW);
    rc522_set(RC522_PIN_NSS, GPIO_LEVEL_LOW);

    for(uint8_t i = 0; i < 8; i++)
    {
        rc522_set(RC522_PIN_MOSI, (command & 0x80) ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW);
        rc522_set(RC522_PIN_SCK, GPIO_LEVEL_HIGH);
        command <<= 1;
        rc522_set(RC522_PIN_SCK, GPIO_LEVEL_LOW);
    }

    for(uint8_t i = 0; i < 8; i++)
    {
        rc522_set(RC522_PIN_MOSI, (value & 0x80) ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW);
        rc522_set(RC522_PIN_SCK, GPIO_LEVEL_HIGH);
        value <<= 1;
        rc522_set(RC522_PIN_SCK, GPIO_LEVEL_LOW);
    }

    rc522_set(RC522_PIN_NSS, GPIO_LEVEL_HIGH);
    rc522_set(RC522_PIN_SCK, GPIO_LEVEL_HIGH);
}
