#ifndef DEBUG_LED_H
#define DEBUG_LED_H

#include "stm32g4xx_hal.h"
#include <stdint.h>

typedef enum {
    DBG_LED_OFF        = 0x00,
    DBG_LED_ON         = 0x01,
    DBG_LED_BLINK_1HZ  = 0x02,
    DBG_LED_BLINK_4HZ  = 0x03,
} DebugLedMode_t;

void DebugLed_Init(void);
void DebugLed_Set(uint8_t led_mask, DebugLedMode_t mode);
void DebugLed_Update(void);
void DebugLed_AllOff(void);

#endif /* DEBUG_LED_H */
