#include "icm20948.h"
#include <limits.h>
#include <stddef.h>

/* ICM-20948本体のアドレスと識別値。 */
#define ICM20948_ADDRESS_LOW               0x68U
#define ICM20948_ADDRESS_HIGH              0x69U
#define ICM20948_WHO_AM_I_VALUE            0xEAU

/* レジスターバンク切り替え。 */
#define ICM20948_REG_BANK_SEL              0x7FU

/* Bank 0: 識別、電源、センサーデータ、内部I2C状態。 */
#define ICM20948_REG_WHO_AM_I              0x00U
#define ICM20948_REG_USER_CTRL             0x03U
#define ICM20948_REG_PWR_MGMT_1            0x06U
#define ICM20948_REG_PWR_MGMT_2            0x07U
#define ICM20948_REG_I2C_MST_STATUS        0x17U
#define ICM20948_REG_ACCEL_XOUT_H          0x2DU
#define ICM20948_REG_EXT_SLV_SENS_DATA_00  0x3BU

#define ICM20948_REG_GYRO_SMPLRT_DIV       0x00U
#define ICM20948_REG_GYRO_CONFIG_1         0x01U
#define ICM20948_REG_ACCEL_SMPLRT_H        0x10U
#define ICM20948_REG_ACCEL_SMPLRT_L        0x11U
#define ICM20948_REG_ACCEL_CONFIG          0x14U

#define ICM20948_REG_I2C_MST_ODR_CONFIG    0x00U
#define ICM20948_REG_I2C_MST_CTRL          0x01U
#define ICM20948_REG_I2C_SLV0_ADDR         0x03U
#define ICM20948_REG_I2C_SLV0_REG          0x04U
#define ICM20948_REG_I2C_SLV0_CTRL         0x05U
#define ICM20948_REG_I2C_SLV4_ADDR         0x13U
#define ICM20948_REG_I2C_SLV4_REG          0x14U
#define ICM20948_REG_I2C_SLV4_CTRL         0x15U
#define ICM20948_REG_I2C_SLV4_DO           0x16U
#define ICM20948_REG_I2C_SLV4_DI           0x17U

#define ICM20948_BANK_0                    0x00U
#define ICM20948_BANK_2                    0x20U
#define ICM20948_BANK_3                    0x30U

#define ICM20948_USER_CTRL_I2C_MST_RST     0x02U
#define ICM20948_USER_CTRL_I2C_MST_EN      0x20U
#define ICM20948_I2C_SLV_ENABLE            0x80U
#define ICM20948_I2C_READ_FLAG             0x80U
#define ICM20948_I2C_SLV4_DONE             0x40U
#define ICM20948_I2C_SLV4_NACK             0x10U

#define AK09916_ADDRESS                    0x0CU
#define AK09916_REG_WIA2                   0x01U
#define AK09916_REG_ST1                    0x10U
#define AK09916_REG_CNTL2                  0x31U
#define AK09916_REG_CNTL3                  0x32U
#define AK09916_WIA2_VALUE                 0x09U
#define AK09916_MODE_CONTINUOUS_100HZ      0x08U
#define AK09916_DATA_LENGTH                9U
#define AK09916_ST1_DATA_READY             0x01U
#define AK09916_ST2_OVERFLOW               0x08U

#define ICM20948_I2C_TIMEOUT_MS            10U
#define ICM20948_AUX_TIMEOUT_MS            20U
#define ICM20948_ACCEL_SENSITIVITY         8192L
#define ICM20948_GYRO_SCALE_NUMERATOR      20L
#define ICM20948_GYRO_SCALE_DENOMINATOR    131L

/**
 * @brief 現在のバンクにある1レジスターへ書き込む。
 * @param device 接続情報。
 * @param reg レジスターアドレス。
 * @param value 書き込む値。
 * @return 成功時は`ICM20948_OK`。
 */
