#include "clearchain_device_http.h"
#include "clearchain_config.h"
#include "clearchain_runtime_config.h"
#include "cJSON.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "my_wifi_tcp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEVICE_RESPONSE_SIZE 2048U
#define DEVICE_READINGS_PER_POST 32U

static int send_all(int fd, const char *data, size_t length)
{
    size_t sent = 0;
    while (sent < length) {
        int n = send(fd, data + sent, (int)(length - sent), 0);
        if (n <= 0) { return -1; }
        sent += (size_t)n;
    }
    return 0;
}

/* A bounded HTTP/1.1 client for the Pi's JSON API. No implicit redirect follows. */
static int exchange(const char *method, const char *path, const char *body,
                    char *response_body, size_t response_size)
{
    struct sockaddr_in address = {0};
    struct hostent *resolved;
    struct timeval timeout = {2, 0};
    char header[384];
    char response[DEVICE_RESPONSE_SIZE];
    char *payload;
    size_t used = 0;
    size_t body_size = body == NULL ? 0U : strlen(body);
    int fd, n, status = -1;
    long content_length = -1;
    if (!CLEARCHAIN_UPLOAD_ALLOWED || method == NULL || path == NULL ||
        response_body == NULL || response_size == 0U ||
        CLEARCHAIN_DEVICE_HOST[0] == '\0') { return -1; }
    response_body[0] = '\0';
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { return -1; }
    address.sin_family = AF_INET;
    address.sin_port = htons(CLEARCHAIN_DEVICE_PORT);
    address.sin_addr.s_addr = inet_addr(CLEARCHAIN_DEVICE_HOST);
    if (address.sin_addr.s_addr == INADDR_NONE) {
        resolved = gethostbyname(CLEARCHAIN_DEVICE_HOST);
        if (resolved == NULL || resolved->h_addr_list == NULL || resolved->h_addr_list[0] == NULL) {
            goto done;
        }
        (void)memcpy(&address.sin_addr, resolved->h_addr_list[0], sizeof(address.sin_addr));
    }
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0) { goto done; }
    n = snprintf(header, sizeof(header),
        "%s %s HTTP/1.1\r\nHost: %s:%d\r\nContent-Type: application/json\r\n"
        "Content-Length: %u\r\nConnection: close\r\n\r\n",
        method, path, CLEARCHAIN_DEVICE_HOST, CLEARCHAIN_DEVICE_PORT,
        (unsigned int)body_size);
    if (n < 0 || (size_t)n >= sizeof(header) || send_all(fd, header, (size_t)n) != 0 ||
        (body_size != 0U && send_all(fd, body, body_size) != 0)) { goto done; }
    while (used < sizeof(response) - 1U) {
        n = recv(fd, response + used, (int)(sizeof(response) - 1U - used), 0);
        if (n < 0) { goto done; }
        if (n == 0) { break; }
        used += (size_t)n;
        response[used] = '\0';
        payload = strstr(response, "\r\n\r\n");
        if (payload != NULL) {
            const char *length_header = strstr(response, "\r\nContent-Length:");
            if (length_header == NULL) { length_header = strstr(response, "\r\ncontent-length:"); }
            if (length_header != NULL) { content_length = strtol(strchr(length_header + 2, ':') + 1, NULL, 10); }
            if (content_length >= 0 && (size_t)(used - (size_t)(payload + 4 - response)) >=
                (size_t)content_length) { break; }
        }
    }
    response[used] = '\0';
    payload = strstr(response, "\r\n\r\n");
    if (payload == NULL || sscanf(response, "HTTP/%*d.%*d %d", &status) != 1) {
        status = -1;
        goto done;
    }
    payload += 4;
    if (content_length >= 0 && strlen(payload) < (size_t)content_length) { status = -1; goto done; }
    if (strlen(payload) >= response_size) { status = -1; goto done; }
    (void)strcpy(response_body, payload);
done:
    (void)TCP_CloseClient(fd);
    return status;
}

