#include "motor_tb6612.h"
#include "config.h"

/* ---------- 内部定数 ---------- */
#define TB6612_PWM_FREQ_HZ      20000   /* 20kHz (可聴域外) */
#define TB6612_SPEED_MAX        1000    /* speed 値の上限 */
#define TB6612_SLEW_RATE        50      /* 1ms あたりの最大 duty 変化量 */
#define TB6612_REVERSE_DECEL_MS 100     /* 反転時の減速待機時間 */

/* ---------- 内部変数 ---------- */
static TIM_HandleTypeDef *s_htim;

typedef struct {
    TB6612_Dir_t  target_dir;
    TB6612_Dir_t  current_dir;
    uint16_t      target_speed;
    uint16_t      current_speed;
    uint32_t      reverse_tick;     /* 反転シーケンス用タイムスタンプ */
    uint8_t       reversing;        /* 反転シーケンス中フラグ */
} MotorState_t;

static MotorState_t s_motor[2];

/* ---------- GPIO 制御 ---------- */
static void set_direction_pins(TB6612_Channel_t ch, TB6612_Dir_t dir)
{
    GPIO_TypeDef *port_in1, *port_in2;
    uint16_t pin_in1, pin_in2;

    if (ch == TB6612_CH_A) {
        port_in1 = PORT_TB6612_AIN1; pin_in1 = PIN_TB6612_AIN1;
        port_in2 = PORT_TB6612_AIN2; pin_in2 = PIN_TB6612_AIN2;
    } else {
        port_in1 = PORT_TB6612_BIN1; pin_in1 = PIN_TB6612_BIN1;
        port_in2 = PORT_TB6612_BIN2; pin_in2 = PIN_TB6612_BIN2;
    }

    switch (dir) {
    case TB6612_DIR_CW:
        HAL_GPIO_WritePin(port_in1, pin_in1, GPIO_PIN_SET);
        HAL_GPIO_WritePin(port_in2, pin_in2, GPIO_PIN_RESET);
        break;
    case TB6612_DIR_CCW:
        HAL_GPIO_WritePin(port_in1, pin_in1, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(port_in2, pin_in2, GPIO_PIN_SET);
        break;
    case TB6612_DIR_BRAKE:
        HAL_GPIO_WritePin(port_in1, pin_in1, GPIO_PIN_SET);
        HAL_GPIO_WritePin(port_in2, pin_in2, GPIO_PIN_SET);
        break;
    case TB6612_DIR_COAST:
    default:
        HAL_GPIO_WritePin(port_in1, pin_in1, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(port_in2, pin_in2, GPIO_PIN_RESET);
        break;
    }
}

/* ---------- PWM CCR 設定 ---------- */
static void set_pwm_ccr(TB6612_Channel_t ch, uint16_t speed)
{
    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(s_htim);
    uint32_t ccr = (uint32_t)speed * arr / TB6612_SPEED_MAX;
    if (ccr > arr) ccr = arr;

    if (ch == TB6612_CH_A) {
        __HAL_TIM_SET_COMPARE(s_htim, TIM_CHANNEL_1, ccr);
    } else {
        __HAL_TIM_SET_COMPARE(s_htim, TIM_CHANNEL_2, ccr);
    }
}

/* ---------- 初期化 ---------- */
void TB6612_Init(TIM_HandleTypeDef *htim)
{
    s_htim = htim;

    /*
     * TIM1 を 20kHz PWM に再設定
     * SystemCoreClock = 64MHz, APB2 prescaler = 1 → TIM1 CLK = 64MHz
     * PSC = 0, ARR = (64MHz / 20kHz) - 1 = 3199
     */
    s_htim->Instance->PSC = 0;
    s_htim->Instance->ARR = (SystemCoreClock / TB6612_PWM_FREQ_HZ) - 1;
    s_htim->Instance->EGR = TIM_EGR_UG;    /* Update event → レジスタ反映 */

    /* 両ch の CCR を 0 にしてから PWM 開始 */
    __HAL_TIM_SET_COMPARE(s_htim, TIM_CHANNEL_1, 0);
    __HAL_TIM_SET_COMPARE(s_htim, TIM_CHANNEL_2, 0);
    HAL_TIM_PWM_Start(s_htim, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(s_htim, TIM_CHANNEL_2);

    /* TIM1 は Advanced Timer → MOE を有効にしないと出力されない */
    __HAL_TIM_MOE_ENABLE(s_htim);

    /* 方向ピンをコースト状態に */
    set_direction_pins(TB6612_CH_A, TB6612_DIR_COAST);
    set_direction_pins(TB6612_CH_B, TB6612_DIR_COAST);

    /* 内部状態初期化 */
    for (int i = 0; i < 2; i++) {
        s_motor[i].target_dir    = TB6612_DIR_COAST;
        s_motor[i].current_dir   = TB6612_DIR_COAST;
        s_motor[i].target_speed  = 0;
        s_motor[i].current_speed = 0;
        s_motor[i].reverse_tick  = 0;
        s_motor[i].reversing     = 0;
    }

    /* STBY = HIGH → 動作状態 */
    TB6612_StandbyEnable();
}

/* ---------- 方向設定 ---------- */
void TB6612_SetDirection(TB6612_Channel_t ch, TB6612_Dir_t dir)
{
    if (ch > TB6612_CH_B || dir > TB6612_DIR_COAST) return;
    s_motor[ch].target_dir = dir;
}

/* ---------- 速度設定 ---------- */
void TB6612_SetSpeed(TB6612_Channel_t ch, uint16_t speed)
{
    if (ch > TB6612_CH_B) return;
    if (speed > TB6612_SPEED_MAX) speed = TB6612_SPEED_MAX;
    s_motor[ch].target_speed = speed;
}

/* ---------- 方向 + 速度 一括設定 ---------- */
void TB6612_SetMotor(TB6612_Channel_t ch, TB6612_Dir_t dir, uint16_t speed)
{
    if (ch > TB6612_CH_B || dir > TB6612_DIR_COAST) return;
    if (speed > TB6612_SPEED_MAX) speed = TB6612_SPEED_MAX;
    s_motor[ch].target_dir   = dir;
    s_motor[ch].target_speed = speed;
}

/* ---------- チャンネル停止（コースト） ---------- */
void TB6612_Stop(TB6612_Channel_t ch)
{
    if (ch > TB6612_CH_B) return;
    s_motor[ch].target_dir   = TB6612_DIR_COAST;
    s_motor[ch].target_speed = 0;
}

/* ---------- 緊急停止 ---------- */
void TB6612_EmergencyStop(void)
{
    /* 即座にブレーキ + PWM=0 + STBY=LOW */
    for (int i = 0; i < 2; i++) {
        s_motor[i].target_dir    = TB6612_DIR_BRAKE;
        s_motor[i].target_speed  = 0;
        s_motor[i].current_dir   = TB6612_DIR_BRAKE;
        s_motor[i].current_speed = 0;
        s_motor[i].reversing     = 0;
        set_direction_pins((TB6612_Channel_t)i, TB6612_DIR_BRAKE);
        set_pwm_ccr((TB6612_Channel_t)i, 0);
    }
    TB6612_StandbyDisable();
}

/* ---------- STBY 制御 ---------- */
void TB6612_StandbyEnable(void)
{
    HAL_GPIO_WritePin(PORT_TB6612_STBY, PIN_TB6612_STBY, GPIO_PIN_SET);
}

void TB6612_StandbyDisable(void)
{
    HAL_GPIO_WritePin(PORT_TB6612_STBY, PIN_TB6612_STBY, GPIO_PIN_RESET);
}

/* ---------- 周期更新（メインループから呼ぶ） ---------- */
static void update_channel(TB6612_Channel_t ch)
{
    MotorState_t *m = &s_motor[ch];

    /* ---- 反転シーケンス処理 ---- */
    if (m->reversing) {
        if (m->current_speed > 0) {
            /* 減速中: スルーレートで速度を落とす */
            uint16_t dec = TB6612_SLEW_RATE;
            m->current_speed = (m->current_speed > dec) ? (m->current_speed - dec) : 0;
            set_pwm_ccr(ch, m->current_speed);
            return;
        }
        /* 速度がゼロに到達 → 停止待機 */
        if ((HAL_GetTick() - m->reverse_tick) < TB6612_REVERSE_DECEL_MS) {
            return;
        }
        /* 待機完了 → 新方向に切り替え */
        m->reversing = 0;
        m->current_dir = m->target_dir;
        set_direction_pins(ch, m->current_dir);
    }

    /* ---- 方向変更検出（回転中の反転 = シーケンス起動） ---- */
    uint8_t cur_rotating = (m->current_dir == TB6612_DIR_CW || m->current_dir == TB6612_DIR_CCW);
    uint8_t tgt_rotating = (m->target_dir == TB6612_DIR_CW || m->target_dir == TB6612_DIR_CCW);

    if (cur_rotating && tgt_rotating && m->current_dir != m->target_dir && m->current_speed > 0) {
        /* 反転シーケンス開始 */
        m->reversing = 1;
        m->reverse_tick = HAL_GetTick();
        return;
    }

    /* ---- 方向が変わった場合（停止状態 or ブレーキ/コースト切替） ---- */
    if (m->current_dir != m->target_dir) {
        m->current_dir = m->target_dir;
        set_direction_pins(ch, m->current_dir);
    }

    /* ---- スルーレート制限付き速度更新 ---- */
    if (m->current_speed < m->target_speed) {
        uint16_t inc = TB6612_SLEW_RATE;
        m->current_speed += inc;
        if (m->current_speed > m->target_speed) m->current_speed = m->target_speed;
    } else if (m->current_speed > m->target_speed) {
        uint16_t dec = TB6612_SLEW_RATE;
        m->current_speed = (m->current_speed > dec) ? (m->current_speed - dec) : 0;
        if (m->current_speed < m->target_speed) m->current_speed = m->target_speed;
    }

    set_pwm_ccr(ch, m->current_speed);
}

void TB6612_Update(void)
{
    update_channel(TB6612_CH_A);
    update_channel(TB6612_CH_B);
}
