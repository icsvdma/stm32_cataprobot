#ifndef BMP280_H
#define BMP280_H

#include "stm32g4xx_hal.h"
#include <stdint.h>

/* BMP280ドライバーの処理結果。 */
typedef enum {
    BMP280_OK = 0,                /* 正常終了。 */
    BMP280_ERROR_ARGUMENT,        /* 引数または初期化状態が不正。 */
    BMP280_ERROR_NOT_FOUND,       /* I2Cアドレスに応答がない。 */
    BMP280_ERROR_ID,              /* チップIDがBMP280と一致しない。 */
    BMP280_ERROR_IO,              /* I2C読み書きに失敗した。 */
    BMP280_ERROR_CALIBRATION,     /* 工場補正値が不正。 */
} BMP280_Status_t;

/**
 * @brief BMP280の接続情報、補正値、計算途中の値を保持する。
 */
typedef struct {
    I2C_HandleTypeDef *hi2c;  /* 使用するI2Cポート。 */
    uint8_t address;          /* 検出した7 bit I2Cアドレス。 */
    uint8_t initialized;      /* 初期化済みなら1。 */
    uint16_t dig_t1;          /* 以下は個体ごとの工場補正値。 */
    int16_t dig_t2;
    int16_t dig_t3;
    uint16_t dig_p1;
    int16_t dig_p2;
    int16_t dig_p3;
    int16_t dig_p4;
    int16_t dig_p5;
    int16_t dig_p6;
    int16_t dig_p7;
    int16_t dig_p8;
    int16_t dig_p9;
    int32_t t_fine;          /* 気圧補正にも使う温度の中間値。 */
} BMP280_t;

/**
 * @brief BMP280を検出し、工場補正値を読み、通常測定モードを開始する。
 * @param device 初期化するドライバー状態。
 * @param hi2c 初期化済みのHAL I2Cハンドル。
 * @return 成功時は`BMP280_OK`、失敗時は原因別のエラー。
 */
BMP280_Status_t BMP280_Init(BMP280_t *device, I2C_HandleTypeDef *hi2c);

/**
 * @brief 生データを読み、工場補正値を使って温度と気圧へ変換する。
 * @param device 初期化済みのドライバー状態。
 * @param temperature_centi_c 温度の格納先[0.01度C]。
 * @param pressure_pa 気圧の格納先[Pa]。
 * @return 成功時は`BMP280_OK`、失敗時は引数・I2C・計算エラー。
 */
BMP280_Status_t BMP280_ReadTemperaturePressure(
    BMP280_t *device, int16_t *temperature_centi_c, uint32_t *pressure_pa);

#endif /* BMP280_H */
