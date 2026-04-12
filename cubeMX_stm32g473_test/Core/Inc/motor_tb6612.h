#ifndef MOTOR_TB6612_H
#define MOTOR_TB6612_H

#include "stm32g4xx_hal.h"
#include <stdint.h>

typedef enum {
    TB6612_CH_A = 0,
    TB6612_CH_B = 1,
} TB6612_Channel_t;

typedef enum {
    TB6612_DIR_CW     = 0x00,   /* 正転: xIN1=H, xIN2=L */
    TB6612_DIR_CCW    = 0x01,   /* 逆転: xIN1=L, xIN2=H */
    TB6612_DIR_BRAKE  = 0x02,   /* ブレーキ: xIN1=H, xIN2=H */
    TB6612_DIR_COAST  = 0x03,   /* コースト: xIN1=L, xIN2=L */
} TB6612_Dir_t;

void     TB6612_Init(TIM_HandleTypeDef *htim);
void     TB6612_SetDirection(TB6612_Channel_t ch, TB6612_Dir_t dir);
void     TB6612_SetSpeed(TB6612_Channel_t ch, uint16_t speed);
void     TB6612_SetMotor(TB6612_Channel_t ch, TB6612_Dir_t dir, uint16_t speed);
void     TB6612_Stop(TB6612_Channel_t ch);
void     TB6612_EmergencyStop(void);
void     TB6612_StandbyEnable(void);
void     TB6612_StandbyDisable(void);
void     TB6612_Update(void);

#endif /* MOTOR_TB6612_H */
