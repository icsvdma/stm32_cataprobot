#include "spi_frame.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(value) do { if (!(value)) { fprintf(stderr, "line %d: %s\n", __LINE__, #value); exit(1); } } while (0)
/** メインループを呼ばず、NSS境界だけで次フレームと不正フレームを処理する。 */
int main(void)
{
    SpiFrame frame;
    Packet_t packet;
    const uint8_t wire[] = {0xAAU, 0x04U, 0U, 0x04U};
    SpiFrame_Reset(&frame);
    for (unsigned transaction = 0U; transaction < 3U; ++transaction) {
        for (unsigned byte = 0U; byte < sizeof(wire) - 1U; ++byte) {
            CHECK(SpiFrame_Push(&frame, wire[byte], &packet) == SPI_FRAME_PENDING);
        }
        CHECK(SpiFrame_Push(&frame, wire[3], &packet) == SPI_FRAME_READY);
        CHECK(packet.cmd == 0x04U && packet.length == 0U);
        CHECK(SpiFrame_Push(&frame, 0xAAU, &packet) == SPI_FRAME_PENDING);
        SpiFrame_Reset(&frame);
    }
    const uint8_t corrupt[] = {0xAAU, 0x04U, 0U, 0x05U};
    for (unsigned byte = 0U; byte < 3U; ++byte) { CHECK(SpiFrame_Push(&frame, corrupt[byte], &packet) == SPI_FRAME_PENDING); }
    CHECK(SpiFrame_Push(&frame, corrupt[3], &packet) == SPI_FRAME_INVALID);
    CHECK(packet.cmd == 0x04U);
    SpiFrame_Reset(&frame);
    CHECK(SpiFrame_Push(&frame, 0xAAU, &packet) == SPI_FRAME_PENDING);
    CHECK(SpiFrame_Push(&frame, 0x03U, &packet) == SPI_FRAME_PENDING);
    CHECK(SpiFrame_Push(&frame, 33U, &packet) == SPI_FRAME_INVALID);
    puts("SPI frames: passed");
    return 0;
}
