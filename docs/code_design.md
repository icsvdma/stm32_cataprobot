# コード設計書
## デュアルMCUモーター制御システム（ESP32 + STM32）

| 項目 | 内容 |
|---|---|
| ドキュメントバージョン | 1.0 |
| 作成日 | 2026-03-21 |
| ステータス | ドラフト |
| 参照元 | docs/required_spec.md, docs/implementation_plan.md |

---

## 1. システムアーキテクチャ概要

```
┌───────────────────────────────────────────┐
│              コントローラー (BT)           │
│  PS4/PS5 / BLE ゲームパッド               │
└──────────────────────┬────────────────────┘
                       │ Bluetooth (SPP/BLE HID)
┌──────────────────────▼────────────────────┐
│               ESP32-WROOM-32D              │
│                                            │
│  ┌──────────────┐  ┌────────────────────┐  │
│  │ bt_controller│  │  battery_monitor   │  │
│  │  (HID受信)   │  │  (ADC 1秒周期)     │  │
│  └──────┬───────┘  └─────────┬──────────┘  │
│         │                    │              │
│  ┌──────▼───────┐  ┌─────────▼──────────┐  │
│  │  uart_comm   │  │  power_manager     │  │
│  │ (UART2送受信)│  │ (GPIO_POW_ON制御)  │  │
│  └──────┬───────┘  └────────────────────┘  │
│         │ SPI1 (冗長路)                    │
│  ┌──────▼───────┐                          │
│  │  spi_comm    │                          │
│  │ (SPI1 Master)│                          │
│  └──────┬───────┘                          │
└─────────┼─────────────────────────────────┘
          │ UART2 (115200bps, 8N1) / SPI1
┌─────────▼─────────────────────────────────┐
│               STM32G473RCTx                │
│                                            │
│  ┌──────────────┐  ┌────────────────────┐  │
│  │  uart_comm   │  │    spi_comm        │  │
│  │ (USART2受信) │  │  (SPI1 Slave)      │  │
│  └──────┬───────┘  └────────────────────┘  │
│         │ コマンドディスパッチ              │
│  ┌──────┴────────────────────────────────┐ │
│  │            コマンドハンドラ            │ │
│  └──┬──────────┬──────────┬──────────┬───┘ │
│     │          │          │          │     │
│  ┌──▼────┐ ┌───▼───┐ ┌───▼────┐ ┌───▼───┐ │
│  │motor  │ │motor  │ │sensor  │ │ led   │ │
│  │_dc    │ │_step  │ │_mpu6050│ │_ctrl  │ │
│  │(TIM3) │ │(TIM割 │ │(I2C)   │ │(TIM   │ │
│  │       │ │ 込)   │ │        │ │ PWM)  │ │
│  └───────┘ └───────┘ └────────┘ └───────┘ │
└───────────────────────────────────────────┘
```

---

## 2. ESP32 モジュール設計

### 2.1 `config.h` — グローバル定数・ピン定義

```c
// ピン定義
#define PIN_UART2_RX        16
#define PIN_UART2_TX        17
#define PIN_SPI_MISO        12
#define PIN_SPI_MOSI        13
#define PIN_SPI_SCK         14
#define PIN_SPI_NSS         15
#define PIN_GPIO_POW_ON     25   // GPIO_POW_ON (F-E02)
#define PIN_BAT_SENSE       36   // SENSOR_VP (ADC1_CH0)
#define PIN_DEBUG_LED_0     22
#define PIN_DEBUG_LED_1     23
// NOTE: 要件定義書では D9 (UART LED) に IO25 が割り当てられているが、
//       IO25 は GPIO_POW_ON (安全制御ピン) と競合する。
//       回路図レビュー時に別ピンへの変更を推奨 (例: IO21)。
#define PIN_DEBUG_LED_2     21   // D9: UART通信 LED (IO25競合のためIO21代替案)
#define PIN_DEBUG_LED_3     26
#define PIN_DEBUG_LED_4     27
#define PIN_DEBUG_LED_5     32
#define PIN_DEBUG_LED_6     33

// バッテリー閾値 (mV)
#define BAT_VOLTAGE_FULL    12000
#define BAT_VOLTAGE_NORMAL  10500
#define BAT_VOLTAGE_LOW     10500
#define BAT_VOLTAGE_CRITICAL 9500

// 通信設定
#define UART2_BAUD          115200
#define UART_ACK_TIMEOUT_MS 200
#define UART_RETRY_MAX      3

// タイムアウト設定
#define BT_DISCONNECT_TIMEOUT_MS  500
#define BT_RECONNECT_TIMEOUT_MS   30000
#define COMM_WATCHDOG_MS          1000
#define SHUTDOWN_TIMEOUT_MS       3000
```

