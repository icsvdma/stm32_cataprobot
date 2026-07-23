#ifndef LED_CONTROL_H
#define LED_CONTROL_H

#include "stm32g4xx_hal.h"
#include <stdint.h>

/* ヘッドライトの点灯パターン。通常は光量指定を使用する。 */
typedef enum {
    LM_OFF      = 0x00,  /* 消灯。 */
    LM_LOW      = 0x01,  /* 左右30%点灯。 */
    LM_HIGH     = 0x02,  /* 左右100%点灯。 */
    LM_AUTO     = 0x03,  /* 現在はLow相当。 */
    LM_ADAPTIVE = 0x04,  /* 現在はHigh相当。 */
    LM_BRAKE    = 0x05,  /* 左右100%点灯。 */
    LM_HAZARD   = 0x06,  /* 左右同時点滅。 */
    LM_TURN_L   = 0x07,  /* 左だけ点滅。 */
    LM_TURN_R   = 0x08,  /* 右だけ点滅。 */
    LM_BREATH   = 0x09,  /* ゆっくり明暗を繰り返す。 */
    LM_STROBE   = 0x0A,  /* 高速点滅。 */
    LM_WELCOME  = 0x0B,  /* 左右交互点灯。 */
    LM_BATTERY  = 0x0C,  /* 現在はLow相当。 */
} LedMode_t;

/* 光量変更の対象。 */
typedef enum {
    LED_SIDE_RIGHT = 0x00,
    LED_SIDE_LEFT  = 0x01,
    LED_SIDE_BOTH  = 0xFF,
} LedSide_t;

/**
 * @brief TIM2の左右PWMを開始し、消灯状態で初期化する。
 * @param htim ヘッドライトPWMに使用するTIM2ハンドル。
 * @return なし。
 */
void LED_Init(TIM_HandleTypeDef *htim);

/**
 * @brief ヘッドライトの点灯パターンを変更する。
 * @param mode 設定する`LedMode_t`。
 * @return なし。
 */
void LED_SetMode(LedMode_t mode);

/**
 * @brief 指定したライトのPWM光量を直接設定する。
 * @param side 右、左、または左右両方。
 * @param brightness 光量[%]。100を超える値は100に制限する。
 * @return なし。
 */
void LED_SetBrightness(LedSide_t side, uint8_t brightness);

/** @brief 時刻に合わせて点滅や呼吸パターンを更新する。@return なし。 */
void LED_Update(void);

/** @brief 左右ライトを消灯し、動作モードもOFFへ戻す。@return なし。 */
void LED_AllOff(void);

/** @brief 現在の点灯パターンを返す。@return 現在の`LedMode_t`。 */
LedMode_t LED_GetMode(void);

#endif /* LED_CONTROL_H */
