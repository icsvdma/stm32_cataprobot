#include "external_sensors.h"
#include "icm20948.h"
#include "bmp280.h"
#include <string.h>

/* 取得周期、再試行間隔、連続失敗回数、データ有効期限。 */
#define MOTION_PERIOD_MS             10U
#define MAGNETIC_PERIOD_MS           10U
#define ENV_PERIOD_MS                100U
#define SENSOR_RETRY_PERIOD_MS       1000U
#define SENSOR_FAILURE_THRESHOLD     3U
#define MOTION_STALE_TIMEOUT_MS      250U
#define MAGNETIC_STALE_TIMEOUT_MS    250U
#define ENV_STALE_TIMEOUT_MS         1000U

/* 外部センサー管理で共有するドライバー状態と最新値。 */
static I2C_HandleTypeDef *s_hi2c;
static ICM20948_t s_motion;
static BMP280_t s_environment;
static ExternalSensorSnapshot_t s_snapshot;
static uint32_t s_last_motion_poll;
static uint32_t s_last_magnetic_poll;
static uint32_t s_last_env_poll;
static uint32_t s_last_motion_success;
static uint32_t s_last_magnetic_success;
static uint32_t s_last_env_success;
static uint32_t s_last_motion_retry;
static uint32_t s_last_magnetic_retry;
static uint32_t s_last_env_retry;
static uint8_t s_motion_failures;
static uint8_t s_magnetic_failures;
static uint8_t s_env_failures;

/* mainの作業値と、割り込みへ公開する完成済み値を分ける。 */
typedef struct {
    ExternalSensorSnapshot_t snapshot;
    uint32_t motion_time;
    uint32_t magnetic_time;
    uint32_t environment_time;
} PublishedSnapshot;
static PublishedSnapshot s_published;

/** 完成した値と取得時刻を短い排他区間で公開する。戻り値なし。 */
static void publish_snapshot(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    s_published.snapshot = s_snapshot;
    s_published.motion_time = s_last_motion_success;
    s_published.magnetic_time = s_last_magnetic_success;
    s_published.environment_time = s_last_env_success;
    __set_PRIMASK(primask);
}

/**
 * @brief 加速度・ジャイロを初期化し、同時に地磁気も初期化する。
 * @param now_ms 再試行時刻として記録する現在時刻[ms]。
 * @return なし。
 */
static void initialize_motion(uint32_t now_ms)
{
    s_last_motion_retry = now_ms;
    s_last_magnetic_retry = now_ms;
    if (ICM20948_Init(&s_motion, s_hi2c) == ICM20948_OK) {
        s_motion_failures = 0U;
        s_snapshot.status &=
            (uint8_t)~(SENSOR_STATUS_ACCEL_ERROR | SENSOR_STATUS_GYRO_ERROR);
        if (s_motion.magnetometer_initialized) {
            s_magnetic_failures = 0U;
            s_snapshot.status &= (uint8_t)~SENSOR_STATUS_MAG_ERROR;
        } else {
            s_snapshot.status &= (uint8_t)~SENSOR_STATUS_MAG_VALID;
            s_snapshot.status |= SENSOR_STATUS_MAG_ERROR;
        }
    } else {
        s_snapshot.status &=
            (uint8_t)~(SENSOR_STATUS_ACCEL_VALID |
                       SENSOR_STATUS_GYRO_VALID |
                       SENSOR_STATUS_MAG_VALID);
        s_snapshot.status |=
            SENSOR_STATUS_ACCEL_ERROR |
            SENSOR_STATUS_GYRO_ERROR |
            SENSOR_STATUS_MAG_ERROR;
    }
}

/**
 * @brief AK09916だけを再初期化する。
 * @param now_ms 再試行時刻として記録する現在時刻[ms]。
 * @return なし。
 */
static void initialize_magnetometer(uint32_t now_ms)
{
    s_last_magnetic_retry = now_ms;
    if (ICM20948_InitMagnetometer(&s_motion) == ICM20948_OK) {
        s_magnetic_failures = 0U;
        s_snapshot.status &= (uint8_t)~SENSOR_STATUS_MAG_ERROR;
    } else {
        s_snapshot.status &= (uint8_t)~SENSOR_STATUS_MAG_VALID;
        s_snapshot.status |= SENSOR_STATUS_MAG_ERROR;
    }
}

/**
 * @brief BMP280を初期化する。
 * @param now_ms 再試行時刻として記録する現在時刻[ms]。
 * @return なし。
 */
static void initialize_environment(uint32_t now_ms)
{
    s_last_env_retry = now_ms;
    if (BMP280_Init(&s_environment, s_hi2c) == BMP280_OK) {
        s_env_failures = 0U;
        s_snapshot.status &= (uint8_t)~SENSOR_STATUS_ENV_ERROR;
    } else {
        s_snapshot.status &= (uint8_t)~SENSOR_STATUS_ENV_VALID;
        s_snapshot.status |= SENSOR_STATUS_ENV_ERROR;
    }
}