---

### 2.2 `packet` — パケット構造体・チェックサム

#### データ構造

```c
#define PACKET_HEADER   0xAA
#define PACKET_MAX_PAYLOAD  32

typedef struct {
    uint8_t header;           // 0xAA 固定
    uint8_t cmd;              // コマンド ID
    uint8_t length;           // ペイロード長 (0-32)
    uint8_t payload[PACKET_MAX_PAYLOAD];
    uint8_t checksum;         // XOR(cmd, length, payload[0..length-1])
} Packet_t;
```

#### 主要関数

| 関数 | シグネチャ | 説明 |
|---|---|---|
| `packet_build` | `void packet_build(Packet_t *p, uint8_t cmd, const uint8_t *payload, uint8_t len)` | パケット生成 + チェックサム計算 |
| `packet_serialize` | `uint8_t packet_serialize(const Packet_t *p, uint8_t *buf)` | バイト列に変換、返値=バイト数 |
| `packet_parse` | `bool packet_parse(const uint8_t *buf, uint8_t len, Packet_t *out)` | 受信バイト列を解析・検証 |
| `packet_checksum` | `uint8_t packet_checksum(const Packet_t *p)` | チェックサム算出 |

---

### 2.3 `uart_comm` — UART2 通信

#### 状態遷移

```
IDLE → SENDING → WAIT_ACK → IDLE (ACK受信)
                           → RETRY  (タイムアウト)
                               → SENDING (リトライ回数 < 3)
                               → ERROR   (リトライ回数 >= 3)
```

#### 主要関数

| 関数 | シグネチャ | 説明 |
|---|---|---|
| `UartComm::begin` | `void begin()` | UART2 初期化 |
| `UartComm::send` | `bool send(const Packet_t &p)` | 送信 + ACK 待ち（リトライあり） |
| `UartComm::receive` | `bool receive(Packet_t &out)` | 受信パケット取得 |
| `UartComm::update` | `void update()` | 受信バッファ処理（ループ内で呼ぶ） |
| `UartComm::getErrorRate` | `float getErrorRate()` | 通信エラー率取得 |

---

### 2.4 `spi_comm` — SPI1 Master 通信

#### 主要関数

| 関数 | シグネチャ | 説明 |
|---|---|---|
| `SpiComm::begin` | `void begin()` | SPI1 初期化 (最大 1MHz 初期) |
| `SpiComm::transfer` | `bool transfer(const uint8_t *tx, uint8_t *rx, uint8_t len)` | フルデュプレックス転送 |
| `SpiComm::sendPacket` | `bool sendPacket(const Packet_t &p)` | パケット送信 |

---

### 2.5 `bt_controller` — Bluetooth コントローラー

#### 状態遷移

```
INIT → PAIRING → CONNECTED → DISCONNECTED
                    ↑              ↓
                    └──RECONNECTING┘ (30秒タイムアウト)
```

#### コントローラーマッピング

| 入力 | 処理 | 送信コマンド |
|---|---|---|
| 左スティック Y軸 | 速度算出（デッドゾーン ±5%） | CMD 0x01 |
| 左スティック X軸 | ステアリング（ステッパ） | CMD 0x03 |
| R2 トリガー | 速度倍率 | CMD 0x01 (補正) |
| L2 トリガー | ブレーキ（speed=0）| CMD 0x02 |
| × ボタン | 緊急停止 | CMD 0x04 |
| △ ボタン | LED モード切替 | CMD 0x11 |
| ○ ボタン | ハザードトグル | CMD 0x11 |
| OPTIONS 長押し 3秒 | シャットダウン | CMD 0x30 |

#### 主要関数