static ICM20948_Status_t write_register(
    ICM20948_t *device, uint8_t reg, uint8_t value)
{
    HAL_StatusTypeDef status = HAL_I2C_Mem_Write(
        device->hi2c, (uint16_t)(device->address << 1), reg,
        I2C_MEMADD_SIZE_8BIT, &value, 1U, ICM20948_I2C_TIMEOUT_MS);
    return (status == HAL_OK) ? ICM20948_OK : ICM20948_ERROR_IO;
}

/**
 * @brief 現在のバンクから連続レジスターを読み込む。
 * @param device 接続情報。
 * @param reg 読み始めるレジスター。
 * @param data 読み取り先。
 * @param length 読み取るbyte数。
 * @return 成功時は`ICM20948_OK`。
 */
static ICM20948_Status_t read_registers(
    ICM20948_t *device, uint8_t reg, uint8_t *data, uint16_t length)
{
    HAL_StatusTypeDef status = HAL_I2C_Mem_Read(
        device->hi2c, (uint16_t)(device->address << 1), reg,
        I2C_MEMADD_SIZE_8BIT, data, length, ICM20948_I2C_TIMEOUT_MS);
    return (status == HAL_OK) ? ICM20948_OK : ICM20948_ERROR_IO;
}

/**
 * @brief ICM-20948のレジスターバンクを切り替える。
 * @param device 接続情報。
 * @param bank Bank 0、2、3の選択値。
 * @return 成功時は`ICM20948_OK`。
 */
static ICM20948_Status_t select_bank(ICM20948_t *device, uint8_t bank)
{
    return write_register(device, ICM20948_REG_BANK_SEL, bank);
}

/**
 * @brief 候補の2アドレスへ応答確認し、ICM-20948を探す。
 * @param hi2c 使用するI2Cポート。
 * @return 見つけた7 bitアドレス。見つからない場合は0。
 */
static uint8_t find_device(I2C_HandleTypeDef *hi2c)
{
    static const uint8_t addresses[] = {
        ICM20948_ADDRESS_LOW,
        ICM20948_ADDRESS_HIGH,
    };

    for (uint32_t i = 0U; i < sizeof(addresses) / sizeof(addresses[0]); i++) {
        if (HAL_I2C_IsDeviceReady(
                hi2c, (uint16_t)(addresses[i] << 1), 2U,
                ICM20948_I2C_TIMEOUT_MS) == HAL_OK) {
            return addresses[i];
        }
    }
    return 0U;
}

/** @brief ビッグエンディアン2 byteを符号付き値へ戻す。@param data 入力。@return 変換値。 */
static int16_t read_s16_be(const uint8_t *data)
{
    return (int16_t)(((uint16_t)data[0] << 8) | data[1]);
}

/** @brief リトルエンディアン2 byteを符号付き値へ戻す。@param data 入力。@return 変換値。 */
static int16_t read_s16_le(const uint8_t *data)
{
    return (int16_t)(((uint16_t)data[1] << 8) | data[0]);
}

/**
 * @brief 32 bit計算値をint16_tの範囲へ収める。
 * @param value 変換前の値。
 * @return -32768から32767に制限した値。
 */
static int16_t saturate_i16(int32_t value)
{
    if (value > INT16_MAX) {
        return INT16_MAX;
    }
    if (value < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)value;
}

/**
 * @brief 内部I2CのSlave 4を使い、AK09916の1レジスターを読み書きする。
 * @param device 初期化済みのICM-20948状態。
 * @param read_operation 0以外なら読み取り、0なら書き込み。
 * @param reg AK09916のレジスターアドレス。
 * @param value 書き込み値、または読み取り結果の格納先。
 * @return 完了時は`ICM20948_OK`、失敗時はI2Cエラー。
 */
