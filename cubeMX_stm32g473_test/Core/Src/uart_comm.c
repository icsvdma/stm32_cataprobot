#include "uart_comm.h"
#include "config.h"
#include <string.h>

/* ---------- 外部ハンドラ（Phase 2 以降で実装） ---------- */
__attribute__((weak)) void handle_motor_speed(const Packet_t *pkt)    { (void)pkt; }
__attribute__((weak)) void handle_motor_stop(const Packet_t *pkt)     { (void)pkt; }
__attribute__((weak)) void handle_stepper_ctrl(const Packet_t *pkt)   { (void)pkt; }
__attribute__((weak)) void handle_emergency_stop(const Packet_t *pkt) { (void)pkt; }
__attribute__((weak)) void handle_status_req(const Packet_t *pkt)     { (void)pkt; }
__attribute__((weak)) void handle_led_mode(const Packet_t *pkt)       { (void)pkt; }
__attribute__((weak)) void handle_led_brightness(const Packet_t *pkt) { (void)pkt; }
__attribute__((weak)) void handle_sensor_req(const Packet_t *pkt)     { (void)pkt; }
__attribute__((weak)) void handle_shutdown(const Packet_t *pkt)       { (void)pkt; }
__attribute__((weak)) void handle_debug_led(const Packet_t *pkt)      { (void)pkt; }

/* ---------- コマンドテーブル ---------- */
typedef void (*CmdHandler_t)(const Packet_t *pkt);

static const struct {
    uint8_t       cmd;
    CmdHandler_t  handler;
} cmd_table[] = {
    { CMD_MOTOR_SPEED,    handle_motor_speed    },
    { CMD_MOTOR_STOP,     handle_motor_stop     },
    { CMD_STEPPER_CTRL,   handle_stepper_ctrl   },
    { CMD_EMERGENCY_STOP, handle_emergency_stop },
    { CMD_STATUS_REQ,     handle_status_req     },
    { CMD_LED_MODE,       handle_led_mode       },
    { CMD_LED_BRIGHTNESS, handle_led_brightness },
    { CMD_SENSOR_REQ,     handle_sensor_req     },
    { CMD_SHUTDOWN,       handle_shutdown       },
    { CMD_DEBUG_LED,      handle_debug_led      },
};
#define CMD_TABLE_SIZE  (sizeof(cmd_table) / sizeof(cmd_table[0]))

/* ---------- 内部変数 ---------- */
static UART_HandleTypeDef *s_huart;

static uint8_t  s_dma_buf[UART_DMA_BUF_SIZE];
static uint8_t  s_rx_buf[UART_RX_BUF_SIZE];
static volatile uint16_t s_rx_head;
static volatile uint16_t s_rx_tail;

static volatile uint32_t s_last_rx_tick;

/* ---------- 初期化 ---------- */
void UART_Init(UART_HandleTypeDef *huart)
{
    s_huart = huart;
    s_rx_head = 0;
    s_rx_tail = 0;
    s_last_rx_tick = HAL_GetTick();

    /* IDLE Line 割込 + DMA 受信開始 */
    __HAL_UART_ENABLE_IT(s_huart, UART_IT_IDLE);
    HAL_UARTEx_ReceiveToIdle_DMA(s_huart, s_dma_buf, UART_DMA_BUF_SIZE);
    /* Half-Transfer 割込を無効化 */
    __HAL_DMA_DISABLE_IT(s_huart->hdmarx, DMA_IT_HT);
}

/* ---------- パケット送信 ---------- */
HAL_StatusTypeDef UART_Send(const Packet_t *p)
{
    uint8_t buf[PACKET_MAX_SIZE];
    uint8_t len = Packet_Serialize(p, buf);
    return HAL_UART_Transmit(s_huart, buf, len, 100);
}

/* ---------- ACK/NACK 送信 ---------- */
static void send_ack(void)
{
    Packet_t ack;
    Packet_Build(&ack, CMD_ACK, NULL, 0);
    UART_Send(&ack);
}

