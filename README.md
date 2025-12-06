# Plant Monitor - SoilMonitorRev2

## 概要

Plant Monitorは、ESP32-C6を使用した植物環境モニタリングシステムです。土壌水分、温度、湿度、照度をセンシングし、BLE (Bluetooth Low Energy)経由でデータを取得できます。

### 主な機能

- **センサーモニタリング**
  - 土壌水分センサー (ADC)
  - 温湿度センサー (SHT40)
  - 照度センサー (TSL2591)
- **データ保存**
  - 1分ごとのセンサーデータを24時間分保存
  - NVSへの植物プロファイル保存
- **BLE通信**
  - コマンド/レスポンス方式でのデータ取得
  - センサーデータのリアルタイム通知
  - 過去データの時間指定取得
- **視覚フィードバック**
  - WS2812フルカラーLEDで植物状態を表示
  - ステータスLED（青色/赤色）

## ハードウェア情報

| パラメータ | 値 |
|-----------|-----|
| ハードウェアバージョン | Rev2.0 (HARDWARE_VERSION=20) |
| ソフトウェアバージョン | 2.0.0 |
| 対応チップ | ESP32-C6 |

### GPIO配置 (Rev2)

| 機能 | GPIO |
|------|------|
| 土壌水分センサー | GPIO3 (ADC1_CH3) |
| I2C SDA | GPIO5 |
| I2C SCL | GPIO6 |
| スイッチ入力 | GPIO7 |
| WS2812 LED | GPIO1 |
| 青色LED | GPIO0 |
| 赤色LED | GPIO2 |

## ビルドとフラッシュ

### 1. WiFi認証情報の設定

WiFi機能を使用する場合（`CONFIG_WIFI_ENABLED=1`の場合）、WiFi認証情報を設定する必要があります。

```bash
# サンプルファイルをコピー
cp main/wifi_credentials.h.example main/wifi_credentials.h

# エディタで実際のSSIDとパスワードを設定
# main/wifi_credentials.h を編集してください
```

**main/wifi_credentials.h の例:**
```c
#define WIFI_SSID                "your-actual-ssid"
#define WIFI_PASSWORD            "your-actual-password"
```

**注意:** `wifi_credentials.h` は `.gitignore` で除外されており、Gitリポジトリには含まれません。

### 2. ターゲット設定

```bash
idf.py set-target esp32c6
```

### 3. ビルドとフラッシュ

```bash
idf.py build
idf.py -p <PORT> flash monitor
```

例：
```bash
idf.py -p COM3 flash monitor
```

(シリアルモニタを終了するには `Ctrl-]` を入力)

---

# Bluetooth通信マニュアル

## 接続情報

### デバイス名

デバイスは起動時にMACアドレスから動的に生成されたBLE名でアドバタイズします：

```
PlantMonitor_<HW_VERSION>_<DEVICE_ID>
```

- `HW_VERSION`: ハードウェアバージョン（Rev2の場合は`20`）
- `DEVICE_ID`: BLE MACアドレスの下位2バイトから生成される4桁の16進数

例：`PlantMonitor_20_A1B2`

### サービスUUID

プライマリサービスUUID（128-bit）：
```
592F4612-9543-9999-12C8-58B459A2712D
```

## GATTキャラクタリスティック

| 名称 | UUID | プロパティ | 説明 |
|------|------|----------|------|
| Sensor Data | `6A3B2C01-4E5F-6A7B-8C9D-E0F123456789` | Read, Notify | 最新のセンサーデータ |
| Data Status | `6A3B2C1D-4E5F-6A7B-8C9D-E0F123456790` | Read, Write | データバッファのステータス |
| Command | `6A3B2C1D-4E5F-6A7B-8C9D-E0F123456791` | Write, Write No Response | コマンド送信用 |
| Response | `6A3B2C1D-4E5F-6A7B-8C9D-E0F123456792` | Read, Notify | コマンドレスポンス受信用 |
| Data Transfer | `6A3B2C1D-4E5F-6A7B-8C9D-E0F123456793` | Read, Write, Notify | 大容量データ転送用 |

---

## コマンド/レスポンスプロトコル

### コマンドパケット構造

すべてのコマンドは以下の構造で送信します：

