#include "r200_protocol.h"

#include "osal_debug.h"
#include <stdio.h>
#include <string.h>

/* YND-R200 V2.3.3, PDF pages 9-10. Native multi mode is opt-in. */
int r200_protocol_build_multi_inventory(uint8_t *frame, size_t frame_size,
                                        size_t *frame_length, uint16_t rounds)
{
    uint8_t command[] = {0xAA, 0x00, R200_NATIVE_MULTI_COMMAND, 0x00,
                         R200_MULTI_PAYLOAD_LENGTH, R200_MULTI_INVENTORY_PARAMETER,
                         0, 0, 0, 0xDD};
    if (frame == NULL || frame_length == NULL || frame_size < sizeof(command) ||
        rounds < R200_MIN_MULTI_ROUNDS) {
        return -1;
    }
    command[6] = (uint8_t)(rounds >> 8);
    command[7] = (uint8_t)rounds;
    command[8] = (uint8_t)(R200_NATIVE_MULTI_COMMAND + R200_MULTI_PAYLOAD_LENGTH +
                           R200_MULTI_INVENTORY_PARAMETER + command[6] + command[7]);
    memcpy(frame, command, sizeof(command));
    *frame_length = sizeof(command);
    return 0;
}

int r200_protocol_build_stop_inventory(uint8_t *frame, size_t frame_size,
                                       size_t *frame_length)
{
    const uint8_t command[] = {0xAA, 0x00, R200_NATIVE_STOP_COMMAND, 0x00,
                               0x00, R200_NATIVE_STOP_COMMAND, 0xDD};
    if (frame == NULL || frame_length == NULL || frame_size < sizeof(command)) { return -1; }
    memcpy(frame, command, sizeof(command));
    *frame_length = sizeof(command);
    return 0;
}


/*
 * R200通信协议定义
 *
 * 数据格式：
 *
 * AA | TYPE | CMD | LEN_H | LEN_L | DATA | CHECK | DD
 *
 */


#define R200_HEADER             0xAA
#define R200_END                0xDD


// 数据类型
#define R200_TYPE_COMMAND       0x00
#define R200_TYPE_RESPONSE      0x01
#define R200_TYPE_NOTIFY        0x02


// 命令
#define R200_COMMAND_INVENTORY  0x22
#define R200_COMMAND_ERROR      0xFF
#define R200_ERROR_NO_TAG       0x15



/*
 * 校验函数
 *
 * R200协议：
 *
 * 从 TYPE 开始，
 * 到 DATA结束，
 * 所有字节累加
 *
 * 例如：
 *
 * 00 + 22 + 00 + 00
 *
 * = 22
 *
 */
static uint8_t r200_checksum(const uint8_t *data, size_t length)
{
    uint16_t sum = 0;


    for (size_t i = 0; i < length; i++)
    {
        sum = (uint16_t)(sum + data[i]);
    }


    return (uint8_t)sum;
}

/* The manual encodes transmit power in hundredths of a dBm, high byte first. */
enum {
    R200_FRAME_HEADER_INDEX = 0,
    R200_FRAME_TYPE_INDEX = 1,
    R200_FRAME_COMMAND_INDEX = 2,
    R200_FRAME_LENGTH_HIGH_INDEX = 3,
    R200_FRAME_LENGTH_LOW_INDEX = 4,
    R200_FRAME_PARAMETER_INDEX = 5,
    R200_FRAME_END_OFFSET = 1,
    R200_FRAME_CHECKSUM_OFFSET = 2,
    R200_CHECKSUM_EXCLUDED_BYTES = 3, /* Header, checksum, and end are outside the checksum input. */
    R200_POWER_HIGH_BYTE_SHIFT = 8,
    R200_GET_POWER_FRAME_LENGTH = 7,
    R200_SET_POWER_FRAME_LENGTH = 9,
    R200_GET_POWER_REPLY_LENGTH = 9,
    R200_SET_POWER_REPLY_LENGTH = 8,
    R200_EMPTY_PAYLOAD_LENGTH = 0,
    R200_SET_POWER_PARAMETER_LENGTH = 2,
    R200_POWER_REPLY_PARAMETER_LENGTH = 2,
    R200_SET_REPLY_PARAMETER_LENGTH = 1,
    R200_COMMAND_SUCCESS = 0
};

