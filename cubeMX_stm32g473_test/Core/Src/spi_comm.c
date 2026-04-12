#include "spi_comm.h"
#include "config.h"
#include "debug_led.h"
#include "led_control.h"
#include "motor_tb6612.h"
#include <string.h>

/* ---------- 内部変数 ---------- */
static SPI_HandleTypeDef *s_hspi;
static uint8_t  s_rx_buf[SPI_RX_BUF_SIZE];
static uint8_t  s_rx_idx = 0;

/* ---------- 応答 TX バッファ ---------- */
static uint8_t  s_tx_buf[PACKET_MAX_SIZE];
static uint8_t  s_tx_len = 0;
static uint8_t  s_tx_idx = 0;

/* ---------- 応答プリロード ---------- */
static void preload_tx(void)
{
    SPI_TypeDef *spi = s_hspi->Instance;
    while (s_tx_idx < s_tx_len && (spi->SR & SPI_SR_TXE)) {
        *(volatile uint8_t *)&spi->DR = s_tx_buf[s_tx_idx++];
    }
}

static void prepare_ack(void)
{
    Packet_t ack;
    Packet_Build(&ack, CMD_ACK, NULL, 0);
    s_tx_len = Packet_Serialize(&ack, s_tx_buf);
    s_tx_idx = 0;
    preload_tx();
}

static void prepare_nack(uint8_t error_code)
{
    Packet_t nack;
    Packet_Build(&nack, CMD_NACK, &error_code, 1);
    s_tx_len = Packet_Serialize(&nack, s_tx_buf);
    s_tx_idx = 0;
    preload_tx();
}

/* ---------- CMD 0x40 ハンドラ ---------- */
void handle_debug_led(const Packet_t *pkt)
{
    if (pkt == NULL || pkt->length < 1) {
        return;
    }
    uint8_t led_mask = pkt->payload[0];
    /* payload[1] が省略された場合は DBG_LED_ON をデフォルト */
    DebugLedMode_t mode = (pkt->length >= 2)
                          ? (DebugLedMode_t)pkt->payload[1]
                          : DBG_LED_ON;

    led_mask &= 0x3F;
    if (mode > DBG_LED_BLINK_4HZ) {
        return;
    }

    DebugLed_Set(led_mask, mode);
}

/* ---------- CMD 0x11 ハンドラ ---------- */
static void handle_led_mode(const Packet_t *pkt)
{
    if (pkt == NULL || pkt->length < 1) return;
    uint8_t mode_id = pkt->payload[0];
    if (mode_id > LM_BATTERY) return;
    LED_SetMode((LedMode_t)mode_id);
}

/* ---------- CMD 0x12 ハンドラ ---------- */
static void handle_led_brightness(const Packet_t *pkt)
{
    if (pkt == NULL || pkt->length < 2) return;
    uint8_t led_id     = pkt->payload[0];
    uint8_t brightness = pkt->payload[1];

    LedSide_t side;
    if (led_id == 0x00)      side = LED_SIDE_RIGHT;
    else if (led_id == 0x01) side = LED_SIDE_LEFT;
    else if (led_id == 0xFF) side = LED_SIDE_BOTH;
    else return;

    LED_SetBrightness(side, brightness);
}

/* ---------- CMD 0x01 モーター速度設定ハンドラ ---------- */
static void handle_motor_speed(const Packet_t *pkt)
{
    if (pkt == NULL || pkt->length < 4) return;
    uint8_t ch_id  = pkt->payload[0];
    uint8_t dir_id = pkt->payload[1];
    uint16_t speed = ((uint16_t)pkt->payload[2] << 8) | pkt->payload[3];

    if (dir_id > TB6612_DIR_COAST) return;
    TB6612_Dir_t dir = (TB6612_Dir_t)dir_id;

    if (ch_id == 0xFF) {
        TB6612_SetMotor(TB6612_CH_A, dir, speed);
        TB6612_SetMotor(TB6612_CH_B, dir, speed);
    } else if (ch_id <= TB6612_CH_B) {
        TB6612_SetMotor((TB6612_Channel_t)ch_id, dir, speed);
    }
}

