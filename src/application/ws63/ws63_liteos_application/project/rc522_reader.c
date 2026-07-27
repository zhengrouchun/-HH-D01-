#include "rc522_reader.h"

#include <stdio.h>
#include <string.h>

#include "osal_debug.h"
#include "soc_osal.h"
#include "rc522_ws63_gpio.h"

#define MAXRLEN 18

#define MI_OK 0
#define MI_NOTAGERR (-1)
#define MI_ERR (-2)

#define PCD_IDLE 0x00
#define PCD_AUTHENT 0x0E
#define PCD_TRANSCEIVE 0x0C
#define PCD_RESETPHASE 0x0F
#define PCD_CALCCRC 0x03

#define PICC_REQALL 0x52
#define PICC_ANTICOLL1 0x93
#define PICC_HALT 0x50

#define CommandReg 0x01
#define ComIEnReg 0x02
#define ComIrqReg 0x04
#define DivIrqReg 0x05
#define ErrorReg 0x06
#define Status2Reg 0x08
#define FIFODataReg 0x09
#define FIFOLevelReg 0x0A
#define ControlReg 0x0C
#define BitFramingReg 0x0D
#define CollReg 0x0E
#define ModeReg 0x11
#define TxControlReg 0x14
#define TxAutoReg 0x15
#define CRCResultRegM 0x21
#define CRCResultRegL 0x22
#define TModeReg 0x2A
#define TPrescalerReg 0x2B
#define TReloadRegH 0x2C
#define TReloadRegL 0x2D

static uint8_t read_raw_rc(uint8_t address)
{
    return rc522_spi_read_reg(address);
}

static void write_raw_rc(uint8_t address, uint8_t value)
{
    rc522_spi_write_reg(address, value);
}

static void set_bit_mask(uint8_t reg, uint8_t mask)
{
    uint8_t tmp = read_raw_rc(reg);
    write_raw_rc(reg, tmp | mask);
}

static void clear_bit_mask(uint8_t reg, uint8_t mask)
{
    uint8_t tmp = read_raw_rc(reg);
    write_raw_rc(reg, tmp & (uint8_t)(~mask));
}

static void calculate_crc(uint8_t *in_data, uint8_t len, uint8_t *out_data)
{
    uint8_t i;
    uint8_t n;

    clear_bit_mask(DivIrqReg, 0x04);
    write_raw_rc(CommandReg, PCD_IDLE);
    set_bit_mask(FIFOLevelReg, 0x80);

    for(i = 0; i < len; i++)
    {
        write_raw_rc(FIFODataReg, in_data[i]);
    }

    write_raw_rc(CommandReg, PCD_CALCCRC);

    i = 0xFF;
    do
    {
        n = read_raw_rc(DivIrqReg);
        i--;
    } while((i != 0) && !(n & 0x04));

    out_data[0] = read_raw_rc(CRCResultRegL);
    out_data[1] = read_raw_rc(CRCResultRegM);
}

static int pcd_com_mf522(uint8_t command, uint8_t *in_data, uint8_t in_len_byte, uint8_t *out_data, uint16_t *out_len_bit)
{
    int status = MI_ERR;
    uint8_t irq_en = 0x00;
    uint8_t wait_for = 0x00;
    uint8_t last_bits;
    uint8_t n;
    uint16_t i;

    switch(command)
    {
        case PCD_AUTHENT:
            irq_en = 0x12;
            wait_for = 0x10;
            break;
        case PCD_TRANSCEIVE:
            irq_en = 0x77;
            wait_for = 0x30;
            break;
        default:
            break;
    }

    write_raw_rc(ComIEnReg, irq_en | 0x80);
    clear_bit_mask(ComIrqReg, 0x80);
    write_raw_rc(CommandReg, PCD_IDLE);
    set_bit_mask(FIFOLevelReg, 0x80);

    for(i = 0; i < in_len_byte; i++)
    {
        write_raw_rc(FIFODataReg, in_data[i]);
    }

    write_raw_rc(CommandReg, command);

    if(command == PCD_TRANSCEIVE)
    {
        set_bit_mask(BitFramingReg, 0x80);
    }

    i = 600;
    do
    {
        n = read_raw_rc(ComIrqReg);
        i--;
    } while((i != 0) && !(n & 0x01) && !(n & wait_for));

    clear_bit_mask(BitFramingReg, 0x80);

    if(i != 0)
    {
        if(!(read_raw_rc(ErrorReg) & 0x1B))
        {
            status = MI_OK;

            if(n & irq_en & 0x01)
            {
                status = MI_NOTAGERR;
            }

            if(command == PCD_TRANSCEIVE)
            {
                n = read_raw_rc(FIFOLevelReg);
                last_bits = read_raw_rc(ControlReg) & 0x07;

                if(last_bits)
                {
                    *out_len_bit = (uint16_t)((n - 1) * 8 + last_bits);
                }
                else
                {
                    *out_len_bit = (uint16_t)(n * 8);
                }

                if(n == 0)
                {
                    n = 1;
                }

                if(n > MAXRLEN)
                {
                    n = MAXRLEN;
                }

                for(i = 0; i < n; i++)
                {
                    out_data[i] = read_raw_rc(FIFODataReg);
                }
            }
        }
        else
        {
            status = MI_ERR;
        }
    }

    set_bit_mask(ControlReg, 0x80);
    write_raw_rc(CommandReg, PCD_IDLE);

    return status;
}

