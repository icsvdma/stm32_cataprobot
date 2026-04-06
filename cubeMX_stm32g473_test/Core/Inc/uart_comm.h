#ifndef UART_COMM_H
#define UART_COMM_H

#include "stm32g4xx_hal.h"
#include "packet.h"
#include <stdbool.h>

#define UART_RX_BUF_SIZE    128
#define UART_DMA_BUF_SIZE   64

void                UART_Init(UART_HandleTypeDef *huart);
HAL_StatusTypeDef   UART_Send(const Packet_t *p);
void                UART_Dispatch(const uint8_t *buf, uint8_t len);
void                UART_ProcessReceived(void);
void                UART_CheckWatchdog(void);
void                UART_ResetWatchdog(void);
void                UART_RxEventCallback(uint16_t size);

#endif /* UART_COMM_H */
