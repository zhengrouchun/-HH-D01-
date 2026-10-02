#include <stdio.h>
#include <string.h>

#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "osal_debug.h"

#include "my_wifi_tcp.h"
#include "clearchain_config.h"
#include "clearchain_http.h"
#include "clearchain_key.h"
#include "clearchain_runtime_config.h"

static void clearchain_print_redirect_location(const char *response)
{
    const char *location = strstr(response, "\r\nLocation:");
    const char *value;
    const char *end;
    char location_buf[256];
    int location_len;

    if (location == NULL) {
        location = strstr(response, "\r\nlocation:");
    }

    if (location == NULL) {
        printf("HTTP redirect Location header not found\r\n");
        return;
    }

    value = strchr(location + 2, ':');
    if (value == NULL) {
        printf("HTTP redirect Location header parse failed\r\n");
        return;
    }

    value++;
    while (*value == ' ') {
        value++;
    }

    end = strstr(value, "\r\n");
    if (end == NULL) {
        end = value + strlen(value);
    }

    location_len = (int)(end - value);
    if (location_len >= (int)sizeof(location_buf)) {
        location_len = (int)sizeof(location_buf) - 1;
    }

    memcpy(location_buf, value, location_len);
    location_buf[location_len] = '\0';

    printf("HTTP redirect Location: %s\r\n", location_buf);
}

static const char *clearchain_skip_json_space(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }

    return p;
}

static int clearchain_json_string_field_equals(const char *json, const char *field, const char *value)
{
    char key[48];
    const char *p = json;
    size_t value_len = strlen(value);

    snprintf(key, sizeof(key), "\"%s\"", field);

    while ((p = strstr(p, key)) != NULL) {
        p += strlen(key);
        p = clearchain_skip_json_space(p);

        if (*p != ':') {
            continue;
        }

        p++;
        p = clearchain_skip_json_space(p);

        if (*p != '"') {
            continue;
        }

        p++;
        if (strncmp(p, value, value_len) == 0 && p[value_len] == '"') {
            return 1;
        }
    }

    return 0;
}

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

    if (status_code < 0 && response_len > 0) {
        sscanf(response, "HTTP/%*d.%*d %d", &status_code);
    }

    if (status_code >= 200 && status_code < 300) {
        if (clearchain_json_string_field_equals(response, "color", "RED") ||
            clearchain_json_string_field_equals(response, "status", "RED") ||
            clearchain_json_string_field_equals(response, "status", "INSPECTION REQUIRED") ||
            clearchain_json_string_field_equals(response, "status", "ALERT") ||
            clearchain_json_string_field_equals(response, "result", "ALERT")) {
            return CLEARCHAIN_SCAN_LED_RED;
        }

        if (clearchain_json_string_field_equals(response, "color", "ORANGE") ||
            clearchain_json_string_field_equals(response, "color", "YELLOW") ||
            clearchain_json_string_field_equals(response, "status", "YELLOW") ||
            clearchain_json_string_field_equals(response, "status", "VERIFY") ||
            clearchain_json_string_field_equals(response, "status", "MONITOR") ||
            clearchain_json_string_field_equals(response, "result", "MONITOR")) {
            return CLEARCHAIN_SCAN_LED_ORANGE;
        }

        if (clearchain_json_string_field_equals(response, "color", "GREEN") ||
            clearchain_json_string_field_equals(response, "status", "GREEN") ||
            clearchain_json_string_field_equals(response, "status", "APPROVED") ||
            clearchain_json_string_field_equals(response, "status", "AUTHORIZED") ||
            clearchain_json_string_field_equals(response, "result", "AUTHORIZED")) {
            return CLEARCHAIN_SCAN_LED_GREEN;
        }

        printf("HTTP response LED state not found, default ORANGE\r\n");
        return CLEARCHAIN_SCAN_LED_ORANGE;
    }

    if (status_code >= 300 && status_code < 400) {
        printf("HTTP redirect received: %d\r\n", status_code);
        clearchain_print_redirect_location(response);
        printf("Plain HTTP request was redirected. Backend/ngrok still needs HTTP-to-HTTPS redirect disabled.\r\n");
    }

    printf("HTTP response not successful:\r\n%s\r\n", response_len > 0 ? response : "(empty)");
    return -1;
}