static ICM20948_Status_t auxiliary_transfer(
    ICM20948_t *device, uint8_t read_operation, uint8_t reg, uint8_t *value)
{
    uint8_t status = 0U;
    uint32_t start_tick;

    if (value == NULL) {
        return ICM20948_ERROR_ARGUMENT;
    }
    if (select_bank(device, ICM20948_BANK_3) != ICM20948_OK) {
        return ICM20948_ERROR_IO;
    }
    if (!read_operation &&
        write_register(device, ICM20948_REG_I2C_SLV4_DO, *value) != ICM20948_OK) {
        return ICM20948_ERROR_IO;
    }
    if (write_register(
            device, ICM20948_REG_I2C_SLV4_ADDR,
            (uint8_t)(AK09916_ADDRESS |
                      (read_operation ? ICM20948_I2C_READ_FLAG : 0U))) != ICM20948_OK ||
        write_register(device, ICM20948_REG_I2C_SLV4_REG, reg) != ICM20948_OK ||
        write_register(
            device, ICM20948_REG_I2C_SLV4_CTRL,
            ICM20948_I2C_SLV_ENABLE) != ICM20948_OK ||
        select_bank(device, ICM20948_BANK_0) != ICM20948_OK) {
        return ICM20948_ERROR_IO;
    }

    /* Slave 4の完了フラグを最大20 ms待つ。 */
    start_tick = HAL_GetTick();
    do {
        if (read_registers(
                device, ICM20948_REG_I2C_MST_STATUS, &status, 1U) != ICM20948_OK) {
            return ICM20948_ERROR_IO;
        }
        if ((status & ICM20948_I2C_SLV4_NACK) != 0U) {
            return ICM20948_ERROR_IO;
        }
        if ((status & ICM20948_I2C_SLV4_DONE) != 0U) {
            break;
        }
        HAL_Delay(1U);
    } while ((HAL_GetTick() - start_tick) < ICM20948_AUX_TIMEOUT_MS);

    if ((status & ICM20948_I2C_SLV4_DONE) == 0U) {
        return ICM20948_ERROR_IO;
    }
    if (read_operation) {
        if (select_bank(device, ICM20948_BANK_3) != ICM20948_OK ||
            read_registers(
                device, ICM20948_REG_I2C_SLV4_DI, value, 1U) != ICM20948_OK ||
            select_bank(device, ICM20948_BANK_0) != ICM20948_OK) {
            return ICM20948_ERROR_IO;
        }
    }
    return ICM20948_OK;
}

ICM20948_Status_t ICM20948_InitMagnetometer(ICM20948_t *device)
{
    uint8_t value;

    if (device == NULL || !device->initialized) {
        return ICM20948_ERROR_ARGUMENT;
    }
    device->magnetometer_initialized = 0U;

    if (select_bank(device, ICM20948_BANK_3) != ICM20948_OK ||
        write_register(device, ICM20948_REG_I2C_SLV0_CTRL, 0U) != ICM20948_OK ||
        select_bank(device, ICM20948_BANK_0) != ICM20948_OK) {
        return ICM20948_ERROR_IO;
    }

    /* AK09916をリセットしてから識別値を確認する。 */
    value = 0x01U;
    if (auxiliary_transfer(
            device, 0U, AK09916_REG_CNTL3, &value) != ICM20948_OK) {
        return ICM20948_ERROR_IO;
    }
    HAL_Delay(100U);

    value = 0U;
    if (auxiliary_transfer(
            device, 1U, AK09916_REG_WIA2, &value) != ICM20948_OK) {
        return ICM20948_ERROR_IO;
    }
    if (value != AK09916_WIA2_VALUE) {
        return ICM20948_ERROR_ID;
    }

    /*
     * 100 Hz連続測定を開始し、Slave 0でST1からST2までの9 byteを
     * ICM-20948本体へ自動転送する。
     */
    value = AK09916_MODE_CONTINUOUS_100HZ;
    if (auxiliary_transfer(
            device, 0U, AK09916_REG_CNTL2, &value) != ICM20948_OK ||
        select_bank(device, ICM20948_BANK_3) != ICM20948_OK ||
        write_register(
            device, ICM20948_REG_I2C_SLV0_ADDR,
            AK09916_ADDRESS | ICM20948_I2C_READ_FLAG) != ICM20948_OK ||
        write_register(
            device, ICM20948_REG_I2C_SLV0_REG,
            AK09916_REG_ST1) != ICM20948_OK ||
        write_register(
            device, ICM20948_REG_I2C_SLV0_CTRL,
            ICM20948_I2C_SLV_ENABLE | AK09916_DATA_LENGTH) != ICM20948_OK ||
        select_bank(device, ICM20948_BANK_0) != ICM20948_OK) {
        return ICM20948_ERROR_IO;
    }

    HAL_Delay(20U);
    device->magnetometer_initialized = 1U;
    return ICM20948_OK;
}

