#ifndef R200_PROTOCOL_H
#define R200_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define R200_MAX_FRAME_SIZE 128
#define R200_PARSE_COMMAND_ERROR (-2)

int r200_protocol_build_inventory(uint8_t *frame,
                                  size_t frame_size,
                                  size_t *frame_length);

int r200_protocol_parse_inventory(const uint8_t *frame,
                                  size_t frame_length,
                                  char *epc,
                                  size_t epc_size);

#endif
