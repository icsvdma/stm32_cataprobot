#ifndef MOTOR_SAFETY_H
#define MOTOR_SAFETY_H
#include <stdint.h>
#include <stdbool.h>
#include "drive_protocol.h"

typedef struct {
    MotorState state;
    MotorReason reason;
    uint16_t generation;
    int16_t output[2];
    int16_t target[2];
    uint32_t command_time[2];
    uint32_t last_update;
    uint32_t neutral_since;
    uint32_t neutral_last;
    uint32_t zero_since[2];
    int8_t last_direction[2];
    bool neutral_tracking;
} MotorSafety;

/** @brief 安全状態を初期化。@param motor 状態。@param now_ms 時刻。@param retained 保持理由。@return なし。 */
void MotorSafety_Init(MotorSafety *motor, uint32_t now_ms, MotorReason retained);
/** @brief 左右を一括検証して採用。再開待ち中のゼロは中立確認に使う。
 * @param motor 状態。@param generation 状態世代。@param left 左指令[-1000,1000]。
 * @param right 右指令[-1000,1000]。@param now_ms 時刻。@return 採用時true。 */
bool MotorSafety_SetPair(MotorSafety *motor, uint16_t generation, int16_t left, int16_t right, uint32_t now_ms);
/** @brief 連続した中立確認後に走行許可。@param motor 状態。@param generation 世代。@param now_ms 時刻。@return 許可時true。 */
bool MotorSafety_Arm(MotorSafety *motor, uint16_t generation, uint32_t now_ms);
/** @brief 時間に従い出力・期限を更新。@param motor 状態。@param now_ms 時刻。@return なし。 */
void MotorSafety_Tick(MotorSafety *motor, uint32_t now_ms);
/** @brief 即時停止して保持。@param motor 状態。@param reason 理由。@param now_ms 時刻。@return なし。 */
void MotorSafety_Hold(MotorSafety *motor, MotorReason reason, uint32_t now_ms);
/** @brief 通常減速を要求。@param motor 状態。@param mask A=1,B=2,両方=3。@param now_ms 時刻。@return マスクが有効ならtrue。 */
bool MotorSafety_Stop(MotorSafety *motor, uint8_t mask, uint32_t now_ms);
/** @brief 状態を取得。@param motor 状態。@return 出力・走行許可・停止理由のコピー。 */
MotorStatus MotorSafety_GetStatus(const MotorSafety *motor);
#endif