ICM20948_Status_t ICM20948_Init(ICM20948_t *device, I2C_HandleTypeDef *hi2c)
{
    uint8_t who_am_i = 0U;

    if (device == NULL || hi2c == NULL) {
        return ICM20948_ERROR_ARGUMENT;
    }

    device->hi2c = hi2c;
    device->address = find_device(hi2c);
    device->initialized = 0U;
    device->magnetometer_initialized = 0U;
    if (device->address == 0U) {
        return ICM20948_ERROR_NOT_FOUND;
    }

    if (select_bank(device, ICM20948_BANK_0) != ICM20948_OK ||
        read_registers(
            device, ICM20948_REG_WHO_AM_I, &who_am_i, 1U) != ICM20948_OK) {
        return ICM20948_ERROR_IO;
    }
    if (who_am_i != ICM20948_WHO_AM_I_VALUE) {
        return ICM20948_ERROR_ID;
    }

    /* 本体をソフトリセットし、内部クロックが安定するまで待つ。 */
    if (write_register(device, ICM20948_REG_PWR_MGMT_1, 0x80U) != ICM20948_OK) {
        return ICM20948_ERROR_IO;
    }
    HAL_Delay(100U);

    /*
     * 全軸を有効化し、加速度±4 g、ジャイロ±500 deg/s、
     * DLPF有効、約100 Hzへ設定する。
     */
    if (select_bank(device, ICM20948_BANK_0) != ICM20948_OK ||
        write_register(device, ICM20948_REG_PWR_MGMT_1, 0x01U) != ICM20948_OK ||
        write_register(device, ICM20948_REG_PWR_MGMT_2, 0x00U) != ICM20948_OK ||
        select_bank(device, ICM20948_BANK_2) != ICM20948_OK ||
        write_register(
            device, ICM20948_REG_GYRO_SMPLRT_DIV, 0x0AU) != ICM20948_OK ||
        write_register(
            device, ICM20948_REG_GYRO_CONFIG_1, 0x13U) != ICM20948_OK ||
        write_register(
            device, ICM20948_REG_ACCEL_SMPLRT_H, 0x00U) != ICM20948_OK ||
        write_register(
            device, ICM20948_REG_ACCEL_SMPLRT_L, 0x0AU) != ICM20948_OK ||
        write_register(
            device, ICM20948_REG_ACCEL_CONFIG, 0x1BU) != ICM20948_OK ||
        select_bank(device, ICM20948_BANK_0) != ICM20948_OK ||
        write_register(
            device, ICM20948_REG_USER_CTRL,
            ICM20948_USER_CTRL_I2C_MST_RST) != ICM20948_OK) {
        return ICM20948_ERROR_IO;
    }
    HAL_Delay(1U);

    /* AK09916を操作する内部I2Cマスターを有効化する。 */
    if (write_register(
            device, ICM20948_REG_USER_CTRL,
            ICM20948_USER_CTRL_I2C_MST_EN) != ICM20948_OK ||
        select_bank(device, ICM20948_BANK_3) != ICM20948_OK ||
        write_register(
            device, ICM20948_REG_I2C_MST_CTRL, 0x07U) != ICM20948_OK ||
        write_register(
            device, ICM20948_REG_I2C_MST_ODR_CONFIG, 0x03U) != ICM20948_OK ||
        select_bank(device, ICM20948_BANK_0) != ICM20948_OK) {
        return ICM20948_ERROR_IO;
    }

    device->initialized = 1U;
    (void)ICM20948_InitMagnetometer(device);
    return ICM20948_OK;
}