static int pcd_request(uint8_t req_code, uint8_t *tag_type)
{
    int status;
    uint16_t un_len;
    uint8_t buffer[MAXRLEN];

    clear_bit_mask(Status2Reg, 0x08);
    write_raw_rc(BitFramingReg, 0x07);
    set_bit_mask(TxControlReg, 0x03);

    buffer[0] = req_code;

    status = pcd_com_mf522(PCD_TRANSCEIVE, buffer, 1, buffer, &un_len);

    if((status == MI_OK) && (un_len == 0x10))
    {
        tag_type[0] = buffer[0];
        tag_type[1] = buffer[1];
    }
    else
    {
        status = MI_ERR;
    }

    return status;
}

static int pcd_anticoll(uint8_t *serial)
{
    int status;
    uint8_t i;
    uint8_t serial_check = 0;
    uint16_t un_len;
    uint8_t buffer[MAXRLEN];

    clear_bit_mask(Status2Reg, 0x08);
    write_raw_rc(BitFramingReg, 0x00);
    clear_bit_mask(CollReg, 0x80);

    buffer[0] = PICC_ANTICOLL1;
    buffer[1] = 0x20;

    status = pcd_com_mf522(PCD_TRANSCEIVE, buffer, 2, buffer, &un_len);

    if(status == MI_OK)
    {
        for(i = 0; i < 4; i++)
        {
            serial[i] = buffer[i];
            serial_check ^= buffer[i];
        }

        if(serial_check != buffer[i])
        {
            status = MI_ERR;
        }
    }

    set_bit_mask(CollReg, 0x80);

    return status;
}

static void pcd_halt(void)
{
    uint16_t un_len;
    uint8_t buffer[MAXRLEN];

    buffer[0] = PICC_HALT;
    buffer[1] = 0;
    calculate_crc(buffer, 2, &buffer[2]);

    (void)pcd_com_mf522(PCD_TRANSCEIVE, buffer, 4, buffer, &un_len);
}

static void pcd_reset(void)
{
    rc522_gpio_reset();
    write_raw_rc(CommandReg, PCD_RESETPHASE);
    osal_msleep(5);

    write_raw_rc(ModeReg, 0x3D);
    write_raw_rc(TReloadRegL, 30);
    write_raw_rc(TReloadRegH, 0);
    write_raw_rc(TModeReg, 0x8D);
    write_raw_rc(TPrescalerReg, 0x3E);
    write_raw_rc(TxAutoReg, 0x40);
}

static void pcd_antenna_on(void)
{
    uint8_t value = read_raw_rc(TxControlReg);

    if(!(value & 0x03))
    {
        set_bit_mask(TxControlReg, 0x03);
    }
}

void rc522_reader_init(void)
{
    rc522_gpio_init();
    pcd_reset();
    clear_bit_mask(TxControlReg, 0x03);
    osal_msleep(2);
    pcd_antenna_on();
}

int rc522_reader_read_tag(char *tag_id, unsigned int tag_id_size)
{
    uint8_t buffer[MAXRLEN] = {0};
    uint8_t serial[4] = {0};

    if(tag_id == NULL || tag_id_size < RC522_TAG_ID_MAX_LEN)
    {
        return -1;
    }

    if(pcd_request(PICC_REQALL, buffer) != MI_OK)
    {
        return -1;
    }

    if(pcd_anticoll(serial) != MI_OK)
    {
        return -1;
    }

    snprintf(
        tag_id,
        tag_id_size,
        "%02X%02X%02X%02X",
        serial[0],
        serial[1],
        serial[2],
        serial[3]
    );

    pcd_halt();

    osal_printk("RC522 tag id: %s\r\n", tag_id);

    return 0;
}
