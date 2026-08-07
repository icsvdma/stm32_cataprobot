#include "spi_comm.h"
#include "config.h"
#include "debug_led.h"
#include "led_control.h"
#include "motor_hbridge.h"
#include "external_sensors.h"
#include <string.h>

/* NACKでESP32へ通知するエラー理由。 */
#define SPI_ERROR_UNKNOWN_COMMAND   0x01U
#define SPI_ERROR_INVALID_PARAMETER 0x02U

/* 割り込みとメインループで共有するSPI受信・応答状態。 */
static SPI_HandleTypeDef *s_hspi;
static uint8_t s_rx_buf[SPI_RX_BUF_SIZE];
static volatile uint8_t s_rx_idx = 0U;

static uint8_t s_tx_buf[PACKET_MAX_SIZE + 1U];
static volatile uint8_t s_tx_len = 0U;
static volatile uint8_t s_tx_idx = 0U;
static volatile uint32_t s_last_rx_tick = 0U;
static volatile uint8_t s_frame_parsed = 0U;

/* 各コマンドハンドラーの内部処理結果。 */
typedef enum {
    CMD_HANDLER_OK = 0,
    CMD_HANDLER_INVALID_PARAMETER,
} CmdHandlerResult_t;

/**
 * @brief 符号付き16 bit値をビッグエンディアンで書き込む。
 * @param destination 2 byteの書き込み先。
 * @param value 書き込む値。
 * @return なし。
 */
static void write_s16_be(uint8_t *destination, int16_t value)
{
    destination[0] = (uint8_t)((uint16_t)value >> 8);
    destination[1] = (uint8_t)value;
}

/**
 * @brief 送信可能な間、準備済み応答をSPIデータレジスターへ入れる。
 * @return なし。
 */
static void preload_tx(void)
{
    SPI_TypeDef *spi = s_hspi->Instance;
    while (s_tx_idx < s_tx_len && (spi->SR & SPI_SR_TXE)) {
        *(volatile uint8_t *)&spi->DR = s_tx_buf[s_tx_idx++];
    }
}

/**
 * @brief ターンアラウンド用1 byteの後ろへ応答パケットを準備する。
 * @param packet ESP32のパディングクロック中に返す応答。
 * @return なし。
 */
static void prepare_response(const Packet_t *packet)
{
    /* 最初の送信byteが失われても、次から0xAA付きパケットが始まる。 */
    s_tx_buf[0] = 0x00U;
    s_tx_len = (uint8_t)(1U + Packet_Serialize(packet, &s_tx_buf[1]));
    s_tx_idx = 0U;
    preload_tx();
}

/** @brief 元コマンドID入りACKを準備する。@param original_cmd 元ID。@return なし。 */
static void prepare_ack(uint8_t original_cmd)
{
    Packet_t ack;
    Packet_Build(&ack, CMD_ACK, &original_cmd, 1U);
    prepare_response(&ack);
}

/**
 * @brief 元コマンドIDと理由を含むNACKを準備する。
 * @param original_cmd 元コマンドID。
 * @param error_code エラー理由。
 * @return なし。
 */
static void prepare_nack(uint8_t original_cmd, uint8_t error_code)
{
    Packet_t nack;
    uint8_t payload[2] = {original_cmd, error_code};
    Packet_Build(&nack, CMD_NACK, payload, sizeof(payload));
    prepare_response(&nack);
}

/** @brief シャットダウン完了応答を準備する。@return なし。 */
static void prepare_shutdown_ack(void)
{
    Packet_t ack;
    Packet_Build(&ack, CMD_SHUTDOWN_ACK, NULL, 0U);
    prepare_response(&ack);
}

