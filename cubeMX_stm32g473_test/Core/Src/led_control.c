#include "led_control.h"
#include "config.h"

/* 64 MHzのTIM2から1 kHz PWMを作る設定と各点灯周期。 */
#define LED_PWM_PSC         63      /* 64 MHz / 64 = 1 MHz。 */
#define LED_PWM_ARR         999     /* 1 MHz / 1000 = 1 kHz PWM。 */
#define LED_BRIGHTNESS_MAX  100

#define LED_LOW_PERCENT     30
#define LED_HIGH_PERCENT    100

#define HAZARD_PERIOD_MS    500
#define TURN_PERIOD_MS      500
#define STROBE_PERIOD_MS    100
#define BREATH_PERIOD_MS    3000    /* 呼吸点灯1周期は3秒。 */
#define WELCOME_STEP_MS     80
#define WELCOME_STEPS       12

/* 現在の点灯モード、PWM光量、アニメーション位置。 */
static TIM_HandleTypeDef *s_htim;
static LedMode_t  s_mode        = LM_OFF;
static LedMode_t  s_base_mode   = LM_OFF;   /* 自動割り込み終了後に戻すモード。 */
static uint8_t    s_override    = 0;         /* ブレーキなどの自動割り込み中を表す。 */
static uint8_t    s_brightness_r = 0;        /* 右ライトの光量[0～100%]。 */
static uint8_t    s_brightness_l = 0;        /* 左ライトの光量[0～100%]。 */
static uint32_t   s_last_tick   = 0;
static uint8_t    s_blink_state = 0;
static uint16_t   s_breath_phase = 0;        /* 0 〜 BREATH_PERIOD_MS */
static uint8_t    s_welcome_step = 0;
static uint8_t    s_welcome_done = 0;

/* 呼吸点灯で使う0から255の正弦波1/4周期近似値。 */
static const uint8_t s_sine_quarter[17] = {
    0, 25, 50, 74, 98, 120, 142, 162,
    180, 197, 212, 225, 236, 244, 250, 254, 255
};

/**
 * @brief 0から100%の光量をTIM2比較値へ変換する。
 * @param pct 光量[%]。
 * @return TIM2へ設定する比較値。
 */
static uint16_t percent_to_ccr(uint8_t pct)
{
    if (pct == 0)   return 0;
    if (pct >= 100) return LED_PWM_ARR;
    return (uint16_t)((uint32_t)pct * LED_PWM_ARR / 100);
}

/**
 * @brief 右と左のPWM比較値をTIM2へ書き込む。
 * @param ccr_r 右ライトの比較値。
 * @param ccr_l 左ライトの比較値。
 * @return なし。
 */
static void set_pwm(uint16_t ccr_r, uint16_t ccr_l)
{
    __HAL_TIM_SET_COMPARE(s_htim, TIM_CHANNEL_1, ccr_r);
    __HAL_TIM_SET_COMPARE(s_htim, TIM_CHANNEL_2, ccr_l);
}

/**
 * @brief 左右の光量を保存し、PWMへ反映する。
 * @param pct_r 右ライト光量[%]。
 * @param pct_l 左ライト光量[%]。
 * @return なし。
 */
static void apply_brightness(uint8_t pct_r, uint8_t pct_l)
{
    s_brightness_r = pct_r;
    s_brightness_l = pct_l;
    set_pwm(percent_to_ccr(pct_r), percent_to_ccr(pct_l));
}

/**
 * @brief 正弦波近似表から呼吸点灯の光量を求める。
 * @param phase 現在の周期内時刻。
 * @param period 1周期の長さ。
 * @return 0から100%の光量。
 */
static uint8_t breath_value(uint16_t phase, uint16_t period)
{
    /* 周期内時刻を0から65535へ正規化する。 */
    uint32_t norm = (uint32_t)phase * 65536 / period;
    uint16_t idx;
    uint8_t  val;

    /* 1/4周期の表を折り返し、正弦波1周期へ展開する。 */
    if (norm < 16384) {
        idx = (uint16_t)(norm * 16 / 16384);
        val = s_sine_quarter[idx];
    } else if (norm < 32768) {
        idx = (uint16_t)((32768 - norm) * 16 / 16384);
        val = s_sine_quarter[idx];
    } else if (norm < 49152) {
        idx = (uint16_t)((norm - 32768) * 16 / 16384);
        val = s_sine_quarter[idx];
    } else {
        idx = (uint16_t)((65536 - norm) * 16 / 16384);
        val = s_sine_quarter[idx];
    }

    /* テーブル値0から255を光量0から100%へ変換する。 */
    return (uint8_t)((uint16_t)val * 100 / 255);
}

