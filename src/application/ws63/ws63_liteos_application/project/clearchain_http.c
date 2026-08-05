#include <stdio.h>
#include <string.h>

#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "osal_debug.h"

#include "my_wifi_tcp.h"
#include "clearchain_config.h"
#include "clearchain_http.h"
#include "clearchain_key.h"

static int clearchain_recv_http_response(int fd)
{
    char recv_buf[512];
    char response[1024];
    int response_len = 0;
    int status_code = -1;

    memset(response, 0, sizeof(response));

    while (1) {
        int ret = recv(fd, recv_buf, sizeof(recv_buf) - 1, 0);
        if (ret > 0) {
            int copy_len;

            recv_buf[ret] = '\0';
            printf("HTTP recv chunk (%d bytes):\r\n%s\r\n", ret, recv_buf);

            if (status_code < 0 &&
                sscanf(recv_buf, "HTTP/%*d.%*d %d", &status_code) == 1) {
                printf("HTTP status code: %d\r\n", status_code);
            }

            copy_len = ret;
            if (response_len + copy_len >= (int)sizeof(response) - 1) {
                copy_len = (int)sizeof(response) - 1 - response_len;
            }

            if (copy_len > 0) {
                memcpy(&response[response_len], recv_buf, copy_len);
                response_len += copy_len;
                response[response_len] = '\0';
            }
        } else if (ret == 0) {
            break;
        } else {
            printf("HTTP recv failed\r\n");
            break;
        }
    }

    if (status_code >= 200 && status_code < 300) {
        return 0;
    }

    if (status_code < 0 && response_len > 0) {
        sscanf(response, "HTTP/%*d.%*d %d", &status_code);
    }

    printf("HTTP response not successful:\r\n%s\r\n", response_len > 0 ? response : "(empty)");
    return -1;
}

static int clearchain_connect_http_server(void)
{
    int fd;
    struct sockaddr_in server_addr = {0};
    unsigned long ip_addr;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        printf("HTTP socket create failed\r\n");
        return -1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(CLEARCHAIN_HTTP_PORT);

    ip_addr = inet_addr(CLEARCHAIN_HTTP_HOST);
    if (ip_addr != INADDR_NONE) {
        server_addr.sin_addr.s_addr = ip_addr;
    } else {
        struct hostent *host = gethostbyname(CLEARCHAIN_HTTP_HOST);
        if (host == NULL || host->h_addr_list == NULL || host->h_addr_list[0] == NULL) {
            printf("HTTP DNS resolve failed: %s\r\n", CLEARCHAIN_HTTP_HOST);
            TCP_CloseClient(fd);
            return -1;
        }

        memcpy(&server_addr.sin_addr, host->h_addr_list[0], sizeof(server_addr.sin_addr));
    }

    if (connect(fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        printf("HTTP connect failed: %s:%d\r\n", CLEARCHAIN_HTTP_HOST, CLEARCHAIN_HTTP_PORT);
        TCP_CloseClient(fd);
        return -1;
    }

    {
        struct timeval timeout = {5, 0};
        if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
            printf("HTTP set recv timeout failed\r\n");
        }
    }

    printf("HTTP connect success: %s:%d\r\n", CLEARCHAIN_HTTP_HOST, CLEARCHAIN_HTTP_PORT);
    return fd;
}

int clearchain_send_scan(const char *chip_uid)
{
    char json_body[256];
    char http_request[640];
    const clearchain_stage_config_t *stage_config;
    int fd;

    if (chip_uid == NULL || chip_uid[0] == '\0') {
        printf("HTTP chip_uid empty\r\n");
        return -1;
    }

    stage_config = clearchain_key_get_stage_config();

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
        stage_config->scanner_id,
        SCAN_TYPE,
        stage_config->stage_code
    );

    printf("HTTP scan stage: %u (%s), scanner_id=%s, stage_code=%s\r\n",
           stage_config->stage,
           stage_config->name,
           stage_config->scanner_id,
           stage_config->stage_code);

    /*
     * WS63 sends plain HTTP over raw TCP. The ngrok URL provides the host
     * name; this request uses that host with HTTP port 80.
     */
    snprintf(
        http_request,
        sizeof(http_request),
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: application/json\r\n"
        "ngrok-skip-browser-warning: true\r\n"
        "Connection: close\r\n"
        "Content-Length: %d\r\n"
        "\r\n"
        "%s",
        CLEARCHAIN_HTTP_PATH,
        CLEARCHAIN_HTTP_HOST,
        (int)strlen(json_body),
        json_body
    );

    fd = clearchain_connect_http_server();
    if (fd < 0) {
        return -1;
    }

    if (TCP_SendData(fd, http_request) < 0) {
        printf("HTTP send failed\r\n");
        TCP_CloseClient(fd);
        return -1;
    }

    printf("POST sent:\r\n%s\r\n", http_request);

    if (clearchain_recv_http_response(fd) != 0) {
        printf("HTTP request failed\r\n");
        TCP_CloseClient(fd);
        return -1;
    }

    printf("HTTP request success\r\n");
    TCP_CloseClient(fd);

    return 0;
}
