#include <stdio.h>
#include <string.h>

#include "soc_osal.h"
#include "osal_debug.h"

#include "my_wifi_tcp.h"
#include "clearchain_config.h"
#include "clearchain_http.h"


int clearchain_send_scan(const char *tag_id)
{
    char json_body[256];
    char http_request[512];

    snprintf(
        json_body,
        sizeof(json_body),
        "{"
        "\"tag_id\":\"%s\","
        "\"location\":\"%s\","
        "\"stage\":%d,"
        "\"scan_type\":%d,"
        "\"stage_code\":\"%s\""
        "}",
        tag_id,
        SCAN_LOCATION,
        SCAN_STAGE,
        SCAN_TYPE,
        SCAN_STAGE_CODE
    );

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

    fd = TCPClient_ConnectToServer(
        SERVER_IP,
        SERVER_PORT
    );

    if(fd < 0)
    {
        printf("HTTP connect failed\r\n");
        return -1;
    }

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
