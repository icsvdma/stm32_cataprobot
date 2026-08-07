#include "motor_hbridge.h"
#include "config.h"

/* PWM周波数、指令値の上限、1更新あたりの加減速量、反転待ち時間。 */
#define HBRIDGE_PWM_FREQ_HZ      20000U
#define HBRIDGE_SPEED_MAX        1000U
#define HBRIDGE_SLEW_RATE        50U
#define HBRIDGE_REVERSE_WAIT_MS  100U

/* HブリッジのPWMに使用するTIM8。 */
static TIM_HandleTypeDef *s_htim;

/** @brief モーター1台分の目標値、現在値、反転処理状態。 */
typedef struct {
    HBridge_Direction_t target_dir;
    HBridge_Direction_t current_dir;
    uint16_t target_speed;
    uint16_t current_speed;
    uint32_t reverse_tick;
    uint8_t reversing;
} MotorState_t;

static MotorState_t s_motor[2];

/**
 * @brief 0から1000の速度をTIM8の比較値へ変換する。
 * @param speed 速度指令。
 * @return PWM比較値。
 */
static uint32_t speed_to_ccr(uint16_t speed)
{
    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(s_htim);
    uint32_t ccr = (uint32_t)speed * arr / HBRIDGE_SPEED_MAX;
    if (ccr >= arr && arr > 0U) {
        ccr = arr - 1U;
    }
    return ccr;
}

/**
 * @brief 現在方向と速度から2本のHブリッジ入力PWMを更新する。
 * @param ch モーターAまたはB。
 * @param speed 現在速度。
 * @return なし。
 */
static void set_pwm_ccr(HBridge_Channel_t ch, uint16_t speed)
{
    uint32_t ccr = speed_to_ccr(speed);
    uint32_t ch_in1 = (ch == HBRIDGE_CH_A) ? TIM_CHANNEL_1 : TIM_CHANNEL_3;
    uint32_t ch_in2 = (ch == HBRIDGE_CH_A) ? TIM_CHANNEL_2 : TIM_CHANNEL_4;
    HBridge_Direction_t dir = s_motor[ch].current_dir;

    /*
     * Hブリッジ入力:
     *   逆転: IN1=PWM、IN2=Low
     *   正転: IN1=Low、IN2=PWM
     *   停止: IN1=Low、IN2=Low
     */
    if (dir == HBRIDGE_DIR_CCW) {
        __HAL_TIM_SET_COMPARE(s_htim, ch_in1, ccr);
        __HAL_TIM_SET_COMPARE(s_htim, ch_in2, 0U);
    } else if (dir == HBRIDGE_DIR_CW) {
        __HAL_TIM_SET_COMPARE(s_htim, ch_in1, 0U);
        __HAL_TIM_SET_COMPARE(s_htim, ch_in2, ccr);
    } else {
        __HAL_TIM_SET_COMPARE(s_htim, ch_in1, 0U);
        __HAL_TIM_SET_COMPARE(s_htim, ch_in2, 0U);
    }
}

