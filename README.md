# stm32_cataprobot

Bluetooth コントローラー操作対応の **デュアルMCUモーター制御システム** です。
ESP32 で Bluetooth 入力・電源管理を担い、STM32G473RCTx でリアルタイムモーター制御・センサ取得・LED 制御を行います。

---

## システム概要

```
Bluetooth コントローラー (PS4/PS5 / BLE ゲームパッド)
           │
           │ Bluetooth SPP / BLE HID
           ▼
  ┌─────────────────┐
  │   ESP32-WROOM   │  ← 上位層（通信・監視・電源管理）
  │                 │
  │ ・BT受信        │
  │ ・バッテリー監視│
  │ ・電源保持制御  │
  └───────┬─────────┘
          │ UART2 (115200bps) / SPI1 (冗長路)
          ▼
  ┌─────────────────┐
  │ STM32G473RCTx   │  ← 下位層（リアルタイム制御）
  │                 │
  │ ・DCモーター    │──→ TB6575
  │ ・ステッパ制御  │──→ TB6608 (2軸)
  │ ・姿勢センサ    │──→ MPU6050 (I2C)
  │ ・ヘッドライト  │──→ FRONT_LED (PWM)
  └─────────────────┘
```

---

## リポジトリ構成

```
stm32_cataprobot/
├── README.md                       # 本ファイル
├── docs/
│   ├── required_spec.md            # 要件定義書
│   ├── implementation_plan.md      # コード実装計画書
│   ├── code_design.md              # コード設計書（モジュール・API仕様）
│   └── hardware_spec_template.md   # ハードウェア仕様テンプレート
│
├── esp32/                          # ESP32 ファームウェア (PlatformIO)
│   ├── platformio.ini
│   └── src/
│       ├── main.cpp
│       ├── config.h                # ピン定義・定数
│       ├── battery/                # バッテリー監視 (ADC)
│       ├── power/                  # 電源保持・シャットダウン
│       ├── communication/          # UART2 / SPI1 / パケット
│       ├── bluetooth/              # BT コントローラー入力
│       └── led/                    # デバッグ LED
│
└── stm32/                          # STM32 ファームウェア (STM32CubeIDE)
    ├── STM32G473RCTx.ioc           # CubeMX 設定
    └── Core/
        ├── Inc/                    # ヘッダファイル
        └── Src/                    # ソースファイル
            ├── motor_dc.c          # TB6575 DC モーター制御
            ├── motor_stepper.c     # TB6608 ステッピングモーター制御
            ├── sensor_mpu6050.c    # MPU6050 I2C センサ
            ├── led_control.c       # ヘッドライト LED (PWM)
            ├── uart_comm.c         # USART2 通信
            └── packet.c            # パケット構造体・チェックサム
```

---

## ドキュメント一覧

| ドキュメント | パス | 内容 |
|---|---|---|
| 要件定義書 | [docs/required_spec.md](docs/required_spec.md) | 機能要件・通信仕様・ピン割当・開発フェーズ |
| コード実装計画書 | [docs/implementation_plan.md](docs/implementation_plan.md) | フェーズ別タスク・テスト計画・リスク管理 |
| コード設計書 | [docs/code_design.md](docs/code_design.md) | モジュール設計・データ構造・API・シーケンス図 |
| ハードウェア仕様テンプレート | [docs/hardware_spec_template.md](docs/hardware_spec_template.md) | HW仕様書記入テンプレート |

---

## 開発フェーズ

| フェーズ | 内容 | 目安期間 |
|---|---|---|
| Phase 1 | 基盤確立（UART 疎通・パケットプロトコル） | 2週間 |
| Phase 2 | モーター制御（DC + ステッピング） | 2週間 |
| Phase 3 | センサ・LED 制御（MPU6050 + ヘッドライト） | 2週間 |
| Phase 4 | Bluetooth 統合（コントローラー操作） | 2週間 |
| Phase 5 | 統合・安全・最適化（バッテリー・WDT・SPI） | 2週間 |

詳細は [docs/implementation_plan.md](docs/implementation_plan.md) を参照してください。

---

## 通信プロトコル（UART パケット）

```
Byte:  [0]      [1]     [2]       [3..N]      [N+1]
       Header   CMD     Length    Payload     Checksum
       0xAA     1byte   1byte    0-32bytes   XOR(CMD..Payload)
```

主要コマンド例:

| CMD | 方向 | 内容 |
|---|---|---|
| 0x01 | ESP32→STM32 | モーター速度設定 |
| 0x04 | ESP32→STM32 | 緊急停止 |
| 0x11 | ESP32→STM32 | LED モード設定 |
| 0x30 | ESP32→STM32 | シャットダウン通知 |
| 0x80 | STM32→ESP32 | ACK |
| 0x92 | STM32→ESP32 | 転倒検知通知 |

全コマンド一覧は [docs/required_spec.md](docs/required_spec.md) を参照。

---

## 安全機能

- **BT 切断検知** → 500ms 以内に全モーター停止
- **通信途絶タイムアウト** (1秒) → 全モーター停止
- **転倒検知** (MPU6050) → 全モーター即時停止
- **バッテリー危険電圧** (< 9.5V) → シャットダウンシーケンス
- **ウォッチドッグタイマ** → 両MCU で有効

---

## ライセンス

（ライセンスを記入）
