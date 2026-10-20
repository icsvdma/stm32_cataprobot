#include "motor_protocol.h"

/** 2 byteのビッグエンディアン値を読む。pは2 byte以上。戻り値は符号なし値。 */
static uint16_t read16(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }
/** valueを2 byteへ書く。pは書込み先。戻り値なし。 */
static void write16(uint8_t *p, uint16_t value) { p[0] = (uint8_t)(value >> 8); p[1] = (uint8_t)value; }
/** 元コマンドcmdとerrorを含むNACKをresponseへ書く。戻り値は処理済みtrue。 */
static bool nack(Packet_t *response, uint8_t cmd, uint8_t error)
{
    uint8_t payload[] = {cmd, error};
    Packet_Build(response, 0x81U, payload, sizeof(payload));
    return true;
}

bool MotorProtocol_Handle(MotorSafety *motor, const Packet_t *request, Packet_t *response, uint32_t now_ms)
{
    uint8_t cmd = request->cmd;
    const uint8_t *p = request->payload;
    uint8_t length = request->length;
    if (cmd == 0x01U) { return nack(response, cmd, DRIVE_ERROR_PROTOCOL); }
    if (cmd != DRIVE_CMD_PAIR && cmd != DRIVE_CMD_ARM && cmd != DRIVE_CMD_HOLD &&
        cmd != DRIVE_CMD_STATUS_REQ && cmd != 0x02U && cmd != 0x04U && cmd != 0x30U) {
        return false;
    }
    MotorSafety_Tick(motor, now_ms);
    if (cmd == DRIVE_CMD_STATUS_REQ) {
        if (length != 1U) { return nack(response, cmd, DRIVE_ERROR_PARAMETER); }
        if (p[0] != DRIVE_PROTOCOL_VERSION) { return nack(response, cmd, DRIVE_ERROR_PROTOCOL); }
        MotorStatus status = MotorSafety_GetStatus(motor);
        uint8_t payload[DRIVE_STATUS_LENGTH] = {0};
        payload[0] = DRIVE_PROTOCOL_VERSION;
        payload[1] = (uint8_t)status.state;
        payload[2] = (uint8_t)status.reason;
        payload[3] = DRIVE_CAPABILITIES;
        write16(&payload[4], status.generation);
        write16(&payload[6], (uint16_t)status.output[0]);
        write16(&payload[8], (uint16_t)status.output[1]);
        write16(&payload[10], status.command_age_ms);
        payload[12] = (uint8_t)(now_ms >> 24);
        payload[13] = (uint8_t)(now_ms >> 16);
        payload[14] = (uint8_t)(now_ms >> 8);
        payload[15] = (uint8_t)now_ms;
        Packet_Build(response, DRIVE_CMD_STATUS_RESP, payload, sizeof(payload));
        return true;
    }
    if (cmd == DRIVE_CMD_PAIR) {
        if (length != 6U) { return nack(response, cmd, DRIVE_ERROR_PARAMETER); }
        int16_t left = (int16_t)read16(&p[2]);
        int16_t right = (int16_t)read16(&p[4]);
        if (left < -1000 || left > 1000 || right < -1000 || right > 1000) {
            return nack(response, cmd, DRIVE_ERROR_PARAMETER);
        }
        if (!MotorSafety_SetPair(motor, read16(p), left, right, now_ms)) {
            return nack(response, cmd, DRIVE_ERROR_STATE);
        }
    } else if (cmd == DRIVE_CMD_ARM) {
        if (length != 3U) { return nack(response, cmd, DRIVE_ERROR_PARAMETER); }
        if (p[0] != DRIVE_PROTOCOL_VERSION) { return nack(response, cmd, DRIVE_ERROR_PROTOCOL); }
        if (!MotorSafety_Arm(motor, read16(&p[1]), now_ms)) {
            return nack(response, cmd, DRIVE_ERROR_STATE);
        }
    } else if (cmd == DRIVE_CMD_HOLD) {
        if (length != 1U || (p[0] != MOTOR_REASON_DISCONNECT && p[0] != MOTOR_REASON_RESPONSE)) {
            return nack(response, cmd, DRIVE_ERROR_PARAMETER);
        }
        MotorSafety_Hold(motor, (MotorReason)p[0], now_ms);
    } else if (cmd == 0x02U) {
        if (length != 1U || (p[0] != 0U && p[0] != 1U && p[0] != 0xFFU)) {
            return nack(response, cmd, DRIVE_ERROR_PARAMETER);
        }
        uint8_t mask = (p[0] == 0xFFU) ? 3U : (uint8_t)(1U << p[0]);
        (void)MotorSafety_Stop(motor, mask, now_ms);
    } else {
        if (length != 0U) { return nack(response, cmd, DRIVE_ERROR_PARAMETER); }
        MotorSafety_Hold(motor, (cmd == 0x04U) ? MOTOR_REASON_EMERGENCY : MOTOR_REASON_SHUTDOWN, now_ms);
        if (cmd == 0x30U) {
            Packet_Build(response, 0xF0U, 0, 0U);
            return true;
        }
    }
    Packet_Build(response, 0x80U, &cmd, 1U);
    return true;
}
