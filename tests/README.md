# WiFi設定テストスクリプト使用ガイド

PlantMonitor ESP32-C6デバイスのWiFi設定をBluetooth経由で行うためのPythonテストスクリプトです。

## 必要な環境

- **OS**: Linux（Raspberry Pi OS推奨）、macOS
- **Python**: 3.7以上
- **Bluetooth**: BLE対応のBluetoothアダプタ

## セットアップ

### 1. Pythonパッケージのインストール

```bash
# requirements.txtを使用（推奨）
pip3 install -r requirements.txt

# または個別にインストール
pip3 install bleak
```

### 2. Bluetooth権限の設定（Linux）

Raspberry PiなどのLinux環境では、Bluetooth使用のために権限設定が必要な場合があります：

```bash
# ユーザーをbluetoothグループに追加
sudo usermod -a -G bluetooth $USER

# ログアウト/ログインして設定を反映
```

## スクリプトの種類

### 1. `wifi_setup_interactive.py` - 対話式設定（推奨）

最も使いやすいスクリプトです。ウィザード形式で設定を進められます。

**使い方：**
```bash
python3 wifi_setup_interactive.py
```

**機能：**
- デバイスの自動検索
- 現在の設定の表示
- 対話式でSSID/パスワードを入力
- WiFi接続の開始
- 接続状態の確認

**実行例：**
```
╔══════════════════════════════════════════════════════════════╗
║     PlantMonitor WiFi セットアップツール                     ║
╚══════════════════════════════════════════════════════════════╝

🔍 PlantMonitorデバイスを検索中...
✅ デバイスを発見: PlantMonitor_20_1A2B

🔗 接続中...
✅ 接続完了

📡 現在のWiFi設定を取得中...
現在の設定:
  SSID: (未設定)
  Password: (未設定)

🔧 WiFi設定の入力
SSIDを入力: MyHomeNetwork
パスワードを入力: MySecurePassword123

📝 設定確認
  SSID: MyHomeNetwork
  Password: ********************

この設定でよろしいですか? (y/n): y

✅ WiFi設定を送信しました
今すぐWiFi接続を開始しますか? (y/n): y

✅ WiFi接続を開始しました
⏳ 接続処理中... 5秒待機します

📊 システムステータスを確認中...
システムステータス:
  稼働時間: 3600秒 (1時間0分)
  デバイス時刻: 2025-12-06 14:30:25
  空きメモリ: 245,678バイト
  最小空きメモリ: 230,000バイト
  タスク数: 12
  WiFi接続: ✅ 接続済み
  BLE接続: ✅ 接続済み

🎉 WiFi接続に成功しました!
```

---

### 2. `test_wifi_config.py` - コマンドライン設定

コマンドラインから直接パラメータを指定して実行するスクリプトです。自動化やスクリプト化に適しています。

**基本的な使い方：**
```bash
# WiFi設定と接続を一度に実行
python3 test_wifi_config.py --ssid "MyNetwork" --password "MyPassword"
```

**詳細なオプション：**

```bash
# WiFi設定のみ（接続は実行しない）
python3 test_wifi_config.py --ssid "MyNetwork" --password "MyPassword" --no-connect

# 現在の設定を確認するだけ
python3 test_wifi_config.py --get-only

# タイムゾーン情報を取得
python3 test_wifi_config.py --get-timezone

# ステータスチェックも含める
python3 test_wifi_config.py --ssid "MyNetwork" --password "MyPassword" --check-status

# 特定のBLEアドレスを指定（複数デバイスがある場合）
python3 test_wifi_config.py --address "AA:BB:CC:DD:EE:FF" --ssid "MyNetwork" --password "MyPassword"

# デバイス名のプレフィックスを変更
python3 test_wifi_config.py --device-name "CustomName" --ssid "MyNetwork" --password "MyPassword"
```

**全オプション一覧：**

| オプション | 説明 | デフォルト |
|-----------|------|-----------|
| `--ssid` | WiFi SSID（最大31文字） | 必須（--get-only, --get-timezoneを除く） |
| `--password` | WiFiパスワード（最大63文字） | 必須（--get-only, --get-timezoneを除く） |
| `--address` | デバイスのBLEアドレス | 自動検索 |
| `--device-name` | 検索するデバイス名のプレフィックス | PlantMonitor |
| `--no-connect` | WiFi設定のみで接続しない | False |
| `--get-only` | 現在の設定を取得のみ | False |
| `--get-timezone` | デバイスのタイムゾーン情報を取得 | False |
| `--check-status` | 操作後にステータス確認 | False |

---

## 使用シーン別の実行例

### シーン1: 初めてWiFiを設定する

```bash
# 対話式で設定（推奨）
python3 wifi_setup_interactive.py

# または、コマンドラインで
python3 test_wifi_config.py --ssid "MyNetwork" --password "MyPassword"
```

### シーン2: WiFiのSSID/パスワードを変更する

```bash
# 新しい設定を送信して接続
python3 test_wifi_config.py --ssid "NewNetwork" --password "NewPassword"
```

