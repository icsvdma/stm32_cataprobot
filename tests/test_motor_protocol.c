#include "motor_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(value) do { if (!(value)) { fprintf(stderr, "line %d: %s\n", __LINE__, #value); exit(1); } } while (0)

/** 旧速度指令の拒否を、実パケットの公開インターフェースで検証する。 */
int main(void)
{
    MotorSafety motor;
    Packet_t request, response;
    const uint8_t legacy[] = {0xFFU, 0U, 3U, 0xE8U};
    MotorSafety_Init(&motor, 0U, MOTOR_REASON_STARTUP);
    Packet_Build(&request, 0x01U, legacy, sizeof(legacy));
    CHECK(MotorProtocol_Handle(&motor, &request, &response, 0U));
    CHECK(response.cmd == 0x81U && response.length == 2U);
    CHECK(response.payload[0] == 0x01U && response.payload[1] == DRIVE_ERROR_PROTOCOL);
    const uint8_t version[] = {1U};
    Packet_Build(&request, DRIVE_CMD_STATUS_REQ, version, 1U);
    CHECK(MotorProtocol_Handle(&motor, &request, &response, 0U));
    CHECK(response.cmd == DRIVE_CMD_STATUS_RESP && response.length == 16U);
    CHECK(response.payload[0] == 1U && response.payload[1] == MOTOR_STATE_WAIT);
    CHECK(response.payload[3] == 7U && response.payload[4] == 0U && response.payload[5] == 1U);
    const uint8_t zero[] = {0U, 1U, 0U, 0U, 0U, 0U};
    for (uint32_t now = 0U; now <= 300U; now += 50U) {
        Packet_Build(&request, DRIVE_CMD_PAIR, zero, sizeof(zero));
        CHECK(MotorProtocol_Handle(&motor, &request, &response, now));
        CHECK(response.cmd == 0x80U && response.payload[0] == DRIVE_CMD_PAIR);
    }
    const uint8_t arm[] = {1U, 0U, 1U};
    Packet_Build(&request, DRIVE_CMD_ARM, arm, sizeof(arm));
    CHECK(MotorProtocol_Handle(&motor, &request, &response, 300U));
    CHECK(response.cmd == 0x80U);
    const uint8_t pair[] = {0U, 1U, 3U, 0xE8U, 0xFEU, 0x0CU}; /* +1000, -500 */
    Packet_Build(&request, DRIVE_CMD_PAIR, pair, sizeof(pair));
    CHECK(MotorProtocol_Handle(&motor, &request, &response, 300U));
    uint8_t wire[PACKET_MAX_SIZE];
    uint8_t wire_length = Packet_Serialize(&response, wire);
    CHECK(wire_length == 5U && wire[0] == 0xAAU && wire[4] == 0x82U);
    MotorSafety_Tick(&motor, 305U);
    MotorStatus status = MotorSafety_GetStatus(&motor);
    CHECK(status.output[0] == 10 && status.output[1] == -10);
    const uint8_t bad_pair[] = {0U, 1U, 0U, 0U, 3U, 0xE9U};
    Packet_Build(&request, DRIVE_CMD_PAIR, bad_pair, sizeof(bad_pair));
    CHECK(MotorProtocol_Handle(&motor, &request, &response, 305U));
    CHECK(response.cmd == 0x81U && response.payload[1] == DRIVE_ERROR_PARAMETER);
    const uint8_t disconnect[] = {MOTOR_REASON_DISCONNECT};
    Packet_Build(&request, DRIVE_CMD_HOLD, disconnect, 1U);
    CHECK(MotorProtocol_Handle(&motor, &request, &response, 305U));
    status = MotorSafety_GetStatus(&motor);
    CHECK(status.state == MOTOR_STATE_WAIT && status.output[0] == 0 && status.output[1] == 0);
    CHECK(status.generation == 2U);
    Packet_Build(&request, DRIVE_CMD_PAIR, zero, sizeof(zero));
    CHECK(MotorProtocol_Handle(&motor, &request, &response, 305U));
    CHECK(response.cmd == 0x81U && response.payload[1] == DRIVE_ERROR_STATE);
    Packet_Build(&request, 0x04U, NULL, 0U);
    CHECK(MotorProtocol_Handle(&motor, &request, &response, 305U));
    CHECK(MotorSafety_GetStatus(&motor).state == MOTOR_STATE_EMERGENCY);
    Packet_Build(&request, DRIVE_CMD_HOLD, disconnect, 1U);
    CHECK(MotorProtocol_Handle(&motor, &request, &response, 305U));
    CHECK(MotorSafety_GetStatus(&motor).state == MOTOR_STATE_EMERGENCY);
    Packet_Build(&request, 0x20U, version, 1U);
    CHECK(!MotorProtocol_Handle(&motor, &request, &response, 305U));
    puts("motor protocol: passed");
    return 0;
}
