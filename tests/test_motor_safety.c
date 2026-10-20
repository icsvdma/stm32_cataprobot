#include "motor_safety.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(value) do { if (!(value)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #value); exit(1); } } while (0)

/** 中立確認から走行許可まで進め、許可時刻を返す。 */
static uint32_t arm_at(MotorSafety *motor, uint32_t start)
{
    MotorSafety_Init(motor, start, MOTOR_REASON_STARTUP);
    for (uint32_t offset = 0U; offset <= 300U; offset += 50U) {
        CHECK(MotorSafety_SetPair(motor, 1U, 0, 0, start + offset));
    }
    CHECK(MotorSafety_Arm(motor, 1U, start + 300U));
    return start + 300U;
}

/** 5 ms刻みで更新し、50 msごとに左右指令を更新する。終了時刻を返す。 */
static uint32_t drive_for(MotorSafety *motor, uint32_t start, uint32_t duration, int16_t value)
{
    uint16_t generation = MotorSafety_GetStatus(motor).generation;
    for (uint32_t offset = 5U; offset <= duration; offset += 5U) {
        uint32_t now = start + offset;
        MotorSafety_Tick(motor, now);
        if (offset % 50U == 0U) { CHECK(MotorSafety_SetPair(motor, generation, value, value, now)); }
    }
    return start + duration;
}

/** 通常停止を途中で逆転に変えても、ゼロ到達後100 msの待ちを保つ。 */
static void stop_then_reverse(void)
{
    MotorSafety motor;
    uint32_t now = arm_at(&motor, 0U);
    CHECK(MotorSafety_SetPair(&motor, 1U, 1000, 1000, now));
    now = drive_for(&motor, now, 500U, 1000);
    CHECK(MotorSafety_Stop(&motor, 3U, now));
    MotorSafety_Tick(&motor, now + 5U);
    CHECK(MotorSafety_SetPair(&motor, 1U, -1000, -1000, now + 5U));
    now = drive_for(&motor, now + 5U, 495U, -1000);
    MotorStatus status = MotorSafety_GetStatus(&motor);
    CHECK(status.output[0] == 0);
    now = drive_for(&motor, now, 95U, -1000);
    status = MotorSafety_GetStatus(&motor);
    CHECK(status.output[0] == 0);
    MotorSafety_Tick(&motor, now + 5U);
    status = MotorSafety_GetStatus(&motor);
    CHECK(status.output[0] == -10);
}

/** 部分的な異常指令は左右を変更せず、期限も延長しない。 */
static void invalid_pair_and_stale_generation(void)
{
    MotorSafety motor;
    uint32_t now = arm_at(&motor, 0U);
    CHECK(MotorSafety_SetPair(&motor, 1U, 1000, 500, now));
    for (uint32_t offset = 5U; offset <= 245U; offset += 5U) { MotorSafety_Tick(&motor, now + offset); }
    CHECK(!MotorSafety_SetPair(&motor, 1U, 1000, 1001, now + 245U));
    MotorSafety_Tick(&motor, now + 250U);
    CHECK(MotorSafety_GetStatus(&motor).reason == MOTOR_REASON_TIMEOUT);
    CHECK(!MotorSafety_SetPair(&motor, 1U, 0, 0, now + 250U));
    CHECK(!MotorSafety_Arm(&motor, 1U, now + 250U));
}

/** 中立の欠落・古い入力・早すぎる再開を拒否する。 */
static void neutral_must_be_continuous(void)
{
    MotorSafety motor;
    MotorSafety_Init(&motor, 0U, MOTOR_REASON_STARTUP);
    CHECK(MotorSafety_SetPair(&motor, 1U, 0, 0, 0U));
    CHECK(!MotorSafety_Arm(&motor, 1U, 300U));
    CHECK(MotorSafety_SetPair(&motor, 1U, 0, 0, 300U));
    for (uint32_t now = 350U; now <= 550U; now += 50U) { CHECK(MotorSafety_SetPair(&motor, 1U, 0, 0, now)); }
    CHECK(!MotorSafety_Arm(&motor, 1U, 599U));
    CHECK(MotorSafety_SetPair(&motor, 1U, 0, 0, 600U));
    CHECK(MotorSafety_Arm(&motor, 1U, 600U));
}

