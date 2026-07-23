#ifndef PACKET_H
#define PACKET_H

#include <stdint.h>
#include <stdbool.h>

#define PACKET_HEADER           0xAA
#define PACKET_MAX_PAYLOAD      32

/* ヘッダー、コマンド、長さ、データ、チェックサムを合わせた最大長。 */
#define PACKET_MAX_SIZE         (1 + 1 + 1 + PACKET_MAX_PAYLOAD + 1)

/** @brief ESP32と共通のSPI/UARTパケット形式。 */
typedef struct {
    uint8_t header;                       /* 固定値`0xAA`。 */
    uint8_t cmd;                          /* コマンドID。 */
    uint8_t length;                       /* payloadの有効バイト数。 */
    uint8_t payload[PACKET_MAX_PAYLOAD];  /* コマンドごとのデータ。 */
    uint8_t checksum;                     /* cmd、length、payloadのXOR。 */
} Packet_t;

/**
 * @brief 引数から送信用パケットを組み立てる。
 * @param p 組み立て結果の格納先。
 * @param cmd コマンドID。
 * @param payload 送信データ。ない場合は`NULL`。
 * @param len 送信データ長。
 * @return なし。最大長を超えたデータは32 byteへ切り詰める。
 */
void    Packet_Build(Packet_t *p, uint8_t cmd, const uint8_t *payload, uint8_t len);

/**
 * @brief パケットを送信用の連続したバイト列へ変換する。
 * @param p 変換するパケット。
 * @param buf 変換結果の格納先。
 * @return 変換後の総バイト数。
 */
uint8_t Packet_Serialize(const Packet_t *p, uint8_t *buf);

/**
 * @brief 受信バイト列を検証してパケットへ戻す。
 * @param buf 受信したバイト列。
 * @param len 受信したバイト数。
 * @param out 解析結果の格納先。
 * @return 形式とチェックサムが正しければ`true`。
 */
bool    Packet_Parse(const uint8_t *buf, uint8_t len, Packet_t *out);

/** @brief XORチェックサムを計算する。@param p 対象パケット。@return 計算値。 */
uint8_t Packet_Checksum(const Packet_t *p);

#endif /* PACKET_H */