uint8_t ExternalSensors_Init(I2C_HandleTypeDef *hi2c)
{
    /* 再起動前の値が残らないよう、すべての管理状態を初期化する。 */
    s_hi2c = hi2c;
    memset(&s_motion, 0, sizeof(s_motion));
    memset(&s_environment, 0, sizeof(s_environment));
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_last_motion_poll = 0U;
    s_last_magnetic_poll = 0U;
    s_last_env_poll = 0U;
    s_last_motion_success = 0U;
    s_last_magnetic_success = 0U;
    s_last_env_success = 0U;
    s_last_motion_retry = 0U;
    s_last_magnetic_retry = 0U;
    s_last_env_retry = 0U;
    s_motion_failures = 0U;
    s_magnetic_failures = 0U;
    s_env_failures = 0U;
    publish_snapshot();

    if (s_hi2c == NULL) {
        s_snapshot.status =
            SENSOR_STATUS_ACCEL_ERROR |
            SENSOR_STATUS_GYRO_ERROR |
            SENSOR_STATUS_MAG_ERROR |
            SENSOR_STATUS_ENV_ERROR;
        publish_snapshot();
        return s_snapshot.status;
    }

    initialize_motion(HAL_GetTick());
    initialize_environment(HAL_GetTick());
    publish_snapshot();
    return s_snapshot.status;
}

uint8_t ExternalSensors_Poll(uint32_t now_ms)
{
    uint8_t updated = 0U;

    if (s_hi2c == NULL) {
        return s_snapshot.status;
    }

    /* 異常中のセンサーだけを1秒間隔で再初期化する。 */
    if (!s_motion.initialized &&
        (now_ms - s_last_motion_retry) >= SENSOR_RETRY_PERIOD_MS) {
        initialize_motion(now_ms);
    }
    if (s_motion.initialized && !s_motion.magnetometer_initialized &&
        (now_ms - s_last_magnetic_retry) >= SENSOR_RETRY_PERIOD_MS) {
        initialize_magnetometer(now_ms);
    }
    if (!s_environment.initialized &&
        (now_ms - s_last_env_retry) >= SENSOR_RETRY_PERIOD_MS) {
        initialize_environment(now_ms);
    }

    now_ms = HAL_GetTick();
    /* 加速度とジャイロは同じ連続レジスターから100 Hzで取得する。 */
    if (s_motion.initialized &&
        (now_ms - s_last_motion_poll) >= MOTION_PERIOD_MS) {
        int16_t acceleration[3];
        int16_t gyroscope[3];
        s_last_motion_poll = now_ms;
        if (ICM20948_ReadMotion(
                &s_motion, acceleration, gyroscope) == ICM20948_OK) {
            s_snapshot.acceleration_x_mg = acceleration[0];
            s_snapshot.acceleration_y_mg = acceleration[1];
            s_snapshot.acceleration_z_mg = acceleration[2];
            s_snapshot.gyroscope_x_deci_dps = gyroscope[0];
            s_snapshot.gyroscope_y_deci_dps = gyroscope[1];
            s_snapshot.gyroscope_z_deci_dps = gyroscope[2];
            s_motion_failures = 0U;
            s_last_motion_success = HAL_GetTick();
            s_snapshot.sample_time_ms = s_last_motion_success;
            s_snapshot.status |=
                SENSOR_STATUS_ACCEL_VALID | SENSOR_STATUS_GYRO_VALID;
            s_snapshot.status &=
                (uint8_t)~(SENSOR_STATUS_ACCEL_ERROR | SENSOR_STATUS_GYRO_ERROR);
            updated = 1U;
        } else if (++s_motion_failures >= SENSOR_FAILURE_THRESHOLD) {
            /* 3回連続で失敗したら一度未初期化へ戻して復旧を試す。 */
            s_motion.initialized = 0U;
            s_motion.magnetometer_initialized = 0U;
            s_snapshot.status &=
                (uint8_t)~(SENSOR_STATUS_ACCEL_VALID |
                           SENSOR_STATUS_GYRO_VALID |
                           SENSOR_STATUS_MAG_VALID);
            s_snapshot.status |=
                SENSOR_STATUS_ACCEL_ERROR |
                SENSOR_STATUS_GYRO_ERROR |
                SENSOR_STATUS_MAG_ERROR;
        }
    }

    /* 地磁気の未更新は正常なので、NO_DATAでは失敗回数を増やさない。 */
    if (s_motion.magnetometer_initialized &&
        (now_ms - s_last_magnetic_poll) >= MAGNETIC_PERIOD_MS) {
        int16_t magnetic[3];
        ICM20948_Status_t magnetic_status;
        s_last_magnetic_poll = now_ms;
        magnetic_status = ICM20948_ReadMagneticField(&s_motion, magnetic);
        if (magnetic_status == ICM20948_OK) {
            s_snapshot.magnetic_x_deci_ut = magnetic[0];
            s_snapshot.magnetic_y_deci_ut = magnetic[1];
            s_snapshot.magnetic_z_deci_ut = magnetic[2];
            s_magnetic_failures = 0U;
            s_last_magnetic_success = HAL_GetTick();
            s_snapshot.sample_time_ms = s_last_magnetic_success;
            s_snapshot.status |= SENSOR_STATUS_MAG_VALID;
            s_snapshot.status &= (uint8_t)~SENSOR_STATUS_MAG_ERROR;
            updated = 1U;
        } else if (magnetic_status != ICM20948_NO_DATA &&
                   ++s_magnetic_failures >= SENSOR_FAILURE_THRESHOLD) {
            s_motion.magnetometer_initialized = 0U;
            s_snapshot.status &= (uint8_t)~SENSOR_STATUS_MAG_VALID;
            s_snapshot.status |= SENSOR_STATUS_MAG_ERROR;
        }
    }

    /* 温度と気圧は変化が緩やかなため10 Hzで取得する。 */
    if (s_environment.initialized &&
        (now_ms - s_last_env_poll) >= ENV_PERIOD_MS) {
        s_last_env_poll = now_ms;
        if (BMP280_ReadTemperaturePressure(
                &s_environment,
                &s_snapshot.temperature_centi_c,
                &s_snapshot.pressure_pa) == BMP280_OK) {
            s_env_failures = 0U;
            s_last_env_success = HAL_GetTick();
            s_snapshot.sample_time_ms = s_last_env_success;
            s_snapshot.status |= SENSOR_STATUS_ENV_VALID;
            s_snapshot.status &= (uint8_t)~SENSOR_STATUS_ENV_ERROR;
            updated = 1U;
        } else if (++s_env_failures >= SENSOR_FAILURE_THRESHOLD) {
            s_environment.initialized = 0U;
            s_snapshot.status &= (uint8_t)~SENSOR_STATUS_ENV_VALID;
            s_snapshot.status |= SENSOR_STATUS_ENV_ERROR;
        }
    }

    now_ms = HAL_GetTick();
    /* 一度成功していても、期限内に更新されなければ有効状態を取り消す。 */
    if ((s_snapshot.status & SENSOR_STATUS_ACCEL_VALID) != 0U &&
        (now_ms - s_last_motion_success) > MOTION_STALE_TIMEOUT_MS) {
        s_snapshot.status &=
            (uint8_t)~(SENSOR_STATUS_ACCEL_VALID | SENSOR_STATUS_GYRO_VALID);
        s_snapshot.status |=
            SENSOR_STATUS_ACCEL_ERROR | SENSOR_STATUS_GYRO_ERROR;
    }
    if ((s_snapshot.status & SENSOR_STATUS_MAG_VALID) != 0U &&
        (now_ms - s_last_magnetic_success) > MAGNETIC_STALE_TIMEOUT_MS) {
        s_snapshot.status &= (uint8_t)~SENSOR_STATUS_MAG_VALID;
        s_snapshot.status |= SENSOR_STATUS_MAG_ERROR;
    }
    if ((s_snapshot.status & SENSOR_STATUS_ENV_VALID) != 0U &&
        (now_ms - s_last_env_success) > ENV_STALE_TIMEOUT_MS) {
        s_snapshot.status &= (uint8_t)~SENSOR_STATUS_ENV_VALID;
        s_snapshot.status |= SENSOR_STATUS_ENV_ERROR;
    }

    /* どれか1種類でも更新したときだけ通し番号と取得時刻を進める。 */
    if (updated) {
        s_snapshot.sequence++;
    }
    publish_snapshot();
    return s_snapshot.status;
}

