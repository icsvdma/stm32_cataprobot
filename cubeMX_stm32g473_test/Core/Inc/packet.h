#ifndef PACKET_H
#define PACKET_H

#include <stdint.h>
#include <stdbool.h>

#define PACKET_HEADER           0xAA
#define PACKET_MAX_PAYLOAD      32

/* ヘッダ(1) + CMD(1) + Length(1) + Payload(max32) + Checksum(1) */
#define PACKET_MAX_SIZE         (1 + 1 + 1 + PACKET_MAX_PAYLOAD + 1)

typedef struct {
    uint8_t header;
    uint8_t cmd;
    uint8_t length;
    uint8_t payload[PACKET_MAX_PAYLOAD];
    uint8_t checksum;
} Packet_t;

void    Packet_Build(Packet_t *p, uint8_t cmd, const uint8_t *payload, uint8_t len);
uint8_t Packet_Serialize(const Packet_t *p, uint8_t *buf);
bool    Packet_Parse(const uint8_t *buf, uint8_t len, Packet_t *out);
uint8_t Packet_Checksum(const Packet_t *p);

#endif /* PACKET_H */
