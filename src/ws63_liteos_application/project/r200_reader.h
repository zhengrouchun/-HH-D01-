#ifndef R200_READER_H
#define R200_READER_H

#include <stddef.h>

#define R200_TAG_ID_MAX_LEN 65

int r200_reader_init(void);
int r200_reader_read_epc(char *epc, size_t epc_size);

#endif
