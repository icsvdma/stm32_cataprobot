#include "bmp280.h"
#include <stddef.h>

/* BMP280のI2Cアドレス、主要レジスター、通信待ち時間。 */
#define BMP280_ADDRESS_LOW              0x76U
#define BMP280_ADDRESS_HIGH             0x77U
#define BMP280_CHIP_ID                   0x58U
#define BMP280_REG_CALIBRATION           0x88U
#define BMP280_REG_ID                    0xD0U
#define BMP280_REG_RESET                 0xE0U
#define BMP280_REG_STATUS                0xF3U
#define BMP280_REG_CTRL_MEAS             0xF4U
#define BMP280_REG_CONFIG                0xF5U
#define BMP280_REG_PRESS_MSB             0xF7U
#define BMP280_I2C_TIMEOUT_MS            10U

/**
 * @brief リトルエンディアン2 byteを符号なし16 bit値へ戻す。
 * @param data 下位byteから並んだデータ。
 * @return 変換した値。
 */
static uint16_t read_u16_le(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

/** @brief リトルエンディアン2 byteを符号付き値へ戻す。@param data 入力。@return 変換値。 */
static int16_t read_s16_le(const uint8_t *data)
{
    return (int16_t)read_u16_le(data);
}

/**
 * @brief BMP280の1レジスターへ値を書き込む。
 * @param device 接続情報。
 * @param reg レジスターアドレス。
 * @param value 書き込む値。
 * @return 成功時は`BMP280_OK`。
 */
static BMP280_Status_t write_register(BMP280_t *device, uint8_t reg, uint8_t value)
{
    HAL_StatusTypeDef status = HAL_I2C_Mem_Write(
        device->hi2c, (uint16_t)(device->address << 1), reg,
        I2C_MEMADD_SIZE_8BIT, &value, 1, BMP280_I2C_TIMEOUT_MS);
    return (status == HAL_OK) ? BMP280_OK : BMP280_ERROR_IO;
}

/**
 * @brief BMP280の連続レジスターを読み込む。
 * @param device 接続情報。
 * @param reg 読み始めるレジスター。
 * @param data 読み取り先。
 * @param length 読み取るbyte数。
 * @return 成功時は`BMP280_OK`。
 */
static BMP280_Status_t read_registers(
    BMP280_t *device, uint8_t reg, uint8_t *data, uint16_t length)
{
    HAL_StatusTypeDef status = HAL_I2C_Mem_Read(
        device->hi2c, (uint16_t)(device->address << 1), reg,
        I2C_MEMADD_SIZE_8BIT, data, length, BMP280_I2C_TIMEOUT_MS);
    return (status == HAL_OK) ? BMP280_OK : BMP280_ERROR_IO;
}

/**
 * @brief 候補の2アドレスへ応答確認し、BMP280を探す。
 * @param hi2c 使用するI2Cポート。
 * @return 見つけた7 bitアドレス。見つからない場合は0。
 */
static uint8_t find_device(I2C_HandleTypeDef *hi2c)
{
    static const uint8_t addresses[] = {
        BMP280_ADDRESS_LOW,
        BMP280_ADDRESS_HIGH,
    };

    for (uint32_t i = 0; i < sizeof(addresses); i++) {
        if (HAL_I2C_IsDeviceReady(
                hi2c, (uint16_t)(addresses[i] << 1), 2, BMP280_I2C_TIMEOUT_MS) == HAL_OK) {
            return addresses[i];
        }
    }
    return 0U;
}

/**
 * @brief 24 byteの工場補正データを各メンバーへ展開する。
 * @param device 補正値の格納先。
 * @param data レジスター0x88から読んだデータ。
 * @return なし。
 */
static void load_calibration(BMP280_t *device, const uint8_t *data)
{
    device->dig_t1 = read_u16_le(&data[0]);
    device->dig_t2 = read_s16_le(&data[2]);
    device->dig_t3 = read_s16_le(&data[4]);
    device->dig_p1 = read_u16_le(&data[6]);
    device->dig_p2 = read_s16_le(&data[8]);
    device->dig_p3 = read_s16_le(&data[10]);
    device->dig_p4 = read_s16_le(&data[12]);
    device->dig_p5 = read_s16_le(&data[14]);
    device->dig_p6 = read_s16_le(&data[16]);
    device->dig_p7 = read_s16_le(&data[18]);
    device->dig_p8 = read_s16_le(&data[20]);
    device->dig_p9 = read_s16_le(&data[22]);
}

/**
 * @brief データシートの整数式で温度を補正する。
 * @param device 工場補正値を持つ状態。
 * @param adc_temperature ADCの生温度値。
 * @return 温度[0.01度C]。
 */
static int32_t compensate_temperature(BMP280_t *device, int32_t adc_temperature)
{
    int32_t var1 = ((((adc_temperature >> 3) - ((int32_t)device->dig_t1 << 1))) *
                    (int32_t)device->dig_t2) >> 11;
    int32_t var2 = (((((adc_temperature >> 4) - (int32_t)device->dig_t1) *
                      ((adc_temperature >> 4) - (int32_t)device->dig_t1)) >> 12) *
                    (int32_t)device->dig_t3) >> 14;
    device->t_fine = var1 + var2;
    return (device->t_fine * 5 + 128) >> 8;
}

/**
 * @brief データシートの64 bit整数式で気圧を補正する。
 * @param device 工場補正値と`t_fine`を持つ状態。
 * @param adc_pressure ADCの生気圧値。
 * @return 気圧[Pa]。計算不能なら0。
 */
static uint32_t compensate_pressure(BMP280_t *device, int32_t adc_pressure)
{
    int64_t var1 = (int64_t)device->t_fine - 128000;
    int64_t var2 = var1 * var1 * (int64_t)device->dig_p6;
    var2 += (var1 * (int64_t)device->dig_p5) << 17;
    var2 += (int64_t)device->dig_p4 << 35;
    var1 = ((var1 * var1 * (int64_t)device->dig_p3) >> 8) +
           ((var1 * (int64_t)device->dig_p2) << 12);
    var1 = (((((int64_t)1) << 47) + var1) * (int64_t)device->dig_p1) >> 33;
    if (var1 == 0) {
        return 0U;
    }

    int64_t pressure = 1048576 - adc_pressure;
    pressure = (((pressure << 31) - var2) * 3125) / var1;
    var1 = ((int64_t)device->dig_p9 * (pressure >> 13) * (pressure >> 13)) >> 25;
    var2 = ((int64_t)device->dig_p8 * pressure) >> 19;
    pressure = ((pressure + var1 + var2) >> 8) + ((int64_t)device->dig_p7 << 4);
    return (uint32_t)(pressure >> 8);
}

BMP280_Status_t BMP280_Init(BMP280_t *device, I2C_HandleTypeDef *hi2c)
{
    uint8_t chip_id = 0;
    uint8_t status = 0;
    uint8_t calibration[24];
    uint32_t start_tick;

    if (device == NULL || hi2c == NULL) {
        return BMP280_ERROR_ARGUMENT;
    }

    /* 0x76と0x77のうち、実機で応答したアドレスを保存する。 */
    device->hi2c = hi2c;
    device->address = find_device(hi2c);
    device->initialized = 0U;
    if (device->address == 0U) {
        return BMP280_ERROR_NOT_FOUND;
    }

    if (read_registers(device, BMP280_REG_ID, &chip_id, 1) != BMP280_OK) {
        return BMP280_ERROR_IO;
    }
    if (chip_id != BMP280_CHIP_ID) {
        return BMP280_ERROR_ID;
    }
    /* ソフトリセット後、補正値のコピーが終わるまで最大100 ms待つ。 */
    if (write_register(device, BMP280_REG_RESET, 0xB6U) != BMP280_OK) {
        return BMP280_ERROR_IO;
    }

    start_tick = HAL_GetTick();
    do {
        HAL_Delay(2);
        if (read_registers(device, BMP280_REG_STATUS, &status, 1) != BMP280_OK) {
            return BMP280_ERROR_IO;
        }
    } while ((status & 0x01U) != 0U && (HAL_GetTick() - start_tick) < 100U);
    if ((status & 0x01U) != 0U) {
        return BMP280_ERROR_IO;
    }

    if (read_registers(
            device, BMP280_REG_CALIBRATION, calibration, sizeof(calibration)) != BMP280_OK) {
        return BMP280_ERROR_IO;
    }
    load_calibration(device, calibration);
    if (device->dig_t1 == 0U || device->dig_p1 == 0U) {
        return BMP280_ERROR_CALIBRATION;
    }

    /* 温度・気圧のオーバーサンプリングと通常測定モードを設定する。 */
    if (write_register(device, BMP280_REG_CONFIG, 0x28U) != BMP280_OK ||
        write_register(device, BMP280_REG_CTRL_MEAS, 0x2FU) != BMP280_OK) {
        return BMP280_ERROR_IO;
    }

    device->initialized = 1U;
    return BMP280_OK;
}

BMP280_Status_t BMP280_ReadTemperaturePressure(
    BMP280_t *device, int16_t *temperature_centi_c, uint32_t *pressure_pa)
{
    uint8_t data[6];
    int32_t adc_pressure;
    int32_t adc_temperature;
    int32_t temperature;
    uint32_t pressure;

    if (device == NULL || temperature_centi_c == NULL || pressure_pa == NULL) {
        return BMP280_ERROR_ARGUMENT;
    }
    if (!device->initialized) {
        return BMP280_ERROR_NOT_FOUND;
    }
    if (read_registers(device, BMP280_REG_PRESS_MSB, data, sizeof(data)) != BMP280_OK) {
        return BMP280_ERROR_IO;
    }

    /* 各20 bitの生データを6 byteから組み立てる。 */
    adc_pressure = ((int32_t)data[0] << 12) | ((int32_t)data[1] << 4) | (data[2] >> 4);
    adc_temperature = ((int32_t)data[3] << 12) | ((int32_t)data[4] << 4) | (data[5] >> 4);
    if (adc_pressure == 0x80000 || adc_temperature == 0x80000) {
        return BMP280_ERROR_IO;
    }

    /* 気圧補正は温度補正で更新される`t_fine`を使用するため温度が先。 */
    temperature = compensate_temperature(device, adc_temperature);
    pressure = compensate_pressure(device, adc_pressure);
    if (pressure == 0U) {
        return BMP280_ERROR_CALIBRATION;
    }

    if (temperature > 32767) temperature = 32767;
    if (temperature < -32768) temperature = -32768;
    *temperature_centi_c = (int16_t)temperature;
    *pressure_pa = pressure;
    return BMP280_OK;
}
