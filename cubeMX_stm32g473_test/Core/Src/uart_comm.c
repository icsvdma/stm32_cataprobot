#include "uart_comm.h"
#include "config.h"
#include <string.h>

/*
 * UARTは保守用の互換経路。各処理が別ファイルで定義されない場合でも
 * リンクできるよう、何もしないweak関数を用意する。
 */
__attribute__((weak)) void handle_motor_speed(const Packet_t *pkt)    { (void)pkt; }
__attribute__((weak)) void handle_motor_stop(const Packet_t *pkt)     { (void)pkt; }
__attribute__((weak)) void handle_emergency_stop(const Packet_t *pkt) { (void)pkt; }
__attribute__((weak)) void handle_status_req(const Packet_t *pkt)     { (void)pkt; }
__attribute__((weak)) void handle_led_mode(const Packet_t *pkt)       { (void)pkt; }
__attribute__((weak)) void handle_led_brightness(const Packet_t *pkt) { (void)pkt; }
__attribute__((weak)) void handle_sensor_req(const Packet_t *pkt)     { (void)pkt; }
__attribute__((weak)) void handle_shutdown(const Packet_t *pkt)       { (void)pkt; }
__attribute__((weak)) void handle_debug_led(const Packet_t *pkt)      { (void)pkt; }

/* コマンドIDと処理関数の対応表。 */
typedef void (*CmdHandler_t)(const Packet_t *pkt);

static const struct {
    uint8_t       cmd;
    CmdHandler_t  handler;
} cmd_table[] = {
    { CMD_MOTOR_SPEED,    handle_motor_speed    },
    { CMD_MOTOR_STOP,     handle_motor_stop     },
    { CMD_EMERGENCY_STOP, handle_emergency_stop },
    { CMD_STATUS_REQ,     handle_status_req     },
    { CMD_LED_MODE,       handle_led_mode       },
    { CMD_LED_BRIGHTNESS, handle_led_brightness },
    { CMD_SENSOR_REQ,     handle_sensor_req     },
    { CMD_SHUTDOWN,       handle_shutdown       },
    { CMD_DEBUG_LED,      handle_debug_led      },
};
#define CMD_TABLE_SIZE  (sizeof(cmd_table) / sizeof(cmd_table[0]))

/* DMA受信データを一時保存するリングバッファと読み書き位置。 */
static UART_HandleTypeDef *s_huart;

static uint8_t  s_dma_buf[UART_DMA_BUF_SIZE];
static uint8_t  s_rx_buf[UART_RX_BUF_SIZE];
static volatile uint16_t s_rx_head;
static volatile uint16_t s_rx_tail;

static volatile uint32_t s_last_rx_tick;

void UART_Init(UART_HandleTypeDef *huart)
{
    s_huart = huart;
    s_rx_head = 0;
    s_rx_tail = 0;
    s_last_rx_tick = HAL_GetTick();

    /* 無通信区間を検出できるReceiveToIdle DMAを開始する。 */
    __HAL_UART_ENABLE_IT(s_huart, UART_IT_IDLE);
    HAL_UARTEx_ReceiveToIdle_DMA(s_huart, s_dma_buf, UART_DMA_BUF_SIZE);
    /* 半分到達通知は使わず、IDLEまたはバッファ満杯だけを受け取る。 */
    __HAL_DMA_DISABLE_IT(s_huart->hdmarx, DMA_IT_HT);
}

HAL_StatusTypeDef UART_Send(const Packet_t *p)
{
    uint8_t buf[PACKET_MAX_SIZE];
    uint8_t len = Packet_Serialize(p, buf);
    return HAL_UART_Transmit(s_huart, buf, len, 100);
}

/** @brief データなしのACKをUARTへ送る。@return なし。 */
static void send_ack(void)
{
    Packet_t ack;
    Packet_Build(&ack, CMD_ACK, NULL, 0);
    UART_Send(&ack);
}

