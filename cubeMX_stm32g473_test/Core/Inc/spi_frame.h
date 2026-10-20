#ifndef SPI_FRAME_H
#define SPI_FRAME_H
#include "packet.h"
#include <stdbool.h>
typedef struct {
    uint8_t bytes[PACKET_MAX_SIZE];
    uint8_t size;
    uint8_t expected;
    bool done;
} SpiFrame;
typedef enum { SPI_FRAME_PENDING, SPI_FRAME_READY, SPI_FRAME_INVALID } SpiFrameResult;
/** @brief NSS境界で受信状態を初期化。@param frame 受信状態。@return なし。 */
void SpiFrame_Reset(SpiFrame *frame);
/** @brief 1 byte受信。1 CSで1パケットだけ通知する。
 * @param frame 受信状態。@param byte 受信byte。@param packet 完成パケット先。
 * @return 完成/不正/未完成。不正時もpacket->cmdは元CMDを示す。 */
SpiFrameResult SpiFrame_Push(SpiFrame *frame, uint8_t byte, Packet_t *packet);
#endif
