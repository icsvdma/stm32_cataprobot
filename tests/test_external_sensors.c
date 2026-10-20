#include "external_sensors.h"
#include "icm20948.h"
#include "bmp280.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(value) do { if (!(value)) { fprintf(stderr, "line %d: %s\n", __LINE__, #value); exit(1); } } while (0)
static uint32_t now_ms, irq_mask;
static int16_t sample_value;
static int inspect_during_read;
static ExternalSensorSnapshot_t observed;
uint32_t HAL_GetTick(void) { return now_ms; }
uint32_t __get_PRIMASK(void) { return irq_mask; }
void __disable_irq(void) { irq_mask = 1U; }
void __set_PRIMASK(uint32_t value) { irq_mask = value; }
ICM20948_Status_t ICM20948_Init(ICM20948_t *device, I2C_HandleTypeDef *i2c)
{ device->hi2c = i2c; device->initialized = 1U; device->magnetometer_initialized = 1U; return ICM20948_OK; }
ICM20948_Status_t ICM20948_InitMagnetometer(ICM20948_t *device)
{ device->magnetometer_initialized = 1U; return ICM20948_OK; }
ICM20948_Status_t ICM20948_ReadMotion(ICM20948_t *device, int16_t acceleration[3], int16_t gyroscope[3])
{ (void)device; for (unsigned i = 0U; i < 3U; ++i) { acceleration[i] = sample_value; gyroscope[i] = sample_value; } return ICM20948_OK; }
ICM20948_Status_t ICM20948_ReadMagneticField(ICM20948_t *device, int16_t magnetic[3])
{ (void)device; for (unsigned i = 0U; i < 3U; ++i) { magnetic[i] = sample_value; } return ICM20948_OK; }
BMP280_Status_t BMP280_Init(BMP280_t *device, I2C_HandleTypeDef *i2c)
{ device->hi2c = i2c; device->initialized = 1U; return BMP280_OK; }
BMP280_Status_t BMP280_ReadTemperaturePressure(BMP280_t *device, int16_t *temperature, uint32_t *pressure)
{
    (void)device;
    if (inspect_during_read) { ExternalSensors_GetSnapshot(&observed); }
    *temperature = sample_value; *pressure = (uint32_t)sample_value;
    return BMP280_OK;
}
/** 実I2Cドライバーだけを置換し、公開スナップショットの整合性と期限を検証する。 */
int main(void)
{
    I2C_HandleTypeDef i2c = {0};
    ExternalSensorSnapshot_t snapshot;
    ExternalSensors_Init(&i2c);
    now_ms = 100U; sample_value = 100;
    ExternalSensors_Poll(now_ms);
    ExternalSensors_GetSnapshot(&snapshot);
    CHECK(snapshot.acceleration_x_mg == 100 && snapshot.temperature_centi_c == 100);
    now_ms = 200U; sample_value = 200; inspect_during_read = 1;
    ExternalSensors_Poll(now_ms);
    CHECK(observed.acceleration_x_mg == 100 && observed.temperature_centi_c == 100);
    ExternalSensors_GetSnapshot(&snapshot);
    CHECK(snapshot.acceleration_x_mg == 200 && snapshot.temperature_centi_c == 200);
    now_ms = 450U;
    ExternalSensors_GetSnapshot(&snapshot);
    CHECK((snapshot.status & (SENSOR_STATUS_ACCEL_VALID | SENSOR_STATUS_GYRO_VALID | SENSOR_STATUS_MAG_VALID)) == 0U);
    CHECK((snapshot.status & SENSOR_STATUS_ENV_VALID) != 0U);
    now_ms = 1200U;
    ExternalSensors_GetSnapshot(&snapshot);
    CHECK((snapshot.status & SENSOR_STATUS_ENV_VALID) == 0U);
    CHECK(irq_mask == 0U);
    puts("external sensors: passed");
    return 0;
}