```c
struct ble_command_packet {
    uint8_t  command_id;      // コマンド識別子
    uint8_t  sequence_num;    // シーケンス番号（0-255）
    uint16_t data_length;     // データ長（リトルエンディアン）
    uint8_t  data[];          // コマンドデータ（可変長）
} __attribute__((packed));
```

**送信先**: `Command` キャラクタリスティック

### レスポンスパケット構造

すべてのレスポンスは以下の構造で受信します：

```c
struct ble_response_packet {
    uint8_t  response_id;     // レスポンス識別子（コマンドIDと同じ）
    uint8_t  status_code;     // ステータスコード
    uint8_t  sequence_num;    // シーケンス番号（コマンドと同じ）
    uint16_t data_length;     // レスポンスデータ長（リトルエンディアン）
    uint8_t  data[];          // レスポンスデータ（可変長）
} __attribute__((packed));
```

**受信元**: `Response` キャラクタリスティック（Notify推奨）

### ステータスコード

| コード | 名称 | 説明 |
|--------|------|------|
| 0x00 | SUCCESS | 成功 |
| 0x01 | ERROR | エラー |
| 0x02 | INVALID_COMMAND | 無効なコマンド |
| 0x03 | INVALID_PARAMETER | 無効なパラメータ |
| 0x04 | BUSY | ビジー状態 |
| 0x05 | NOT_SUPPORTED | サポートされていない |

---

## コマンドリファレンス

### 0x01: CMD_GET_SENSOR_DATA - 最新センサーデータ取得

最新のセンサーデータを取得します。

**コマンド**
```
command_id: 0x01
sequence_num: <任意>
data_length: 0x0000
data: (なし)
```

**レスポンス**
```c
struct soil_data {
    struct tm datetime;       // タイムスタンプ（44バイト）
    float lux;                // 照度 [lux]
    float temperature;        // 温度 [°C]
    float humidity;           // 湿度 [%]
    float soil_moisture;      // 土壌水分 [mV]
} __attribute__((packed));
```

**サイズ**: 60バイト

**struct tm構造**
```c
struct tm {
    int tm_sec;      // 秒 (0-59)
    int tm_min;      // 分 (0-59)
    int tm_hour;     // 時 (0-23)
    int tm_mday;     // 日 (1-31)
    int tm_mon;      // 月 (0-11)
    int tm_year;     // 年 (1900年からの経過年数)
    int tm_wday;     // 曜日 (0-6, 日曜=0)
    int tm_yday;     // 年内通算日 (0-365)
    int tm_isdst;    // 夏時間フラグ
};
```

---

### 0x02: CMD_GET_SYSTEM_STATUS - システムステータス取得

システムの稼働状態を取得します。

**コマンド**
```
command_id: 0x02
sequence_num: <任意>
data_length: 0x0000
data: (なし)
```

**レスポンス**

ASCII文字列で以下のフォーマット：
```
Uptime: <秒数> s, Free Heap: <バイト数>, Min Free: <バイト数>
```

例：`Uptime: 3600 s, Free Heap: 245678, Min Free: 230000`

---

### 0x03: CMD_SET_PLANT_PROFILE - 植物プロファイル設定

植物の管理プロファイルを設定します。設定内容はNVSに保存されます。

**コマンド**
```
command_id: 0x03
sequence_num: <任意>
data_length: 56 (sizeof(plant_profile_t))
data: <plant_profile_t構造体>
```

**plant_profile_t構造**
```c
struct plant_profile {
    char  plant_name[32];                 // 植物名（NULL終端文字列）
    float soil_dry_threshold;             // 乾燥閾値 [mV] (例: 2500.0)
    float soil_wet_threshold;             // 湿潤閾値 [mV] (例: 1000.0)
    int   soil_dry_days_for_watering;     // 水やり判定日数 [日] (例: 3)
    float temp_high_limit;                // 高温警告閾値 [°C] (例: 35.0)
    float temp_low_limit;                 // 低温警告閾値 [°C] (例: 10.0)
} __attribute__((packed));
```

**サイズ**: 56バイト

**レスポンス**

ステータスコードのみ（data_length = 0）

---

### 0x0C: CMD_GET_PLANT_PROFILE - 植物プロファイル取得

現在設定されている植物プロファイルを取得します。

**コマンド**
```
command_id: 0x0C
sequence_num: <任意>
data_length: 0x0000
data: (なし)
```