| 関数 | シグネチャ | 説明 |
|---|---|---|
| `BtController::begin` | `void begin()` | BT 初期化・ペアリング開始 |
| `BtController::update` | `void update()` | 入力取得・コマンド生成（ループ内） |
| `BtController::isConnected` | `bool isConnected()` | 接続状態取得 |
| `BtController::onDisconnect` | `void onDisconnect(cb_t cb)` | 切断コールバック登録 |

---

### 2.6 `battery_monitor` — バッテリー監視

#### 電圧レベル定義

```
// バッテリーレベルは動作状態（挙動の切り替え）を定義するものであり、
// 残量(%)は電圧-残量マップから線形補間して算出する。
// 以下のパーセンテージは各レベルの下限目安値（実際は補間値を使用）。
FULL     : >= 12.0V (100%)   通常サンプリング 1s
NORMAL   : >= 10.5V ( ~50%)  通常サンプリング 1s
LOW      :  < 10.5V ( ~20%)  高速サンプリング 100ms + 警告LED点滅 + BT通知
CRITICAL :  <  9.5V (  ~5%)  自動シャットダウン開始
// 残量 % = (電圧 - 9.5V) / (12.0V - 9.5V) * 100 をクランプして算出
```

#### 主要関数

| 関数 | シグネチャ | 説明 |
|---|---|---|
| `BatteryMonitor::begin` | `void begin()` | ADC 初期化 |
| `BatteryMonitor::update` | `void update()` | 電圧取得・レベル判定（ループ内） |
| `BatteryMonitor::getVoltage` | `float getVoltage()` | 現在電圧 (V) |
| `BatteryMonitor::getLevel` | `BatLevel_t getLevel()` | FULL/NORMAL/LOW/CRITICAL |
| `BatteryMonitor::getPercent` | `uint8_t getPercent()` | 残量 (%) |

---

### 2.7 `power_manager` — 電源管理

#### シャットダウンシーケンス

```
1. STM32 へ CMD 0x30 (シャットダウン通知) 送信
2. CMD 0xF0 (シャットダウンACK) 受信待ち (タイムアウト 3秒)
3. タイムアウト or ACK受信後
4. GPIO_POW_ON = LOW → 電源遮断
```

#### 主要関数

| 関数 | シグネチャ | 説明 |
|---|---|---|
| `PowerManager::begin` | `void begin()` | GPIO_POW_ON = HIGH (電源保持) |
| `PowerManager::shutdown` | `void shutdown(ShutdownReason_t reason)` | シャットダウンシーケンス開始 |
| `PowerManager::isShuttingDown` | `bool isShuttingDown()` | シャットダウン中フラグ |

---

### 2.8 `debug_led` — デバッグ LED

#### LED割当

| LED | GPIO | 機能 |
|---|---|---|
| D7 | IO22 | 電源状態 |
| D8 | IO23 | BT 接続 |
| D9 | IO25 | UART 通信 |
| D10 | IO26 | SPI 通信 |
| D11 | IO27 | バッテリー警告 |
| D12 | IO32 | エラー |
| D13 | IO33 | ユーザー定義 |

#### 主要関数

| 関数 | シグネチャ | 説明 |
|---|---|---|
| `DebugLed::begin` | `void begin()` | GPIO 初期化 |
| `DebugLed::startupSequence` | `void startupSequence()` | ナイトライダー演出（約1秒） |
| `DebugLed::set` | `void set(LedId_t id, LedMode_t mode)` | ON/OFF/1Hz/4Hz 点滅設定 |
| `DebugLed::update` | `void update()` | 点滅タイマ処理（ループ内） |

---

## 3. STM32 モジュール設計

### 3.1 `config.h` — グローバル定数・ピン定義

```c
/* ピン定義 */
#define PIN_FRONT_LED_R     GPIO_PIN_0  // PA0
#define PIN_FRONT_LED_L     GPIO_PIN_1  // PA1
#define PIN_TB6575_FST      GPIO_PIN_1  // PB1
#define PIN_TB6575_CW_CCW   GPIO_PIN_2  // PB2
#define PIN_TB6575_FMAX     GPIO_PIN_3  // PB3
#define PIN_TB6575_FGOUT    GPIO_PIN_4  // PB4
#define PIN_6608_CK_A       GPIO_PIN_10 // PB10
#define PIN_6608_CK_B       GPIO_PIN_11 // PB11
#define PIN_6608_CCWA       GPIO_PIN_12 // PB12
#define PIN_6608_CCWB       GPIO_PIN_13 // PB13
#define PIN_6608_RESET_A    GPIO_PIN_14 // PB14
#define PIN_6608_RESET_B    GPIO_PIN_15 // PB15
#define PIN_6608_MO_A       GPIO_PIN_6  // PB6
#define PIN_6608_MO_B       GPIO_PIN_7  // PB7

/* タイミング */
#define COMM_WATCHDOG_MS    1000
#define MOTOR_REVERSE_DECEL_MS 100
```

