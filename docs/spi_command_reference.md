# SPI コマンドリファレンス

## 概要

ESP32 → STM32 間の SPI スレーブ通信で使用するコマンド一覧。  
パケットフォーマット: `[Header(0xAA)] [CMD] [Length] [Payload...] [Checksum(XOR)]`

## コマンド一覧

### マスター → スレーブ（ESP32 → STM32）

| CMD ID | 定義名 | 説明 | Payload | 現在の実装状態 |
|--------|--------|------|---------|---------------|
| `0x01` | `CMD_MOTOR_SPEED` | DC モーター速度設定 | (未定義) | 未実装 (weak stub) |
| `0x02` | `CMD_MOTOR_STOP` | DC モーター停止 | (未定義) | 未実装 (weak stub) |
| `0x03` | `CMD_STEPPER_CTRL` | ステッピングモーター制御 | (未定義) | **暫定実装**: LED3 を点灯する |
| `0x04` | `CMD_EMERGENCY_STOP` | 緊急停止 | (未定義) | 未実装 (weak stub) |
| `0x10` | `CMD_STATUS_REQ` | ステータス要求 | (未定義) | 未実装 (weak stub) |
| `0x11` | `CMD_LED_MODE` | ヘッドライト LED モード設定 | 後述 | 未実装 (weak stub) |
| `0x12` | `CMD_LED_BRIGHTNESS` | ヘッドライト LED 輝度設定 | 後述 | 未実装 (weak stub) |
| `0x20` | `CMD_SENSOR_REQ` | センサー値要求 | (未定義) | 未実装 (weak stub) |
| `0x30` | `CMD_SHUTDOWN` | シャットダウン要求 | (未定義) | 未実装 (weak stub) |
| `0x40` | `CMD_DEBUG_LED` | デバッグ LED 制御 | 後述 | **実装済み** |

### スレーブ → マスター（STM32 → ESP32）応答

| CMD ID | 定義名 | 説明 | Payload |
|--------|--------|------|---------|
| `0x80` | `CMD_ACK` | コマンド正常受理 | なし (length=0) |
| `0x81` | `CMD_NACK` | コマンド拒否/エラー | `[0]` エラーコード (0x01=不明CMD, 0x02=パースエラー) |
| `0x90` | `CMD_STATUS_RESP` | ステータス応答 | (未定義) |
| `0x91` | `CMD_SENSOR_RESP` | センサー値応答 | (未定義) |
| `0x92` | `CMD_FALL_NOTIFY` | 落下検知通知 | (未定義) |
| `0xF0` | `CMD_SHUTDOWN_ACK` | シャットダウン応答 | (未定義) |

---

## SPI 経由で LED を制御するコマンド詳細

### CMD_DEBUG_LED (0x40) — デバッグ LED 制御

6 個のデバッグ LED (PC10〜PC15) を個別に制御する。

#### Payload 構造

| Byte | フィールド | 説明 |
|------|-----------|------|
| `payload[0]` | `led_mask` | 制御対象の LED ビットマスク (bit0〜bit5)。上位 2bit は無視される |
| `payload[1]` | `mode` | LED モード (省略時 = `0x01` ON) |

#### led_mask ビット割当

| Bit | LED 番号 | GPIO ピン |
|-----|----------|----------|
| bit 0 | LED0 | PC10 |
| bit 1 | LED1 | PC11 |
| bit 2 | LED2 | PC12 |
| bit 3 | LED3 | PC13 |
| bit 4 | LED4 | PC14 |
| bit 5 | LED5 | PC15 |

#### mode 値

| 値 | 定義名 | 動作 |
|----|--------|------|
| `0x00` | `DBG_LED_OFF` | 消灯 |
| `0x01` | `DBG_LED_ON` | 常時点灯 |
| `0x02` | `DBG_LED_BLINK_1HZ` | 1Hz 点滅 (500ms ON / 500ms OFF) |
| `0x03` | `DBG_LED_BLINK_4HZ` | 4Hz 点滅 (125ms ON / 125ms OFF) |

> `mode` が `0x03` より大きい場合、コマンドは無視される（ACK は返るがLED状態は変化しない）。

#### 送信例

LED0 と LED2 を 1Hz 点滅させる:
```
0xAA 0x40 0x02 0x05 0x02 [checksum]
```
- `0xAA` = Header
- `0x40` = CMD_DEBUG_LED
- `0x02` = Length (payload 2 bytes)
- `0x05` = led_mask (bit0 + bit2 = LED0 + LED2)
- `0x02` = mode (BLINK_1HZ)
- `checksum` = `0x40 ^ 0x02 ^ 0x05 ^ 0x02` = `0x45`

### CMD_STEPPER_CTRL (0x03) — ステッピングモーター制御（暫定）

現在の暫定実装では、このコマンドを受信すると **LED3 (PC13) が常時点灯** になる。  
ステッピングモーター制御の本実装後はこの LED 動作は撤去される見込み。

---

## フロントLED（ヘッドライト）制御コマンド詳細

フロント LED は PA0 (右 = FRONT_LED_R) と PA1 (左 = FRONT_LED_L) に接続され、TIM PWM で調光制御される。

### CMD_LED_MODE (0x11) — ヘッドライト LED モード設定

ヘッドライトのライティングモードを切り替える。

#### Payload 構造

| Byte | フィールド | 説明 |
|------|-----------|------|
| `payload[0]` | `mode_id` | ライティングモード ID |
| `payload[1]` | `param1` | モード固有パラメータ 1（モードにより意味が異なる） |
| `payload[2]` | `param2` | モード固有パラメータ 2（モードにより意味が異なる） |

#### mode_id 一覧