**レスポンス**

`plant_profile_t`構造体（56バイト）が返されます。構造は`CMD_SET_PLANT_PROFILE`と同じです。

---

### 0x05: CMD_SYSTEM_RESET - システムリセット

デバイスを再起動します。

**コマンド**
```
command_id: 0x05
sequence_num: <任意>
data_length: 0x0000
data: (なし)
```

**レスポンス**

ステータスコードのみ（data_length = 0）

レスポンス送信後、約500ms後にデバイスが再起動します。

---

### 0x06: CMD_GET_DEVICE_INFO - デバイス情報取得

デバイスの識別情報を取得します。

**コマンド**
```
command_id: 0x06
sequence_num: <任意>
data_length: 0x0000
data: (なし)
```

**レスポンス**
```c
struct device_info {
    char device_name[32];           // デバイス名
    char firmware_version[16];      // ファームウェアバージョン
    char hardware_version[16];      // ハードウェアバージョン
    uint32_t uptime_seconds;        // 稼働時間 [秒]
    uint32_t total_sensor_readings; // センサー読み取り総回数
} __attribute__((packed));
```

**サイズ**: 72バイト

---

### 0x0A: CMD_GET_TIME_DATA - 時間指定データ取得

指定した時刻のセンサーデータを取得します（24時間分のバッファから検索）。

**コマンド**
```
command_id: 0x0A
sequence_num: <任意>
data_length: 44 (sizeof(time_data_request_t))
data: <time_data_request_t構造体>
```

**time_data_request_t構造**
```c
struct time_data_request {
    struct tm requested_time;  // 取得したい時刻
} __attribute__((packed));
```

**レスポンス**
```c
struct time_data_response {
    struct tm actual_time;     // 実際に見つかったデータの時刻
    float temperature;         // 温度 [°C]
    float humidity;            // 湿度 [%]
    float lux;                 // 照度 [lux]
    float soil_moisture;       // 土壌水分 [mV]
} __attribute__((packed));
```

**サイズ**: 60バイト

データが見つからない場合、`status_code`が`RESP_STATUS_ERROR`になります。

---

### 0x0B: CMD_GET_SWITCH_STATUS - スイッチ状態取得

デバイスのスイッチ入力状態を取得します。

**コマンド**
```
command_id: 0x0B
sequence_num: <任意>
data_length: 0x0000
data: (なし)
```

**レスポンス**

1バイトのスイッチ状態：
- `0x00`: スイッチOFF（非押下）
- `0x01`: スイッチON（押下）

---

### 0x0D: CMD_SET_WIFI_CONFIG - WiFi設定

WiFiのSSIDとパスワードを設定します。設定は即座に適用されます。

**コマンド**
```
command_id: 0x0D
sequence_num: <任意>
data_length: 96 (sizeof(wifi_config_data_t))
data: <wifi_config_data_t構造体>
```

**wifi_config_data_t構造**
```c
struct wifi_config_data {
    char ssid[32];          // SSID（NULL終端文字列）
    char password[64];      // パスワード（NULL終端文字列）
} __attribute__((packed));
```

**サイズ**: 96バイト

**レスポンス**

ステータスコードのみ（data_length = 0）

**注意事項**:
- WiFi設定は即座に`esp_wifi_set_config()`で適用されます
- WiFi再接続が必要な場合は別途接続処理を実行してください
- SSID/パスワードは自動的にNULL終端されます

---

### 0x0E: CMD_GET_WIFI_CONFIG - WiFi設定取得

現在設定されているWiFi設定を取得します。

**コマンド**
```
command_id: 0x0E
sequence_num: <任意>
data_length: 0x0000
data: (なし)
```

**レスポンス**

`wifi_config_data_t`構造体（96バイト）が返されます。

**セキュリティ機能**:
- パスワードはマスク表示されます（最初の3文字 + "***"）
- 例: 実際のパスワードが"mypassword"の場合、"myp***"として返されます

---

### 0x0F: CMD_WIFI_CONNECT - WiFi接続実行

設定されているWiFi情報を使用してWiFi接続を開始します。

**コマンド**
```
command_id: 0x0F
sequence_num: <任意>
data_length: 0x0000
data: (なし)
```

