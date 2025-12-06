#!/usr/bin/env python3
"""
Plant Profile Test Script for PlantMonitor ESP32-C6
Raspberry Pi用のBLE植物プロファイル取得テストスクリプト

必要なパッケージ:
pip3 install bleak

使用方法:
python3 test_plant_profile.py
python3 test_plant_profile.py --address "AA:BB:CC:DD:EE:FF"
"""

import asyncio
import argparse
import struct
import sys
from bleak import BleakClient, BleakScanner

# UUIDs
SERVICE_UUID = "592F4612-9543-9999-12C8-58B459A2712D"
COMMAND_UUID = "6A3B2C1D-4E5F-6A7B-8C9D-E0F123456791"
RESPONSE_UUID = "6A3B2C1D-4E5F-6A7B-8C9D-E0F123456792"

# Commands
CMD_GET_PLANT_PROFILE = 0x0C

# Response Status
RESP_STATUS_SUCCESS = 0x00
RESP_STATUS_ERROR = 0x01

class PlantMonitorTester:
    def __init__(self):
        self.client = None
        self.response_data = None
        self.sequence_num = 0

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

    def response_handler(self, sender, data):
        """レスポンス通知ハンドラ"""
        self.response_data = bytes(data)

    async def connect(self, address):
        """デバイスに接続"""
        print(f"🔗 接続中...")
        self.client = BleakClient(address)
        await self.client.connect()

        # レスポンス通知を有効化
        await self.client.start_notify(RESPONSE_UUID, self.response_handler)
        print(f"✅ 接続完了")

    async def send_command(self, command_id, data=b''):
        """コマンド送信とレスポンス受信"""
        self.sequence_num = (self.sequence_num + 1) % 256

        # コマンドパケット構築
        packet = struct.pack('<BBH', command_id, self.sequence_num, len(data))
        packet += data

        # レスポンスをクリア
        self.response_data = None

        # コマンド送信
        await self.client.write_gatt_char(COMMAND_UUID, packet)

        # レスポンス待機（最大5秒）
        for _ in range(50):
            await asyncio.sleep(0.1)
            if self.response_data is not None:
                break

        if self.response_data is None:
            raise Exception("No response received")

        # レスポンスパース
        if len(self.response_data) < 4:
            raise Exception("Invalid response length")

        response_id, status, seq, data_len = struct.unpack('<BBBH', self.response_data[:5])
        response_payload = self.response_data[5:] if len(self.response_data) > 5 else b''

        return {
            "response_id": response_id,
            "status": status,
            "sequence_num": seq,
            "data_length": data_len,
            "data": response_payload
        }

    async def get_plant_profile(self):
        """植物プロファイルを取得"""
        print(f"\n🌿 Getting plant profile...")

        resp = await self.send_command(CMD_GET_PLANT_PROFILE)

        if resp["status"] != RESP_STATUS_SUCCESS:
            print(f"❌ Failed to get plant profile (status: {resp['status']})")
            return None

        # plant_profile_t構造体をパース
        # struct plant_profile_t {
        #     char plant_name[32];
        #     float soil_dry_threshold;
        #     float soil_wet_threshold;
        #     int soil_dry_days_for_watering;
        #     float temp_high_limit;
        #     float temp_low_limit;
        # }; (32 + 4 + 4 + 4 + 4 + 4 = 52 bytes)
        if len(resp["data"]) < 52:
            print(f"❌ Invalid data length for plant profile: {len(resp['data'])}")
            return None

        (plant_name_bytes,
         soil_dry_threshold,
         soil_wet_threshold,
         soil_dry_days_for_watering,
         temp_high_limit,
         temp_low_limit) = struct.unpack('<32sffiff', resp["data"][:52])

        plant_name = plant_name_bytes.decode('utf-8').rstrip('\x00')

        print("✅ Current Plant Profile:")
        print(f"   Plant Name: {plant_name}")
        print(f"   Soil Dry Threshold: {soil_dry_threshold:.2f} mV")
        print(f"   Soil Wet Threshold: {soil_wet_threshold:.2f} mV")
        print(f"   Watering Trigger (dry days): {soil_dry_days_for_watering} days")
        print(f"   High Temperature Limit: {temp_high_limit:.2f} °C")
        print(f"   Low Temperature Limit: {temp_low_limit:.2f} °C")

        return {
            "plant_name": plant_name,
            "soil_dry_threshold": soil_dry_threshold,
            "soil_wet_threshold": soil_wet_threshold,
            "soil_dry_days_for_watering": soil_dry_days_for_watering,
            "temp_high_limit": temp_high_limit,
            "temp_low_limit": temp_low_limit,
        }

    async def disconnect(self):
        """切断"""
        if self.client and self.client.is_connected:
            try:
                await self.client.disconnect()
                print("\n👋 Disconnected")
            except Exception as e:
                # 切断エラーは無視
                pass


async def main():
    print("""
╔══════════════════════════════════════════════════════════════╗
║                                                              ║
║     PlantMonitor Plant Profile Test Tool                     ║
║                                                              ║
╚══════════════════════════════════════════════════════════════╝
    """)
    tester = PlantMonitorTester()

    try:
        # デバイス検索
        address = await tester.find_device()
        if not address:
            return 1

        # 接続
        await tester.connect(address=address)

        # 植物プロファイルを取得
        await tester.get_plant_profile()

        print("\n" + "="*60)
        print("✅ すべての処理が完了しました")
        print("="*60)
        return 0

    except KeyboardInterrupt:
        print("\n\n❌ ユーザーによって中断されました")
        return 1
    except Exception as e:
        print(f"\n❌ Error: {e}")
        import traceback
        traceback.print_exc()
        return 1

    finally:
        await tester.disconnect()
        print("\n👋 終了しました\n")
