#ifndef R200_PROTOCOL_H
#define R200_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define R200_MAX_FRAME_SIZE 128
#define R200_PARSE_COMMAND_ERROR (-2)
#define R200_PARSE_NO_TAG        (-3)
/* Frame type and command bytes defined by the R200 serial protocol. */
#define R200_RESPONSE_FRAME_TYPE 0x01
#define R200_NATIVE_MULTI_COMMAND 0x27
#define R200_NATIVE_STOP_COMMAND 0x28
#define R200_GET_POWER_COMMAND 0xB7
#define R200_SET_POWER_COMMAND 0xB6
#define R200_MIN_MULTI_ROUNDS 1
#define R200_MULTI_PAYLOAD_LENGTH 3
#define R200_MULTI_INVENTORY_PARAMETER 0x22

int r200_protocol_build_inventory(uint8_t *frame,
                                  size_t frame_size,
                                  size_t *frame_length);
int r200_protocol_build_multi_inventory(uint8_t *frame, size_t frame_size,
                                        size_t *frame_length, uint16_t rounds);
int r200_protocol_build_stop_inventory(uint8_t *frame, size_t frame_size,
                                       size_t *frame_length);
int r200_protocol_build_get_power(uint8_t *frame, size_t frame_size,
                                  size_t *frame_length);
int r200_protocol_build_set_power(uint8_t *frame, size_t frame_size,
                                  size_t *frame_length, uint16_t power_cdbm);
int r200_protocol_parse_get_power(const uint8_t *frame, size_t frame_length,
                                  uint16_t *power_cdbm);
int r200_protocol_parse_set_power(const uint8_t *frame, size_t frame_length);

int r200_protocol_parse_inventory(const uint8_t *frame,
                                  size_t frame_length,
                                  char *epc,
                                  size_t epc_size,
                                  int8_t *rssi);

#endif