**レスポンス**
```
response_id: 0x0F
status_code: 0x00 (成功) / 0x01 (エラー)
sequence_num: <対応するシーケンス番号>
data_length: 0x0000
data: (なし)
```

**注意事項**:
- 事前に`CMD_SET_WIFI_CONFIG`でSSIDとパスワードを設定しておく必要があります
- WiFi接続は非同期で実行されます（レスポンスは接続開始の成否を示します）
- 実際の接続状態は`CMD_GET_SYSTEM_STATUS`などで確認してください

---

## 通信例

### Python実装例（bleak使用）

```python
import asyncio
from bleak import BleakClient, BleakScanner
import struct

# UUIDs
SERVICE_UUID = "592F4612-9543-9999-12C8-58B459A2712D"
COMMAND_UUID = "6A3B2C1D-4E5F-6A7B-8C9D-E0F123456791"
RESPONSE_UUID = "6A3B2C1D-4E5F-6A7B-8C9D-E0F123456792"

class PlantMonitor:
    def __init__(self):
        self.client = None
        self.response_data = None

    async def connect(self, device_name_prefix="PlantMonitor"):
        """デバイスに接続"""
        devices = await BleakScanner.discover()
        device = None

        for d in devices:
            if d.name and d.name.startswith(device_name_prefix):
                device = d
                break

        if not device:
            raise Exception(f"Device with prefix '{device_name_prefix}' not found")

        self.client = BleakClient(device.address)
        await self.client.connect()

        # Responseキャラクタリスティックの通知を有効化
        await self.client.start_notify(RESPONSE_UUID, self._notification_handler)

    def _notification_handler(self, sender, data):
        """レスポンス通知ハンドラ"""
        self.response_data = data

    async def send_command(self, command_id, data=b"", sequence_num=0):
        """コマンド送信"""
        packet = struct.pack("<BBH", command_id, sequence_num, len(data)) + data
        await self.client.write_gatt_char(COMMAND_UUID, packet, response=False)

        # レスポンス待機
        timeout = 50  # 5秒
        for _ in range(timeout):
            if self.response_data:
                break
            await asyncio.sleep(0.1)

        if not self.response_data:
            raise Exception("Response timeout")

        # レスポンスをパース
        resp_id, status, resp_seq, data_len = struct.unpack("<BBHH", self.response_data[:6])
        resp_data = self.response_data[6:]

        self.response_data = None  # リセット

        return {
            "response_id": resp_id,
            "status": status,
            "sequence_num": resp_seq,
            "data": resp_data
        }

    async def get_sensor_data(self):
        """最新センサーデータ取得"""
        resp = await self.send_command(0x01)

        if resp["status"] != 0x00:
            raise Exception(f"Command failed with status {resp['status']}")

        # struct tmをパース（9個のint = 36バイト）
        tm_data = struct.unpack("<9i", resp["data"][:36])
        sensor_data = struct.unpack("<ffff", resp["data"][36:52])

        return {
            "timestamp": tm_data,
            "lux": sensor_data[0],
            "temperature": sensor_data[1],
            "humidity": sensor_data[2],
            "soil_moisture": sensor_data[3]
        }

    async def set_plant_profile(self, name, dry_threshold, wet_threshold,
                                dry_days, temp_high, temp_low):
        """植物プロファイル設定"""
        # 名前を32バイトにパディング
        name_bytes = name.encode('utf-8')[:31].ljust(32, b'\x00')

        # プロファイルデータをパック
        data = name_bytes + struct.pack("<ffiff",
            dry_threshold, wet_threshold, dry_days, temp_high, temp_low)

        resp = await self.send_command(0x03, data)
        return resp["status"] == 0x00

    async def get_plant_profile(self):
        """植物プロファイル取得"""
        resp = await self.send_command(0x0C)

        if resp["status"] != 0x00:
            raise Exception(f"Command failed with status {resp['status']}")

        # プロファイルをパース
        name = resp["data"][:32].decode('utf-8').rstrip('\x00')
        values = struct.unpack("<ffiff", resp["data"][32:56])

        return {
            "plant_name": name,
            "soil_dry_threshold": values[0],
            "soil_wet_threshold": values[1],
            "soil_dry_days_for_watering": values[2],
            "temp_high_limit": values[3],
            "temp_low_limit": values[4]
        }

    async def get_device_info(self):
        """デバイス情報取得"""
        resp = await self.send_command(0x06)

        if resp["status"] != 0x00:
            raise Exception(f"Command failed with status {resp['status']}")

        name = resp["data"][:32].decode('utf-8').rstrip('\x00')
        fw_ver = resp["data"][32:48].decode('utf-8').rstrip('\x00')
        hw_ver = resp["data"][48:64].decode('utf-8').rstrip('\x00')
        uptime, readings = struct.unpack("<II", resp["data"][64:72])

        return {
            "device_name": name,
            "firmware_version": fw_ver,
            "hardware_version": hw_ver,
            "uptime_seconds": uptime,
            "total_sensor_readings": readings
        }

    async def set_wifi_config(self, ssid, password):
        """WiFi設定"""
        # SSIDとパスワードを固定長にパディング
        ssid_bytes = ssid.encode('utf-8')[:31].ljust(32, b'\x00')
        password_bytes = password.encode('utf-8')[:63].ljust(64, b'\x00')

        # WiFi設定データをパック
        data = ssid_bytes + password_bytes

        resp = await self.send_command(0x0D, data)
        return resp["status"] == 0x00

    async def get_wifi_config(self):
        """WiFi設定取得"""
        resp = await self.send_command(0x0E)

        if resp["status"] != 0x00:
            raise Exception(f"Command failed with status {resp['status']}")

        # WiFi設定をパース
        ssid = resp["data"][:32].decode('utf-8').rstrip('\x00')
        password_masked = resp["data"][32:96].decode('utf-8').rstrip('\x00')

        return {
            "ssid": ssid,
            "password": password_masked  # マスク表示（例: "abc***"）
        }

    async def wifi_connect(self):
        """WiFi接続実行"""
        resp = await self.send_command(0x0F)

        if resp["status"] != 0x00:
            raise Exception(f"Failed to start WiFi connection: status {resp['status']}")

        return True

    async def disconnect(self):
        """切断"""
        if self.client:
            await self.client.disconnect()

# 使用例
async def main():
    monitor = PlantMonitor()

    try:
        await monitor.connect()
        print("Connected!")

        # デバイス情報取得
        info = await monitor.get_device_info()
        print(f"Device: {info['device_name']}")
        print(f"FW Version: {info['firmware_version']}")

        # センサーデータ取得
        data = await monitor.get_sensor_data()
        print(f"Temperature: {data['temperature']:.1f}°C")
        print(f"Humidity: {data['humidity']:.1f}%")
        print(f"Lux: {data['lux']:.0f} lux")
        print(f"Soil Moisture: {data['soil_moisture']:.0f} mV")

        # プロファイル設定
        await monitor.set_plant_profile(
            name="Tomato",
            dry_threshold=2500.0,
            wet_threshold=1000.0,
            dry_days=3,
            temp_high=35.0,
            temp_low=10.0
        )
        print("Profile set!")

        # プロファイル取得
        profile = await monitor.get_plant_profile()
        print(f"Current Profile: {profile['plant_name']}")

        # WiFi設定
        await monitor.set_wifi_config(
            ssid="MyWiFiNetwork",
            password="MyPassword123"
        )
        print("WiFi config updated!")

        # WiFi設定取得
        wifi_config = await monitor.get_wifi_config()
        print(f"Current WiFi SSID: {wifi_config['ssid']}")
        print(f"Password (masked): {wifi_config['password']}")

        # WiFi接続実行
        await monitor.wifi_connect()
        print("WiFi connection started!")

    finally:
        await monitor.disconnect()

if __name__ == "__main__":
    asyncio.run(main())
```