static void send_nack(void)
{
    Packet_t nack;
    Packet_Build(&nack, CMD_NACK, NULL, 0);
    UART_Send(&nack);
}

/* ---------- コマンドディスパッチ ---------- */
void UART_Dispatch(const uint8_t *buf, uint8_t len)
{
    Packet_t pkt;
    if (!Packet_Parse(buf, len, &pkt)) {
        send_nack();
        return;
    }

    s_last_rx_tick = HAL_GetTick();

    for (uint16_t i = 0; i < CMD_TABLE_SIZE; i++) {
        if (cmd_table[i].cmd == pkt.cmd) {
            cmd_table[i].handler(&pkt);
            send_ack();
            return;
        }
    }
    /* 未知のコマンド */
    send_nack();
}

/* ---------- DMA 受信完了コールバック ---------- */
/* HAL_UARTEx_RxEventCallback を main.c 等で呼び出す。
   ここではリングバッファにコピーする処理を提供。 */
void UART_RxEventCallback(uint16_t size)
{
    for (uint16_t i = 0; i < size; i++) {
        uint16_t next = (s_rx_head + 1) % UART_RX_BUF_SIZE;
        if (next != s_rx_tail) {
            s_rx_buf[s_rx_head] = s_dma_buf[i];
            s_rx_head = next;
        }
    }
    /* DMA 再開始 */
    HAL_UARTEx_ReceiveToIdle_DMA(s_huart, s_dma_buf, UART_DMA_BUF_SIZE);
    __HAL_DMA_DISABLE_IT(s_huart->hdmarx, DMA_IT_HT);
}

/* ---------- メインループから呼ぶ受信処理 ---------- */
void UART_ProcessReceived(void)
{
    /* リングバッファからヘッダ(0xAA)を探す */
    while (s_rx_tail != s_rx_head) {
        if (s_rx_buf[s_rx_tail] != PACKET_HEADER) {
            s_rx_tail = (s_rx_tail + 1) % UART_RX_BUF_SIZE;
            continue;
        }

        /* 利用可能なバイト数を計算 */
        uint16_t available;
        if (s_rx_head >= s_rx_tail) {
            available = s_rx_head - s_rx_tail;
        } else {
            available = UART_RX_BUF_SIZE - s_rx_tail + s_rx_head;
        }

        /* 最低限のヘッダ部 (header + cmd + length) が揃うまで待機 */
        if (available < 3) {
            break;
        }

        /* length フィールドを読み出す */
        uint16_t len_idx = (s_rx_tail + 2) % UART_RX_BUF_SIZE;
        uint8_t payload_len = s_rx_buf[len_idx];

        if (payload_len > PACKET_MAX_PAYLOAD) {
            s_rx_tail = (s_rx_tail + 1) % UART_RX_BUF_SIZE;
            continue;
        }

        uint8_t total_len = 3 + payload_len + 1; /* header+cmd+len + payload + checksum */
        if (available < total_len) {
            break; /* まだ全バイト揃っていない */
        }

        /* 線形バッファにコピーしてパース */
        uint8_t linear[PACKET_MAX_SIZE];
        for (uint8_t i = 0; i < total_len; i++) {
            linear[i] = s_rx_buf[(s_rx_tail + i) % UART_RX_BUF_SIZE];
        }

        UART_Dispatch(linear, total_len);

        /* 消費 */
        s_rx_tail = (s_rx_tail + total_len) % UART_RX_BUF_SIZE;
    }
}

/* ---------- 通信途絶監視 ---------- */
void UART_CheckWatchdog(void)
{
    if ((HAL_GetTick() - s_last_rx_tick) > COMM_WATCHDOG_MS) {
        handle_emergency_stop(NULL);
    }
}

void UART_ResetWatchdog(void)
{
    s_last_rx_tick = HAL_GetTick();
}