/* ---------- CMD 0x02 モーター停止ハンドラ ---------- */
static void handle_motor_stop(const Packet_t *pkt)
{
    if (pkt == NULL) return;
    if (pkt->length >= 1) {
        uint8_t ch_id = pkt->payload[0];
        if (ch_id == 0xFF) {
            TB6612_Stop(TB6612_CH_A);
            TB6612_Stop(TB6612_CH_B);
        } else if (ch_id <= TB6612_CH_B) {
            TB6612_Stop((TB6612_Channel_t)ch_id);
        }
    } else {
        TB6612_Stop(TB6612_CH_A);
        TB6612_Stop(TB6612_CH_B);
    }
}

/* ---------- CMD 0x04 緊急停止ハンドラ ---------- */
static void handle_emergency_stop(const Packet_t *pkt)
{
    (void)pkt;
    TB6612_EmergencyStop();
}

/* ---------- CMD 0x03 暫定ハンドラ ---------- */
static void handle_stepper_ctrl_led(const Packet_t *pkt)
{
    if (pkt == NULL) {
        return;
    }
    DebugLed_Set(0x08, DBG_LED_ON);
}

/* ---------- 初期化 ---------- */
void SPI_Slave_Init(SPI_HandleTypeDef *hspi)
{
    s_hspi = hspi;
    s_rx_idx = 0;
    memset(s_rx_buf, 0, sizeof(s_rx_buf));
    s_tx_len = 0;
    s_tx_idx = 0;

    /*
     * CubeMX の SPI 設定 (Hardware NSS Slave) をそのまま使用。
     * DMA・EXTI は一切使わず、メインループでレジスタ直接ポーリング。
     * 全 SPI 関連割込みを無効化 → ISR 競合を根本排除。
     */
    s_hspi->Instance->CR2 &= ~(SPI_CR2_TXEIE | SPI_CR2_RXNEIE | SPI_CR2_ERRIE);
    HAL_NVIC_DisableIRQ(SPI1_IRQn);
    HAL_NVIC_DisableIRQ(DMA1_Channel2_IRQn);
    HAL_NVIC_DisableIRQ(EXTI4_IRQn);

    __HAL_SPI_ENABLE(s_hspi);
}

/* ---------- DMA 受信開始 (互換性のため残す — no-op) ---------- */
void SPI_Slave_StartReceive(void) { /* no-op */ }

/* ---------- コマンドテーブル (SPI 経由) ---------- */
typedef void (*CmdHandler_t)(const Packet_t *pkt);

static const struct {
    uint8_t       cmd;
    CmdHandler_t  handler;
} spi_cmd_table[] = {
    { CMD_MOTOR_SPEED,    handle_motor_speed      },
    { CMD_MOTOR_STOP,     handle_motor_stop        },
    { CMD_STEPPER_CTRL,   handle_stepper_ctrl_led  },
    { CMD_EMERGENCY_STOP, handle_emergency_stop    },
    { CMD_LED_MODE,       handle_led_mode          },
    { CMD_LED_BRIGHTNESS, handle_led_brightness    },
    { CMD_DEBUG_LED,      handle_debug_led         },
};
#define SPI_CMD_TABLE_SIZE  (sizeof(spi_cmd_table) / sizeof(spi_cmd_table[0]))

/* ---------- パケットディスパッチ ---------- */
void SPI_Slave_Dispatch(const uint8_t *buf, uint8_t len)
{
    Packet_t pkt;
    if (!Packet_Parse(buf, len, &pkt)) {
        prepare_nack(0x02);   /* パラメータ範囲外 / パースエラー */
        return;
    }

    for (uint16_t i = 0; i < SPI_CMD_TABLE_SIZE; i++) {
        if (spi_cmd_table[i].cmd == pkt.cmd) {
            spi_cmd_table[i].handler(&pkt);
            prepare_ack();
            return;
        }
    }

    prepare_nack(0x01);       /* 不明なコマンド */
}

/* ---------- 応答送信 ---------- */
HAL_StatusTypeDef SPI_Slave_SendResponse(const Packet_t *p)
{
    uint8_t buf[PACKET_MAX_SIZE];
    uint8_t len = Packet_Serialize(p, buf);
    return HAL_SPI_Transmit(s_hspi, buf, len, 100);
}