/** @brief 最新センサー値をVersion 2の32 byte応答へ変換する。@return なし。 */
static void prepare_sensor_response(void)
{
    ExternalSensorSnapshot_t snapshot;
    Packet_t response;
    uint8_t payload[32];

    /* 複数byte値はESP32仕様に合わせてビッグエンディアンで並べる。 */
    ExternalSensors_GetSnapshot(&snapshot);
    payload[0] = 0x02U;
    payload[1] = snapshot.status;
    payload[2] = (uint8_t)(snapshot.sequence >> 8);
    payload[3] = (uint8_t)snapshot.sequence;
    payload[4] = (uint8_t)(snapshot.sample_time_ms >> 24);
    payload[5] = (uint8_t)(snapshot.sample_time_ms >> 16);
    payload[6] = (uint8_t)(snapshot.sample_time_ms >> 8);
    payload[7] = (uint8_t)snapshot.sample_time_ms;
    write_s16_be(&payload[8], snapshot.acceleration_x_mg);
    write_s16_be(&payload[10], snapshot.acceleration_y_mg);
    write_s16_be(&payload[12], snapshot.acceleration_z_mg);
    write_s16_be(&payload[14], snapshot.gyroscope_x_deci_dps);
    write_s16_be(&payload[16], snapshot.gyroscope_y_deci_dps);
    write_s16_be(&payload[18], snapshot.gyroscope_z_deci_dps);
    write_s16_be(&payload[20], snapshot.magnetic_x_deci_ut);
    write_s16_be(&payload[22], snapshot.magnetic_y_deci_ut);
    write_s16_be(&payload[24], snapshot.magnetic_z_deci_ut);
    write_s16_be(&payload[26], snapshot.temperature_centi_c);
    payload[28] = (uint8_t)(snapshot.pressure_pa >> 24);
    payload[29] = (uint8_t)(snapshot.pressure_pa >> 16);
    payload[30] = (uint8_t)(snapshot.pressure_pa >> 8);
    payload[31] = (uint8_t)snapshot.pressure_pa;

    Packet_Build(&response, CMD_SENSOR_RESP, payload, sizeof(payload));
    prepare_response(&response);
}

/**
 * @brief デバッグLEDコマンドを検証して実行する。
 * @param pkt 受信パケット。
 * @return 正常またはパラメーターエラー。
 */
static CmdHandlerResult_t process_debug_led(const Packet_t *pkt)
{
    if (pkt == NULL || (pkt->length != 1U && pkt->length != 2U)) {
        return CMD_HANDLER_INVALID_PARAMETER;
    }

    uint8_t led_mask = pkt->payload[0] & 0x3FU;
    DebugLedMode_t mode = (pkt->length == 2U)
                              ? (DebugLedMode_t)pkt->payload[1]
                              : DBG_LED_ON;
    if (mode > DBG_LED_BLINK_4HZ) {
        return CMD_HANDLER_INVALID_PARAMETER;
    }

    DebugLed_Set(led_mask, mode);
    return CMD_HANDLER_OK;
}

/* UARTのコマンドテーブルからも呼べる公開入口。 */
void handle_debug_led(const Packet_t *pkt)
{
    (void)process_debug_led(pkt);
}

/**
 * @brief ライト動作モードコマンドを処理する。
 * @param pkt 受信パケット。
 * @return 正常またはパラメーターエラー。
 */
static CmdHandlerResult_t handle_led_mode(const Packet_t *pkt)
{
    if (pkt == NULL || pkt->length != 1U || pkt->payload[0] > LM_BATTERY) {
        return CMD_HANDLER_INVALID_PARAMETER;
    }
    LED_SetMode((LedMode_t)pkt->payload[0]);
    return CMD_HANDLER_OK;
}

/**
 * @brief 左右ヘッドライト光量コマンドを処理する。
 * @param pkt 受信パケット。
 * @return 正常またはパラメーターエラー。
 */
static CmdHandlerResult_t handle_led_brightness(const Packet_t *pkt)
{
    if (pkt == NULL || pkt->length != 2U || pkt->payload[1] > 100U) {
        return CMD_HANDLER_INVALID_PARAMETER;
    }

    LedSide_t side;
    if (pkt->payload[0] == 0x00U) {
        side = LED_SIDE_RIGHT;
    } else if (pkt->payload[0] == 0x01U) {
        side = LED_SIDE_LEFT;
    } else if (pkt->payload[0] == 0xFFU) {
        side = LED_SIDE_BOTH;
    } else {
        return CMD_HANDLER_INVALID_PARAMETER;
    }

    LED_SetBrightness(side, pkt->payload[1]);
    return CMD_HANDLER_OK;
}

/**
 * @brief モーター方向・速度コマンドをHブリッジ制御へ渡す。
 * @param pkt 受信パケット。
 * @return 正常またはパラメーターエラー。
 */
static CmdHandlerResult_t handle_motor_speed(const Packet_t *pkt)
{
    if (pkt == NULL || pkt->length != 4U) {
        return CMD_HANDLER_INVALID_PARAMETER;
    }

    uint8_t ch_id = pkt->payload[0];
    uint8_t dir_id = pkt->payload[1];
    uint16_t speed = ((uint16_t)pkt->payload[2] << 8) | pkt->payload[3];
    if (dir_id > HBRIDGE_DIR_BRAKE || speed > 1000U) {
        return CMD_HANDLER_INVALID_PARAMETER;
    }

    HBridge_Direction_t dir = (HBridge_Direction_t)dir_id;
    if (ch_id == 0xFFU) {
        HBridge_SetMotor(HBRIDGE_CH_A, dir, speed);
        HBridge_SetMotor(HBRIDGE_CH_B, dir, speed);
    } else if (ch_id <= HBRIDGE_CH_B) {
        HBridge_SetMotor((HBridge_Channel_t)ch_id, dir, speed);
    } else {
        return CMD_HANDLER_INVALID_PARAMETER;
    }
    return CMD_HANDLER_OK;
}

