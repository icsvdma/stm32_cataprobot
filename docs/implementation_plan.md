# コード実装計画書
## デュアルMCUモーター制御システム（ESP32 + STM32）

| 項目 | 内容 |
|---|---|
| ドキュメントバージョン | 1.0 |
| 作成日 | 2026-03-21 |
| ステータス | ドラフト |
| 参照元 | docs/required_spec.md |

---

## 1. 目的

本文書は `docs/required_spec.md` で定義された要件に基づき、ESP32 および STM32 ファームウェアの
実装計画を段階的に定義する。

---

## 2. 技術スタック

| 対象 | 言語 | フレームワーク / SDK | ビルドシステム |
|---|---|---|---|
| ESP32 | C++ | Arduino / ESP-IDF v5.x | PlatformIO |
| STM32 | C | STM32Cube HAL (STM32G4) | STM32CubeIDE / CMake |
| 共通 | — | — | — |

---

## 3. ディレクトリ構成（計画）

```
stm32_cataprobot/
├── README.md
├── docs/
│   ├── required_spec.md          # 要件定義書
│   ├── implementation_plan.md    # 本文書
│   ├── code_design.md            # コード設計書
│   └── hardware_spec_template.md # ハードウェア仕様テンプレート
│
├── esp32/                        # ESP32ファームウェア
│   ├── platformio.ini
│   └── src/
│       ├── main.cpp              # エントリポイント・タスク起動
│       ├── config.h              # グローバル定数・ピン定義
│       ├── battery/
│       │   ├── battery_monitor.h
│       │   └── battery_monitor.cpp
│       ├── power/
│       │   ├── power_manager.h
│       │   └── power_manager.cpp
│       ├── communication/
│       │   ├── packet.h          # パケット構造体・チェックサム
│       │   ├── packet.cpp
│       │   ├── uart_comm.h       # UART2送受信・リトライ
│       │   ├── uart_comm.cpp
│       │   ├── spi_comm.h        # SPI1 Master送受信
│       │   └── spi_comm.cpp
│       ├── bluetooth/
│       │   ├── bt_controller.h   # BT/BLE HID受信・マッピング
│       │   └── bt_controller.cpp
│       └── led/
│           ├── debug_led.h       # デバッグLED制御
│           └── debug_led.cpp
│
└── stm32/                        # STM32ファームウェア
    ├── STM32G473RCTx.ioc         # CubeMX設定ファイル
    └── Core/
        ├── Inc/
        │   ├── config.h          # グローバル定数・ピン定義
        │   ├── packet.h          # パケット構造体（共通定義）
        │   ├── uart_comm.h       # USART2受信・送信
        │   ├── spi_comm.h        # SPI1 Slave受信
        │   ├── debug_led.h       # デバッグLED制御 (PC10-PC15)
        │   ├── motor_dc.h        # TB6575 DCモーター制御
        │   ├── motor_stepper.h   # TB6608 ステッピング制御
        │   ├── sensor_mpu6050.h  # MPU6050 I2C取得
        │   └── led_control.h     # ヘッドライトLED制御
        └── Src/
            ├── main.c
            ├── packet.c
            ├── uart_comm.c
            ├── spi_comm.c
            ├── debug_led.c
            ├── motor_dc.c
            ├── motor_stepper.c
            ├── sensor_mpu6050.c
            └── led_control.c
```

---

## 4. 実装フェーズ詳細

### Phase 1: 基盤確立（目安 2週間）

**目標**: 両MCUの単体起動・UART疎通・パケットプロトコル実装

#### ESP32 タスク

