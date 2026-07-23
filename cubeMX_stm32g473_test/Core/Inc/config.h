#ifndef CONFIG_H
#define CONFIG_H

#include "stm32g4xx_hal.h"

/*
 * STM32全体で共有するピン番号、通信コマンド、制御時間を定義する。
 * CubeMXの設定を変えた場合は、このファイルとの一致も確認する。
 */

/* ---------- ヘッドライトPWM出力 ---------- */
#define PIN_FRONT_LED_R         GPIO_PIN_0  /* PA0 / TIM2_CH1。 */
#define PORT_FRONT_LED_R        GPIOA
#define PIN_FRONT_LED_L         GPIO_PIN_1  /* PA1 / TIM2_CH2。 */
#define PORT_FRONT_LED_L        GPIOA

/* USART2はPA2=TX、PA3=RX。CubeMXが設定する。 */

/* SPI1 SlaveはPA4=NSS、PA5=SCK、PA6=MISO、PA7=MOSI。 */

/* ---------- 2ch HブリッジDCモーター駆動回路 ---------- */
#define PIN_HBRIDGE_ENABLE      GPIO_PIN_2  /* PC2: 共通駆動許可。 */
#define PORT_HBRIDGE_ENABLE     GPIOC
#define PIN_HBRIDGE_A_IN1       GPIO_PIN_6  /* PC6 / TIM8_CH1。 */
#define PORT_HBRIDGE_A_IN1      GPIOC
#define PIN_HBRIDGE_A_IN2       GPIO_PIN_7  /* PC7 / TIM8_CH2。 */
#define PORT_HBRIDGE_A_IN2      GPIOC
#define PIN_HBRIDGE_B_IN1       GPIO_PIN_8  /* PC8 / TIM8_CH3。 */
#define PORT_HBRIDGE_B_IN1      GPIOC
#define PIN_HBRIDGE_B_IN2       GPIO_PIN_9  /* PC9 / TIM8_CH4。 */
#define PORT_HBRIDGE_B_IN2      GPIOC

/* ---------- 状態確認用LED ---------- */
#define PIN_DBG_LED0            GPIO_PIN_10 /* PC10 */
#define PIN_DBG_LED1            GPIO_PIN_11 /* PC11 */
#define PIN_DBG_LED2            GPIO_PIN_12 /* PC12 */
#define PIN_DBG_LED3            GPIO_PIN_13 /* PC13 */
#define PIN_DBG_LED4            GPIO_PIN_14 /* PC14 */
#define PIN_DBG_LED5            GPIO_PIN_15 /* PC15 */
#define PORT_DBG_LED            GPIOC
#define DBG_LED_COUNT           6

/* ---------- 安全制御時間 ---------- */
#define COMM_WATCHDOG_MS        1000 /* UART無通信を異常とする時間[ms]。 */
#define MOTOR_REVERSE_DECEL_MS  100  /* 方向反転前に減速する時間[ms]。 */

/* ---------- ESP32と共有するコマンドID ---------- */
#define CMD_MOTOR_SPEED         0x01
#define CMD_MOTOR_STOP          0x02
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
