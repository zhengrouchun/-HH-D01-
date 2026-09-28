#include "clearchain_tca9555.h"

#include "clearchain_soft_i2c.h"
#include "soc_osal.h"

#define TCA9555_REG_INPUT0       0x00U
#define TCA9555_REG_INPUT1       0x01U
#define TCA9555_REG_OUTPUT0      0x02U
#define TCA9555_REG_OUTPUT1      0x03U
#define TCA9555_REG_POLARITY0    0x04U
#define TCA9555_REG_POLARITY1    0x05U
#define TCA9555_REG_CONFIG0      0x06U
#define TCA9555_REG_CONFIG1      0x07U

#define TCA9555_PORT0_OUTPUT_MASK 0x0FU

static clearchain_tca9555_device_t g_tca9555_devices[CLEARCHAIN_TCA9555_DEVICE_COUNT] = {
    {
        CLEARCHAIN_TCA9555_ADDRESS_1,
        { 0xFFU, 0xFFU },
        { (uint8_t)~TCA9555_PORT0_OUTPUT_MASK, 0xFFU },
        0U
    },
    {
        CLEARCHAIN_TCA9555_ADDRESS_2,
        { 0xFFU, 0xFFU },
        { 0xFFU, 0xFFU },
        0U
    }
};

static int tca9555_address_valid(uint8_t address)
{
    return address >= 0x20U && address <= 0x27U;
}

static errcode_t tca9555_write_reg_locked(const clearchain_tca9555_device_t *device,
                                          uint8_t reg, uint8_t value)
{
    clearchain_soft_i2c_start();
    if (clearchain_soft_i2c_write_byte((uint8_t)(device->address << 1)) != CLEARCHAIN_SOFT_I2C_ACK ||
        clearchain_soft_i2c_write_byte(reg) != CLEARCHAIN_SOFT_I2C_ACK ||
        clearchain_soft_i2c_write_byte(value) != CLEARCHAIN_SOFT_I2C_ACK) {
        clearchain_soft_i2c_stop();
        return ERRCODE_I2C_ACK_ERR;
    }
    clearchain_soft_i2c_stop();
    return ERRCODE_SUCC;
}

static errcode_t tca9555_read_reg_locked(const clearchain_tca9555_device_t *device,
                                         uint8_t reg, uint8_t *value)
{
    if (value == NULL) {
        return ERRCODE_INVALID_PARAM;
    }

    clearchain_soft_i2c_start();
    if (clearchain_soft_i2c_write_byte((uint8_t)(device->address << 1)) != CLEARCHAIN_SOFT_I2C_ACK ||
        clearchain_soft_i2c_write_byte(reg) != CLEARCHAIN_SOFT_I2C_ACK) {
        clearchain_soft_i2c_stop();
        return ERRCODE_I2C_ACK_ERR;
    }

    clearchain_soft_i2c_start();
    if (clearchain_soft_i2c_write_byte((uint8_t)((device->address << 1) | 1U)) !=
        CLEARCHAIN_SOFT_I2C_ACK) {
        clearchain_soft_i2c_stop();
        return ERRCODE_I2C_ACK_ERR;
    }

    *value = clearchain_soft_i2c_read_byte(CLEARCHAIN_SOFT_I2C_NACK);
    clearchain_soft_i2c_stop();
    return ERRCODE_SUCC;
}

static errcode_t tca9555_device_init_locked(clearchain_tca9555_device_t *device)
{
    errcode_t ret;

    if (device->ready != 0U) {
        return ERRCODE_SUCC;
    }

    device->output_latch[0] = 0xFFU;
    device->output_latch[1] = 0xFFU;

    ret = tca9555_write_reg_locked(device, TCA9555_REG_OUTPUT0, device->output_latch[0]);
    if (ret != ERRCODE_SUCC) {
        return ret;
    }
    ret = tca9555_write_reg_locked(device, TCA9555_REG_OUTPUT1, device->output_latch[1]);
    if (ret != ERRCODE_SUCC) {
        return ret;
    }

    ret = tca9555_write_reg_locked(device, TCA9555_REG_POLARITY0, 0x00U);
    if (ret != ERRCODE_SUCC) {
        return ret;
    }
    ret = tca9555_write_reg_locked(device, TCA9555_REG_POLARITY1, 0x00U);
    if (ret != ERRCODE_SUCC) {
        return ret;
    }

    ret = tca9555_write_reg_locked(device, TCA9555_REG_CONFIG0, device->configuration[0]);
    if (ret != ERRCODE_SUCC) {
        return ret;
    }
    ret = tca9555_write_reg_locked(device, TCA9555_REG_CONFIG1, device->configuration[1]);
    if (ret != ERRCODE_SUCC) {
        return ret;
    }

    device->ready = 1U;
    return ERRCODE_SUCC;
}

clearchain_tca9555_device_t *clearchain_tca9555_get_device(uint8_t index)
{
    if (index >= CLEARCHAIN_TCA9555_DEVICE_COUNT) {
        return NULL;
    }
    return &g_tca9555_devices[index];
}

errcode_t clearchain_tca9555_device_setup(clearchain_tca9555_device_t *device,
                                          uint8_t address,
                                          uint8_t port0_output_mask,
                                          uint8_t port1_output_mask)
{
    if (device == NULL || !tca9555_address_valid(address) || device->ready != 0U) {
        return ERRCODE_INVALID_PARAM;
    }

    device->address = address;
    device->output_latch[0] = 0xFFU;
    device->output_latch[1] = 0xFFU;
    device->configuration[0] = (uint8_t)~port0_output_mask;
    device->configuration[1] = (uint8_t)~port1_output_mask;
    return ERRCODE_SUCC;
}