| タスクID | 内容 | 担当モジュール | 優先度 |
|---|---|---|---|
| E1-01 | PlatformIO プロジェクト作成・ピン定義 (`config.h`) | esp32/src/config.h | 必須 |
| E1-02 | デバッグLED起動シーケンス実装 (`debug_led`) | esp32/src/led/ | 必須 |
| E1-03 | パケット構造体・チェックサム関数実装 (`packet`) | esp32/src/communication/ | 必須 |
| E1-04 | UART2 送受信・エコーバックテスト (`uart_comm`) | esp32/src/communication/ | 必須 |
| E1-05 | 電源保持 GPIO 初期化 (`power_manager`) | esp32/src/power/ | 必須 |

#### STM32 タスク

| タスクID | 内容 | 担当モジュール | 優先度 |
|---|---|---|---|
| S1-01 | CubeMX プロジェクト作成・ペリフェラル初期設定 | stm32/STM32G473RCTx.ioc | 必須 |
| S1-02 | パケット構造体・チェックサム関数実装 (`packet`) | stm32/Core/Src/packet.c | 必須 |
| S1-03 | USART2 DMA受信・割込送信 (`uart_comm`) | stm32/Core/Src/uart_comm.c | 必須 |
| S1-04 | ウォッチドッグタイマ (IWDG) 有効化 | stm32/Core/Src/main.c | 必須 |
| S1-05 | SPI1 Slave DMA受信初期化 (`spi_comm`) | stm32/Core/Src/spi_comm.c | 必須 |
| S1-06 | デバッグ LED GPIO初期化・制御実装 (`debug_led`) | stm32/Core/Src/debug_led.c | 必須 |
| S1-07 | SPI受信 → デバッグ LED 制御ハンドラ (CMD 0x40) | stm32/Core/Src/spi_comm.c | 必須 |

#### 完了基準
- [ ] ESP32 からのパケット送信を STM32 が受信し ACK を返す
- [ ] チェックサム不一致時に NACK が返る
- [ ] デバッグLED起動シーケンスが約1秒で完了する
- [ ] ESP32 から SPI 経由で CMD 0x40 を送信し、STM32 デバッグ LED (PC10-PC15) が制御できる
- [ ] デバッグ LED の ON/OFF/1Hz点滅/4Hz点滅 が正しく動作する

---

### Phase 2: モーター制御（目安 2週間）

**目標**: TB6575 DC制御・TB6608 ステッパ制御の実装と UART 経由の遠隔操作

#### ESP32 タスク

| タスクID | 内容 | 担当モジュール | 優先度 |
|---|---|---|---|
| E2-01 | モーター速度設定コマンド送信 (CMD 0x01/0x02) | uart_comm | 必須 |
| E2-02 | ステッピング制御コマンド送信 (CMD 0x03) | uart_comm | 必須 |
| E2-03 | 緊急停止コマンド送信 (CMD 0x04) | uart_comm | 必須 |

#### STM32 タスク

| タスクID | 内容 | 担当モジュール | 優先度 |
|---|---|---|---|
| S2-01 | TB6575 PWM生成 (TIM3_CH3)・方向GPIO制御 | motor_dc.c | 必須 |
| S2-02 | FGOUT 入力キャプチャ (TIM) による RPM 算出 | motor_dc.c | 必須 |
| S2-03 | 方向反転シーケンス（減速→停止→逆転）実装 | motor_dc.c | 必須 |
| S2-04 | TB6608 パルス生成（タイマ割込）・A/B 軸独立制御 | motor_stepper.c | 必須 |
| S2-05 | ステッピング台形加減速プロファイル | motor_stepper.c | 推奨 |
| S2-06 | MO 出力監視による脱調検出 | motor_stepper.c | 推奨 |
| S2-07 | UART コマンドハンドラ実装 (0x01〜0x04) | uart_comm.c | 必須 |

#### 完了基準
- [ ] ESP32 からの UART コマンドで DC モーターの速度・方向を制御できる
- [ ] RPM 値が STM32 → ESP32 のステータス応答 (0x90) に含まれる
- [ ] ステッパが指定ステップ数を正確に動作する

---

### Phase 3: センサ・LED制御（目安 2週間）

**目標**: MPU6050 姿勢取得・転倒検知・ヘッドライト全モード実装