| 値 | 定義名 | 動作 | 優先度 |
|----|--------|------|--------|
| `0x00` | `LM_OFF` | 両 LED 消灯 | 必須 |
| `0x01` | `LM_LOW` | ロービーム（両 LED 30% 点灯） | 必須 |
| `0x02` | `LM_HIGH` | ハイビーム（両 LED 100% 点灯） | 必須 |
| `0x03` | `LM_AUTO` | オートライト（MPU6050 の姿勢角に応じて自動調光） | 推奨 |
| `0x04` | `LM_ADAPTIVE` | アダプティブ（モーター速度に連動して調光） | 推奨 |
| `0x05` | `LM_BRAKE` | ブレーキライト（減速検知時に自動で全灯） | 必須 |
| `0x06` | `LM_HAZARD` | ハザード（左右同時点滅、500ms 周期） | 必須 |
| `0x07` | `LM_TURN_L` | 左ウインカー（左 LED 点滅 / 右 LED 通常） | 推奨 |
| `0x08` | `LM_TURN_R` | 右ウインカー（右 LED 点滅 / 左 LED 通常） | 推奨 |
| `0x09` | `LM_BREATH` | ブリージング（正弦波で明滅） | 任意 |
| `0x0A` | `LM_STROBE` | ストロボ（高速点滅、100ms 周期） | 任意 |
| `0x0B` | `LM_WELCOME` | ウェルカム（起動時の演出パターン、1 回のみ） | 任意 |
| `0x0C` | `LM_BATTERY` | バッテリー表示（点滅回数でバッテリー残量通知） | 推奨 |

#### モード遷移規則

コントローラー △ボタン短押しで以下の順に遷移する:
```
OFF → LOW → HIGH → AUTO → ADAPTIVE → OFF ...
```

自動割込モード（優先的に発動し、解除後は元のモードに自動復帰）:
- **LM_BRAKE** : 減速検知で自動発動 → 加速再開で復帰
- **LM_HAZARD** : ○ボタンでトグル
- **LM_WELCOME** : 電源 ON 時に 1 回だけ自動実行
- **LM_BATTERY** : バッテリー低下時に通常モードに重畳

#### 送信例

ハイビームに設定する:
```
0xAA 0x11 0x03 0x02 0x00 0x00 [checksum]
```
- `0xAA` = Header
- `0x11` = CMD_LED_MODE
- `0x03` = Length (payload 3 bytes)
- `0x02` = mode_id (LM_HIGH)
- `0x00` = param1 (未使用)
- `0x00` = param2 (未使用)
- `checksum` = `0x11 ^ 0x03 ^ 0x02 ^ 0x00 ^ 0x00` = `0x10`

ハザードを有効にする:
```
0xAA 0x11 0x03 0x06 0x00 0x00 [checksum]
```
- `0x06` = mode_id (LM_HAZARD)
- `checksum` = `0x11 ^ 0x03 ^ 0x06 ^ 0x00 ^ 0x00` = `0x14`

### CMD_LED_BRIGHTNESS (0x12) — ヘッドライト LED 輝度設定

個別の LED の PWM 輝度を直接設定する。モードに関わらず即時反映される。

#### Payload 構造

| Byte | フィールド | 説明 |
|------|-----------|------|
| `payload[0]` | `led_id` | 対象 LED |
| `payload[1]` | `brightness` | 輝度 0〜100（%） |

#### led_id 一覧

| 値 | 対象 | GPIO ピン |
|----|------|----------|
| `0x00` | 右ヘッドライト (FRONT_LED_R) | PA0 |
| `0x01` | 左ヘッドライト (FRONT_LED_L) | PA1 |
| `0xFF` | 両方同時 | PA0 + PA1 |

#### 送信例

左ヘッドライトを 50% 輝度に設定する:
```
0xAA 0x12 0x02 0x01 0x32 [checksum]
```
- `0xAA` = Header
- `0x12` = CMD_LED_BRIGHTNESS
- `0x02` = Length (payload 2 bytes)
- `0x01` = led_id (左ヘッドライト)
- `0x32` = brightness (50%)
- `checksum` = `0x12 ^ 0x02 ^ 0x01 ^ 0x32` = `0x23`

両方を 100% にする:
```
0xAA 0x12 0x02 0xFF 0x64 [checksum]
```
- `0xFF` = led_id (両方)
- `0x64` = brightness (100%)
- `checksum` = `0x12 ^ 0x02 ^ 0xFF ^ 0x64` = `0x8B`

### フロント LED ハードウェア仕様

| 項目 | 値 |
|------|-----|
| 制御方式 | TIM PWM 調光 |
| 右 LED (FRONT_LED_R) | PA0、100Ω 直列抵抗 |
| 左 LED (FRONT_LED_L) | PA1、100Ω 直列抵抗 |
| PWM 分解能 | 0〜100%（TIM ARR 設定に依存） |

---

## 起動時の LED 初期状態

SPI コマンド受信前に `main.c` の初期化で以下が設定される:

| LED | モード | 目的 |
|-----|--------|------|
| LED0 (PC10) | 4Hz 点滅 | メインループ動作確認 |
| LED1 (PC11) | 常時点灯 | 全初期化完了マーカー |

---

## SPI 通信プロトコル

- ESP32 は `[コマンド(4-36 bytes)] + [0x00 パディング(8 bytes 以上)]` を 1 つの CS アサーション内で送信
- STM32 はコマンド受信完了後、パディング期間中に MISO で ACK/NACK を応答
- ESP32 はパディング中の MISO バイトから `0xAA` ヘッダを探して応答を取得
- ポーリング方式（割込み・DMA 不使用）
