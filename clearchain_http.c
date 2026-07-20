#include <string.h>

#include "soc_osal.h"
#include "osal_debug.h"

#include "my_wifi_tcp.h"
#include "clearchain_config.h"
#include "clearchain_http.h"


int clearchain_send_scan(const char *tag_id)
{

    char http_request[512];


    snprintf(
        http_request,
        sizeof(http_request),

        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "\r\n"
        "{"
        "\"tag_id\":\"%s\","
        "\"location\":\"Checkpoint-01\","
        "\"stage\":4,"
        "\"scan_type\":1,"
        "\"stage_code\":\"PUB-c72m\""
        "}",

        SERVER_PATH,
        SERVER_IP,
        strlen(tag_id)+120,
        tag_id
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


    TCP_SendData(
        fd,
        http_request
    );


    printf(
        "POST sent:\r\n%s\r\n",
        http_request
    );


    return 0;
}