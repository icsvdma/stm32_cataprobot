#include "packet.h"
#include <string.h>

uint8_t Packet_Checksum(const Packet_t *p)
{
    uint8_t cs = p->cmd ^ p->length;
    for (uint8_t i = 0; i < p->length; i++) {
        cs ^= p->payload[i];
    }
    return cs;
}

void Packet_Build(Packet_t *p, uint8_t cmd, const uint8_t *payload, uint8_t len)
{
    if (len > PACKET_MAX_PAYLOAD) {
        len = PACKET_MAX_PAYLOAD;
    }
    p->header = PACKET_HEADER;
    p->cmd    = cmd;
    p->length = len;
    if (payload != NULL && len > 0) {
        memcpy(p->payload, payload, len);
    }
    p->checksum = Packet_Checksum(p);
}

uint8_t Packet_Serialize(const Packet_t *p, uint8_t *buf)
{
    uint8_t idx = 0;
    buf[idx++] = p->header;
    buf[idx++] = p->cmd;
    buf[idx++] = p->length;
    for (uint8_t i = 0; i < p->length; i++) {
        buf[idx++] = p->payload[i];
    }
    buf[idx++] = p->checksum;
    return idx;
}

bool Packet_Parse(const uint8_t *buf, uint8_t len, Packet_t *out)
{
    /* 最小パケット長: header(1) + cmd(1) + length(1) + checksum(1) = 4 */
    if (len < 4) {
        return false;
    }
    if (buf[0] != PACKET_HEADER) {
        return false;
    }
    out->header = buf[0];
    out->cmd    = buf[1];
    out->length = buf[2];

    if (out->length > PACKET_MAX_PAYLOAD) {
        return false;
    }
    /* 必要な合計バイト数を検証 */
    uint8_t expected_len = 3 + out->length + 1;
    if (len < expected_len) {
        return false;
    }
    memcpy(out->payload, &buf[3], out->length);
    out->checksum = buf[3 + out->length];

    return (out->checksum == Packet_Checksum(out));
}