/* ---------- コールバック (互換性のため残す — no-op) ---------- */
void SPI_Slave_RxCompleteCallback(void) { /* no-op */ }
void SPI_Slave_NssRisingCallback(void)  { /* no-op */ }

/* ---------- メインループから呼ぶ SPI 受信処理 ---------- */
/*
 * 割込み・DMA を一切使わないレジスタ直接ポーリング方式。
 * NOP ディレイなしでメインループを高速周回させ、
 * コマンド受信 → 早期パース → パディング期間中に MISO で ACK/NACK 送出。
 *
 * プロトコル仕様:
 *   ESP32 は [コマンド(4-36bytes)] + [0x00 パディング(8bytes以上)] を
 *   1つの CS アサーション内で送信する。
 *   STM32 はコマンドバイト受信完了後、パディング期間中に MISO で応答を返す。
 *   ESP32 はパディング期間中の MISO バイトから 0xAA ヘッダを探して応答を取得。
 */
static uint32_t s_last_rx_tick = 0;
static uint8_t  s_frame_parsed = 0;

void SPI_Slave_Poll(void)
{
    SPI_TypeDef *spi = s_hspi->Instance;

    /* ---- RXNE がある限り読む。同時に TX 応答バイトを書き込む ---- */
    while (spi->SR & SPI_SR_RXNE) {
        /* TX: 応答バイトがあり TXE が立っていれば DR に書く */
        if (s_tx_idx < s_tx_len && (spi->SR & SPI_SR_TXE)) {
            *(volatile uint8_t *)&spi->DR = s_tx_buf[s_tx_idx++];
        }

        /* RX: 受信バイトを格納 */
        uint8_t byte = *(volatile uint8_t *)&spi->DR;
        if (s_rx_idx < SPI_RX_BUF_SIZE) {
            s_rx_buf[s_rx_idx++] = byte;
        }
        s_last_rx_tick = HAL_GetTick();

        /* ---- 早期パース: フレームをまだパースしていない場合 ---- */
        if (!s_frame_parsed && s_rx_idx >= 4) {
            for (uint8_t i = 0; i < s_rx_idx; i++) {
                if (s_rx_buf[i] != PACKET_HEADER) continue;
                uint8_t rem = s_rx_idx - i;
                if (rem < 4) break;
                uint8_t payload_len = s_rx_buf[i + 2];
                if (payload_len > PACKET_MAX_PAYLOAD) break;
                uint8_t pkt_len = 3 + payload_len + 1;
                if (rem < pkt_len) break;

                /* パース完了 → Dispatch (内部で prepare_ack/nack + preload_tx) */
                SPI_Slave_Dispatch(&s_rx_buf[i], pkt_len);
                s_frame_parsed = 1;

                /* Dispatch 直後: TXE が立っていれば続けて TX FIFO に書き込む */
                while (s_tx_idx < s_tx_len && (spi->SR & SPI_SR_TXE)) {
                    *(volatile uint8_t *)&spi->DR = s_tx_buf[s_tx_idx++];
                }
                break;
            }
        }
    }

    /* ---- OVR (オーバーラン) クリア ---- */
    if (spi->SR & SPI_SR_OVR) {
        (void)spi->DR;
        (void)spi->SR;
    }

    /* ---- CS HIGH 検出 → フレーム終了、次フレームの準備 ---- */
    if (s_rx_idx > 0) {
        /* PA4 = NSS: Hardware NSS は SPI ペリフェラルが使うが GPIO でも読める */
        uint8_t cs_high  = (GPIOA->IDR & GPIO_PIN_4) ? 1 : 0;
        uint32_t elapsed = HAL_GetTick() - s_last_rx_tick;

        /* BSY=0 かつ (CS HIGH または 10ms タイムアウト) でフレーム終了 */
        if (!(spi->SR & SPI_SR_BSY) && (cs_high || elapsed >= 10)) {
            s_rx_idx = 0;
            s_tx_len = 0;
            s_tx_idx = 0;
            s_frame_parsed = 0;
            s_last_rx_tick = 0;

            /* TX/RX FIFO フラッシュ: SPE OFF→ON */
            spi->CR1 &= ~SPI_CR1_SPE;
            spi->CR1 |= SPI_CR1_SPE;
        }
    }
}