---

### 3.2 `packet` — パケット（共通定義、C言語版）

ESP32 側と同一の構造体・関数を C 言語で実装。

```c
typedef struct {
    uint8_t header;
    uint8_t cmd;
    uint8_t length;
    uint8_t payload[PACKET_MAX_PAYLOAD];
    uint8_t checksum;
} Packet_t;

void     Packet_Build(Packet_t *p, uint8_t cmd, const uint8_t *payload, uint8_t len);
uint8_t  Packet_Serialize(const Packet_t *p, uint8_t *buf);
bool     Packet_Parse(const uint8_t *buf, uint8_t len, Packet_t *out);
uint8_t  Packet_Checksum(const Packet_t *p);
```

---

### 3.3 `uart_comm` — USART2 (STM32側)

- USART2 を DMA 受信 + 割込送信で実装
- 受信完了後にコマンドハンドラへディスパッチ
- ACK/NACK を即時返送

#### コマンドハンドラ登録テーブル

```c
typedef void (*CmdHandler_t)(const Packet_t *pkt);

static const struct {
    uint8_t       cmd;
    CmdHandler_t  handler;
} cmd_table[] = {
    { 0x01, handle_motor_speed    },
    { 0x02, handle_motor_stop     },
    { 0x03, handle_stepper_ctrl   },
    { 0x04, handle_emergency_stop },
    { 0x10, handle_status_req     },
    { 0x11, handle_led_mode       },
    { 0x12, handle_led_brightness },
    { 0x20, handle_sensor_req     },
    { 0x30, handle_shutdown       },
    { 0x40, handle_debug_led      },
};
```

---

### 3.3.1 `spi_comm` — SPI1 Slave 通信 (STM32側)

ESP32 (Master) からの SPI コマンドを受信し、コマンドハンドラへディスパッチする。
UART と同一のパケットプロトコルを使用する。

#### SPI1 設定

| 項目 | 値 |
|---|---|
| 動作モード | Slave |
| NSS | PA4 (ハードウェア NSS) |
| SCK | PA5 |
| MISO | PA6 |
| MOSI | PA7 |
| クロック | 最大 10MHz（ESP32 Master 側で設定） |
| データサイズ | 8bit |
| CPOL/CPHA | 0/0 (Mode 0) |
| 受信方式 | DMA (リングバッファ) |

#### 受信フロー

```
NSS Low (ESP32がCSアサート)
    │
    ▼
SPI DMA 受信開始
    │
    ▼
NSS High (転送完了)
    │
    ▼
パケット解析 (Packet_Parse)
    │
    ├─ 正常 → コマンドハンドラへディスパッチ（cmd_table と共通）
    │         → MISO 経由で ACK (0x80) 返送
    │
    └─ エラー → MISO 経由で NACK (0x81) 返送
```

#### SPI コマンド定義（デバッグ LED 制御）

| CMD | 名称 | ペイロード | 説明 |
|---|---|---|---|
| 0x40 | DEBUG_LED_SET | `[led_mask(1byte)][mode(1byte)]` | ビットマスクで指定した LED の ON/OFF/点滅を設定 |

**led_mask ビット割当:**

| Bit | LED | GPIO |
|---|---|---|
| bit0 | D0 | PC10 |
| bit1 | D1 | PC11 |
| bit2 | D2 | PC12 |
| bit3 | D3 | PC13 |
| bit4 | D4 | PC14 |
| bit5 | D5 | PC15 |

**mode 値:**

| 値 | 動作 |
|---|---|
| 0x00 | OFF |
| 0x01 | ON |
| 0x02 | 1Hz 点滅 |
| 0x03 | 4Hz 点滅 |

#### 主要関数