### シーン3: WiFi設定を確認したい

```bash
# 現在の設定を表示（パスワードはマスク表示）
python3 test_wifi_config.py --get-only
```

出力例：
```
✅ Current WiFi configuration:
   SSID: MyNetwork
   Password (masked): MyP***
```

### シーン4: デバイスのタイムゾーン情報を確認する

```bash
# タイムゾーン情報を取得
python3 test_wifi_config.py --get-timezone
```

出力例：
```
✅ Device timezone: JST-9
```

### シーン5: 設定は送信するが、すぐには接続しない

```bash
# 設定のみ送信（デバイス再起動時に自動接続される）
python3 test_wifi_config.py --ssid "MyNetwork" --password "MyPassword" --no-connect
```

### シーン6: 複数のPlantMonitorデバイスがある

```bash
# 特定のアドレスを指定
python3 test_wifi_config.py --address "AA:BB:CC:DD:EE:FF" --ssid "MyNetwork" --password "MyPassword"

# または対話式で選択
python3 wifi_setup_interactive.py
```

対話式の場合、複数デバイスがあると自動的に選択肢が表示されます：
```
複数のデバイスが見つかりました:
  1. PlantMonitor_20_1A2B (AA:BB:CC:DD:EE:FF)
  2. PlantMonitor_20_3C4D (11:22:33:44:55:66)

接続するデバイスを選択 (1-2): 1
```

---

## トラブルシューティング

### エラー: "No device found"

**原因：**
- PlantMonitorデバイスの電源が入っていない
- Bluetoothの範囲外
- デバイスが既に他のBLEクライアントに接続されている

**解決方法：**
```bash
# 1. デバイスの電源を確認
# 2. デバイスをリセット
# 3. Bluetoothサービスの再起動
sudo systemctl restart bluetooth

# 4. 手動でBLEデバイスをスキャン
sudo hcitool lescan
```

### エラー: "Permission denied"

**原因：**
- Bluetooth使用権限がない

**解決方法：**
```bash
# ユーザーをbluetoothグループに追加
sudo usermod -a -G bluetooth $USER

# ログアウト/ログインして再試行
```

### エラー: "Failed to start WiFi connection"

**原因：**
- WiFiマネージャーが初期化されていない
- デバイスのWiFi機能が無効

**解決方法：**
- デバイスのファームウェアが最新か確認
- `CONFIG_WIFI_ENABLED=1`でビルドされているか確認

### WiFi接続が成功しない

**原因：**
- SSIDまたはパスワードが間違っている
- WiFiルーターが範囲外
- 5GHz WiFiを使用している（ESP32-C6は2.4GHzのみ対応）

**解決方法：**
```bash
# 1. 設定を確認
python3 test_wifi_config.py --get-only

# 2. システムステータスを確認
python3 test_wifi_config.py --get-only --check-status

# 3. 正しい設定で再設定
python3 test_wifi_config.py --ssid "CorrectSSID" --password "CorrectPassword"

# 4. 2.4GHz WiFiを使用していることを確認
```

### BLEスキャンが遅い

**原因：**
- 多数のBLEデバイスが周囲にある

**解決方法：**
```bash
# デバイスアドレスが分かっている場合、直接指定
python3 test_wifi_config.py --address "AA:BB:CC:DD:EE:FF" --ssid "MyNetwork" --password "MyPassword"
```

---

## スクリプトの動作フロー

### `wifi_setup_interactive.py`のフロー

```
1. BLEデバイススキャン
   ↓
2. PlantMonitorデバイスを検出
   ↓
3. デバイスに接続
   ↓
4. 現在のWiFi設定を取得・表示
   ↓
5. ユーザーからSSID/パスワード入力
   ↓
6. 設定を確認
   ↓
7. WiFi設定を送信（CMD_SET_WIFI_CONFIG）
   ↓
8. 設定を再取得して確認
   ↓
9. WiFi接続を開始（CMD_WIFI_CONNECT）
   ↓
10. 5秒待機
   ↓
11. システムステータスを確認（CMD_GET_SYSTEM_STATUS）
   ↓
12. WiFi接続状態を表示
   ↓
13. 切断
```

### `test_wifi_config.py`のフロー

```
1. コマンドライン引数をパース
   ↓
2. BLEデバイススキャン（アドレス未指定の場合）
   ↓
3. デバイスに接続
   ↓
4. 現在の設定を取得（--get-onlyの場合はここで終了）
   ↓
5. WiFi設定を送信（CMD_SET_WIFI_CONFIG）
   ↓
6. WiFi接続を開始（CMD_WIFI_CONNECT）
   ↓  (--no-connectの場合はスキップ)
7. 5秒待機
   ↓
8. システムステータスを確認（CMD_GET_SYSTEM_STATUS）
   ↓
9. 結果を表示
   ↓
10. 切断
```

---

## セキュリティに関する注意

### パスワードの取り扱い

