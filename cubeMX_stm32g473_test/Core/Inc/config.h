#ifndef CONFIG_H
#define CONFIG_H

#include "stm32g4xx_hal.h"

/* ---------- ピン定義 ---------- */

/* ヘッドライト LED (PWM) */
#define PIN_FRONT_LED_R         GPIO_PIN_0  /* PA0 */
#define PORT_FRONT_LED_R        GPIOA
#define PIN_FRONT_LED_L         GPIO_PIN_1  /* PA1 */
#define PORT_FRONT_LED_L        GPIOA

/* USART2 (ESP32 通信) — CubeMX が自動設定 */
/* PA2 = TX, PA3 = RX */

/* SPI1 Slave (ESP32 通信) — CubeMX が自動設定 */
/* PA4 = NSS, PA5 = SCK, PA6 = MISO, PA7 = MOSI */

/* TB6575 DC モーター */
#define PIN_TB6575_PWM          GPIO_PIN_0  /* PB0 (TIM3_CH3) */
#define PORT_TB6575_PWM         GPIOB
#define PIN_TB6575_FST          GPIO_PIN_1  /* PB1 */
#define PORT_TB6575_FST         GPIOB
#define PIN_TB6575_CW_CCW       GPIO_PIN_2  /* PB2 */
#define PORT_TB6575_CW_CCW      GPIOB
#define PIN_TB6575_FMAX         GPIO_PIN_3  /* PB3 */
#define PORT_TB6575_FMAX        GPIOB
#define PIN_TB6575_FGOUT        GPIO_PIN_4  /* PB4 (TIM IC) */
#define PORT_TB6575_FGOUT       GPIOB

/* TB6608 ステッピングモーター */
#define PIN_6608_MO_A           GPIO_PIN_6  /* PB6 */
#define PORT_6608_MO_A          GPIOB
#define PIN_6608_MO_B           GPIO_PIN_7  /* PB7 */
#define PORT_6608_MO_B          GPIOB
#define PIN_6608_CK_A           GPIO_PIN_10 /* PB10 */
#define PORT_6608_CK_A          GPIOB
#define PIN_6608_CK_B           GPIO_PIN_11 /* PB11 */
#define PORT_6608_CK_B          GPIOB
#define PIN_6608_CCWA           GPIO_PIN_12 /* PB12 */
#define PORT_6608_CCWA          GPIOB
#define PIN_6608_CCWB           GPIO_PIN_13 /* PB13 */
#define PORT_6608_CCWB          GPIOB
#define PIN_6608_RESET_A        GPIO_PIN_14 /* PB14 */
#define PORT_6608_RESET_A       GPIOB
#define PIN_6608_RESET_B        GPIO_PIN_15 /* PB15 */
#define PORT_6608_RESET_B       GPIOB

/* TB6612FNG デュアル DC モーター */
#define PIN_TB6612_PWMA         GPIO_PIN_0  /* PC0 (TIM1_CH1) */
#define PORT_TB6612_PWMA        GPIOC
#define PIN_TB6612_PWMB         GPIO_PIN_1  /* PC1 (TIM1_CH2) */
#define PORT_TB6612_PWMB        GPIOC
#define PIN_TB6612_STBY         GPIO_PIN_2  /* PC2 */
#define PORT_TB6612_STBY        GPIOC
#define PIN_TB6612_AIN1         GPIO_PIN_6  /* PC6 */
#define PORT_TB6612_AIN1        GPIOC
#define PIN_TB6612_AIN2         GPIO_PIN_7  /* PC7 */
#define PORT_TB6612_AIN2        GPIOC
#define PIN_TB6612_BIN1         GPIO_PIN_8  /* PC8 */
#define PORT_TB6612_BIN1        GPIOC
#define PIN_TB6612_BIN2         GPIO_PIN_9  /* PC9 */
#define PORT_TB6612_BIN2        GPIOC

/* デバッグ LED (PC10-PC15) */
#define PIN_DBG_LED0            GPIO_PIN_10 /* PC10 */
#define PIN_DBG_LED1            GPIO_PIN_11 /* PC11 */
#define PIN_DBG_LED2            GPIO_PIN_12 /* PC12 */
#define PIN_DBG_LED3            GPIO_PIN_13 /* PC13 */
#define PIN_DBG_LED4            GPIO_PIN_14 /* PC14 */
#define PIN_DBG_LED5            GPIO_PIN_15 /* PC15 */
#define PORT_DBG_LED            GPIOC
#define DBG_LED_COUNT           6

/* ---------- タイミング定数 ---------- */
#define COMM_WATCHDOG_MS        1000
#define MOTOR_REVERSE_DECEL_MS  100

/* ---------- コマンド ID ---------- */
#define CMD_MOTOR_SPEED         0x01
#define CMD_MOTOR_STOP          0x02
#define CMD_STEPPER_CTRL        0x03
#define CMD_EMERGENCY_STOP      0x04
#define CMD_STATUS_REQ          0x10
#define CMD_LED_MODE            0x11
#define CMD_LED_BRIGHTNESS      0x12
#define CMD_SENSOR_REQ          0x20
#define CMD_SHUTDOWN            0x30
#define CMD_DEBUG_LED           0x40
#define CMD_ACK                 0x80
#define CMD_NACK                0x81
#define CMD_STATUS_RESP         0x90
#define CMD_SENSOR_RESP         0x91
#define CMD_FALL_NOTIFY         0x92
#define CMD_SHUTDOWN_ACK        0xF0

#endif /* CONFIG_H */
