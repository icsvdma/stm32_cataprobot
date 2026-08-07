#ifndef DEBUG_LED_H
#define DEBUG_LED_H

#include "stm32g4xx_hal.h"
#include <stdint.h>

/* デバッグLEDに設定できる動作。 */
typedef enum {
    DBG_LED_OFF        = 0x00,  /* 消灯。 */
    DBG_LED_ON         = 0x01,  /* 連続点灯。 */
    DBG_LED_BLINK_1HZ  = 0x02,  /* 1 Hz点滅。 */
    DBG_LED_BLINK_4HZ  = 0x03,  /* 4 Hz点滅。 */
} DebugLedMode_t;

/** @brief 全デバッグLEDを消灯状態で初期化する。@return なし。 */
void DebugLed_Init(void);

/**
 * @brief ビットマスクで選んだLEDへ動作モードを設定する。
 * @param led_mask bit 0から5がLED0から5に対応する。
 * @param mode 消灯、点灯、1 Hz点滅、4 Hz点滅。
 * @return なし。
 */
void DebugLed_Set(uint8_t led_mask, DebugLedMode_t mode);

/** @brief 現在時刻に合わせて点滅中のLED出力を更新する。@return なし。 */
void DebugLed_Update(void);

/** @brief 全デバッグLEDを消灯する。@return なし。 */
void DebugLed_AllOff(void);

#endif /* DEBUG_LED_H */