void LED_Init(TIM_HandleTypeDef *htim)
{
    s_htim = htim;

    /* CubeMX初期値を1 kHz PWM用のPSCとARRへ上書きする。 */
    s_htim->Instance->PSC = LED_PWM_PSC;
    s_htim->Instance->ARR = LED_PWM_ARR;
    s_htim->Instance->EGR = TIM_EGR_UG;   /* 強制更新して設定を即時反映。 */

    /* duty 0の消灯状態で左右PWMを開始する。 */
    HAL_TIM_PWM_Start(s_htim, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(s_htim, TIM_CHANNEL_2);

    apply_brightness(0, 0);
    s_mode = LM_OFF;
    s_base_mode = LM_OFF;
    s_override = 0;
    s_last_tick = HAL_GetTick();
}

void LED_SetMode(LedMode_t mode)
{
    if (mode > LM_BATTERY) return;

    /* 一時的なブレーキ点灯では、終了後に戻すモードを保存する。 */
    if (mode == LM_BRAKE) {
        if (!s_override) {
            s_base_mode = s_mode;
            s_override = 1;
        }
    } else {
        s_override = 0;
        s_base_mode = mode;
    }

    s_mode = mode;
    s_last_tick = HAL_GetTick();
    s_blink_state = 0;
    s_breath_phase = 0;
    s_welcome_step = 0;

    /* 固定点灯モードはここで反映し、点滅系はLED_Updateで進める。 */
    switch (mode) {
        case LM_OFF:
            apply_brightness(0, 0);
            break;
        case LM_LOW:
            apply_brightness(LED_LOW_PERCENT, LED_LOW_PERCENT);
            break;
        case LM_HIGH:
        case LM_BRAKE:
            apply_brightness(LED_HIGH_PERCENT, LED_HIGH_PERCENT);
            break;
        case LM_AUTO:
        case LM_ADAPTIVE:
            /* センサー連動値は外部からLED_SetBrightnessで更新する。 */
            apply_brightness(LED_LOW_PERCENT, LED_LOW_PERCENT);
            break;
        default:
            /* 点滅・アニメーション系はLED_Updateで処理する。 */
            break;
    }
}

void LED_SetBrightness(LedSide_t side, uint8_t brightness)
{
    if (brightness > LED_BRIGHTNESS_MAX) brightness = LED_BRIGHTNESS_MAX;

    if (side == LED_SIDE_RIGHT || side == LED_SIDE_BOTH) {
        s_brightness_r = brightness;
        __HAL_TIM_SET_COMPARE(s_htim, TIM_CHANNEL_1, percent_to_ccr(brightness));
    }
    if (side == LED_SIDE_LEFT || side == LED_SIDE_BOTH) {
        s_brightness_l = brightness;
        __HAL_TIM_SET_COMPARE(s_htim, TIM_CHANNEL_2, percent_to_ccr(brightness));
    }
}

void LED_Update(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t elapsed = now - s_last_tick;

    switch (s_mode) {
        case LM_HAZARD: {
            if (elapsed >= HAZARD_PERIOD_MS) {
                s_blink_state ^= 1;
                uint8_t pct = s_blink_state ? LED_HIGH_PERCENT : 0;
                apply_brightness(pct, pct);
                s_last_tick = now;
            }
            break;
        }

        case LM_TURN_L: {
            if (elapsed >= TURN_PERIOD_MS) {
                s_blink_state ^= 1;
                uint8_t pct_l = s_blink_state ? LED_HIGH_PERCENT : 0;
                apply_brightness(s_brightness_r, pct_l);
                s_last_tick = now;
            }
            break;
        }

        case LM_TURN_R: {
            if (elapsed >= TURN_PERIOD_MS) {
                s_blink_state ^= 1;
                uint8_t pct_r = s_blink_state ? LED_HIGH_PERCENT : 0;
                apply_brightness(pct_r, s_brightness_l);
                s_last_tick = now;
            }
            break;
        }

        case LM_STROBE: {
            if (elapsed >= STROBE_PERIOD_MS) {
                s_blink_state ^= 1;
                uint8_t pct = s_blink_state ? LED_HIGH_PERCENT : 0;
                apply_brightness(pct, pct);
                s_last_tick = now;
            }
            break;
        }

        case LM_BREATH: {
            /* 現在時刻から呼吸点灯の周期内位置を求める。 */
            s_breath_phase = (uint16_t)(now % BREATH_PERIOD_MS);
            uint8_t pct = breath_value(s_breath_phase, BREATH_PERIOD_MS);
            apply_brightness(pct, pct);
            break;
        }

        case LM_WELCOME: {
            if (s_welcome_done) {
                /* ウェルカム完了後はOFFへ移行する。 */
                LED_SetMode(LM_OFF);
                break;
            }
            if (elapsed >= WELCOME_STEP_MS) {
                s_last_tick = now;
                /* 左右を交互に明るくする。 */
                if (s_welcome_step < WELCOME_STEPS) {
                    uint8_t pct = (uint8_t)((uint16_t)(s_welcome_step + 1) * 100 / WELCOME_STEPS);
                    if (s_welcome_step & 1) {
                        apply_brightness(0, pct);
                    } else {
                        apply_brightness(pct, 0);
                    }
                    s_welcome_step++;
                } else {
                    /* 最後に左右を全点灯し、次回更新で消灯する。 */
                    apply_brightness(LED_HIGH_PERCENT, LED_HIGH_PERCENT);
                    s_welcome_done = 1;
                }
            }
            break;
        }

        case LM_BATTERY: {
            /* バッテリー通知は30%光量で1 Hz点滅する。 */
            if (elapsed >= 500) {
                s_blink_state ^= 1;
                uint8_t pct = s_blink_state ? LED_LOW_PERCENT : 0;
                apply_brightness(pct, pct);
                s_last_tick = now;
            }
            break;
        }

        default:
            /* LM_OFF, LM_LOW, LM_HIGH, LM_BRAKE, LM_AUTO, LM_ADAPTIVE は
               SetMode で即時設定済み。ここでは何もしない。 */
            break;
    }
}

void LED_AllOff(void)
{
    s_mode = LM_OFF;
    s_override = 0;
    apply_brightness(0, 0);
}

LedMode_t LED_GetMode(void)
{
    return s_mode;
}