---

## データ型とエンディアン

- **整数型**: リトルエンディアン
- **浮動小数点**: IEEE 754形式（32-bit単精度）
- **文字列**: NULL終端、UTF-8エンコーディング
- **構造体**: パック構造（アライメントなし）

---

## テストスクリプト

プロジェクトには、Raspberry PiなどのLinuxデバイスからWiFi設定をテストするためのPythonスクリプトが含まれています。

テストスクリプトは `tests/` ディレクトリに格納されています。詳細は [tests/README.md](tests/README.md) を参照してください。

### 必要なパッケージのインストール

```bash
cd tests
pip3 install -r requirements.txt
```

### 1. 対話式WiFi設定スクリプト（推奨）

最も簡単な方法は対話式スクリプトを使用することです：

```bash
cd tests
python3 wifi_setup_interactive.py
```

このスクリプトは以下を自動的に実行します：
- PlantMonitorデバイスの検索
- 現在のWiFi設定の表示
- 新しいSSID/パスワードの入力
- WiFi設定の送信と接続
- 接続状態の確認

**実行例：**
```
╔══════════════════════════════════════════════════════════════╗
║                                                              ║
║     PlantMonitor WiFi セットアップツール                     ║
║     ESP32-C6 対話式設定スクリプト                            ║
║                                                              ║
╚══════════════════════════════════════════════════════════════╝

🔍 PlantMonitorデバイスを検索中...
✅ デバイスを発見: PlantMonitor_20_1A2B
   アドレス: AA:BB:CC:DD:EE:FF

🔗 接続中...
✅ 接続完了

SSIDを入力: MyWiFiNetwork
パスワードを入力: MyPassword123

✅ WiFi設定を送信しました
🎉 WiFi接続に成功しました!
```

