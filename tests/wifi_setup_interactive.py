#!/usr/bin/env python3
"""
Interactive WiFi Setup Script for PlantMonitor ESP32-C6
対話式WiFi設定スクリプト（Raspberry Pi用）

必要なパッケージ:
pip3 install bleak

使用方法:
python3 wifi_setup_interactive.py
"""

import asyncio
import struct
import sys
from bleak import BleakClient, BleakScanner

# UUIDs
SERVICE_UUID = "592F4612-9543-9999-12C8-58B459A2712D"
COMMAND_UUID = "6A3B2C1D-4E5F-6A7B-8C9D-E0F123456791"
RESPONSE_UUID = "6A3B2C1D-4E5F-6A7B-8C9D-E0F123456792"

# Commands
CMD_SET_WIFI_CONFIG = 0x0D
CMD_GET_WIFI_CONFIG = 0x0E
CMD_WIFI_CONNECT = 0x0F
CMD_GET_SYSTEM_STATUS = 0x02

# Response Status
RESP_STATUS_SUCCESS = 0x00


class WiFiSetup:
    def __init__(self):
        self.client = None
        self.response_data = None
        self.sequence_num = 0

    def response_handler(self, sender, data):
        self.response_data = bytes(data)

    async def find_device(self):
        """デバイスを検索"""
        print("\n" + "="*60)
        print("🔍 PlantMonitorデバイスを検索中...")
        print("="*60)

        devices = await BleakScanner.discover(timeout=10.0)
        plant_monitors = []

        for device in devices:
            if device.name and device.name.startswith("PlantMonitor"):
                plant_monitors.append(device)

        if not plant_monitors:
            print("❌ PlantMonitorデバイスが見つかりません")
            return None

        if len(plant_monitors) == 1:
            device = plant_monitors[0]
            print(f"\n✅ デバイスを発見: {device.name}")
            print(f"   アドレス: {device.address}")
            return device.address

        # 複数のデバイスがある場合、選択させる
        print(f"\n複数のデバイスが見つかりました:")
        for i, device in enumerate(plant_monitors, 1):
            print(f"  {i}. {device.name} ({device.address})")

        while True:
            try:
                choice = input(f"\n接続するデバイスを選択 (1-{len(plant_monitors)}): ")
                idx = int(choice) - 1
                if 0 <= idx < len(plant_monitors):
                    return plant_monitors[idx].address
            except (ValueError, IndexError):
                pass
            print("❌ 無効な選択です")

    async def connect(self, address):
        """接続"""
        print(f"\n🔗 接続中...")
        self.client = BleakClient(address)
        await self.client.connect()
        await self.client.start_notify(RESPONSE_UUID, self.response_handler)
        print("✅ 接続完了")

    async def send_command(self, command_id, data=b''):
        """コマンド送信"""
        self.sequence_num = (self.sequence_num + 1) % 256
        packet = struct.pack('<BBH', command_id, self.sequence_num, len(data)) + data
        self.response_data = None
        await self.client.write_gatt_char(COMMAND_UUID, packet)

        # レスポンス待機
        for _ in range(50):
            await asyncio.sleep(0.1)
            if self.response_data:
                break

        if not self.response_data:
            raise Exception("レスポンスがありません")

        response_id, status, seq, data_len = struct.unpack('<BBBH', self.response_data[:5])
        response_payload = self.response_data[5:] if len(self.response_data) > 5 else b''

        return {
            "status": status,
            "data": response_payload
        }

    async def get_current_config(self):
        """現在の設定を表示"""
        print("\n" + "="*60)
        print("📡 現在のWiFi設定を取得中...")
        print("="*60)

        resp = await self.send_command(CMD_GET_WIFI_CONFIG)

        if resp["status"] == RESP_STATUS_SUCCESS:
            ssid = resp["data"][:32].decode('utf-8').rstrip('\x00')
            password_masked = resp["data"][32:96].decode('utf-8').rstrip('\x00')

            print(f"\n現在の設定:")
            print(f"  SSID: {ssid if ssid else '(未設定)'}")
            print(f"  Password: {password_masked if password_masked else '(未設定)'}")
            return ssid
        else:
            print("❌ 設定の取得に失敗しました")
            return None

    async def set_wifi_config(self, ssid, password):
        """WiFi設定"""
        print(f"\n📡 WiFi設定を送信中...")

        ssid_bytes = ssid.encode('utf-8')[:31]
        password_bytes = password.encode('utf-8')[:63]

        data = bytearray(96)
        data[0:len(ssid_bytes)] = ssid_bytes
        data[32:32+len(password_bytes)] = password_bytes

        resp = await self.send_command(CMD_SET_WIFI_CONFIG, bytes(data))

        if resp["status"] == RESP_STATUS_SUCCESS:
            print("✅ WiFi設定を送信しました")
            return True
        else:
            print("❌ WiFi設定の送信に失敗しました")
            return False

    async def connect_wifi(self):
        """WiFi接続"""
        print(f"\n📡 WiFi接続を開始中...")

        resp = await self.send_command(CMD_WIFI_CONNECT)

        if resp["status"] == RESP_STATUS_SUCCESS:
            print("✅ WiFi接続を開始しました")
            print("⏳ 接続完了まで数秒お待ちください...")
            return True
        else:
            print("❌ WiFi接続の開始に失敗しました")
            return False

    async def check_status(self):
        """ステータス確認"""
        print(f"\n📊 システムステータスを確認中...")

        resp = await self.send_command(CMD_GET_SYSTEM_STATUS)

        if resp["status"] == RESP_STATUS_SUCCESS and len(resp["data"]) >= 20:
            # system_status_t: uptime(4), heap_free(4), heap_min(4), task_count(4), wifi_connected(1), ble_connected(1), padding(2)
            uptime, heap_free, heap_min, task_count, wifi_connected, ble_connected = \
                struct.unpack('<IIIIBBxx', resp["data"][:20])

            print(f"\nシステムステータス:")
            print(f"  稼働時間: {uptime}秒 ({uptime//3600}時間{(uptime%3600)//60}分)")
            print(f"  空きメモリ: {heap_free:,}バイト")
            print(f"  最小空きメモリ: {heap_min:,}バイト")
            print(f"  タスク数: {task_count}")
            print(f"  WiFi接続: {'✅ 接続済み' if wifi_connected else '❌ 未接続'}")
            print(f"  BLE接続: {'✅ 接続済み' if ble_connected else '❌ 未接続'}")

            return bool(wifi_connected)
        else:
            print("❌ ステータスの取得に失敗しました")
            return False

    async def disconnect(self):
        """切断"""
        if self.client and self.client.is_connected:
            await self.client.disconnect()


