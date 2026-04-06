#include "debug_led.h"
#include "config.h"

typedef struct {
    GPIO_TypeDef   *port;
    uint16_t        pin;
    DebugLedMode_t  mode;
    uint8_t         state;
    uint32_t        last_tick;
} DebugLedEntry_t;

static DebugLedEntry_t s_leds[DBG_LED_COUNT] = {
    { PORT_DBG_LED, PIN_DBG_LED0, DBG_LED_OFF, 0, 0 },
    { PORT_DBG_LED, PIN_DBG_LED1, DBG_LED_OFF, 0, 0 },
    { PORT_DBG_LED, PIN_DBG_LED2, DBG_LED_OFF, 0, 0 },
    { PORT_DBG_LED, PIN_DBG_LED3, DBG_LED_OFF, 0, 0 },
    { PORT_DBG_LED, PIN_DBG_LED4, DBG_LED_OFF, 0, 0 },
    { PORT_DBG_LED, PIN_DBG_LED5, DBG_LED_OFF, 0, 0 },
};

void DebugLed_Init(void)
{
    /* PC10-PC15 のクロックは CubeMX で有効化済みの前提 */
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = PIN_DBG_LED0 | PIN_DBG_LED1 | PIN_DBG_LED2
               | PIN_DBG_LED3 | PIN_DBG_LED4 | PIN_DBG_LED5;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(PORT_DBG_LED, &gpio);

    DebugLed_AllOff();
    uint32_t now = HAL_GetTick();
    for (uint8_t i = 0; i < DBG_LED_COUNT; i++) {
        s_leds[i].last_tick = now;
    }
}

void DebugLed_Set(uint8_t led_mask, DebugLedMode_t mode)
{
    for (uint8_t i = 0; i < DBG_LED_COUNT; i++) {
        if (led_mask & (1U << i)) {
            s_leds[i].mode = mode;
            s_leds[i].last_tick = HAL_GetTick();
            if (mode == DBG_LED_OFF) {
                s_leds[i].state = 0;
                HAL_GPIO_WritePin(s_leds[i].port, s_leds[i].pin, GPIO_PIN_RESET);
            } else if (mode == DBG_LED_ON) {
                s_leds[i].state = 1;
                HAL_GPIO_WritePin(s_leds[i].port, s_leds[i].pin, GPIO_PIN_SET);
            }
        }
    }
}

void DebugLed_Update(void)
{
    uint32_t now = HAL_GetTick();

    for (uint8_t i = 0; i < DBG_LED_COUNT; i++) {
        uint32_t period_ms;
        switch (s_leds[i].mode) {
            case DBG_LED_BLINK_1HZ:
                period_ms = 500;  /* 500ms ON / 500ms OFF = 1Hz */
                break;
            case DBG_LED_BLINK_4HZ:
                period_ms = 125;  /* 125ms ON / 125ms OFF = 4Hz */
                break;
            default:
                continue;
        }

        if ((now - s_leds[i].last_tick) >= period_ms) {
            s_leds[i].state ^= 1;
            HAL_GPIO_WritePin(s_leds[i].port, s_leds[i].pin,
                              s_leds[i].state ? GPIO_PIN_SET : GPIO_PIN_RESET);
            s_leds[i].last_tick = now;
        }
    }
}

void DebugLed_AllOff(void)
{
    for (uint8_t i = 0; i < DBG_LED_COUNT; i++) {
        s_leds[i].mode  = DBG_LED_OFF;
        s_leds[i].state = 0;
        HAL_GPIO_WritePin(s_leds[i].port, s_leds[i].pin, GPIO_PIN_RESET);
    }
}
