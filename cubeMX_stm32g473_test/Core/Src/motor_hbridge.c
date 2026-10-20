#include "motor_hbridge.h"
#include "motor_protocol.h"
#include "config.h"
#define STOP_MAGIC 0x43500000UL
static TIM_HandleTypeDef *s_htim;
static MotorSafety s_motor;
static volatile bool s_ready;

/** バックアップ保持値を読む。破損値は緊急停止として返す。 */
static MotorReason retained_reason(void)
{
    uint32_t word = TAMP->BKP0R;
    if (word == 0U && TAMP->BKP1R == 0U) { return MOTOR_REASON_STARTUP; }
    if (word == (STOP_MAGIC | MOTOR_REASON_SHUTDOWN) && TAMP->BKP1R == ~word) {
        return MOTOR_REASON_SHUTDOWN;
    }
    return MOTOR_REASON_EMERGENCY;
}

/** statusをGPIO/TIM8へ反映し、不可逆停止なら保持値も保存。戻り値なし。 */
static void apply_outputs(MotorStatus status)
{
    const uint32_t in1[] = {TIM_CHANNEL_1, TIM_CHANNEL_3};
    const uint32_t in2[] = {TIM_CHANNEL_2, TIM_CHANNEL_4};
    uint32_t period = __HAL_TIM_GET_AUTORELOAD(s_htim) + 1U;
    for (uint8_t i = 0U; i < 2U; ++i) {
        int16_t output = status.output[i];
        uint32_t magnitude = (uint32_t)((output < 0) ? -output : output);
        uint32_t ccr = magnitude * period / 1000U;
        __HAL_TIM_SET_COMPARE(s_htim, in1[i], 0U);
        __HAL_TIM_SET_COMPARE(s_htim, in2[i], 0U);
        if (output > 0) { __HAL_TIM_SET_COMPARE(s_htim, in2[i], ccr); }
        if (output < 0) { __HAL_TIM_SET_COMPARE(s_htim, in1[i], ccr); }
    }
    HAL_GPIO_WritePin(PORT_HBRIDGE_ENABLE, PIN_HBRIDGE_ENABLE,
        (status.state == MOTOR_STATE_RUNNING) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    if (status.state == MOTOR_STATE_EMERGENCY || status.state == MOTOR_STATE_SHUTDOWN) {
        uint32_t word = STOP_MAGIC | (uint32_t)status.reason;
        if (TAMP->BKP0R != word || TAMP->BKP1R != ~word) {
            TAMP->BKP0R = word;
            TAMP->BKP1R = ~word;
            __DSB();
        }
    }
}

void HBridge_Init(TIM_HandleTypeDef *htim)
{
    s_ready = false;
    s_htim = htim;
    HAL_GPIO_WritePin(PORT_HBRIDGE_ENABLE, PIN_HBRIDGE_ENABLE, GPIO_PIN_RESET);
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    __HAL_RCC_RTCAPB_CLK_ENABLE();
    MotorSafety_Init(&s_motor, HAL_GetTick(), retained_reason());
    htim->Instance->PSC = 0U;
    htim->Instance->ARR = SystemCoreClock / 20000U - 1U;
    htim->Instance->EGR = TIM_EGR_UG;
    apply_outputs(MotorSafety_GetStatus(&s_motor));
    HAL_TIM_PWM_Start(htim, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(htim, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(htim, TIM_CHANNEL_3);
    HAL_TIM_PWM_Start(htim, TIM_CHANNEL_4);
    s_ready = true;
}

void HBridge_Update(void)
{
    if (!s_ready) { return; }
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    MotorSafety_Tick(&s_motor, HAL_GetTick());
    apply_outputs(MotorSafety_GetStatus(&s_motor));
    __set_PRIMASK(primask);
}

bool HBridge_Process(const Packet_t *request, Packet_t *response)
{
    if (!s_ready) { return false; }
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    bool handled = MotorProtocol_Handle(&s_motor, request, response, HAL_GetTick());
    if (handled) { apply_outputs(MotorSafety_GetStatus(&s_motor)); }
    __set_PRIMASK(primask);
    return handled;
}

void HBridge_EmergencyStop(void)
{
    HAL_GPIO_WritePin(PORT_HBRIDGE_ENABLE, PIN_HBRIDGE_ENABLE, GPIO_PIN_RESET);
    if (!s_ready) { return; }
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    MotorSafety_Hold(&s_motor, MOTOR_REASON_EMERGENCY, HAL_GetTick());
    apply_outputs(MotorSafety_GetStatus(&s_motor));
    __set_PRIMASK(primask);
}