#### STM32 タスク

| タスクID | 内容 | 担当モジュール | 優先度 |
|---|---|---|---|
| S3-01 | MPU6050 I2C 初期化・レジスタ設定 | sensor_mpu6050.c | 必須 |
| S3-02 | 加速度・ジャイロ 3軸 データ取得 (100Hz) | sensor_mpu6050.c | 必須 |
| S3-03 | 相補フィルタによる姿勢角算出 | sensor_mpu6050.c | 推奨 |
| S3-04 | 転倒検知閾値超過時の緊急停止発行 | sensor_mpu6050.c | 必須 |
| S3-05 | センサデータ要求コマンド (0x20) 対応 | uart_comm.c | 推奨 |
| S3-06 | ヘッドライト LED PWM 調光 (TIM) 実装 | led_control.c | 必須 |
| S3-07 | LED モード全実装（LM_OFF〜LM_WELCOME） | led_control.c | 必須/推奨/任意 |
| S3-08 | LED モード設定コマンド (0x11/0x12) 対応 | uart_comm.c | 必須 |

#### 完了基準
- [ ] MPU6050 から加速度・ジャイロ値を取得できる
- [ ] 姿勢角が ±5° 以内で算出できる
- [ ] 転倒検知で全モーター停止する
- [ ] LED 全モードが動作する

---

### Phase 4: Bluetooth 統合（目安 2週間）

**目標**: BT/BLE コントローラーペアリング・入力マッピング・切断検知

#### ESP32 タスク

| タスクID | 内容 | 担当モジュール | 優先度 |
|---|---|---|---|
| E4-01 | BT Classic SPP or BLE HID ライブラリ選定・初期化 | bt_controller.cpp | 必須 |
| E4-02 | ペアリング・接続管理（再接続タイムアウト 30秒） | bt_controller.cpp | 必須 |
| E4-03 | アナログスティック → モーター速度マッピング（デッドゾーン付き） | bt_controller.cpp | 必須 |
| E4-04 | ボタンマッピング（緊急停止・LED 切替・シャットダウン等） | bt_controller.cpp | 必須 |
| E4-05 | 切断 500ms 以内検知 → 安全停止コマンド送信 | bt_controller.cpp | 必須 |
| E4-06 | コントローラーバッテリー残量取得 (BLE HID) | bt_controller.cpp | 任意 |

#### 完了基準
- [ ] PS4/PS5 またはBLE ゲームパッドでモーターを制御できる
- [ ] BT 切断時に 500ms 以内で全モーター停止する
- [ ] 再接続が自動的に行われる

---

### Phase 5: 統合・安全・最適化（目安 2週間）

**目標**: バッテリー監視・シャットダウン・WDT・フェイルセーフ・SPI 冗長路・全体結合テスト

#### ESP32 タスク

| タスクID | 内容 | 担当モジュール | 優先度 |
|---|---|---|---|
| E5-01 | バッテリー ADC 定期取得・残量算出 | battery_monitor.cpp | 必須 |
| E5-02 | 低電圧/危険電圧閾値検知・警告/シャットダウン | battery_monitor.cpp | 必須 |
| E5-03 | シャットダウンシーケンス実装 (CMD 0x30 → ACK 0xF0) | power_manager.cpp | 必須 |
| E5-04 | 通信途絶タイムアウト（1秒）検知→停止コマンド | uart_comm.cpp | 必須 |
| E5-05 | SPI 冗長通信路実装 | spi_comm.cpp | 推奨 |
| E5-06 | ウォッチドッグタイマ有効化 (ESP32) | main.cpp | 必須 |
| E5-07 | 通信エラー率集計・ログ出力 | uart_comm.cpp | 推奨 |

#### STM32 タスク