### 2. コマンドラインWiFi設定スクリプト

コマンドラインから直接実行する場合：

```bash
cd tests

# WiFi設定と接続
python3 test_wifi_config.py --ssid "YourSSID" --password "YourPassword"

# WiFi設定のみ（接続しない）
python3 test_wifi_config.py --ssid "YourSSID" --password "YourPassword" --no-connect

# 現在の設定を確認のみ
python3 test_wifi_config.py --get-only

# ステータスチェックも実行
python3 test_wifi_config.py --ssid "YourSSID" --password "YourPassword" --check-status

# 特定のデバイスアドレスを指定
python3 test_wifi_config.py --address "AA:BB:CC:DD:EE:FF" --ssid "YourSSID" --password "YourPassword"
```

**オプション：**
- `--ssid`: WiFi SSID（最大31文字）
- `--password`: WiFiパスワード（最大63文字）
- `--address`: デバイスのBLEアドレス（オプション）
- `--device-name`: デバイス名のプレフィックス（デフォルト: PlantMonitor）
- `--no-connect`: WiFi設定のみ行い、接続は実行しない
- `--get-only`: 現在の設定を取得のみ
- `--check-status`: 操作後にシステムステータスをチェック

### スクリプトの動作フロー

1. **デバイス検索**: BLE経由でPlantMonitorデバイスを検索
2. **接続**: デバイスに接続し、通知を有効化
3. **現在の設定取得**: `CMD_GET_WIFI_CONFIG`で現在の設定を表示
4. **WiFi設定送信**: `CMD_SET_WIFI_CONFIG`でSSID/パスワードを送信
5. **WiFi接続開始**: `CMD_WIFI_CONNECT`で接続を開始
6. **ステータス確認**: `CMD_GET_SYSTEM_STATUS`で接続状態を確認

### トラブルシューティング（テストスクリプト）

**デバイスが見つからない場合：**
```bash
# Bluetoothサービスの確認
sudo systemctl status bluetooth

# Bluetoothの再起動
sudo systemctl restart bluetooth
```

**Permission deniedエラーの場合：**
```bash
# ユーザーをbluetoothグループに追加
sudo usermod -a -G bluetooth $USER

# ログアウト/ログインして再試行
```

**接続できない場合：**
- デバイスが電源投入されているか確認
- 他のBLEクライアントが接続していないか確認
- デバイスをリセットして再試行

---

## トラブルシューティング

### 接続できない

1. デバイスが電源投入され、BLEアドバタイジング中か確認
2. デバイス名が正しいか確認（`PlantMonitor_XX_XXXX`形式）
3. 他のBLEデバイスとの干渉を確認
4. ESP32-C6のBLE機能が有効か確認

### レスポンスが返ってこない

1. `Response`キャラクタリスティックの通知が有効になっているか確認
2. シーケンス番号が正しく設定されているか確認
3. コマンドパケットのバイトオーダーを確認（リトルエンディアン）

### データが正しく取得できない

1. 構造体のパディングとアライメントを確認
2. データ長が正しいか確認
3. ステータスコードを確認（0x00が成功）

---

## ライセンス

このプロジェクトはESP-IDFのサンプルコードをベースにしています。

## 問い合わせ

技術的な質問については、GitHubのIssueで報告してください。