static int clearchain_connect_http_server(void)
{
    int fd;
    struct sockaddr_in server_addr = {0};
    unsigned long ip_addr;

    /* Gate every endpoint before socket creation or DNS resolution. */
    if (!CLEARCHAIN_UPLOAD_ALLOWED) {
        printf("[CLEAR HTTP] upload disabled; destination is not confirmed\r\n");
        return -1;
    }

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
    char host_header[96];
    const clearchain_stage_config_t *stage_config;
    int fd;
    int scan_led;

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
    printf("HTTP target: http://%s:%d%s\r\n",
           CLEARCHAIN_HTTP_HOST,
           CLEARCHAIN_HTTP_PORT,
           CLEARCHAIN_HTTP_PATH);

    if (CLEARCHAIN_HTTP_PORT == 80) {
        snprintf(host_header, sizeof(host_header), "%s", CLEARCHAIN_HTTP_HOST);
    } else {
        snprintf(host_header, sizeof(host_header), "%s:%d",
                 CLEARCHAIN_HTTP_HOST, CLEARCHAIN_HTTP_PORT);
    }

    /*
     * WS63 sends plain HTTP over raw TCP. For ngrok TCP tunnels the public
     * host usually uses a non-80 port, so include the port in Host too.
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
        host_header,
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

    printf("POST sent: %s, body_len=%d\r\n",
           CLEARCHAIN_HTTP_PATH,
           (int)strlen(json_body));

    scan_led = clearchain_recv_http_response(fd);
    if (scan_led < 0) {
        printf("HTTP request failed\r\n");
        TCP_CloseClient(fd);
        return -1;
    }

    printf("HTTP request success, scan LED=%d\r\n", scan_led);
    TCP_CloseClient(fd);

    return scan_led;
}

static int clearchain_json_escape(const char *input, char *output, size_t output_size)
{
    size_t used = 0;
    if (input == NULL || output == NULL || output_size == 0U) {
        return -1;
    }
    for (const unsigned char *p = (const unsigned char *)input; *p != 0U; p++) {
        if (*p < 0x20U) {
            return -1;
        }
        if (*p == '"' || *p == '\\') {
            if (used + 2U >= output_size) {
                return -1;
            }
            output[used++] = '\\';
        } else if (used + 1U >= output_size) {
            return -1;
        }
        output[used++] = (char)*p;
    }
    output[used] = '\0';
    return 0;
}

static int clearchain_send_all(int fd, const char *data, size_t length)
{
    size_t sent = 0U;
    while (sent < length) {
        int ret = send(fd, data + sent, (int)(length - sent), 0);
        if (ret <= 0) {
            return -1;
        }
        sent += (size_t)ret;
    }
    return 0;
}

static int clearchain_receive_raw_response(int fd, const char *path,
                                           char *response, size_t response_size)
{
    size_t received = 0U;
    int status_code = -1;

    if (response == NULL || response_size < 16U) {
        return -1;
    }
    response[0] = '\0';
    for (;;) {
        int ret;
        if (received + 1U >= response_size) {
            return -1;
        }
        ret = recv(fd, response + received, (int)(response_size - received - 1U), 0);
        if (ret == 0) {
            break;
        }
        if (ret < 0) {
            return -1;
        }
        received += (size_t)ret;
        response[received] = '\0';
    }
    if (sscanf(response, "HTTP/%*d.%*d %d", &status_code) != 1 ||
        status_code < 200 || status_code >= 300) {
        printf("HTTP %s failed: status=%d\r\n", path, status_code);
        return -1;
    }
    return 0;
}

/* Transport shared by the small JSON endpoints. response receives the raw
 * HTTP response, including headers. */
static int clearchain_post_json(const char *path, const char *body,
                                char *response, size_t response_size)
{
    char header[384];
    char host_header[96];
    int fd;
    int written;

    if (path == NULL || body == NULL || response == NULL || response_size < 16U) {
        return -1;
    }
    if (CLEARCHAIN_HTTP_PORT == 80) {
        written = snprintf(host_header, sizeof(host_header), "%s", CLEARCHAIN_HTTP_HOST);
    } else {
        written = snprintf(host_header, sizeof(host_header), "%s:%d",
                           CLEARCHAIN_HTTP_HOST, CLEARCHAIN_HTTP_PORT);
    }
    if (written < 0 || (size_t)written >= sizeof(host_header)) {
        return -1;
    }
    written = snprintf(header, sizeof(header),
                       "POST %s HTTP/1.1\r\n"
                       "Host: %s\r\n"
                       "Content-Type: application/json\r\n"
                       "ngrok-skip-browser-warning: true\r\n"
                       "Connection: close\r\n"
                       "Content-Length: %u\r\n\r\n",
                       path, host_header, (unsigned int)strlen(body));
    if (written < 0 || (size_t)written >= sizeof(header)) {
        return -1;
    }
    fd = clearchain_connect_http_server();
    if (fd < 0) {
        return -1;
    }
    if (clearchain_send_all(fd, header, (size_t)written) != 0 ||
        clearchain_send_all(fd, body, strlen(body)) != 0) {
        TCP_CloseClient(fd);
        return -1;
    }
    written = clearchain_receive_raw_response(fd, path, response, response_size);
    TCP_CloseClient(fd);
    return written;
}

int clearchain_send_factory_scan(const char *chip_uid)
{
    char escaped_uid[2U * 65U];
    char body[160];
    char response[1024];
    int written;
    if (chip_uid == NULL || chip_uid[0] == '\0' ||
        clearchain_json_escape(chip_uid, escaped_uid, sizeof(escaped_uid)) != 0) {
        return -1;
    }
    /* Only chip_uid is required by the confirmed /factory_scan contract. */
    written = snprintf(body, sizeof(body), "{\"chip_uid\":\"%s\"}", escaped_uid);
    if (written < 0 || (size_t)written >= sizeof(body)) {
        return -1;
    }
    return clearchain_post_json("/factory_scan", body, response, sizeof(response));
}

static int clearchain_json_uint_field(const char *json, const char *field,
                                      unsigned int *value)
{
    char key[48];
    const char *p;
    if (json == NULL || field == NULL || value == NULL) {
        return -1;
    }
    (void)snprintf(key, sizeof(key), "\"%s\"", field);
    p = strstr(json, key);
    if (p == NULL || (p = strchr(p + strlen(key), ':')) == NULL ||
        sscanf(p + 1, " %u", value) != 1) {
        return -1;
    }
    return 0;
}

int clearchain_send_register_batch(const char *batch_id, const r200_batch_t *batch,
                                   clearchain_register_response_t *result)
{
    static const char suffix[] = "]}";
    char escaped_batch_id[160];
    char escaped_uid[2U * R200_TAG_ID_MAX_LEN];
    char prefix[224];
    char item[224];
    char header[384];
    char host_header[96];
    char response[1024];
    const char *json;
    size_t content_length;
    size_t emitted = 0U;
    int fd;
    int written;

    if (batch_id == NULL || batch_id[0] == '\0' || batch == NULL || result == NULL ||
        batch->tag_count == 0U || batch->tag_count > R200_MAX_TAGS || batch->total_samples == 0U ||
        clearchain_json_escape(batch_id, escaped_batch_id, sizeof(escaped_batch_id)) != 0) {
        return -1;
    }
    result->registered_tags = 0U;
    result->total_samples = 0U;
    written = snprintf(prefix, sizeof(prefix),
                       "{\"batch_id\":\"%s\",\"readings\":[", escaped_batch_id);
    if (written < 0 || (size_t)written >= sizeof(prefix)) {
        return -1;
    }
    content_length = (size_t)written + strlen(suffix);
    for (size_t i = 0; i < batch->tag_count; i++) {
        const r200_tag_samples_t *tag = &batch->tags[i];
        if (tag->sample_count == 0U || tag->sample_count > R200_MAX_SAMPLES_PER_TAG ||
            memchr(tag->chip_uid, '\0', sizeof(tag->chip_uid)) == NULL ||
            clearchain_json_escape(tag->chip_uid, escaped_uid, sizeof(escaped_uid)) != 0) {
            return -1;
        }
        for (uint8_t sample = 0U; sample < tag->sample_count; sample++) {
            written = snprintf(item, sizeof(item),
                               "%s{\"chip_uid\":\"%s\",\"rssi_dbm\":%d}",
                               emitted == 0U ? "" : ",", escaped_uid,
                               (int)tag->rssi_dbm[sample]);
            if (written < 0 || (size_t)written >= sizeof(item)) {
                return -1;
            }
            content_length += (size_t)written;
            emitted++;
        }
    }
    if (emitted != batch->total_samples) {
        return -1;
    }
    if (CLEARCHAIN_HTTP_PORT == 80) {
        written = snprintf(host_header, sizeof(host_header), "%s", CLEARCHAIN_HTTP_HOST);
    } else {
        written = snprintf(host_header, sizeof(host_header), "%s:%d",
                           CLEARCHAIN_HTTP_HOST, CLEARCHAIN_HTTP_PORT);
    }
    if (written < 0 || (size_t)written >= sizeof(host_header)) {
        return -1;
    }
    written = snprintf(header, sizeof(header),
                       "POST /register_batch HTTP/1.1\r\n"
                       "Host: %s\r\nContent-Type: application/json\r\n"
                       "ngrok-skip-browser-warning: true\r\n"
                       "Connection: close\r\nContent-Length: %u\r\n\r\n",
                       host_header, (unsigned int)content_length);
    if (written < 0 || (size_t)written >= sizeof(header)) {
        return -1;
    }
    fd = clearchain_connect_http_server();
    if (fd < 0 || clearchain_send_all(fd, header, (size_t)written) != 0 ||
        clearchain_send_all(fd, prefix, strlen(prefix)) != 0) {
        if (fd >= 0) {
            TCP_CloseClient(fd);
        }
        return -1;
    }
    emitted = 0U;
    for (size_t i = 0; i < batch->tag_count; i++) {
        const r200_tag_samples_t *tag = &batch->tags[i];
        if (clearchain_json_escape(tag->chip_uid, escaped_uid, sizeof(escaped_uid)) != 0) {
            TCP_CloseClient(fd);
            return -1;
        }
        for (uint8_t sample = 0U; sample < tag->sample_count; sample++) {
            written = snprintf(item, sizeof(item),
                               "%s{\"chip_uid\":\"%s\",\"rssi_dbm\":%d}",
                               emitted == 0U ? "" : ",", escaped_uid,
                               (int)tag->rssi_dbm[sample]);
            if (written < 0 || (size_t)written >= sizeof(item) ||
                clearchain_send_all(fd, item, (size_t)written) != 0) {
                TCP_CloseClient(fd);
                return -1;
            }
            emitted++;
        }
    }
    if (clearchain_send_all(fd, suffix, strlen(suffix)) != 0 ||
        clearchain_receive_raw_response(fd, "/register_batch", response,
                                        sizeof(response)) != 0) {
        TCP_CloseClient(fd);
        return -1;
    }
    TCP_CloseClient(fd);
    json = strstr(response, "\r\n\r\n");
    if (json == NULL) {
        return -1;
    }
    json += 4;
    if (!clearchain_json_string_field_equals(json, "batch_id", batch_id) ||
        clearchain_json_uint_field(json, "registered_tags", &result->registered_tags) != 0 ||
        clearchain_json_uint_field(json, "total_samples", &result->total_samples) != 0 ||
        result->registered_tags != batch->tag_count ||
        result->total_samples != batch->total_samples) {
        printf("register_batch response mismatch\r\n");
        return -1;
    }
    return 0;
}

int clearchain_send_factory_batch(const char *batch_id, const r200_batch_t *batch,
                                  clearchain_register_response_t *result)
{
    char escaped_batch_id[160];
    if (batch_id == NULL || batch_id[0] == '\0' || batch == NULL || result == NULL ||
        batch->tag_count == 0U || batch->tag_count > R200_MAX_TAGS ||
        clearchain_json_escape(batch_id, escaped_batch_id, sizeof(escaped_batch_id)) != 0) {
        return -1;
    }

    /* Validate the entire batch before any endpoint can create records. */
    size_t samples = 0U;
    for (size_t i = 0U; i < batch->tag_count; i++) {
        if (batch->tags[i].sample_count == 0U ||
            batch->tags[i].sample_count > R200_MAX_SAMPLES_PER_TAG ||
            batch->tags[i].chip_uid[0] == '\0' ||
            memchr(batch->tags[i].chip_uid, '\0', R200_TAG_ID_MAX_LEN) == NULL) {
            return -1;
        }
        samples += batch->tags[i].sample_count;
    }
    if (samples != batch->total_samples) {
        return -1;
    }

    /* Factory enrollment is one request per unique EPC. Repeated RSSI samples
     * are intentionally preserved only in the final /register_batch body. */
    for (size_t i = 0U; i < batch->tag_count; i++) {
        if (clearchain_send_factory_scan(batch->tags[i].chip_uid) != 0) {
            printf("factory batch stopped at tag %u/%u\r\n",
                   (unsigned int)(i + 1U), (unsigned int)batch->tag_count);
            return -1;
        }
    }
    return clearchain_send_register_batch(batch_id, batch, result);
}

int clearchain_send_verify_scan(const char *chip_uid, const char *location,
                                clearchain_verify_response_t *result)
{
    char escaped_uid[2U * 65U];
    char escaped_location[192];
    char body[384];
    char response[1536];
    const char *json;
    const char *risk;
    int written;
    int score;

    if (chip_uid == NULL || chip_uid[0] == '\0' || location == NULL || result == NULL ||
        clearchain_json_escape(chip_uid, escaped_uid, sizeof(escaped_uid)) != 0 ||
        clearchain_json_escape(location, escaped_location, sizeof(escaped_location)) != 0) {
        return -1;
    }
    result->result = CLEARCHAIN_VERIFY_UNKNOWN;
    result->risk_score = 0;
    result->risk_score_valid = 0;
    written = snprintf(body, sizeof(body),
                       "{\"chip_uid\":\"%s\",\"verify_code\":\"VERIFY-q4m8\","
                       "\"location\":\"%s\"}", escaped_uid, escaped_location);
    if (written < 0 || (size_t)written >= sizeof(body) ||
        clearchain_post_json("/verify_scan", body, response, sizeof(response)) != 0) {
        return -1;
    }
    json = strstr(response, "\r\n\r\n");
    if (json == NULL) {
        return -1;
    }
    json += 4;
    if (clearchain_json_string_field_equals(json, "result", "AUTHORIZED")) {
        result->result = CLEARCHAIN_VERIFY_AUTHORIZED;
    } else if (clearchain_json_string_field_equals(json, "result", "MONITOR")) {
        result->result = CLEARCHAIN_VERIFY_MONITOR;
    } else if (clearchain_json_string_field_equals(json, "result", "ALERT")) {
        result->result = CLEARCHAIN_VERIFY_ALERT;
    } else {
        return -1;
    }
    risk = strstr(json, "\"risk_score\"");
    if (risk != NULL) {
        risk = strchr(risk, ':');
        if (risk != NULL && sscanf(risk + 1, " %d", &score) == 1 &&
            score >= 0 && score <= 100) {
            result->risk_score = score;
            result->risk_score_valid = 1;
        }
    }
    return 0;
}

clearchain_scan_led_t clearchain_verify_result_to_scan_led(clearchain_verify_result_t result)
{
    switch (result) {
        case CLEARCHAIN_VERIFY_AUTHORIZED:
            return CLEARCHAIN_SCAN_LED_GREEN;
        case CLEARCHAIN_VERIFY_MONITOR:
            return CLEARCHAIN_SCAN_LED_ORANGE;
        case CLEARCHAIN_VERIFY_ALERT:
            return CLEARCHAIN_SCAN_LED_RED;
        default:
            return CLEARCHAIN_SCAN_LED_UNKNOWN;
    }
}
