#ifndef CLEARCHAIN_TCA9555_H
#define CLEARCHAIN_TCA9555_H

#include <stdint.h>

#include "errcode.h"

#define CLEARCHAIN_TCA9555_DEVICE_COUNT 2U
#define CLEARCHAIN_TCA9555_ADDRESS_1    0x20U
#define CLEARCHAIN_TCA9555_ADDRESS_2    0x21U

#define CLEARCHAIN_TCA9555_PORT0 0U
#define CLEARCHAIN_TCA9555_PORT1 1U

#define CLEARCHAIN_TCA9555_LEVEL_LOW  0U
#define CLEARCHAIN_TCA9555_LEVEL_HIGH 1U

typedef struct {
    uint8_t address;
    uint8_t output_latch[2];
    uint8_t configuration[2];
    uint8_t ready;
} clearchain_tca9555_device_t;

clearchain_tca9555_device_t *clearchain_tca9555_get_device(uint8_t index);
errcode_t clearchain_tca9555_device_setup(clearchain_tca9555_device_t *device,
                                          uint8_t address,
                                          uint8_t port0_output_mask,
                                          uint8_t port1_output_mask);
errcode_t clearchain_tca9555_device_init(clearchain_tca9555_device_t *device);
errcode_t clearchain_tca9555_device_probe(clearchain_tca9555_device_t *device);
errcode_t clearchain_tca9555_device_write_pin(clearchain_tca9555_device_t *device,
                                              uint8_t port, uint8_t pin, uint8_t level);
errcode_t clearchain_tca9555_device_read_pin(clearchain_tca9555_device_t *device,
                                             uint8_t port, uint8_t pin, uint8_t *level);

/* Compatibility API: existing modules continue to use TCA9555 #1 at 0x20. */
errcode_t clearchain_tca9555_init(void);
errcode_t clearchain_tca9555_probe(void);
errcode_t clearchain_tca9555_write_pin(uint8_t port, uint8_t pin, uint8_t level);
errcode_t clearchain_tca9555_read_pin(uint8_t port, uint8_t pin, uint8_t *level);

#endif