| 関数 | シグネチャ | 説明 |
|---|---|---|
| `SPI_Slave_Init` | `void SPI_Slave_Init(void)` | SPI1 Slave + DMA 初期化、受信バッファ準備 |
| `SPI_Slave_StartReceive` | `void SPI_Slave_StartReceive(void)` | DMA 受信開始（NSS 割込連動） |
| `SPI_Slave_Dispatch` | `void SPI_Slave_Dispatch(const uint8_t *buf, uint8_t len)` | 受信パケット解析 → cmd_table ハンドラ呼出 |
| `SPI_Slave_SendResponse` | `HAL_StatusTypeDef SPI_Slave_SendResponse(const Packet_t *p)` | MISO 経由で応答パケット送信 |

---

### 3.3.2 `debug_led` — デバッグ LED (STM32側)

STM32 の PC10〜PC15 に接続されたデバッグ LED を制御する。
ESP32 から SPI 経由で受信した CMD 0x40 コマンドに応じて各 LED の状態を更新する。

#### ピン構成

| LED | GPIO | 説明 |
|---|---|---|
| D0 | PC10 | デバッグ LED 0 |
| D1 | PC11 | デバッグ LED 1 |
| D2 | PC12 | デバッグ LED 2 |
| D3 | PC13 | デバッグ LED 3 |
| D4 | PC14 | デバッグ LED 4 |
| D5 | PC15 | デバッグ LED 5 |

#### LED モード定義

```c
typedef enum {
    DBG_LED_OFF    = 0x00,
    DBG_LED_ON     = 0x01,
    DBG_LED_BLINK_1HZ = 0x02,
    DBG_LED_BLINK_4HZ = 0x03,
} DebugLedMode_t;

typedef struct {
    GPIO_TypeDef   *port;
    uint16_t        pin;
    DebugLedMode_t  mode;
    uint8_t         state;       // 現在の出力状態 (0/1)
} DebugLed_t;
```

#### 主要関数

| 関数 | シグネチャ | 説明 |
|---|---|---|
| `DebugLed_Init` | `void DebugLed_Init(void)` | PC10〜PC15 を GPIO 出力に初期化、全 LED OFF |
| `DebugLed_Set` | `void DebugLed_Set(uint8_t led_mask, DebugLedMode_t mode)` | ビットマスクで指定した LED のモード設定 |
| `DebugLed_Update` | `void DebugLed_Update(void)` | 点滅タイマ処理（メインループ内で周期的に呼出） |
| `DebugLed_AllOff` | `void DebugLed_AllOff(void)` | 全 LED 消灯 |

#### コマンドハンドラ

```c
static void handle_debug_led(const Packet_t *pkt)
{
    if (pkt->length < 2) return;
    uint8_t led_mask = pkt->payload[0];
    DebugLedMode_t mode = (DebugLedMode_t)pkt->payload[1];
    DebugLed_Set(led_mask, mode);
}
```

#### 主要関数

| 関数 | シグネチャ | 説明 |
|---|---|---|
| `UART_Init` | `void UART_Init(void)` | USART2 + DMA 初期化 |
| `UART_Send` | `HAL_StatusTypeDef UART_Send(const Packet_t *p)` | パケット送信 |
| `UART_Dispatch` | `void UART_Dispatch(const uint8_t *buf, uint8_t len)` | 受信ハンドラ呼出 |
| `UART_CheckWatchdog` | `void UART_CheckWatchdog(void)` | 通信途絶1秒検知→全停止 |

---

### 3.4 `motor_dc` — DC モーター制御 (TB6575)

#### ピン構成

| 信号 | MCU ピン | 方向 |
|---|---|---|
| PWM | PB0 (TIM3_CH3) | OUT |
| FST | PB1 | OUT |
| CW_CCW | PB2 | OUT |
| FMAX | PB3 | OUT |
| FGOUT | PB4 (TIM IC) | IN |

#### 状態遷移（方向転換）

```
RUNNING_CW
    │ 逆転指令
    ▼
DECELERATING  (0まで減速, 最大100ms)
    │
    ▼
STOPPED       (20ms 待機)
    │
    ▼
RUNNING_CCW
```

#### 主要関数

