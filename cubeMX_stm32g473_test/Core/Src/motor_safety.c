#include "motor_safety.h"
#include <string.h>

void MotorSafety_Init(MotorSafety *motor, uint32_t now_ms, MotorReason retained)
{
    memset(motor, 0, sizeof(*motor));
    motor->last_update = now_ms;
    motor->generation = 1U;
    motor->reason = MOTOR_REASON_STARTUP;
    motor->zero_since[0] = motor->zero_since[1] = now_ms;
    if (retained == MOTOR_REASON_EMERGENCY || retained == MOTOR_REASON_SHUTDOWN) {
        MotorSafety_Hold(motor, retained, now_ms);
    }
}

bool MotorSafety_SetPair(MotorSafety *motor, uint16_t generation, int16_t left, int16_t right, uint32_t now_ms)
{
    MotorSafety_Tick(motor, now_ms);
    if (generation != motor->generation || left < -1000 || left > 1000 ||
        right < -1000 || right > 1000) {
        motor->neutral_tracking = false;
        return false;
    }
    if (motor->state == MOTOR_STATE_WAIT && left == 0 && right == 0) {
        if (!motor->neutral_tracking || now_ms - motor->neutral_last > 100U) {
            motor->neutral_since = now_ms;
        }
        motor->neutral_last = now_ms;
        motor->neutral_tracking = true;
        return true;
    }
    motor->neutral_tracking = false;
    if (motor->state != MOTOR_STATE_RUNNING) { return false; }
    motor->target[0] = left;
    motor->target[1] = right;
    motor->command_time[0] = now_ms;
    motor->command_time[1] = now_ms;
    return true;
}

MotorStatus MotorSafety_GetStatus(const MotorSafety *motor)
{
    MotorStatus status = {motor->state, motor->reason, motor->generation,
        {motor->output[0], motor->output[1]}, 0U, motor->last_update};
    uint32_t age_a = motor->last_update - motor->command_time[0];
    uint32_t age_b = motor->last_update - motor->command_time[1];
    uint32_t age = (age_a > age_b) ? age_a : age_b;
    status.command_age_ms = (uint16_t)((age > UINT16_MAX) ? UINT16_MAX : age);
    return status;
}

bool MotorSafety_Arm(MotorSafety *motor, uint16_t generation, uint32_t now_ms)
{
    MotorSafety_Tick(motor, now_ms);
    if (motor->state != MOTOR_STATE_WAIT || generation != motor->generation ||
        !motor->neutral_tracking || now_ms - motor->neutral_since < 300U ||
        now_ms - motor->neutral_last > 100U) { return false; }
    motor->state = MOTOR_STATE_RUNNING;
    motor->reason = MOTOR_REASON_NONE;
    motor->command_time[0] = motor->command_time[1] = now_ms;
    motor->last_update = now_ms;
    motor->neutral_tracking = false;
    return true;
}

void MotorSafety_Tick(MotorSafety *motor, uint32_t now_ms)
{
    uint32_t elapsed = now_ms - motor->last_update;
    motor->last_update = now_ms;
    if (motor->state != MOTOR_STATE_RUNNING) { return; }
    if (elapsed > 10U) {
        MotorSafety_Hold(motor, MOTOR_REASON_TIMING, now_ms);
        return;
    }
    if (now_ms - motor->command_time[0] >= 250U ||
        now_ms - motor->command_time[1] >= 250U) {
        MotorSafety_Hold(motor, MOTOR_REASON_TIMEOUT, now_ms);
        return;
    }
    for (uint8_t i = 0U; i < 2U; ++i) {
        int16_t current = motor->output[i];
        int16_t desired = motor->target[i];
        int8_t direction = (desired > 0) ? 1 : ((desired < 0) ? -1 : 0);
        bool opposite = direction != 0 && motor->last_direction[i] != 0 &&
            direction != motor->last_direction[i];
        if (opposite && (current != 0 || now_ms - motor->zero_since[i] < 100U)) {
            desired = 0;
        }
        int16_t step = (int16_t)(elapsed * 2U);
        if (current < desired) {
            current = (int16_t)(current + step);
            if (current > desired) { current = desired; }
        } else if (current > desired) {
            current = (int16_t)(current - step);
            if (current < desired) { current = desired; }
        }
        if (current == 0 && motor->output[i] != 0) { motor->zero_since[i] = now_ms; }
        if (current != 0) { motor->last_direction[i] = (current > 0) ? 1 : -1; }
        motor->output[i] = current;
    }
}

void MotorSafety_Hold(MotorSafety *motor, MotorReason reason, uint32_t now_ms)
{
    if (motor->state == MOTOR_STATE_EMERGENCY || motor->state == MOTOR_STATE_SHUTDOWN) { return; }
    if (motor->state != MOTOR_STATE_WAIT || motor->reason != reason) {
        ++motor->generation;
        if (motor->generation == 0U) { motor->generation = 1U; }
    }
    motor->state = (reason == MOTOR_REASON_EMERGENCY) ? MOTOR_STATE_EMERGENCY :
        ((reason == MOTOR_REASON_SHUTDOWN) ? MOTOR_STATE_SHUTDOWN : MOTOR_STATE_WAIT);
    motor->reason = reason;
    motor->last_update = now_ms;
    motor->neutral_tracking = false;
    for (uint8_t i = 0U; i < 2U; ++i) {
        if (motor->output[i] != 0) { motor->zero_since[i] = now_ms; }
        motor->output[i] = motor->target[i] = 0;
    }
}

bool MotorSafety_Stop(MotorSafety *motor, uint8_t mask, uint32_t now_ms)
{
    if (mask == 0U || mask > 3U) { return false; }
    MotorSafety_Tick(motor, now_ms);
    for (uint8_t i = 0U; i < 2U; ++i) {
        if ((mask & (1U << i)) != 0U) {
            motor->target[i] = 0;
            motor->command_time[i] = now_ms;
        }
    }
    return true;
}
