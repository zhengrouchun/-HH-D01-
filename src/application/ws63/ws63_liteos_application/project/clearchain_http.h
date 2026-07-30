#ifndef CLEARCHAIN_HTTP_H
#define CLEARCHAIN_HTTP_H


/**
 * @brief
 * 发送 RFID 扫描数据到后端 /scan 接口
 *
 * @param chip_uid
 * RFID标签 EPC 唯一编号
 *
 * 示例:
 * E28011704000021D35AFADD9
 *
 * @return
 * 0   成功
 * -1  失败
 */
int clearchain_send_scan(
    const char *chip_uid
);


#endif