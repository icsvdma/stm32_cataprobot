#ifndef MOTOR_HBRIDGE_H
#define MOTOR_HBRIDGE_H

#include "stm32g4xx_hal.h"
#include <stdint.h>

/* 左右モーターの識別子。Aが左、Bが右。 */
typedef enum {
    HBRIDGE_CH_A = 0,
    HBRIDGE_CH_B = 1,
} HBridge_Channel_t;

/* Hブリッジへ与える回転方向またはブレーキ状態。 */
typedef enum {
    HBRIDGE_DIR_CW    = 0x00,
    HBRIDGE_DIR_CCW   = 0x01,
    HBRIDGE_DIR_BRAKE = 0x02,
} HBridge_Direction_t;

/**
 * @brief 2ch Hブリッジと4本のPWM出力を初期化する。
 * @param htim 4本のPWM入力に使用するTIM8ハンドル。
 * @return なし。
 */
void HBridge_Init(TIM_HandleTypeDef *htim);

/**
 * @brief 1台のモーターへ目標方向を設定する。
 * @param ch モーターAまたはB。
 * @param dir 正転、逆転、ブレーキのいずれか。
 * @return なし。
 */
void HBridge_SetDirection(HBridge_Channel_t ch, HBridge_Direction_t dir);

/**
 * @brief 1台のモーターへ目標速度を設定する。
 * @param ch モーターAまたはB。
 * @param speed 0から1000の速度指令。
 * @return なし。
 */
void HBridge_SetSpeed(HBridge_Channel_t ch, uint16_t speed);

/**
 * @brief 1台分の目標方向と目標速度をまとめて設定する。
 * @param ch モーターAまたはB。
 * @param dir 正転、逆転、ブレーキのいずれか。
 * @param speed 0から1000の速度指令。
 * @return なし。
 */
void HBridge_SetMotor(HBridge_Channel_t ch, HBridge_Direction_t dir, uint16_t speed);

/**
 * @brief 1台を減速させ、速度0でブレーキ状態へ移行する。
 * @param ch モーターAまたはB。
 * @return なし。
 */
void HBridge_Stop(HBridge_Channel_t ch);

/** @brief 左右を即時ブレーキし、Hブリッジ出力を無効化する。@return なし。 */
void HBridge_EmergencyStop(void);

/** @brief Hブリッジの共通駆動許可信号を有効にする。@return なし。 */
void HBridge_Enable(void);

/** @brief Hブリッジの共通駆動許可信号を無効にする。@return なし。 */
void HBridge_Disable(void);

/** @brief 加減速と方向反転待ちを1ステップ進める。@return なし。 */
void HBridge_Update(void);

#endif /* MOTOR_HBRIDGE_H */
