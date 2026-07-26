#ifndef RC522_READER_H
#define RC522_READER_H

#include <stdint.h>

#define RC522_TAG_ID_MAX_LEN 16

void rc522_reader_init(void);
int rc522_reader_read_tag(char *tag_id, unsigned int tag_id_size);

#endif