| 関数 | シグネチャ | 説明 |
|---|---|---|
| `MotorDC_Init` | `void MotorDC_Init(void)` | TIM3_CH3 PWM + GPIO 初期化 |
| `MotorDC_SetSpeed` | `void MotorDC_SetSpeed(uint8_t dir, uint16_t speed)` | dir=0:CW, 1:CCW, speed=0-1000 |
| `MotorDC_Stop` | `void MotorDC_Stop(void)` | デューティ 0 → 停止 |
| `MotorDC_GetRPM` | `uint16_t MotorDC_GetRPM(void)` | FGOUT パルスから RPM 算出 |
| `MotorDC_Update` | `void MotorDC_Update(void)` | 台形加減速・方向転換シーケンス処理 |

---

### 3.5 `motor_stepper` — ステッピングモーター制御 (TB6608)

#### ピン構成

| 信号 | MCU ピン | 軸 |
|---|---|---|
| CK_A | PB10 | A |
| CK_B | PB11 | B |
| CW_CCW_A | PB12 | A |
| CW_CCW_B | PB13 | B |
| RESET_A | PB14 | A |
| RESET_B | PB15 | B |
| MO_A | PB6 (IN) | A |
| MO_B | PB7 (IN) | B |

#### パルス生成（タイマ割込）

- TIM を使用してパルスをタイマ割込で生成
- メインループをブロックしない
- 台形加減速: 加速→定速→減速の 3 フェーズ

#### 主要関数

| 関数 | シグネチャ | 説明 |
|---|---|---|
| `Stepper_Init` | `void Stepper_Init(void)` | GPIO + タイマ初期化 |
| `Stepper_Move` | `void Stepper_Move(StepperAxis_t axis, int32_t steps, uint16_t speed)` | 指定ステップ移動（非ブロッキング） |
| `Stepper_Stop` | `void Stepper_Stop(StepperAxis_t axis)` | 即時停止 |
| `Stepper_Home` | `void Stepper_Home(StepperAxis_t axis)` | 原点復帰 |
| `Stepper_GetStatus` | `StepperStatus_t Stepper_GetStatus(StepperAxis_t axis)` | 動作状態取得 |
| `Stepper_IRQHandler` | `void Stepper_IRQHandler(void)` | タイマ割込ハンドラ（パルス生成） |

---

### 3.6 `sensor_mpu6050` — 加速度・ジャイロセンサ (MPU6050)

#### I2C 設定

| 項目 | 値 |
|---|---|
| I2C アドレス | 0x68 (AD0=LOW) または 0x69 |
| サンプリング | 100Hz (10ms 周期) |
| 通信速度 | Fast mode (400kHz) |

#### センサデータ構造体

```c
typedef struct {
    int16_t accel_x, accel_y, accel_z;  // 加速度 (raw)
    int16_t gyro_x,  gyro_y,  gyro_z;   // ジャイロ (raw)
    int16_t temp;                         // 温度 (raw)
    float   roll, pitch;                  // 姿勢角 (deg) - 相補フィルタ
} MPU6050_Data_t;
```

#### 主要関数

| 関数 | シグネチャ | 説明 |
|---|---|---|
| `MPU6050_Init` | `HAL_StatusTypeDef MPU6050_Init(void)` | I2C 疎通確認 + レジスタ設定 |
| `MPU6050_Read` | `HAL_StatusTypeDef MPU6050_Read(MPU6050_Data_t *data)` | 全データ取得 |
| `MPU6050_Calibrate` | `void MPU6050_Calibrate(void)` | オフセット補正（静止状態で実行） |
| `MPU6050_UpdateAttitude` | `void MPU6050_UpdateAttitude(MPU6050_Data_t *data, float dt)` | 相補フィルタ（roll/pitch更新） |
| `MPU6050_CheckFallDetect` | `bool MPU6050_CheckFallDetect(const MPU6050_Data_t *data)` | 転倒検知（閾値判定） |

---

### 3.7 `led_control` — ヘッドライト LED 制御

#### ピン構成

| 信号 | MCU ピン | 説明 |
|---|---|---|
| FRONT_LED_R | PA0 (TIM PWM) | 右ヘッドライト |
| FRONT_LED_L | PA1 (TIM PWM) | 左ヘッドライト |

#### LED モード定義

