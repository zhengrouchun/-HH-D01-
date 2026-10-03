#include "clearchain_display_protocol.h"

static uint16_t crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFU;
    for (size_t i = 0; i < length; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (unsigned int b = 0; b < 8; b++) {
            crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000U) ? 0x1021U : 0U));
        }
    }
    return crc;
}
static int valid(uint8_t command, const clearchain_display_state_t *s)
{
    return command >= 1U && command <= 8U && s->stage <= 6U &&
           s->phase <= CLEARCHAIN_DISPLAY_ERROR && s->percent <= 100U &&
           s->result <= CLEARCHAIN_DISPLAY_RESULT_LOCAL_CAPTURE &&
           (s->risk_score <= 100U || s->risk_score == CLEARCHAIN_RISK_SCORE_UNKNOWN) &&
           (s->flags & ~31U) == 0U && s->error <= CLEARCHAIN_ERROR_DISABLED_KEY &&
           s->message[CLEARCHAIN_DISPLAY_MESSAGE_SIZE - 1U] == '\0';
}
int clearchain_display_encode(uint8_t *out, size_t capacity, uint8_t command,
                              uint32_t sequence, const clearchain_display_state_t *s)
{
    if (out == NULL || s == NULL || capacity < CLEARCHAIN_DISPLAY_MAX_PACKET_SIZE || !valid(command, s)) {
        return -1;
    }
    out[0] = 'C'; out[1] = 'C'; out[2] = CLEARCHAIN_DISPLAY_PROTOCOL_VERSION; out[3] = command;
    for (unsigned int i = 0; i < 4; i++) { out[4+i] = (uint8_t)(sequence >> (8*i)); }
    out[8] = CLEARCHAIN_DISPLAY_PAYLOAD_SIZE; out[9] = 0;
    out[10] = s->stage; out[11] = s->phase; out[12] = s->percent; out[13] = s->tag_count;
    out[14] = (uint8_t)s->total_samples; out[15] = (uint8_t)(s->total_samples >> 8);
    out[16] = (uint8_t)s->error; out[17] = (uint8_t)(s->error >> 8);
    out[18] = s->result; out[19] = s->risk_score; out[20] = s->flags; out[21] = 0;
    for (unsigned int i = 0; i < 4; ++i) { out[22+i] = (uint8_t)(s->state_version >> (8U*i)); }
    out[26] = (uint8_t)s->tags_expected; out[27] = (uint8_t)(s->tags_expected >> 8);
    for (unsigned int i = 0; i < CLEARCHAIN_DISPLAY_MESSAGE_SIZE; ++i) {
        out[28+i] = (uint8_t)s->message[i];
    }
    uint16_t crc = crc16(out, CLEARCHAIN_DISPLAY_MAX_PACKET_SIZE - 2U);
    out[76] = (uint8_t)crc; out[77] = (uint8_t)(crc >> 8);
    return CLEARCHAIN_DISPLAY_MAX_PACKET_SIZE;
}
int clearchain_display_decode(const uint8_t *p, size_t length, uint8_t *command,
                              uint32_t *sequence, clearchain_display_state_t *s)
{
    clearchain_display_state_t value = {0};
    if (p == NULL || s == NULL || command == NULL || sequence == NULL ||
        length != CLEARCHAIN_DISPLAY_MAX_PACKET_SIZE || p[0] != 'C' || p[1] != 'C' ||
        p[2] != CLEARCHAIN_DISPLAY_PROTOCOL_VERSION || p[8] != CLEARCHAIN_DISPLAY_PAYLOAD_SIZE ||
        p[9] != 0U || p[21] != 0U || p[75] != 0U ||
        crc16(p, CLEARCHAIN_DISPLAY_MAX_PACKET_SIZE - 2U) !=
        (uint16_t)(p[76] | ((uint16_t)p[77] << 8))) {
        return -1;
    }
    value.stage = p[10]; value.phase = p[11]; value.percent = p[12]; value.tag_count = p[13];
    value.total_samples = (uint16_t)(p[14] | ((uint16_t)p[15] << 8));
    value.error = (uint16_t)(p[16] | ((uint16_t)p[17] << 8));
    value.result = p[18]; value.risk_score = p[19]; value.flags = p[20];
    value.state_version = (uint32_t)p[22] | ((uint32_t)p[23] << 8) |
        ((uint32_t)p[24] << 16) | ((uint32_t)p[25] << 24);
    value.tags_expected = (uint16_t)(p[26] | ((uint16_t)p[27] << 8));
    for (unsigned int i = 0; i < CLEARCHAIN_DISPLAY_MESSAGE_SIZE; ++i) {
        value.message[i] = (char)p[28+i];
    }
    if (!valid(p[3], &value)) { return -1; }
    *s = value; *command = p[3];
    *sequence = (uint32_t)p[4] | ((uint32_t)p[5] << 8) | ((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 24);
    return 0;
}
int clearchain_display_sequence_newer(uint32_t candidate, uint32_t previous)
{
    uint32_t difference = candidate - previous;
    return difference != 0U && difference < 0x80000000U;
}
