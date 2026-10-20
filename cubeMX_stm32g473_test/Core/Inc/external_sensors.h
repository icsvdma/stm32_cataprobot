#ifndef EXTERNAL_SENSORS_H
#define EXTERNAL_SENSORS_H

#include "stm32g4xx_hal.h"
#include <stdint.h>

/* ESP32へ渡すセンサー状態のビット割り当て。 */
#define SENSOR_STATUS_ACCEL_VALID    0x01U
#define SENSOR_STATUS_ENV_VALID      0x02U
#define SENSOR_STATUS_ACCEL_ERROR    0x04U
#define SENSOR_STATUS_ENV_ERROR      0x08U
#define SENSOR_STATUS_GYRO_VALID     0x10U
#define SENSOR_STATUS_GYRO_ERROR     0x20U
#define SENSOR_STATUS_MAG_VALID      0x40U
#define SENSOR_STATUS_MAG_ERROR      0x80U

/**
 * @brief ESP32へ送る最新センサー値一式。
 */
typedef struct {
    uint8_t status;                     /* 有効・異常状態のビット列。 */
    uint16_t sequence;                  /* 値を更新するたびに増える番号。 */
    uint32_t sample_time_ms;            /* 最終更新時刻[ms]。 */
    int16_t acceleration_x_mg;          /* X軸加速度[mg]。 */
    int16_t acceleration_y_mg;          /* Y軸加速度[mg]。 */
    int16_t acceleration_z_mg;          /* Z軸加速度[mg]。 */
    int16_t gyroscope_x_deci_dps;       /* X軸角速度[0.1 deg/s]。 */
    int16_t gyroscope_y_deci_dps;       /* Y軸角速度[0.1 deg/s]。 */
    int16_t gyroscope_z_deci_dps;       /* Z軸角速度[0.1 deg/s]。 */
    int16_t magnetic_x_deci_ut;         /* X軸地磁気[0.1 uT]。 */
    int16_t magnetic_y_deci_ut;         /* Y軸地磁気[0.1 uT]。 */
    int16_t magnetic_z_deci_ut;         /* Z軸地磁気[0.1 uT]。 */
    int16_t temperature_centi_c;        /* 温度[0.01度C]。 */
    uint32_t pressure_pa;               /* 気圧[Pa]。 */
} ExternalSensorSnapshot_t;

/**
 * @brief ICM-20948とBMP280を検出し、センサー管理を初期化する。
 * @param hi2c 両センサーで共有する初期化済みI2Cハンドル。
 * @return 初期化後の`SENSOR_STATUS_*`ビット列。
 */
uint8_t ExternalSensors_Init(I2C_HandleTypeDef *hi2c);

/**
 * @brief 指定周期で各値を取得し、異常時は間隔を空けて再初期化する。
 * @param now_ms 現在のHAL起動後時刻[ms]。
 * @return 更新後の`SENSOR_STATUS_*`ビット列。
 */
uint8_t ExternalSensors_Poll(uint32_t now_ms);

/**
 * @brief 完成した値と状態をコピーし、群別の有効期限を現在時刻で検証する。
 * @param snapshot コピー先。`NULL`の場合は何もしない。
 * @return なし。
 */
void ExternalSensors_GetSnapshot(ExternalSensorSnapshot_t *snapshot);

#endif /* EXTERNAL_SENSORS_H */
