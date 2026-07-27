#include "r200_protocol.h"

#include "osal_debug.h"
#include <stdio.h>


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
                                  size_t epc_size)
{

    uint16_t payload_length;

    size_t epc_length;

    uint8_t expected_checksum;



    // 参数检查

    if(frame == NULL ||
       epc == NULL ||
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

        osal_printk(
        "R200 command error\r\n");


        return -1;
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
       frame[2] != R200_COMMAND_INVENTORY ||
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



    return 0;

}