async def main():
    print("""
╔══════════════════════════════════════════════════════════════╗
║                                                              ║
║     PlantMonitor WiFi セットアップツール                     ║
║     ESP32-C6 対話式設定スクリプト                            ║
║                                                              ║
╚══════════════════════════════════════════════════════════════╝
    """)

    setup = WiFiSetup()

    try:
        # デバイス検索
        address = await setup.find_device()
        if not address:
            return 1

        # 接続
        await setup.connect(address)

        # 現在の設定を表示
        current_ssid = await setup.get_current_config()

        # WiFi設定入力
        print("\n" + "="*60)
        print("🔧 WiFi設定の入力")
        print("="*60)

        use_existing_config = False
        if current_ssid:
            use_current = input(f"\n現在のSSID '{current_ssid}' を使用しますか? (y/n): ").lower()
            if use_current == 'y':
                ssid = current_ssid
                use_existing_config = True
                print("✅ 現在のSSIDとパスワードをそのまま使用します")
            else:
                ssid = input("\n新しいSSIDを入力: ").strip()
        else:
            ssid = input("\nSSIDを入力: ").strip()

        if not ssid:
            print("❌ SSIDが空です")
            return 1

        # 既存の設定を使用しない場合のみパスワード入力
        if not use_existing_config:
            password = input("パスワードを入力: ").strip()

            if not password:
                print("❌ パスワードが空です")
                return 1

            # 確認
            print(f"\n" + "="*60)
            print("📝 設定確認")
            print("="*60)
            print(f"  SSID: {ssid}")
            print(f"  Password: {'*' * len(password)}")

            confirm = input("\nこの設定でよろしいですか? (y/n): ").lower()
            if confirm != 'y':
                print("❌ キャンセルしました")
                return 1
        else:
            # 既存の設定を使用する場合は確認をスキップ
            password = None  # パスワードは既存のものを使用（設定送信をスキップ）

        # WiFi設定を送信（新しい設定の場合のみ）
        if password is not None:
            if not await setup.set_wifi_config(ssid, password):
                return 1

            await asyncio.sleep(0.5)

            # 設定確認
            await setup.get_current_config()
        else:
            print("\n✅ 既存のWiFi設定を使用します（設定送信をスキップ）")

        # WiFi接続
        connect_now = input("\n今すぐWiFi接続を開始しますか? (y/n): ").lower()
        if connect_now == 'y':
            if await setup.connect_wifi():
                print("\n⏳ 接続処理中... 5秒待機します")
                await asyncio.sleep(5)

                # ステータス確認
                wifi_connected = await setup.check_status()

                if wifi_connected:
                    print("\n" + "="*60)
                    print("🎉 WiFi接続に成功しました!")
                    print("="*60)
                    return 0
                else:
                    print("\n" + "="*60)
                    print("⚠️  WiFi接続が確認できません")
                    print("="*60)
                    print("\n考えられる原因:")
                    print("  - SSIDまたはパスワードが間違っている")
                    print("  - WiFiルーターが範囲外")
                    print("  - 接続に時間がかかっている（もう少し待ってみてください）")
                    return 1
        else:
            print("\n✅ 設定は完了しました")
            print("   デバイス再起動時または手動接続時にWiFiに接続されます")

        return 0

    except KeyboardInterrupt:
        print("\n\n❌ ユーザーによって中断されました")
        return 1
    except Exception as e:
        print(f"\n❌ エラー: {e}")
        import traceback
        traceback.print_exc()
        return 1
    finally:
        await setup.disconnect()
        print("\n👋 終了しました\n")


if __name__ == "__main__":
    exit_code = asyncio.run(main())
    sys.exit(exit_code)