errcode_t clearchain_tca9555_device_init(clearchain_tca9555_device_t *device)
{
    errcode_t ret;

    if (device == NULL || !tca9555_address_valid(device->address)) {
        return ERRCODE_INVALID_PARAM;
    }
    if (clearchain_soft_i2c_lock() != OSAL_SUCCESS) {
        return ERRCODE_FAIL;
    }
    ret = tca9555_device_init_locked(device);
    clearchain_soft_i2c_unlock();
    return ret;
}

errcode_t clearchain_tca9555_device_probe(clearchain_tca9555_device_t *device)
{
    uint8_t input0 = 0U;
    uint8_t input1 = 0U;
    errcode_t ret;

    if (device == NULL || !tca9555_address_valid(device->address)) {
        return ERRCODE_INVALID_PARAM;
    }
    if (clearchain_soft_i2c_lock() != OSAL_SUCCESS) {
        return ERRCODE_FAIL;
    }

    ret = tca9555_device_init_locked(device);
    if (ret == ERRCODE_SUCC) {
        ret = tca9555_read_reg_locked(device, TCA9555_REG_INPUT0, &input0);
    }
    if (ret == ERRCODE_SUCC) {
        ret = tca9555_read_reg_locked(device, TCA9555_REG_INPUT1, &input1);
    }
    clearchain_soft_i2c_unlock();

    if (ret == ERRCODE_SUCC) {
        osal_printk("TCA9555 probe ok: addr=0x%x, input0=0x%02x, input1=0x%02x\r\n",
                    device->address, input0, input1);
    } else {
        osal_printk("TCA9555 probe failed: addr=0x%x, ret=0x%x\r\n", device->address, ret);
    }
    return ret;
}

errcode_t clearchain_tca9555_device_write_pin(clearchain_tca9555_device_t *device,
                                              uint8_t port, uint8_t pin, uint8_t level)
{
    uint8_t reg;
    uint8_t bit;
    uint8_t new_latch;
    errcode_t ret;

    if (device == NULL || port > CLEARCHAIN_TCA9555_PORT1 || pin > 7U ||
        (level != CLEARCHAIN_TCA9555_LEVEL_LOW && level != CLEARCHAIN_TCA9555_LEVEL_HIGH)) {
        return ERRCODE_INVALID_PARAM;
    }
    if (clearchain_soft_i2c_lock() != OSAL_SUCCESS) {
        return ERRCODE_FAIL;
    }

    ret = tca9555_device_init_locked(device);
    if (ret != ERRCODE_SUCC) {
        clearchain_soft_i2c_unlock();
        return ret;
    }

    reg = (port == CLEARCHAIN_TCA9555_PORT0) ? TCA9555_REG_OUTPUT0 : TCA9555_REG_OUTPUT1;
    bit = (uint8_t)(1U << pin);
    new_latch = device->output_latch[port];
    if (level == CLEARCHAIN_TCA9555_LEVEL_HIGH) {
        new_latch |= bit;
    } else {
        new_latch &= (uint8_t)~bit;
    }

    ret = tca9555_write_reg_locked(device, reg, new_latch);
    if (ret == ERRCODE_SUCC) {
        device->output_latch[port] = new_latch;
    }
    clearchain_soft_i2c_unlock();
    return ret;
}

errcode_t clearchain_tca9555_device_read_pin(clearchain_tca9555_device_t *device,
                                             uint8_t port, uint8_t pin, uint8_t *level)
{
    uint8_t reg;
    uint8_t value = 0U;
    errcode_t ret;

    if (device == NULL || port > CLEARCHAIN_TCA9555_PORT1 || pin > 7U || level == NULL) {
        return ERRCODE_INVALID_PARAM;
    }
    if (clearchain_soft_i2c_lock() != OSAL_SUCCESS) {
        return ERRCODE_FAIL;
    }

    ret = tca9555_device_init_locked(device);
    if (ret == ERRCODE_SUCC) {
        reg = (port == CLEARCHAIN_TCA9555_PORT0) ? TCA9555_REG_INPUT0 : TCA9555_REG_INPUT1;
        ret = tca9555_read_reg_locked(device, reg, &value);
    }
    clearchain_soft_i2c_unlock();

    if (ret == ERRCODE_SUCC) {
        *level = ((value & (uint8_t)(1U << pin)) != 0U) ?
            CLEARCHAIN_TCA9555_LEVEL_HIGH : CLEARCHAIN_TCA9555_LEVEL_LOW;
    }
    return ret;
}

errcode_t clearchain_tca9555_init(void)
{
    return clearchain_tca9555_device_init(&g_tca9555_devices[0]);
}

errcode_t clearchain_tca9555_probe(void)
{
    return clearchain_tca9555_device_probe(&g_tca9555_devices[0]);
}

errcode_t clearchain_tca9555_write_pin(uint8_t port, uint8_t pin, uint8_t level)
{
    return clearchain_tca9555_device_write_pin(&g_tca9555_devices[0], port, pin, level);
}

errcode_t clearchain_tca9555_read_pin(uint8_t port, uint8_t pin, uint8_t *level)
{
    return clearchain_tca9555_device_read_pin(&g_tca9555_devices[0], port, pin, level);
}
