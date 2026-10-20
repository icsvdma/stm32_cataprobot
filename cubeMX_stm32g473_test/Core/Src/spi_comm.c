#include "spi_comm.h"
#include "config.h"
#include "debug_led.h"
#include "led_control.h"
#include "motor_hbridge.h"
#include "external_sensors.h"
#include "spi_frame.h"
#include <string.h>

/* NACKでESP32へ通知するエラー理由。 */
#define SPI_ERROR_UNKNOWN_COMMAND   0x01U
#define SPI_ERROR_INVALID_PARAMETER 0x02U

/* 割り込みとメインループで共有するSPI受信・応答状態。 */
static SPI_HandleTypeDef *s_hspi;
static SpiFrame s_frame;

static uint8_t s_tx_buf[PACKET_MAX_SIZE + 1U];
static volatile uint8_t s_tx_len = 0U;
static volatile uint8_t s_tx_idx = 0U;
static void dispatch_packet(const Packet_t *packet);

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

void SPI_Slave_Init(SPI_HandleTypeDef *hspi)
{
    s_hspi = hspi;
    SpiFrame_Reset(&s_frame);
    s_tx_len = 0U;
    s_tx_idx = 0U;

    /*
     * I2Cのブロッキング読み取り中でもSPIを取りこぼさないよう、
     * SPI1割り込みで1 byteずつ処理する。
     */
    s_hspi->Instance->CR2 &= ~SPI_CR2_TXEIE;
    s_hspi->Instance->CR2 |= SPI_CR2_RXNEIE | SPI_CR2_ERRIE;
    HAL_NVIC_DisableIRQ(DMA1_Channel2_IRQn);
    /* NSSをAF5のままEXTIへ接続する。GPIOモードを入力へ変更しない。 */
    SYSCFG->EXTICR[1] &= ~SYSCFG_EXTICR2_EXTI4;
    EXTI->RTSR1 |= GPIO_PIN_4;
    EXTI->FTSR1 &= ~GPIO_PIN_4;
    EXTI->PR1 = GPIO_PIN_4;
    EXTI->IMR1 |= GPIO_PIN_4;
    HAL_NVIC_SetPriority(SPI1_IRQn, 1U, 0U);
    HAL_NVIC_SetPriority(EXTI4_IRQn, 1U, 0U);
    HAL_NVIC_ClearPendingIRQ(EXTI4_IRQn);
    HAL_NVIC_EnableIRQ(EXTI4_IRQn);

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

    dispatch_packet(&pkt);
}

/** 検証済みpacketを実行し、SPI応答を準備する。戻り値なし。 */
static void dispatch_packet(const Packet_t *packet)
{
    Packet_t pkt = *packet;
    Packet_t motor_response;
    if (HBridge_Process(&pkt, &motor_response)) {
        if (motor_response.cmd == CMD_SHUTDOWN_ACK) {
            LED_AllOff();
            DebugLed_AllOff();
        }
        prepare_response(&motor_response);
        return;
    }

    /* センサー要求は専用応答を返す。 */
    if (pkt.cmd == CMD_SENSOR_REQ) {
        if (pkt.length != 1U || pkt.payload[0] != 0xFFU) {
            prepare_nack(pkt.cmd, SPI_ERROR_INVALID_PARAMETER);
            return;
        }
        prepare_sensor_response();
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
    if (s_hspi == NULL || (GPIOA->IDR & GPIO_PIN_4) == 0U) { return; }
    SPI_TypeDef *spi = s_hspi->Instance;
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    spi->CR1 &= ~SPI_CR1_SPE;
    /* NSS Highなので新しい受信はない。残ったFIFOだけを読み捨てる。 */
    for (unsigned i = 0U; i < 4U && (spi->SR & SPI_SR_RXNE); ++i) { (void)spi->DR; }
    (void)spi->SR;
    s_tx_len = s_tx_idx = 0U;
    SpiFrame_Reset(&s_frame);
    spi->CR1 |= SPI_CR1_SPE;
    __set_PRIMASK(primask);
}

void SPI_Slave_IRQHandler(void)
{
    if (s_hspi == NULL) { return; }
    SPI_TypeDef *spi = s_hspi->Instance;
    while (spi->SR & SPI_SR_RXNE) {
        if (s_tx_idx < s_tx_len && (spi->SR & SPI_SR_TXE)) {
            *(volatile uint8_t *)&spi->DR = s_tx_buf[s_tx_idx++];
        }
        uint8_t byte = *(volatile uint8_t *)&spi->DR;
        Packet_t packet;
        SpiFrameResult result = SpiFrame_Push(&s_frame, byte, &packet);
        if (result == SPI_FRAME_READY) { dispatch_packet(&packet); }
        if (result == SPI_FRAME_INVALID) { prepare_nack(packet.cmd, SPI_ERROR_INVALID_PARAMETER); }
        preload_tx();
    }
    if (spi->SR & SPI_SR_OVR) {
        (void)spi->DR;
        (void)spi->SR;
        s_frame.done = true;
    }
}

void SPI_Slave_Poll(void)
{
    /* フレーム境界はEXTI4で処理する。メインループの遅延に依存しない。 */
}