/**
 * @brief 指定モーターの通常停止コマンドを処理する。
 * @param pkt 受信パケット。
 * @return 正常またはパラメーターエラー。
 */
static CmdHandlerResult_t handle_motor_stop(const Packet_t *pkt)
{
    if (pkt == NULL || pkt->length != 1U) {
        return CMD_HANDLER_INVALID_PARAMETER;
    }

    uint8_t ch_id = pkt->payload[0];
    if (ch_id == 0xFFU) {
        HBridge_Stop(HBRIDGE_CH_A);
        HBridge_Stop(HBRIDGE_CH_B);
    } else if (ch_id <= HBRIDGE_CH_B) {
        HBridge_Stop((HBridge_Channel_t)ch_id);
    } else {
        return CMD_HANDLER_INVALID_PARAMETER;
    }
    return CMD_HANDLER_OK;
}

/**
 * @brief 左右モーターの緊急停止コマンドを処理する。
 * @param pkt 受信パケット。
 * @return 正常またはパラメーターエラー。
 */
static CmdHandlerResult_t handle_emergency_stop(const Packet_t *pkt)
{
    if (pkt == NULL || pkt->length != 0U) {
        return CMD_HANDLER_INVALID_PARAMETER;
    }
    HBridge_EmergencyStop();
    return CMD_HANDLER_OK;
}

void SPI_Slave_Init(SPI_HandleTypeDef *hspi)
{
    s_hspi = hspi;
    s_rx_idx = 0U;
    memset(s_rx_buf, 0, sizeof(s_rx_buf));
    s_tx_len = 0U;
    s_tx_idx = 0U;

    /*
     * I2Cのブロッキング読み取り中でもSPIを取りこぼさないよう、
     * SPI1割り込みで1 byteずつ処理する。
     */
    s_hspi->Instance->CR2 &= ~SPI_CR2_TXEIE;
    s_hspi->Instance->CR2 |= SPI_CR2_RXNEIE | SPI_CR2_ERRIE;
    HAL_NVIC_DisableIRQ(DMA1_Channel2_IRQn);
    HAL_NVIC_DisableIRQ(EXTI4_IRQn);

    __HAL_SPI_ENABLE(s_hspi);
    HAL_NVIC_ClearPendingIRQ(SPI1_IRQn);
    HAL_NVIC_EnableIRQ(SPI1_IRQn);
}

void SPI_Slave_StartReceive(void)
{
    /* 受信はSPI_Slave_IRQHandlerが常時処理するため何もしない。 */
}

typedef CmdHandlerResult_t (*CmdHandler_t)(const Packet_t *pkt);

static const struct {
    uint8_t cmd;
    CmdHandler_t handler;
} spi_cmd_table[] = {
    {CMD_MOTOR_SPEED, handle_motor_speed},
    {CMD_MOTOR_STOP, handle_motor_stop},
    {CMD_EMERGENCY_STOP, handle_emergency_stop},
    {CMD_LED_MODE, handle_led_mode},
    {CMD_LED_BRIGHTNESS, handle_led_brightness},
    {CMD_DEBUG_LED, process_debug_led},
};

#define SPI_CMD_TABLE_SIZE (sizeof(spi_cmd_table) / sizeof(spi_cmd_table[0]))

void SPI_Slave_Dispatch(const uint8_t *buf, uint8_t len)
{
    Packet_t pkt;
    uint8_t original_cmd = (buf != NULL && len >= 2U) ? buf[1] : 0U;
    if (buf == NULL || !Packet_Parse(buf, len, &pkt)) {
        prepare_nack(original_cmd, SPI_ERROR_INVALID_PARAMETER);
        return;
    }

    /* センサー要求とシャットダウンは専用応答があるため個別処理する。 */
    if (pkt.cmd == CMD_SENSOR_REQ) {
        if (pkt.length != 1U || pkt.payload[0] != 0xFFU) {
            prepare_nack(pkt.cmd, SPI_ERROR_INVALID_PARAMETER);
            return;
        }
        prepare_sensor_response();
        return;
    }

    if (pkt.cmd == CMD_SHUTDOWN) {
        if (pkt.length != 0U) {
            prepare_nack(pkt.cmd, SPI_ERROR_INVALID_PARAMETER);
            return;
        }
        HBridge_EmergencyStop();
        LED_AllOff();
        DebugLed_AllOff();
        prepare_shutdown_ack();
        return;
    }

    /* 通常コマンドはテーブルから処理関数を探す。 */
    for (uint16_t i = 0U; i < SPI_CMD_TABLE_SIZE; i++) {
        if (spi_cmd_table[i].cmd == pkt.cmd) {
            CmdHandlerResult_t result = spi_cmd_table[i].handler(&pkt);
            if (result == CMD_HANDLER_OK) {
                prepare_ack(pkt.cmd);
            } else {
                prepare_nack(pkt.cmd, SPI_ERROR_INVALID_PARAMETER);
            }
            return;
        }
    }

    prepare_nack(pkt.cmd, SPI_ERROR_UNKNOWN_COMMAND);
}

