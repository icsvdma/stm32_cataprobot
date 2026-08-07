# SPI コマンドリファレンス

[プロジェクトREADME](../README.md) | [資料一覧](README.md) | [コードマップ](コードマップ.md)

## 1. 通信条件

| 項目 | 仕様 |
|---|---|
| Master / Slave | ESP32 / STM32G473 |
| クロック | 1 MHz |
| モード | Mode 0（CPOL=0、CPHA=0） |
| ビット順 | MSB first |
| パケット | `[0xAA][CMD][LEN][PAYLOAD...][CHECKSUM]` |
| CHECKSUM | `CMD`、`LEN`、`PAYLOAD`のXOR |
| 応答用padding | 64 byte |

## 2. 使用コマンド

| CMD | 名前 | 方向 | Payload | 応答 |
|---:|---|---|---|---|
| `0x01` | `CMD_MOTOR_SPEED` | ESP32→STM32 | 4 byte | `ACK/NACK` |
| `0x02` | `CMD_MOTOR_STOP` | ESP32→STM32 | 1 byte | `ACK/NACK` |
| `0x04` | `CMD_EMERGENCY_STOP` | ESP32→STM32 | なし | `ACK/NACK` |
| `0x11` | `CMD_LED_MODE` | ESP32→STM32 | 1 byte | `ACK/NACK` |
| `0x12` | `CMD_LED_BRIGHTNESS` | ESP32→STM32 | 2 byte | `ACK/NACK` |
| `0x20` | `CMD_SENSOR_REQ` | ESP32→STM32 | 1 byte | `CMD_SENSOR_RESP` |
| `0x30` | `CMD_SHUTDOWN` | ESP32→STM32 | なし | `CMD_SHUTDOWN_ACK` |
| `0x80` | `CMD_ACK` | STM32→ESP32 | 元CMD 1 byte | - |
| `0x81` | `CMD_NACK` | STM32→ESP32 | 元CMD、エラーコード | - |
| `0x91` | `CMD_SENSOR_RESP` | STM32→ESP32 | 32 byte | - |
| `0xF0` | `CMD_SHUTDOWN_ACK` | STM32→ESP32 | なし | - |

## 3. モーターコマンド

### 3.1 速度設定 `0x01`

| Offset | 内容 |
|---:|---|
| 0 | チャンネル: `0x00=A`、`0x01=B`、`0xFF=両方` |
| 1 | 方向: `0x00=正転`、`0x01=逆転`、`0x02=ブレーキ` |
| 2～3 | 速度`0～1000`、ビッグエンディアン |

STM32は2ch Hブリッジモータードライバ回路へ20 kHz PWMを出力する。方向反転時は現在速度を0まで減速し、100 msのデッドタイム後に方向を切り替える。

### 3.2 通常停止 `0x02`

Payloadは停止対象チャンネル1 byte。`0x00=A`、`0x01=B`、`0xFF=両方`とする。

### 3.3 緊急停止 `0x04`

Payloadなし。両チャンネルを直ちに停止状態へ遷移させる。

### 3.4 シャットダウン `0x30`

Payloadなし。STM32は両モーターを緊急停止し、通常LEDとデバッグLEDを消灯してから、Payloadなしの`CMD_SHUTDOWN_ACK(0xF0)`を返す。

## 4. ヘッドライトコマンド

### 4.1 光量設定 `CMD_LED_BRIGHTNESS(0x12)`

Payload長は2 byteとする。

| Offset | 内容 |
|---:|---|
| 0 | `0x00=右`、`0x01=左`、`0xFF=左右両方` |
| 1 | PWM光量`0〜100%` |

STM32はTIM2 CH1/CH2の1 kHz PWMへ反映する。範囲外のside、光量、Payload長はエラーコード`0x02`のNACKとする。

PS3の▲操作では`[0xFF][10]`、`[0xFF][25]`、`[0xFF][50]`、`[0xFF][70]`、`[0xFF][0]`の順で送信する。

## 5. センサーコマンド

### 5.1 要求 `CMD_SENSOR_REQ(0x20)`

| Offset | 内容 |
|---:|---|
| 0 | `0xFF`（全センサー） |

ESP32は100 ms周期で要求する。STM32は要求処理中にI2Cアクセスせず、外部センサー管理が保持する最新スナップショットを返す。

### 5.2 応答 `CMD_SENSOR_RESP(0x91)`

Payload長は固定32 byte、形式バージョンは`0x02`、複数byte値はビッグエンディアン。

| Offset | 型 | 単位 | 内容 |
|---:|---|---|---|
| 0 | `uint8_t` | - | 形式バージョン`0x02` |
| 1 | `uint8_t` | - | 状態フラグ |
| 2 | `uint16_t` | - | シーケンス番号 |
| 4 | `uint32_t` | ms | STM32取得時刻 |
| 8 | `int16_t` | mg | 加速度X |
| 10 | `int16_t` | mg | 加速度Y |
| 12 | `int16_t` | mg | 加速度Z |
| 14 | `int16_t` | 0.1 deg/s | ジャイロX |
| 16 | `int16_t` | 0.1 deg/s | ジャイロY |
| 18 | `int16_t` | 0.1 deg/s | ジャイロZ |
| 20 | `int16_t` | 0.1 µT | 地磁気X |
| 22 | `int16_t` | 0.1 µT | 地磁気Y |
| 24 | `int16_t` | 0.1 µT | 地磁気Z |
| 26 | `int16_t` | 0.01 ℃ | 温度 |
| 28 | `uint32_t` | Pa | 気圧 |

状態フラグ:

| bit | 値 | 内容 |
|---:|---:|---|
| 0 | `0x01` | 加速度値が有効 |
| 1 | `0x02` | 温度・気圧値が有効 |
| 2 | `0x04` | 加速度エラー |
| 3 | `0x08` | BMP280エラー |
| 4 | `0x10` | ジャイロ値が有効 |
| 5 | `0x20` | ジャイロエラー |
| 6 | `0x40` | 地磁気値が有効 |
| 7 | `0x80` | 地磁気エラー |

## 6. 応答処理

- STM32は受信パケットのヘッダー、長さ、チェックサムを検証する。
- 未知コマンド、長さ異常、値範囲外は`NACK`とする。
- `CMD_ACK(0x80)`のPayloadは元CMD 1 byteとする。
- `CMD_NACK(0x81)`のPayloadは`[元CMD][エラーコード]`の2 byteとする。
- エラーコード`0x01`は未知コマンド、`0x02`はパケット解析失敗、長さ異常、値範囲外を表す。
- ESP32はモーター送信とセンサー要求をmutexで直列化する。
- センサー応答異常時はWeb表示をオフライン扱いにするが、モーター制御を停止しない。

## 7. 変更履歴

| バージョン | 日付 | 内容 |
|---|---|---|
| 2.0 | 2026-07-23 | 現行Hブリッジ制御とセンサー応答の初期仕様を追加 |
| 2.1 | 2026-07-23 | ACK/NACK、シャットダウン、100 ms反転待ち、ブレーキ停止の実装仕様へ更新 |
| 2.2 | 2026-07-23 | ▲操作で使用する左右ヘッドライトPWM光量シーケンスを追加 |
| 3.0 | 2026-07-24 | センサー応答をVersion 2、32 byte、9軸へ更新 |