int clearchain_device_http_get_state(clearchain_device_state_t *state)
{
    char body[DEVICE_RESPONSE_SIZE];
    int status = exchange("GET", "/device/state", NULL, body, sizeof(body));
    if (status == 200 && clearchain_device_state_parse(body, state) != 0) { return -1; }
    return status;
}

int clearchain_device_http_select_mode(clearchain_device_mode_t mode,
                                       clearchain_device_state_t *returned_state)
{
    cJSON *root;
    char *request;
    char response[DEVICE_RESPONSE_SIZE];
    const char *name = clearchain_device_mode_name(mode);
    const char *code = clearchain_device_access_code(mode);
    int status;
    if (name == NULL || code == NULL || returned_state == NULL) { return -1; }
    root = cJSON_CreateObject();
    if (root == NULL) { return -1; }
    if (cJSON_AddStringToObject(root, "command", "SELECT_MODE") == NULL ||
        cJSON_AddStringToObject(root, "mode", name) == NULL ||
        cJSON_AddStringToObject(root, "access_code", code) == NULL ||
        cJSON_AddStringToObject(root, "source", "DEVICE") == NULL) {
        cJSON_Delete(root); return -1;
    }
    request = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (request == NULL) { return -1; }
    status = exchange("POST", "/device/command", request, response, sizeof(response));
    cJSON_free(request);
    if (status == 200 && clearchain_device_state_parse(response, returned_state) != 0) { return -1; }
    return status;
}

char *clearchain_device_http_scan_json(const clearchain_device_state_t *state,
                                      const clearchain_device_reading_t *readings,
                                      size_t count, int final)
{
    cJSON *root, *array;
    const char *name, *code;
    char *json = NULL;
    if (state == NULL || readings == NULL || count == 0U || count > DEVICE_READINGS_PER_POST ||
        state->batch_id[0] == '\0' ||
        (name = clearchain_device_mode_name(state->mode)) == NULL ||
        (code = clearchain_device_access_code(state->mode)) == NULL) { return NULL; }
    root = cJSON_CreateObject();
    array = cJSON_CreateArray();
    if (root == NULL || array == NULL) { cJSON_Delete(root); cJSON_Delete(array); return NULL; }
    if (cJSON_AddStringToObject(root, "mode", name) == NULL ||
        cJSON_AddStringToObject(root, "batch_id", state->batch_id) == NULL ||
        cJSON_AddStringToObject(root, "access_code", code) == NULL ||
        cJSON_AddBoolToObject(root, "final", final != 0) == NULL ||
        !cJSON_AddItemToObject(root, "readings", array)) {
        cJSON_Delete(array); cJSON_Delete(root); return NULL;
    }
    array = NULL; /* Owned by root after AddItemToObject. */
    for (size_t i = 0; i < count; ++i) {
        cJSON *item;
        if (readings[i].chip_uid == NULL || readings[i].chip_uid[0] == '\0' ||
            (item = cJSON_CreateObject()) == NULL) { goto done; }
        if (cJSON_AddStringToObject(item, "chip_uid", readings[i].chip_uid) == NULL ||
            cJSON_AddNumberToObject(item, "rssi_dbm", readings[i].rssi_dbm) == NULL) {
            cJSON_Delete(item); goto done;
        }
        cJSON_AddItemToArray(cJSON_GetObjectItemCaseSensitive(root, "readings"), item);
    }
    json = cJSON_PrintUnformatted(root);
done:
    cJSON_Delete(root);
    return json;
}

int clearchain_device_http_post_scan(const clearchain_device_state_t *state,
                                     const clearchain_device_reading_t *readings,
                                     size_t count, int final)
{
    char response[DEVICE_RESPONSE_SIZE];
    char *request = clearchain_device_http_scan_json(state, readings, count, final);
    int status;
    if (request == NULL) { return -1; }
    status = exchange("POST", "/device/scan", request, response, sizeof(response));
    cJSON_free(request);
    return status;
}