HAL_StatusTypeDef SPI_Slave_SendResponse(const Packet_t *p)
{
    uint8_t buf[PACKET_MAX_SIZE];
    uint8_t len = Packet_Serialize(p, buf);
    return HAL_SPI_Transmit(s_hspi, buf, len, 100U);
}

void SPI_Slave_RxCompleteCallback(void)
{
    /* 旧DMA方式との互換用。現在は割り込みハンドラーが受信する。 */
}

void SPI_Slave_NssRisingCallback(void)
{
    /* CS解除はSPI_Slave_PollがGPIOと経過時間から判定する。 */
}

void SPI_Slave_IRQHandler(void)
{
    if (s_hspi == NULL) {
        return;
    }

    SPI_TypeDef *spi = s_hspi->Instance;

    /* RXNEが立っている間、送信を補充しながら受信byteを保存する。 */
    while (spi->SR & SPI_SR_RXNE) {
        if (s_tx_idx < s_tx_len && (spi->SR & SPI_SR_TXE)) {
            *(volatile uint8_t *)&spi->DR = s_tx_buf[s_tx_idx++];
        }

        uint8_t byte = *(volatile uint8_t *)&spi->DR;
        if (s_rx_idx < SPI_RX_BUF_SIZE) {
            s_rx_buf[s_rx_idx++] = byte;
        }
        s_last_rx_tick = HAL_GetTick();

        /* 最小4 byteが届いたら0xAAを探し、完全なパケットを早期解析する。 */
        if (!s_frame_parsed && s_rx_idx >= 4U) {
            for (uint8_t i = 0U; i < s_rx_idx; i++) {
                if (s_rx_buf[i] != PACKET_HEADER) {
                    continue;
                }

                uint8_t remaining = s_rx_idx - i;
                if (remaining < 4U) {
                    break;
                }

                uint8_t payload_len = s_rx_buf[i + 2U];
                if (payload_len > PACKET_MAX_PAYLOAD) {
                    break;
                }

                uint8_t packet_len = 3U + payload_len + 1U;
                if (remaining < packet_len) {
                    break;
                }

                SPI_Slave_Dispatch(&s_rx_buf[i], packet_len);
                s_frame_parsed = 1U;
                while (s_tx_idx < s_tx_len && (spi->SR & SPI_SR_TXE)) {
                    *(volatile uint8_t *)&spi->DR = s_tx_buf[s_tx_idx++];
                }
                break;
            }
        }
    }

    /* オーバーランはDR、SRの順に読み、フラグを解除する。 */
    if (spi->SR & SPI_SR_OVR) {
        (void)spi->DR;
        (void)spi->SR;
    }
}

void SPI_Slave_Poll(void)
{
    if (s_hspi == NULL) {
        return;
    }

    SPI_TypeDef *spi = s_hspi->Instance;
    if (s_rx_idx > 0U) {
        uint8_t cs_high = (GPIOA->IDR & GPIO_PIN_4) ? 1U : 0U;
        uint32_t elapsed = HAL_GetTick() - s_last_rx_tick;
        /*
         * CSがHigh、または最後の受信から10 ms経過したら
         * 割り込みを一時停止し、次のフレーム用に状態を戻す。
         */
        if (!(spi->SR & SPI_SR_BSY) && (cs_high || elapsed >= 10U)) {
            HAL_NVIC_DisableIRQ(SPI1_IRQn);
            s_rx_idx = 0U;
            s_tx_len = 0U;
            s_tx_idx = 0U;
            s_frame_parsed = 0U;
            s_last_rx_tick = 0U;

            spi->CR1 &= ~SPI_CR1_SPE;
            spi->CR1 |= SPI_CR1_SPE;
            HAL_NVIC_ClearPendingIRQ(SPI1_IRQn);
            HAL_NVIC_EnableIRQ(SPI1_IRQn);
        }
    }
}