void HBridge_Init(TIM_HandleTypeDef *htim)
{
    s_htim = htim;

    /* TIM8のPWM周波数を20 kHzへ設定する。 */
    s_htim->Instance->PSC = 0U;
    s_htim->Instance->ARR = (SystemCoreClock / HBRIDGE_PWM_FREQ_HZ) - 1U;
    s_htim->Instance->EGR = TIM_EGR_UG;

    __HAL_TIM_SET_COMPARE(s_htim, TIM_CHANNEL_1, 0U);
    __HAL_TIM_SET_COMPARE(s_htim, TIM_CHANNEL_2, 0U);
    __HAL_TIM_SET_COMPARE(s_htim, TIM_CHANNEL_3, 0U);
    __HAL_TIM_SET_COMPARE(s_htim, TIM_CHANNEL_4, 0U);
    HAL_TIM_PWM_Start(s_htim, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(s_htim, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(s_htim, TIM_CHANNEL_3);
    HAL_TIM_PWM_Start(s_htim, TIM_CHANNEL_4);

    __HAL_TIM_MOE_ENABLE(s_htim);

    /* 左右とも速度0のブレーキ状態から開始する。 */
    for (uint8_t i = 0U; i < 2U; i++) {
        s_motor[i].target_dir = HBRIDGE_DIR_BRAKE;
        s_motor[i].current_dir = HBRIDGE_DIR_BRAKE;
        s_motor[i].target_speed = 0U;
        s_motor[i].current_speed = 0U;
        s_motor[i].reverse_tick = 0U;
        s_motor[i].reversing = 0U;
    }

    HBridge_Enable();
}

void HBridge_SetDirection(HBridge_Channel_t ch, HBridge_Direction_t dir)
{
    if (ch > HBRIDGE_CH_B || dir > HBRIDGE_DIR_BRAKE) {
        return;
    }
    s_motor[ch].target_dir = dir;
}

void HBridge_SetSpeed(HBridge_Channel_t ch, uint16_t speed)
{
    if (ch > HBRIDGE_CH_B) {
        return;
    }
    if (speed > HBRIDGE_SPEED_MAX) {
        speed = HBRIDGE_SPEED_MAX;
    }
    s_motor[ch].target_speed = speed;
}

void HBridge_SetMotor(HBridge_Channel_t ch, HBridge_Direction_t dir, uint16_t speed)
{
    if (ch > HBRIDGE_CH_B || dir > HBRIDGE_DIR_BRAKE) {
        return;
    }
    if (speed > HBRIDGE_SPEED_MAX) {
        speed = HBRIDGE_SPEED_MAX;
    }
    s_motor[ch].target_dir = dir;
    s_motor[ch].target_speed = speed;
}

void HBridge_Stop(HBridge_Channel_t ch)
{
    if (ch > HBRIDGE_CH_B) {
        return;
    }
    s_motor[ch].target_dir = HBRIDGE_DIR_BRAKE;
    s_motor[ch].target_speed = 0U;
}

void HBridge_EmergencyStop(void)
{
    /* 加減速を待たずに比較値を0へ落とし、駆動許可も解除する。 */
    for (uint8_t i = 0U; i < 2U; i++) {
        s_motor[i].target_dir = HBRIDGE_DIR_BRAKE;
        s_motor[i].target_speed = 0U;
        s_motor[i].current_dir = HBRIDGE_DIR_BRAKE;
        s_motor[i].current_speed = 0U;
        s_motor[i].reversing = 0U;
        set_pwm_ccr((HBridge_Channel_t)i, 0U);
    }
    HBridge_Disable();
}

void HBridge_Enable(void)
{
    HAL_GPIO_WritePin(PORT_HBRIDGE_ENABLE, PIN_HBRIDGE_ENABLE, GPIO_PIN_SET);
}

void HBridge_Disable(void)
{
    HAL_GPIO_WritePin(PORT_HBRIDGE_ENABLE, PIN_HBRIDGE_ENABLE, GPIO_PIN_RESET);
}

/**
 * @brief 1台分の加減速・方向反転処理を進める。
 * @param ch モーターAまたはB。
 * @return なし。
 */
static void update_channel(HBridge_Channel_t ch)
{
    MotorState_t *m = &s_motor[ch];

    /* 反転中は速度0まで落とし、その後100 ms待って方向を切り替える。 */
    if (m->reversing) {
        if (m->current_speed > 0U) {
            uint16_t dec = HBRIDGE_SLEW_RATE;
            m->current_speed = (m->current_speed > dec) ? (m->current_speed - dec) : 0U;
            set_pwm_ccr(ch, m->current_speed);
            return;
        }
        if ((HAL_GetTick() - m->reverse_tick) < HBRIDGE_REVERSE_WAIT_MS) {
            return;
        }
        m->reversing = 0U;
        m->current_dir = m->target_dir;
    }

    uint8_t cur_rotating =
        (m->current_dir == HBRIDGE_DIR_CW || m->current_dir == HBRIDGE_DIR_CCW);
    uint8_t tgt_rotating =
        (m->target_dir == HBRIDGE_DIR_CW || m->target_dir == HBRIDGE_DIR_CCW);

    /* 回転中に逆方向を要求された場合は、反転状態へ移行する。 */
    if (cur_rotating && tgt_rotating &&
        m->current_dir != m->target_dir && m->current_speed > 0U) {
        m->reversing = 1U;
        m->reverse_tick = HAL_GetTick();
        return;
    }

    if (m->current_dir != m->target_dir) {
        m->current_dir = m->target_dir;
    }

    /* 現在速度を1回50ずつ目標速度へ近づける。 */
    if (m->current_speed < m->target_speed) {
        uint16_t inc = HBRIDGE_SLEW_RATE;
        m->current_speed += inc;
        if (m->current_speed > m->target_speed) {
            m->current_speed = m->target_speed;
        }
    } else if (m->current_speed > m->target_speed) {
        uint16_t dec = HBRIDGE_SLEW_RATE;
        m->current_speed = (m->current_speed > dec) ? (m->current_speed - dec) : 0U;
        if (m->current_speed < m->target_speed) {
            m->current_speed = m->target_speed;
        }
    }

    set_pwm_ccr(ch, m->current_speed);
}

void HBridge_Update(void)
{
    update_channel(HBRIDGE_CH_A);
    update_channel(HBRIDGE_CH_B);
}