```c
typedef enum {
    LM_OFF      = 0x00,
    LM_LOW      = 0x01,
    LM_HIGH     = 0x02,
    LM_AUTO     = 0x03,
    LM_ADAPTIVE = 0x04,
    LM_BRAKE    = 0x05,
    LM_HAZARD   = 0x06,
    LM_TURN_L   = 0x07,
    LM_TURN_R   = 0x08,
    LM_BREATH   = 0x09,
    LM_STROBE   = 0x0A,
    LM_WELCOME  = 0x0B,
    LM_BATTERY  = 0x0C,
} LedMode_t;
```

#### 主要関数

| 関数 | シグネチャ | 説明 |
|---|---|---|
| `LED_Init` | `void LED_Init(void)` | TIM PWM 初期化 |
| `LED_SetMode` | `void LED_SetMode(LedMode_t mode)` | モード設定 |
| `LED_SetBrightness` | `void LED_SetBrightness(LedSide_t side, uint8_t brightness)` | 輝度設定 (0-100%) |
| `LED_Update` | `void LED_Update(void)` | 点滅タイマ・フェード処理（ループ内） |
| `LED_WelcomeSequence` | `void LED_WelcomeSequence(void)` | 起動演出（1回のみ） |

---

## 4. 通信シーケンス図

### 4.1 通常コマンド送受信

```
ESP32                           STM32
  │                               │
  │── CMD 0x01 (モーター速度) ───→│
  │                               │ PWM 更新
  │←── CMD 0x80 (ACK) ───────────│
  │                               │
```

### 4.2 センサデータ取得

```
ESP32                           STM32
  │                               │
  │── CMD 0x20 (センサ要求) ─────→│
  │                               │ MPU6050 読取
  │←── CMD 0x91 (センサ応答) ────│
  │    [ax_H][ax_L]...[gz_L]      │
```

### 4.3 転倒検知フロー

```
STM32 (MPU6050)                 ESP32
  │ 転倒検知                     │
  │── CMD 0x92 (転倒通知) ──────→│
  │                               │ LED警告 + BT通知
  全モーター即時停止              │
```

### 4.4 シャットダウンシーケンス

```
ESP32                           STM32
  │── CMD 0x30 (シャットダウン) →│

```

### 4.5 SPI デバッグ LED 制御

```
ESP32 (SPI Master)              STM32 (SPI Slave)
  │                               │
  │  NSS Low                      │
  │── SPI CMD 0x40 ─────────────→│
  │   [0xAA][0x40][0x02]          │
  │   [led_mask][mode][checksum]  │
  │                               │ DebugLed_Set(led_mask, mode)
  │←── SPI ACK 0x80 ────────────│
  │  NSS High                     │
```
  │                               │ モーター停止
  │                               │ LED消灯
  │←── CMD 0xF0 (ACK) ──────────│
  │ (タイムアウト 3秒)            │
  │                               │
  GPIO_POW_ON = LOW              │
  (電源遮断)                      │
```

---

## 5. タスク/割込優先度（STM32）

| 優先度 | 処理 | 実装 |
|---|---|---|
| 最高 (0) | 緊急停止 (EStop) | EXTI or UART割込 |
| 高 (1) | ステッパパルス生成 | TIM 割込 |
| 高 (1) | USART2 受信 DMA | DMA 割込 |
| 中 (2) | MPU6050 読取 (100Hz) | TIM 周期割込 |
| 中 (2) | DC モーター更新 | TIM 周期割込 |
| 低 (3) | LED 更新 | メインループ / TIM |
| 低 (4) | IWDG リフレッシュ | メインループ |

---

## 6. メモリ・リソース見積もり（STM32G473RCTx）

| リソース | 見積もり | 上限 |
|---|---|---|
| Flash | 〜64KB | 256KB |
| RAM (SRAM) | 〜20KB | 96KB |
| タイマ使用数 | 4 (TIM1,3,4,6) | 11 |
| I2C チャネル | 1 | 3 |
| SPI チャネル | 1 (Slave) | 3 |
| USART チャネル | 1 (USART2) | 5 |
| DMA チャネル | 4 (USART2 RX/TX + SPI1 RX/TX) | 16 |

---

## 7. 変更履歴

| バージョン | 日付 | 変更内容 |
|---|---|---|
| 1.0 | 2026-03-21 | 初版作成 |
