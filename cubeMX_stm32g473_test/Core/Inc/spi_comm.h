#ifndef SPI_COMM_H
#define SPI_COMM_H

#include "stm32g4xx_hal.h"
#include "packet.h"

#define SPI_RX_BUF_SIZE     64

void                SPI_Slave_Init(SPI_HandleTypeDef *hspi);
void                SPI_Slave_StartReceive(void);
void                SPI_Slave_Dispatch(const uint8_t *buf, uint8_t len);
HAL_StatusTypeDef   SPI_Slave_SendResponse(const Packet_t *p);
void                SPI_Slave_RxCompleteCallback(void);
void                SPI_Slave_NssRisingCallback(void);
void                SPI_Slave_Poll(void);

#endif /* SPI_COMM_H */
