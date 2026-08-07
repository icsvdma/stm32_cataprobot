# STM32 CataProbot Controller

ESP32から受信したSPIコマンドに従い、2ch DCモーター、左右ヘッドライト、ICM-20948、BMP280を制御するSTM32G473ファームウェアです。

初めて確認する場合は、このREADMEの次に[資料一覧](docs/README.md)と[コードマップ](docs/コードマップ.md)を読むと、仕様から実装場所まで順番に追えます。

## システム構成

```text
PS3 Controller
      | Bluetooth Classic
      v
ESP32
      | SPI Mode 0 / 1 MHz
      v
STM32G473RCT6
  |- TIM8 CH1-CH4 -> 2ch Hブリッジ回路 -> DC Motor A/B
  |- TIM2 CH1/CH2 -> 左右ヘッドライト
  `- I2C1         -> ICM-20948 / BMP280
```

## 最初に見るコード

| 確認したい内容 | ファイル | 主な場所 |
|---|---|---|
| 起動とメインループ | `cubeMX_stm32g473_test/Core/Src/main.c` | `main` |
| ピン・コマンドID | `cubeMX_stm32g473_test/Core/Inc/config.h` | `PIN_*`、`CMD_*` |
| SPIコマンド処理 | `cubeMX_stm32g473_test/Core/Src/spi_comm.c` | `SPI_Slave_Dispatch` |
| Hブリッジ制御 | `cubeMX_stm32g473_test/Core/Src/motor_hbridge.c` | `HBridge_Update` |
| ヘッドライト | `cubeMX_stm32g473_test/Core/Src/led_control.c` | `LED_SetBrightness` |
| センサー周期管理 | `cubeMX_stm32g473_test/Core/Src/external_sensors.c` | `ExternalSensors_Poll` |
| ICM-20948 | `cubeMX_stm32g473_test/Core/Src/icm20948.c` | `ICM20948_Init` |
| BMP280 | `cubeMX_stm32g473_test/Core/Src/bmp280.c` | `BMP280_Init` |

詳しいファイル対応は[コードマップ](docs/コードマップ.md)にまとめています。

## ディレクトリ

```text
stm32_cataprobot/
|- .vscode/                     VS Codeビルド・DFU書き込みタスク
|- docs/                        要件、設計、通信、センサー資料
`- cubeMX_stm32g473_test/
   |- Core/Inc/                 独自ヘッダーとCubeMXヘッダー
   |- Core/Src/                 独自実装とCubeMX生成コード
   |- Drivers/                  STM32 HAL/CMSIS外部コード
   |- CMakeLists.txt            ビルド対象
   |- CMakePresets.json         Debug/Release設定
   `- cubeMX_stm32g473_test.ioc CubeMX設定
```

`Drivers`、`startup_stm32g473xx.s`、`system_stm32g4xx.c`は生成・外部コードです。CataProbot固有の処理は主に`Core/Inc`と`Core/Src`にあります。

## 実装機能

- 2ch HブリッジDCモーターの方向・速度制御
- 方向反転前の減速と100 ms待機
- 通常停止、緊急停止、シャットダウン
- 左右ヘッドライトの1 kHz PWM光量制御
- ICM-20948の加速度、ジャイロ、地磁気取得
- BMP280の温度、気圧取得
- Version 2、32 byteセンサースナップショット応答
- SPI ACK/NACKとコマンド引数検証

## 主なピン

| 機能 | STM32ピン | 周辺機能 |
|---|---|---|
| Motor A IN1 / IN2 | PC6 / PC7 | TIM8_CH1 / CH2 |
| Motor B IN1 / IN2 | PC8 / PC9 | TIM8_CH3 / CH4 |
| Hブリッジ駆動許可 | PC2 | GPIO Output |
| 右 / 左ヘッドライト | PA0 / PA1 | TIM2_CH1 / CH2 |
| I2C1 SCL / SDA | PA15 / PA14 | AF4、100 kHz |
| SPI1 NSS / SCK / MISO / MOSI | PA4 / PA5 / PA6 / PA7 | SPI Slave |

## ビルド

リポジトリのルートで実行します。

```powershell
cmake --preset Debug -S cubeMX_stm32g473_test
cmake --build cubeMX_stm32g473_test/build/Debug
```

生成物:

```text
cubeMX_stm32g473_test/build/Debug/cubeMX_stm32g473_test.elf
```

## VS Codeタスク

`Terminal` -> `Run Task`から次を実行できます。

| タスク | 処理 |
|---|---|
| `STM32 Build Debug` | Debug版をビルド |
| `STM32 Flash via DFU` | 全消去、書き込み、検証、アプリ起動 |
| `STM32 Start Application via DFU` | Flashを書き換えずアプリ起動 |
| `STM32 Build and Flash via DFU` | ビルド後にDFU書き込み |

DFU書き込み時は`BOOT0=High`でリセットし、`STM32 Bootloader`としてUSB認識させます。通常起動時は`BOOT0=Low`へ戻してください。

`STM32 Flash via DFU`は通常消去で検証不一致が発生する実機に合わせ、内蔵Flashを全消去します。Flash内へ別データを保存する場合はタスクを実行する前に退避してください。

## ドキュメント

- [資料一覧](docs/README.md)
- [コードマップ](docs/コードマップ.md)
- [要件定義書](docs/required_spec.md)
- [コード設計書](docs/code_design.md)
- [SPIコマンドリファレンス](docs/spi_command_reference.md)
- [ハードウェア仕様書](docs/hardware_spec_template.md)
- [外部センサー仕様](docs/STM32_外部センサ仕様.md)
- [実装計画・実績](docs/implementation_plan.md)

## 関連リポジトリ

PS3操作、バッテリー監視、Webモニターは`C:\Users\hiroki\Documents\PlatformIO\Projects\esp32_cataprobot`にあります。
