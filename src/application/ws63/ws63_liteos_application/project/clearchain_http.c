#include <stdio.h>
#include <string.h>

#include "soc_osal.h"
#include "osal_debug.h"

#include "my_wifi_tcp.h"
#include "clearchain_config.h"
#include "clearchain_http.h"


int clearchain_send_scan(const char *chip_uid)
{
    char json_body[256];
    char http_request[512];


    /*
     * ClearChain智能模式JSON
     *
     * 示例:
     *
     * {
     *   "chip_uid":"E28011704000021D35AFADD9",
     *   "scanner_id":"scanner_checkpoint",
     *   "scan_type":1,
     *   "stage_code":"PUB-c72m"
     * }
     *
     */

    snprintf(
        json_body,
        sizeof(json_body),
        "{"
        "\"chip_uid\":\"%s\","
        "\"scanner_id\":\"%s\","
        "\"scan_type\":%d,"
        "\"stage_code\":\"%s\""
        "}",
        chip_uid,
        SCANNER_ID,
        SCAN_TYPE,
        SCAN_STAGE_CODE
    );


    /*
     * 组装HTTP POST请求
     */

    snprintf(
        http_request,
        sizeof(http_request),
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "\r\n"
        "%s",
        SERVER_PATH,
        SERVER_IP,
        (int)strlen(json_body),
        json_body
    );


    int fd;


    /*
     * TCP连接服务器
     */

    fd = TCPClient_ConnectToServer(
        SERVER_IP,
        SERVER_PORT
    );


    if(fd < 0)
    {
        printf("HTTP connect failed\r\n");
        return -1;
    }


    /*
     * 发送HTTP请求
     */

    if(TCP_SendData(
        fd,
        http_request
    ) < 0)
    {
        printf("HTTP send failed\r\n");

        TCP_CloseClient(fd);

        return -1;
    }


    printf(
        "POST sent:\r\n%s\r\n",
        http_request
    );


    TCP_CloseClient(fd);


    return 0;
}