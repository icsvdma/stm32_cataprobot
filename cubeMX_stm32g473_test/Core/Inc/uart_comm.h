#ifndef UART_COMM_H
#define UART_COMM_H

#include "stm32g4xx_hal.h"
#include "packet.h"
#include <stdbool.h>

/* DMA循環受信バッファと、解析待ちバッファの大きさ。 */
#define UART_RX_BUF_SIZE    128
#define UART_DMA_BUF_SIZE   64

/**
 * @brief USART2のDMA受信と通信監視状態を初期化する。
 * @param huart CubeMXで初期化済みのUSART2ハンドル。
 * @return なし。
 */
void                UART_Init(UART_HandleTypeDef *huart);

/** @brief パケットをUARTへ送る。@param p 送信パケット。@return HALの送信結果。 */
HAL_StatusTypeDef   UART_Send(const Packet_t *p);

/**
 * @brief 受信したパケットを解析し、コマンド処理を呼び出す。
 * @param buf パケット先頭を指すデータ。
 * @param len 受信バイト数。
 * @return なし。
 */
void                UART_Dispatch(const uint8_t *buf, uint8_t len);

/** @brief DMAから届いた未処理データをパケット単位で解析する。@return なし。 */
void                UART_ProcessReceived(void);

/** @brief 最終受信から1秒以上経過した場合に通信異常処理を行う。@return なし。 */
void                UART_CheckWatchdog(void);

/** @brief UART通信監視の基準時刻を現在時刻へ戻す。@return なし。 */
void                UART_ResetWatchdog(void);

/**
 * @brief HALのReceiveToIdle通知から受信サイズを保存し、DMAを再開する。
 * @param size 今回DMAバッファへ入ったバイト数。
 * @return なし。
 */
void                UART_RxEventCallback(uint16_t size);

#endif /* UART_COMM_H */
