#include "spi_frame.h"
void SpiFrame_Reset(SpiFrame *frame) { frame->size = 0U; frame->expected = 0U; frame->done = false; }
SpiFrameResult SpiFrame_Push(SpiFrame *frame, uint8_t byte, Packet_t *packet)
{
    if (frame->done) { return SPI_FRAME_PENDING; }
    if (frame->size == 0U && byte != PACKET_HEADER) { return SPI_FRAME_PENDING; }
    frame->bytes[frame->size++] = byte;
    if (frame->size == 3U) {
        if (byte > PACKET_MAX_PAYLOAD) {
            frame->done = true;
            packet->cmd = frame->bytes[1];
            return SPI_FRAME_INVALID;
        }
        frame->expected = (uint8_t)(byte + 4U);
    }
    if (frame->expected == 0U || frame->size < frame->expected) { return SPI_FRAME_PENDING; }
    frame->done = true;
    if (!Packet_Parse(frame->bytes, frame->size, packet)) {
        packet->cmd = frame->bytes[1];
        return SPI_FRAME_INVALID;
    }
    return SPI_FRAME_READY;
}