/** @brief データなしのNACKをUARTへ送る。@return なし。 */
static void send_nack(void)
{
    Packet_t nack;
    Packet_Build(&nack, CMD_NACK, NULL, 0);
    UART_Send(&nack);
}

void UART_Dispatch(const uint8_t *buf, uint8_t len)
{
    Packet_t pkt;
    if (!Packet_Parse(buf, len, &pkt)) {
        send_nack();
        return;
    }

    s_last_rx_tick = HAL_GetTick();

    /* コマンド表から一致する処理を探し、完了後にACKを返す。 */
    for (uint16_t i = 0; i < CMD_TABLE_SIZE; i++) {
        if (cmd_table[i].cmd == pkt.cmd) {
            cmd_table[i].handler(&pkt);
            send_ack();
            return;
        }
    }
    /* 未定義のコマンドはNACKで拒否する。 */
    send_nack();
}

void UART_RxEventCallback(uint16_t size)
{
    /* 未処理データを上書きしない範囲でリングバッファへコピーする。 */
    for (uint16_t i = 0; i < size; i++) {
        uint16_t next = (s_rx_head + 1) % UART_RX_BUF_SIZE;
        if (next != s_rx_tail) {
            s_rx_buf[s_rx_head] = s_dma_buf[i];
            s_rx_head = next;
        }
    }
    /* 次の受信に備えてDMAを再開する。 */
    HAL_UARTEx_ReceiveToIdle_DMA(s_huart, s_dma_buf, UART_DMA_BUF_SIZE);
    __HAL_DMA_DISABLE_IT(s_huart->hdmarx, DMA_IT_HT);
}

void UART_ProcessReceived(void)
{
    /* リングバッファからパケットヘッダー0xAAを探す。 */
    while (s_rx_tail != s_rx_head) {
        if (s_rx_buf[s_rx_tail] != PACKET_HEADER) {
            s_rx_tail = (s_rx_tail + 1) % UART_RX_BUF_SIZE;
            continue;
        }

        /* リングの折り返しを考慮して未処理byte数を求める。 */
        uint16_t available;
        if (s_rx_head >= s_rx_tail) {
            available = s_rx_head - s_rx_tail;
        } else {
            available = UART_RX_BUF_SIZE - s_rx_tail + s_rx_head;
        }

        /* header、cmd、lengthがそろうまで次回処理を待つ。 */
        if (available < 3) {
            break;
        }

        /* lengthからパケット全体の長さを求める。 */
        uint16_t len_idx = (s_rx_tail + 2) % UART_RX_BUF_SIZE;
        uint8_t payload_len = s_rx_buf[len_idx];

        if (payload_len > PACKET_MAX_PAYLOAD) {
            s_rx_tail = (s_rx_tail + 1) % UART_RX_BUF_SIZE;
            continue;
        }

        /* 固定3 byte、データ部、末尾チェックサム1 byteを合計する。 */
        uint8_t total_len = 3 + payload_len + 1;
        if (available < total_len) {
            break; /* 未受信分は次回の処理を待つ。 */
        }

        /* 折り返したデータを連続領域へコピーして解析する。 */
        uint8_t linear[PACKET_MAX_SIZE];
        for (uint8_t i = 0; i < total_len; i++) {
            linear[i] = s_rx_buf[(s_rx_tail + i) % UART_RX_BUF_SIZE];
        }

        UART_Dispatch(linear, total_len);

        /* 処理済みパケット分だけ読み取り位置を進める。 */
        s_rx_tail = (s_rx_tail + total_len) % UART_RX_BUF_SIZE;
    }
}

void UART_CheckWatchdog(void)
{
    /* 1秒以上UARTを受信しない場合は安全のため緊急停止する。 */
    if ((HAL_GetTick() - s_last_rx_tick) > COMM_WATCHDOG_MS) {
        handle_emergency_stop(NULL);
    }
}

void UART_ResetWatchdog(void)
{
    s_last_rx_tick = HAL_GetTick();
}
