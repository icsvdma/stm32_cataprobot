#ifndef DRIVE_PROTOCOL_H
#define DRIVE_PROTOCOL_H
#include <stdint.h>

/* STM32/ESP32双方で同一内容を保持するワイヤー仕様。センサーVersion 2とは独立。 */
#define DRIVE_PROTOCOL_VERSION 1U
#define DRIVE_STATUS_LENGTH 16U
#define DRIVE_CAPABILITIES 0x07U /* 左右同時指令・走行許可・停止保持。 */
#define DRIVE_CMD_PAIR 0x03U
#define DRIVE_CMD_ARM 0x05U
#define DRIVE_CMD_HOLD 0x06U
#define DRIVE_CMD_STATUS_REQ 0x10U
#define DRIVE_CMD_STATUS_RESP 0x90U
#define DRIVE_ERROR_PARAMETER 0x02U
#define DRIVE_ERROR_STATE 0x03U
#define DRIVE_ERROR_PROTOCOL 0x04U

typedef enum {
    MOTOR_STATE_WAIT = 0, MOTOR_STATE_RUNNING = 1,
    MOTOR_STATE_EMERGENCY = 2, MOTOR_STATE_SHUTDOWN = 3
} MotorState;
typedef enum {
    MOTOR_REASON_STARTUP = 0, MOTOR_REASON_NONE = 1,
    MOTOR_REASON_TIMEOUT = 2, MOTOR_REASON_DISCONNECT = 3,
    MOTOR_REASON_RESPONSE = 4, MOTOR_REASON_EMERGENCY = 5,
    MOTOR_REASON_SHUTDOWN = 6, MOTOR_REASON_TIMING = 7
} MotorReason;
typedef struct {
    MotorState state;
    MotorReason reason;
    uint16_t generation;
    int16_t output[2];
    uint16_t command_age_ms;
    uint32_t uptime_ms;
} MotorStatus;
#endif
