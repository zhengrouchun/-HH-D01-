#ifndef TEST_SPI_H
#define TEST_SPI_H
#include <stdbool.h>
#include "errcode.h"
#define SPI_BUS_0 0
#define HAL_SPI_FRAME_FORMAT_STANDARD 0
#define HAL_SPI_FRAME_SIZE_8 7
typedef struct {
    bool is_slave;
    unsigned int slave_num,bus_clk,freq_mhz,clk_polarity,clk_phase,frame_format,spi_frame_format,frame_size,tmod;
} spi_attr_t;
typedef struct { unsigned int unused; } spi_extra_attr_t;
typedef struct { uint8_t *tx_buff; uint32_t tx_bytes; } spi_xfer_data_t;
errcode_t uapi_spi_init(unsigned int bus,const spi_attr_t *attr,const spi_extra_attr_t *extra);
errcode_t uapi_spi_master_write(unsigned int bus,const spi_xfer_data_t *xfer,unsigned int timeout);
#endif
