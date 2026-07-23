#ifndef ICM20948_H
#define ICM20948_H

#include "stm32g4xx_hal.h"
#include <stdint.h>

/* ICM-20948ドライバーの処理結果。 */
typedef enum {
    ICM20948_OK = 0,             /* 正常終了。 */
    ICM20948_NO_DATA,            /* 新しい地磁気データがまだない。 */
    ICM20948_ERROR_ARGUMENT,     /* 引数または初期化状態が不正。 */
    ICM20948_ERROR_NOT_FOUND,    /* I2Cアドレスに応答がない。 */
    ICM20948_ERROR_ID,           /* デバイスIDが一致しない。 */
    ICM20948_ERROR_IO,           /* I2C読み書きに失敗した。 */
    ICM20948_ERROR_OVERFLOW,     /* 地磁気センサーがオーバーフローした。 */
} ICM20948_Status_t;

/** @brief ICM-20948の接続先と初期化状態を保持する。 */
typedef struct {
    I2C_HandleTypeDef *hi2c;          /* 使用するI2Cポート。 */
    uint8_t address;                  /* 検出した7 bit I2Cアドレス。 */
    uint8_t initialized;              /* 加速度・ジャイロ初期化済みなら1。 */
    uint8_t magnetometer_initialized; /* AK09916初期化済みなら1。 */
} ICM20948_t;

/**
 * @brief ICM-20948を検出し、加速度・ジャイロ・AK09916を初期化する。
 * @param device 初期化するドライバー状態。
 * @param hi2c 初期化済みのHAL I2Cハンドル。
 * @return 加速度・ジャイロ開始時は`ICM20948_OK`。地磁気の状態は
 *         `device->magnetometer_initialized`で確認する。
 */
ICM20948_Status_t ICM20948_Init(ICM20948_t *device, I2C_HandleTypeDef *hi2c);

/**
 * @brief 加速度・ジャイロを止めず、AK09916だけを再初期化する。
 * @param device 初期化済みのICM-20948状態。
 * @return 成功時は`ICM20948_OK`、失敗時は識別・I2Cエラー。
 */
ICM20948_Status_t ICM20948_InitMagnetometer(ICM20948_t *device);

/**
 * @brief 連続レジスターから加速度とジャイロをまとめて読み取る。
 * @param device 初期化済みのドライバー状態。
 * @param acceleration_mg X/Y/Z加速度の格納先[mg]。
 * @param gyroscope_deci_dps X/Y/Z角速度の格納先[0.1 deg/s]。
 * @return 成功時は`ICM20948_OK`、失敗時は引数・状態・I2Cエラー。
 */
ICM20948_Status_t ICM20948_ReadMotion(
    ICM20948_t *device,
    int16_t acceleration_mg[3],
    int16_t gyroscope_deci_dps[3]);

/**
 * @brief 内部I2Cマスターが転送したAK09916の最新値を読み取る。
 * @param device 初期化済みのドライバー状態。
 * @param magnetic_deci_ut X/Y/Z地磁気の格納先[0.1 uT]。
 * @return 新値なら`ICM20948_OK`、未更新なら`ICM20948_NO_DATA`、
 *         その他は状態・I2C・オーバーフローエラー。
 */
ICM20948_Status_t ICM20948_ReadMagneticField(
    ICM20948_t *device, int16_t magnetic_deci_ut[3]);

#endif /* ICM20948_H */