int r200_protocol_build_get_power(uint8_t *frame, size_t frame_size,
                                  size_t *frame_length)
{
    const uint8_t command[R200_GET_POWER_FRAME_LENGTH] = {
        R200_HEADER, R200_TYPE_COMMAND, R200_GET_POWER_COMMAND,
        R200_EMPTY_PAYLOAD_LENGTH, R200_EMPTY_PAYLOAD_LENGTH,
        R200_GET_POWER_COMMAND, R200_END
    };
    if (frame == NULL || frame_length == NULL || frame_size < sizeof(command)) { return -1; }
    memcpy(frame, command, sizeof(command));
    *frame_length = sizeof(command);
    return R200_COMMAND_SUCCESS;
}

int r200_protocol_build_set_power(uint8_t *frame, size_t frame_size,
                                  size_t *frame_length, uint16_t power_cdbm)
{
    uint8_t command[R200_SET_POWER_FRAME_LENGTH] = {
        R200_HEADER, R200_TYPE_COMMAND, R200_SET_POWER_COMMAND,
        R200_EMPTY_PAYLOAD_LENGTH, R200_SET_POWER_PARAMETER_LENGTH,
        (uint8_t)(power_cdbm >> R200_POWER_HIGH_BYTE_SHIFT), (uint8_t)power_cdbm,
        R200_EMPTY_PAYLOAD_LENGTH, R200_END
    };
    if (frame == NULL || frame_length == NULL || frame_size < sizeof(command)) { return -1; }
    command[sizeof(command) - R200_FRAME_CHECKSUM_OFFSET] =
        r200_checksum(&command[R200_FRAME_TYPE_INDEX],
                      sizeof(command) - R200_CHECKSUM_EXCLUDED_BYTES);
    memcpy(frame, command, sizeof(command));
    *frame_length = sizeof(command);
    return R200_COMMAND_SUCCESS;
}

static int r200_protocol_power_reply_valid(const uint8_t *frame, size_t frame_length,
                                           uint8_t command, size_t expected_length,
                                           uint8_t parameter_length)
{
    if (frame == NULL || frame_length != expected_length ||
        frame[R200_FRAME_HEADER_INDEX] != R200_HEADER ||
        frame[R200_FRAME_TYPE_INDEX] != R200_TYPE_RESPONSE ||
        frame[R200_FRAME_COMMAND_INDEX] != command ||
        frame[R200_FRAME_LENGTH_HIGH_INDEX] != R200_EMPTY_PAYLOAD_LENGTH ||
        frame[R200_FRAME_LENGTH_LOW_INDEX] != parameter_length ||
        frame[frame_length - R200_FRAME_END_OFFSET] != R200_END ||
        frame[frame_length - R200_FRAME_CHECKSUM_OFFSET] !=
            r200_checksum(&frame[R200_FRAME_TYPE_INDEX],
                          frame_length - R200_CHECKSUM_EXCLUDED_BYTES)) {
        return -1;
    }
    return R200_COMMAND_SUCCESS;
}

int r200_protocol_parse_get_power(const uint8_t *frame, size_t frame_length,
                                  uint16_t *power_cdbm)
{
    if (power_cdbm == NULL ||
        r200_protocol_power_reply_valid(frame, frame_length, R200_GET_POWER_COMMAND,
                                        R200_GET_POWER_REPLY_LENGTH,
                                        R200_POWER_REPLY_PARAMETER_LENGTH) != R200_COMMAND_SUCCESS) {
        return -1;
    }
    *power_cdbm = (uint16_t)(((uint16_t)frame[R200_FRAME_PARAMETER_INDEX] <<
                              R200_POWER_HIGH_BYTE_SHIFT) |
                             frame[R200_FRAME_PARAMETER_INDEX + 1]);
    return R200_COMMAND_SUCCESS;
}

int r200_protocol_parse_set_power(const uint8_t *frame, size_t frame_length)
{
    if (r200_protocol_power_reply_valid(frame, frame_length, R200_SET_POWER_COMMAND,
                                        R200_SET_POWER_REPLY_LENGTH,
                                        R200_SET_REPLY_PARAMETER_LENGTH) != R200_COMMAND_SUCCESS ||
        frame[R200_FRAME_PARAMETER_INDEX] != R200_COMMAND_SUCCESS) {
        return -1;
    }
    return R200_COMMAND_SUCCESS;
}



