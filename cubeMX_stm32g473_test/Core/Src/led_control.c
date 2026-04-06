#include "led_control.h"
#include "config.h"

/* ---------- 定数 ---------- */
#define LED_PWM_PSC         63      /* 64MHz / 64 = 1MHz */
#define LED_PWM_ARR         999     /* 1MHz / 1000 = 1kHz PWM */
#define LED_BRIGHTNESS_MAX  100

#define LED_LOW_PERCENT     30
#define LED_HIGH_PERCENT    100

#define HAZARD_PERIOD_MS    500
#define TURN_PERIOD_MS      500
#define STROBE_PERIOD_MS    100
#define BREATH_PERIOD_MS    3000    /* 1 サイクル 3 秒 */
#define WELCOME_STEP_MS     80
#define WELCOME_STEPS       12

/* ---------- 内部変数 ---------- */
static TIM_HandleTypeDef *s_htim;
static LedMode_t  s_mode        = LM_OFF;
static LedMode_t  s_base_mode   = LM_OFF;   /* 自動割込復帰先 */
static uint8_t    s_override    = 0;         /* ブレーキ等の自動割込中フラグ */
static uint8_t    s_brightness_r = 0;        /* 0-100 */
static uint8_t    s_brightness_l = 0;        /* 0-100 */
static uint32_t   s_last_tick   = 0;
static uint8_t    s_blink_state = 0;
static uint16_t   s_breath_phase = 0;        /* 0 〜 BREATH_PERIOD_MS */
static uint8_t    s_welcome_step = 0;
static uint8_t    s_welcome_done = 0;

/* ---------- 正弦波近似テーブル (0-255, 四半波 16 エントリ) ---------- */
static const uint8_t s_sine_quarter[17] = {
    0, 25, 50, 74, 98, 120, 142, 162,
    180, 197, 212, 225, 236, 244, 250, 254, 255
};

/* ---------- 内部ヘルパ ---------- */
static uint16_t percent_to_ccr(uint8_t pct)
{
    if (pct == 0)   return 0;
    if (pct >= 100) return LED_PWM_ARR;
    return (uint16_t)((uint32_t)pct * LED_PWM_ARR / 100);
}

static void set_pwm(uint16_t ccr_r, uint16_t ccr_l)
{
    __HAL_TIM_SET_COMPARE(s_htim, TIM_CHANNEL_1, ccr_r);
    __HAL_TIM_SET_COMPARE(s_htim, TIM_CHANNEL_2, ccr_l);
}

static void apply_brightness(uint8_t pct_r, uint8_t pct_l)
{
    s_brightness_r = pct_r;
    s_brightness_l = pct_l;
    set_pwm(percent_to_ccr(pct_r), percent_to_ccr(pct_l));
}

/* 正弦波補間 (phase 0〜period → 輝度 0〜100) */
static uint8_t breath_value(uint16_t phase, uint16_t period)
{
    /* phase を 0〜65535 に正規化 */
    uint32_t norm = (uint32_t)phase * 65536 / period;
    uint16_t idx;
    uint8_t  val;

    /* 四半波展開 */
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

    /* 0-255 → 0-100 */
    return (uint8_t)((uint16_t)val * 100 / 255);
}

/* ---------- 公開 API ---------- */

void LED_Init(TIM_HandleTypeDef *htim)
{
    s_htim = htim;

    /* TIM2 を PWM 用に再設定 (CubeMX のデフォルトは Period=MAX) */
    s_htim->Instance->PSC = LED_PWM_PSC;
    s_htim->Instance->ARR = LED_PWM_ARR;
    s_htim->Instance->EGR = TIM_EGR_UG;   /* 強制更新で PSC/ARR を即反映 */

    /* PWM 出力開始 (duty=0 で消灯状態) */
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

    /* ブレーキ等の自動割込モードの場合、復帰先を保存 */
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

    /* 即時反映が必要なモード */
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
            /* センサ/モーター連動は外部から LED_SetBrightness で更新 */
            apply_brightness(LED_LOW_PERCENT, LED_LOW_PERCENT);
            break;
        default:
            /* 点滅系は LED_Update で処理 */
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
            /* 1ms 単位で phase を進める */
            s_breath_phase = (uint16_t)(now % BREATH_PERIOD_MS);
            uint8_t pct = breath_value(s_breath_phase, BREATH_PERIOD_MS);
            apply_brightness(pct, pct);
            break;
        }

        case LM_WELCOME: {
            if (s_welcome_done) {
                /* ウェルカム完了後は OFF に遷移 */
                LED_SetMode(LM_OFF);
                break;
            }
            if (elapsed >= WELCOME_STEP_MS) {
                s_last_tick = now;
                /* 左右交互にフェードイン・フェードアウト */
                if (s_welcome_step < WELCOME_STEPS) {
                    uint8_t pct = (uint8_t)((uint16_t)(s_welcome_step + 1) * 100 / WELCOME_STEPS);
                    if (s_welcome_step & 1) {
                        apply_brightness(0, pct);
                    } else {
                        apply_brightness(pct, 0);
                    }
                    s_welcome_step++;
                } else {
                    /* 最後に両方全灯→消灯 */
                    apply_brightness(LED_HIGH_PERCENT, LED_HIGH_PERCENT);
                    s_welcome_done = 1;
                }
            }
            break;
        }

        case LM_BATTERY: {
            /* バッテリー残量表示: 低速点滅で通知 (1Hz) */
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