| タスクID | 内容 | 担当モジュール | 優先度 |
|---|---|---|---|
| S5-01 | SPI 冗長通信路実装（UART障害時のフォールバック） | spi_comm.c | 推奨 |
| S5-02 | 通信途絶タイムアウト（1秒）監視→全停止 | uart_comm.c | 必須 |
| S5-03 | IWDG リフレッシュ管理 | main.c | 必須 |

#### 完了基準
- [ ] バッテリー電圧 9.5V 未満でシャットダウンシーケンスが動作する
- [ ] BT 切断・通信途絶どちらの場合も全モーター停止する
- [ ] 全体結合テストでシステムが正常動作する

---

## 5. 共通設計方針

### 5.1 パケットプロトコル（UART）

```
Byte:  [0]      [1]     [2]       [3..N]      [N+1]
       Header   CMD     Length    Payload     Checksum
       0xAA     1byte   1byte    0-32bytes   XOR(Byte[1..N])
```

- チェックサム: `Byte[1]` から `Byte[N]` の XOR
- 最大ペイロード: 32 バイト
- ACK 未受信時: 最大 3 回リトライ（200ms タイムアウト）

### 5.2 エラーハンドリング方針

| レベル | 対応 |
|---|---|
| チェックサム不一致 | NACK 送信・リトライ |
| 3回リトライ失敗 | 通信エラーログ・フェイルセーフ |
| 転倒検知 | 全モーター即時停止・STM32→ESP32 通知 (0x92) |
| バッテリー危険電圧 | シャットダウンシーケンス開始 |
| WDT タイムアウト | システムリセット |

### 5.3 コーディング規約

| 項目 | ESP32 (C++) | STM32 (C) |
|---|---|---|
| 命名 | camelCase (変数)、PascalCase (クラス) | snake_case |
| ヘッダガード | `#pragma once` | `#ifndef XXX_H` |
| ログ出力 | `Serial.printf()` | `printf()` via UART (デバッグビルド) |
| 割込ハンドラ | `IRAM_ATTR` | `__attribute__((section(".ccmram")))` (必要時) |

---

## 6. テスト計画

| テストID | 対象 | 内容 | 合否基準 |
|---|---|---|---|
| UT-E01 | battery_monitor | ADC 電圧変換・残量算出 | ±0.1V 以内 |
| UT-E02 | packet | チェックサム生成・検証 | 正常・異常パケット両対応 |
| UT-E03 | uart_comm (ESP32) | ACK/NACK・リトライ | 3回リトライ後エラー |
| UT-S01 | motor_dc | PWM 出力・方向切替 | オシロスコープで確認 |
| UT-S02 | motor_stepper | ステップ数精度 | 目標 ±2 ステップ以内 |
| UT-S03 | sensor_mpu6050 | 姿勢角算出 | ±5° 以内 |
| IT-01 | ESP32 ↔ STM32 | UART パケット疎通 | ACK 受信 200ms 以内 |
| IT-02 | BT → ESP32 → STM32 → Motor | E2E モーター制御 | 50ms 以内反映 |
| IT-03 | 転倒検知 → 緊急停止 | フェイルセーフ | 10ms 以内停止 |
| IT-04 | バッテリー危険電圧 → シャットダウン | フェイルセーフ | シーケンス完了 3秒以内 |

---

## 7. リスクと対策

| リスク | 影響 | 対策 |
|---|---|---|
| UART 通信の遅延 | モーター応答遅延 | DMA + リングバッファ使用 |
| BT ライブラリの互換性 | 接続不安定 | ESP32-BT/BLE 標準ライブラリを優先使用 |
| MPU6050 ドリフト | 誤った転倒検知 | 相補フィルタ + キャリブレーション |
| ステッパの脱調 | 位置ずれ | MO 出力監視 + 速度制限 |
| SPI 配線ノイズ | データ化け | 22Ω直列抵抗・配線長最小化 |

---

## 8. 変更履歴

| バージョン | 日付 | 変更内容 |
|---|---|---|
| 1.0 | 2026-03-21 | 初版作成 |