void ExternalSensors_GetSnapshot(ExternalSensorSnapshot_t *snapshot)
{
    if (snapshot != NULL) {
        uint32_t primask = __get_PRIMASK();
        __disable_irq();
        PublishedSnapshot published = s_published;
        __set_PRIMASK(primask);
        *snapshot = published.snapshot;
        uint32_t now = HAL_GetTick();
        if (now - published.motion_time >= MOTION_STALE_TIMEOUT_MS) {
            if ((snapshot->status & (SENSOR_STATUS_ACCEL_VALID | SENSOR_STATUS_GYRO_VALID)) != 0U) {
                snapshot->status &= (uint8_t)~(SENSOR_STATUS_ACCEL_VALID | SENSOR_STATUS_GYRO_VALID);
                snapshot->status |= SENSOR_STATUS_ACCEL_ERROR | SENSOR_STATUS_GYRO_ERROR;
            }
        }
        if (now - published.magnetic_time >= MAGNETIC_STALE_TIMEOUT_MS &&
            (snapshot->status & SENSOR_STATUS_MAG_VALID) != 0U) {
            snapshot->status &= (uint8_t)~SENSOR_STATUS_MAG_VALID;
            snapshot->status |= SENSOR_STATUS_MAG_ERROR;
        }
        if (now - published.environment_time >= ENV_STALE_TIMEOUT_MS &&
            (snapshot->status & SENSOR_STATUS_ENV_VALID) != 0U) {
            snapshot->status &= (uint8_t)~SENSOR_STATUS_ENV_VALID;
            snapshot->status |= SENSOR_STATUS_ENV_ERROR;
        }
    }
}