/*
 * 构造单次读卡命令
 *
 * 对应厂家Demo:
 *
 * RPRMUM_SendCommand(0x22,0x00,NULL);
 *
 *
 * 最终发送:
 *
 * AA 00 22 00 00 22 DD
 *
 */
int r200_protocol_build_inventory(uint8_t *frame,
                                  size_t frame_size,
                                  size_t *frame_length)
{


    static const uint8_t inventory[] =
    {
        R200_HEADER,        // AA 帧头

        R200_TYPE_COMMAND,  // 00 命令类型

        R200_COMMAND_INVENTORY, //22 单次读卡命令


        0x00,               // 长度高字节
        0x00,               // 长度低字节


        0x22,               // 校验


        R200_END             // DD结束
    };



    if(frame == NULL ||
       frame_length == NULL ||
       frame_size < sizeof(inventory))
    {
        return -1;
    }



    for(size_t i = 0;
        i < sizeof(inventory);
        i++)
    {
        frame[i] = inventory[i];
    }



    *frame_length = sizeof(inventory);


    return 0;
}





/*
 * 解析R200返回数据
 *
 * R200读到标签以后：
 *
 * AA 02 22 .... DD
 *
 *
 */
int r200_protocol_parse_inventory(const uint8_t *frame,
                                  size_t frame_length,
                                  char *epc,
                                  size_t epc_size,
                                  int8_t *rssi)
{

    uint16_t payload_length;

    size_t epc_length;

    uint8_t expected_checksum;



    // 参数检查

    if(frame == NULL ||
       epc == NULL ||
       rssi == NULL ||
       frame_length < 7 ||
       epc_size == 0)
    {
        osal_printk("R200 parse invalid args\r\n");

        return -1;
    }



    // 检查帧头和结束

    if(frame[0] != R200_HEADER ||
       frame[frame_length-1] != R200_END)
    {

        osal_printk("R200 parse bad header/end\r\n");

        return -1;
    }




    /*
     * 获取数据长度
     *
     * LEN_H LEN_L
     *
     */

    payload_length =
        (uint16_t)(((uint16_t)frame[3]<<8)
        | frame[4]);




    /*
     * 总长度:
     *
     * 7 + payload
     *
     */

    if(frame_length != (size_t)payload_length + 7)
    {

        osal_printk(
        "R200 parse length mismatch\r\n");


        return -1;
    }




    /*
     * 校验
     *
     */

    expected_checksum =
        r200_checksum(
        &frame[1],
        4 + payload_length);



    if(expected_checksum != frame[frame_length-2])
    {

        osal_printk(
        "R200 checksum error\r\n");


        return -1;
    }




    /*
     * 判断错误响应
     *
     */

    if(frame[1] == R200_TYPE_RESPONSE &&
       frame[2] == R200_COMMAND_ERROR)
    {
        if(payload_length > 0)
        {
            if(frame[5] == R200_ERROR_NO_TAG)
            {
                return R200_PARSE_NO_TAG;
            }

            osal_printk(
            "R200 command error code: 0x%02X\r\n",
            frame[5]);
        }
        else
        {
            osal_printk(
            "R200 command error\r\n");
        }


        return R200_PARSE_COMMAND_ERROR;
    }





    /*
     * 判断是不是读卡通知
     *
     * 类型:
     *
     * 02
     *
     * 命令:
     *
     * 22
     *
     */

    if(frame[1] != R200_TYPE_NOTIFY ||
       (frame[2] != R200_COMMAND_INVENTORY && frame[2] != R200_NATIVE_MULTI_COMMAND) ||
       payload_length < 5)
    {

        osal_printk(
        "R200 not inventory notify\r\n");


        return -1;
    }





    /*
     *
     * EPC长度
     *
     */

    epc_length = payload_length - 5;



    if(epc_length == 0 ||
       (epc_length*2+1)>epc_size)
    {

        osal_printk(
        "R200 bad epc length\r\n");


        return -1;
    }





    /*
     *
     * 转换EPC
     *
     * 原始:
     *
     * 12 AB CD EF
     *
     *
     * 字符:
     *
     * "12ABCDEF"
     *
     */

    for(size_t i=0;i<epc_length;i++)
    {

        snprintf(
        &epc[i*2],
        3,
        "%02X",
        frame[8+i]);

    }



    epc[epc_length*2]='\0';

    *rssi = (int8_t)frame[5];



    return 0;

}