ICM20948_Status_t ICM20948_ReadMotion(
    ICM20948_t *device,
    int16_t acceleration_mg[3],
    int16_t gyroscope_deci_dps[3])
{
    uint8_t data[12];

    if (device == NULL || acceleration_mg == NULL ||
        gyroscope_deci_dps == NULL) {
        return ICM20948_ERROR_ARGUMENT;
    }
    if (!device->initialized) {
        return ICM20948_ERROR_NOT_FOUND;
    }
    if (select_bank(device, ICM20948_BANK_0) != ICM20948_OK ||
        read_registers(
            device, ICM20948_REG_ACCEL_XOUT_H, data, sizeof(data)) != ICM20948_OK) {
        return ICM20948_ERROR_IO;
    }

    /* X、Y、Zを設定レンジに対応する物理単位へ換算する。 */
    for (uint8_t axis = 0U; axis < 3U; axis++) {
        int16_t accel_raw = read_s16_be(&data[axis * 2U]);
        int16_t gyro_raw = read_s16_be(&data[6U + axis * 2U]);
        acceleration_mg[axis] = (int16_t)(
            ((int32_t)accel_raw * 1000L) / ICM20948_ACCEL_SENSITIVITY);
        gyroscope_deci_dps[axis] = (int16_t)(
            ((int32_t)gyro_raw * ICM20948_GYRO_SCALE_NUMERATOR) /
            ICM20948_GYRO_SCALE_DENOMINATOR);
    }
    return ICM20948_OK;
}

ICM20948_Status_t ICM20948_ReadMagneticField(
    ICM20948_t *device, int16_t magnetic_deci_ut[3])
{
    uint8_t data[AK09916_DATA_LENGTH];

    if (device == NULL || magnetic_deci_ut == NULL) {
        return ICM20948_ERROR_ARGUMENT;
    }
    if (!device->initialized || !device->magnetometer_initialized) {
        return ICM20948_ERROR_NOT_FOUND;
    }
    if (select_bank(device, ICM20948_BANK_0) != ICM20948_OK ||
        read_registers(
            device, ICM20948_REG_EXT_SLV_SENS_DATA_00,
            data, sizeof(data)) != ICM20948_OK) {
        return ICM20948_ERROR_IO;
    }
    /* ST1で新値、ST2でオーバーフローを確認してから使用する。 */
    if ((data[0] & AK09916_ST1_DATA_READY) == 0U) {
        return ICM20948_NO_DATA;
    }
    if ((data[8] & AK09916_ST2_OVERFLOW) != 0U) {
        return ICM20948_ERROR_OVERFLOW;
    }

    /* AK09916は1 LSB=0.15 uTなので、0.1 uT単位へ3/2倍する。 */
    for (uint8_t axis = 0U; axis < 3U; axis++) {
        int16_t raw = read_s16_le(&data[1U + axis * 2U]);
        magnetic_deci_ut[axis] = saturate_i16(((int32_t)raw * 3L) / 2L);
    }
    return ICM20948_OK;
}
/* Bank 2: 加速度・ジャイロのサンプル周期とレンジ。 */
/* Bank 3: AK09916へ接続する内部I2Cマスター。 */
/* 内部I2Cマスター制御ビット。 */
/* ICM-20948内蔵の地磁気センサーAK09916。 */
/* 通信待ち時間と物理単位への換算係数。 */
