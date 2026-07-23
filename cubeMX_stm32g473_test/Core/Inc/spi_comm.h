#ifndef SPI_COMM_H
#define SPI_COMM_H

#include "stm32g4xx_hal.h"
#include "packet.h"

/* 1回のSPI受信で保持できる最大バイト数。 */
#define SPI_RX_BUF_SIZE     64

/**
 * @brief SPI1をESP32向けスレーブ通信として初期化する。
 * @param hspi CubeMXで初期化済みのSPI1ハンドル。
 * @return なし。
 */
void                SPI_Slave_Init(SPI_HandleTypeDef *hspi);

/** @brief 旧インターフェース互換用。現在は何もしない。@return なし。 */
void                SPI_Slave_StartReceive(void);

/**
 * @brief 受信した1パケットを解析し、対応する処理と応答準備を行う。
 * @param buf パケット先頭を指す受信データ。
 * @param len パケットのバイト数。
 * @return なし。
 */
void                SPI_Slave_Dispatch(const uint8_t *buf, uint8_t len);

/**
 * @brief パケットをHALのブロッキング送信で返す互換関数。
 * @param p 送信するパケット。
 * @return HALの送信結果。
 */
HAL_StatusTypeDef   SPI_Slave_SendResponse(const Packet_t *p);

/** @brief 旧DMA受信完了コールバック。現在は何もしない。@return なし。 */
void                SPI_Slave_RxCompleteCallback(void);

/** @brief 旧NSS立ち上がりコールバック。現在は何もしない。@return なし。 */
void                SPI_Slave_NssRisingCallback(void);

/**
 * @brief SPI1の受信・送信・エラーフラグを割り込みコンテキストで処理する。
 * @return なし。
 */
void                SPI_Slave_IRQHandler(void);

/** @brief CS解除後に通信状態を次のフレーム用へ戻す。@return なし。 */
void                SPI_Slave_Poll(void);

#endif /* SPI_COMM_H */