/** 時刻の32 bit周回と更新遅延時の停止を確認する。 */
static void wrapping_clock_and_timing_fault(void)
{
    MotorSafety motor;
    uint32_t now = arm_at(&motor, UINT32_MAX - 350U);
    CHECK(MotorSafety_SetPair(&motor, 1U, 1000, 1000, now));
    now = drive_for(&motor, now, 500U, 1000);
    MotorStatus status = MotorSafety_GetStatus(&motor);
    CHECK(status.output[0] == 1000);
    MotorSafety_Tick(&motor, now + 11U);
    status = MotorSafety_GetStatus(&motor);
    CHECK(status.reason == MOTOR_REASON_TIMING && status.output[0] == 0);
}

/** 通信復旧・STARTに相当する許可要求でも保持状態を解除しない。 */
static void irreversible_stops(void)
{
    MotorSafety motor;
    const MotorReason reasons[] = {MOTOR_REASON_EMERGENCY, MOTOR_REASON_SHUTDOWN};
    for (unsigned i = 0U; i < 2U; ++i) {
        MotorSafety_Init(&motor, 0U, reasons[i]);
        uint16_t generation = MotorSafety_GetStatus(&motor).generation;
        MotorSafety_Hold(&motor, MOTOR_REASON_DISCONNECT, 10U);
        CHECK(!MotorSafety_SetPair(&motor, generation, 0, 0, 10U));
        CHECK(!MotorSafety_Arm(&motor, generation, 400U));
        CHECK(MotorSafety_GetStatus(&motor).reason == reasons[i]);
    }
}

/** 起動直後の速度指令で走行を許可しないことを公開状態で確認する。 */
int main(void)
{
    MotorSafety motor;
    MotorSafety_Init(&motor, 0U, MOTOR_REASON_STARTUP);
    CHECK(!MotorSafety_SetPair(&motor, 1U, 1000, 1000, 0U));
    CHECK(MotorSafety_GetStatus(&motor).state == MOTOR_STATE_WAIT);
    for (uint32_t time = 0U; time <= 300U; time += 50U) {
        CHECK(MotorSafety_SetPair(&motor, 1U, 0, 0, time));
    }
    CHECK(MotorSafety_Arm(&motor, 1U, 300U));
    CHECK(MotorSafety_GetStatus(&motor).state == MOTOR_STATE_RUNNING);
    CHECK(MotorSafety_SetPair(&motor, 1U, 1000, 1000, 300U));
    for (uint32_t time = 305U; time <= 800U; time += 5U) {
        MotorSafety_Tick(&motor, time);
        if (time % 50U == 0U) {
            CHECK(MotorSafety_SetPair(&motor, 1U, 1000, 1000, time));
        }
    }
    MotorStatus status = MotorSafety_GetStatus(&motor);
    CHECK(status.output[0] == 1000);
    for (uint32_t time = 805U; time <= 1050U; time += 5U) {
        MotorSafety_Tick(&motor, time);
    }
    status = MotorSafety_GetStatus(&motor);
    CHECK(status.output[0] == 0);
    CHECK(status.reason == MOTOR_REASON_TIMEOUT);
    MotorSafety_Init(&motor, 2000U, MOTOR_REASON_EMERGENCY);
    CHECK(MotorSafety_GetStatus(&motor).state == MOTOR_STATE_EMERGENCY);
    stop_then_reverse();
    invalid_pair_and_stale_generation();
    neutral_must_be_continuous();
    wrapping_clock_and_timing_fault();
    irreversible_stops();
    puts("motor safety: passed");
    return 0;
}
