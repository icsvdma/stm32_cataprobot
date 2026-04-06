#ifndef LED_CONTROL_H
#define LED_CONTROL_H

#include "stm32g4xx_hal.h"
#include <stdint.h>

typedef enum {
    LM_OFF      = 0x00,
    LM_LOW      = 0x01,
    LM_HIGH     = 0x02,
    LM_AUTO     = 0x03,
    LM_ADAPTIVE = 0x04,
    LM_BRAKE    = 0x05,
    LM_HAZARD   = 0x06,
    LM_TURN_L   = 0x07,
    LM_TURN_R   = 0x08,
    LM_BREATH   = 0x09,
    LM_STROBE   = 0x0A,
    LM_WELCOME  = 0x0B,
    LM_BATTERY  = 0x0C,
} LedMode_t;

typedef enum {
    LED_SIDE_RIGHT = 0x00,
    LED_SIDE_LEFT  = 0x01,
    LED_SIDE_BOTH  = 0xFF,
} LedSide_t;

void LED_Init(TIM_HandleTypeDef *htim);
void LED_SetMode(LedMode_t mode);
void LED_SetBrightness(LedSide_t side, uint8_t brightness);
void LED_Update(void);
void LED_AllOff(void);

LedMode_t LED_GetMode(void);

#endif /* LED_CONTROL_H */