- **コマンドライン履歴**: `--password`オプションを使用すると、パスワードがシェル履歴に残る可能性があります
- **推奨**: セキュリティが重要な場合は、対話式スクリプト（`wifi_setup_interactive.py`）を使用してください
- **履歴削除**: 必要に応じて履歴を削除してください
  ```bash
  # bashの場合
  history -d <行番号>

  # 全履歴をクリア（注意）
  history -c
  ```

### マスク表示

- `CMD_GET_WIFI_CONFIG`で取得されるパスワードは、最初の3文字のみ表示され、残りは`***`でマスクされます
- 例: 実際のパスワード`MyPassword123` → 表示`MyP***`

---

## 自動化の例

### シェルスクリプトでの自動化

```bash
#!/bin/bash
# auto_wifi_setup.sh

SSID="MyNetwork"
PASSWORD="MyPassword"

python3 test_wifi_config.py \
    --ssid "$SSID" \
    --password "$PASSWORD" \
    --check-status

if [ $? -eq 0 ]; then
    echo "WiFi setup completed successfully"
else
    echo "WiFi setup failed"
    exit 1
fi
```

### 複数デバイスへの一括設定

```bash
#!/bin/bash
# setup_multiple_devices.sh

DEVICES=(
    "AA:BB:CC:DD:EE:01"
    "AA:BB:CC:DD:EE:02"
    "AA:BB:CC:DD:EE:03"
)

for addr in "${DEVICES[@]}"; do
    echo "Setting up device: $addr"
    python3 test_wifi_config.py \
        --address "$addr" \
        --ssid "MyNetwork" \
        --password "MyPassword"
    echo "---"
done
```

---

## よくある質問（FAQ）

**Q: Raspberry Piで実行できますか？**
A: はい。Raspberry Pi OS（旧Raspbian）で動作確認済みです。

**Q: WindowsやmacOSでも使えますか？**
A: macOSでは動作します。Windowsでは`bleak`ライブラリが対応していますが、本スクリプトは未検証です。

**Q: WiFi接続に失敗する場合は？**
A:
1. SSIDとパスワードが正しいか確認
2. 2.4GHz WiFi（ESP32-C6は5GHz非対応）
3. WiFiルーターの暗号化がWPA2/WPA3であることを確認

**Q: 複数のPlantMonitorデバイスを同時に設定できますか？**
A: 同時には接続できません。1台ずつ順番に設定してください。

**Q: 設定は保存されますか？**
A: はい。デバイスのNVS（不揮発性ストレージ）に保存されます。

**Q: パスワードの全文を確認できますか？**
A: セキュリティ上、パスワードは最初の3文字のみ表示されます（例: `MyP***`）

---

## 開発者向け情報

### カスタマイズ

スクリプトは自由にカスタマイズできます。主要な関数：

- `find_device()`: デバイス検索
- `send_command()`: BLEコマンド送信
- `set_wifi_config()`: WiFi設定送信
- `get_wifi_config()`: WiFi設定取得
- `wifi_connect()`: WiFi接続開始
- `get_system_status()`: システムステータス取得
- `get_timezone()`: タイムゾーン情報取得

### BLEプロトコル

詳細なプロトコル仕様は[../README.md](../README.md)の「Bluetooth通信マニュアル」セクションを参照してください。

### システムステータス構造体

`CMD_GET_SYSTEM_STATUS`コマンドで取得される`system_status_t`構造体（24バイト）：

| フィールド | 型 | サイズ | 説明 |
|-----------|-----|--------|------|
| uptime_seconds | uint32_t | 4 | 稼働時間（秒） |
| heap_free | uint32_t | 4 | 空きヒープメモリ（バイト） |
| heap_min | uint32_t | 4 | 最小空きヒープメモリ（バイト） |
| task_count | uint32_t | 4 | 実行中のタスク数 |
| current_time | uint32_t | 4 | 現在時刻（UNIXタイムスタンプ、0の場合は未設定） |
| wifi_connected | uint8_t | 1 | WiFi接続状態（0:未接続, 1:接続済み） |
| ble_connected | uint8_t | 1 | BLE接続状態（0:未接続, 1:接続済み） |
| padding | uint8_t[2] | 2 | アライメント用パディング |

**Pythonでのパース:**
```python
import struct
from datetime import datetime

# レスポンスデータをパース
uptime, heap_free, heap_min, task_count, current_time, wifi_connected, ble_connected = \
    struct.unpack('<IIIIIBBxx', response_data[:24])

# 時刻を日時に変換
if current_time > 0:
    device_time = datetime.fromtimestamp(current_time).strftime('%Y-%m-%d %H:%M:%S')
else:
    device_time = "未設定"

print(f"稼働時間: {uptime}秒")
print(f"デバイス時刻: {device_time}")
print(f"WiFi接続: {'接続済み' if wifi_connected else '未接続'}")
```

---

## ライセンス

このスクリプトはMITライセンスで提供されています。

## サポート

問題が発生した場合は、GitHubのIssueで報告してください。
