#!/usr/bin/env python3
"""
Plant Profile Test Script for PlantMonitor ESP32-C6
Raspberry Pi用のBLE Plant Profile取得テストスクリプト

必要なパッケージ:
pip3 install bleak

使用方法:
python3 test_plant_profile.py
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
CMD_SET_PLANT_PROFILE = 0x03
CMD_SAVE_PLANT_PROFILE = 0x14

# Response Status
RESP_STATUS_SUCCESS = 0x00
RESP_STATUS_ERROR = 0x01

class PlantProfileTester:
    def __init__(self, device_name_prefix="PlantMonitor"):
        self.device_name_prefix = device_name_prefix
        self.client = None
        self.response_data = None
        self.sequence_num = 0

    async def find_device(self, timeout=10.0):
        """デバイスを検索"""
        print(f"🔍 Scanning for devices with name starting with '{self.device_name_prefix}'...")

        devices = await BleakScanner.discover(timeout=timeout)

        for device in devices:
            if device.name and device.name.startswith(self.device_name_prefix):
                print(f"✅ Found device: {device.name} ({device.address})")
                return device.address

        print(f"❌ No device found with prefix '{self.device_name_prefix}'")
        return None

    def response_handler(self, sender, data):
        """レスポンス通知ハンドラ"""
        self.response_data = bytes(data)

    async def connect(self, address=None):
        """デバイスに接続"""
        if address is None:
            address = await self.find_device()
            if address is None:
                raise Exception("Device not found")

        print(f"🔗 Connecting to {address}...")
        self.client = BleakClient(address)
        await self.client.connect()

        # レスポンス通知を有効化
        await self.client.start_notify(RESPONSE_UUID, self.response_handler)
        print(f"✅ Connected to {address}")

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
        print(f"\n🌱 Getting plant profile...")

        resp = await self.send_command(CMD_GET_PLANT_PROFILE)

        if resp["status"] != RESP_STATUS_SUCCESS:
            print(f"❌ Failed to get plant profile (status: {resp['status']})")
            return None

        # plant_profile_t構造体をパース
        # struct plant_profile_t {
        #     char plant_name[32];              // 32 bytes
        #     float soil_dry_threshold;         // 4 bytes
        #     float soil_wet_threshold;         // 4 bytes
        #     int soil_dry_days_for_watering;   // 4 bytes
        #     float temp_high_limit;            // 4 bytes
        #     float temp_low_limit;             // 4 bytes
        #     float watering_threshold_mv;      // 4 bytes
        # };  // Total: 60 bytes

        if len(resp["data"]) < 60:
            print(f"❌ Invalid plant profile data length: {len(resp['data'])} (expected 60)")
            return None

        # 植物名（32バイト）を抽出
        plant_name_bytes = resp["data"][:32]
        plant_name = plant_name_bytes.decode('utf-8', errors='ignore').rstrip('\x00')

        # 残りのパラメータを抽出（little-endian）
        soil_dry_threshold, soil_wet_threshold, soil_dry_days, temp_high_limit, temp_low_limit, watering_threshold = \
            struct.unpack('<ffifff', resp["data"][32:60])

        print(f"✅ Plant Profile:")
        print(f"   Plant Name: {plant_name}")
        print(f"   Soil Dry Threshold: {soil_dry_threshold:.1f} mV")
        print(f"   Soil Wet Threshold: {soil_wet_threshold:.1f} mV")
        print(f"   Soil Dry Days for Watering: {soil_dry_days} days")
        print(f"   Temperature High Limit: {temp_high_limit:.1f} °C")
        print(f"   Temperature Low Limit: {temp_low_limit:.1f} °C")
        print(f"   Watering Detection Threshold: {watering_threshold:.1f} mV")

        return {
            "plant_name": plant_name,
            "soil_dry_threshold": soil_dry_threshold,
            "soil_wet_threshold": soil_wet_threshold,
            "soil_dry_days_for_watering": soil_dry_days,
            "temp_high_limit": temp_high_limit,
            "temp_low_limit": temp_low_limit,
            "watering_threshold_mv": watering_threshold
        }

    async def set_plant_profile(self, profile):
        """植物プロファイルを設定"""
        print(f"\n🌱 Setting plant profile...")

        # plant_profile_t構造体を構築（60バイト）
        plant_name_bytes = profile["plant_name"].encode('utf-8')[:31]  # 最大31文字 + NULL

        data = bytearray(60)
        # 植物名（32バイト）
        data[0:len(plant_name_bytes)] = plant_name_bytes
        # 残りのパラメータ（little-endian）
        struct.pack_into('<ffifff', data, 32,
                         profile["soil_dry_threshold"],
                         profile["soil_wet_threshold"],
                         profile["soil_dry_days_for_watering"],
                         profile["temp_high_limit"],
                         profile["temp_low_limit"],
                         profile.get("watering_threshold_mv", 200.0))

        resp = await self.send_command(CMD_SET_PLANT_PROFILE, bytes(data))

        if resp["status"] == RESP_STATUS_SUCCESS:
            print(f"✅ Plant profile set successfully")
            return True
        else:
            print(f"❌ Failed to set plant profile (status: {resp['status']})")
            return False

    async def save_plant_profile(self):
        """植物プロファイルをNVSに保存"""
        print(f"\n💾 Saving plant profile to NVS...")

        resp = await self.send_command(CMD_SAVE_PLANT_PROFILE)

        if resp["status"] == RESP_STATUS_SUCCESS:
            print(f"✅ Plant profile saved to NVS successfully")
            print(f"   Profile will be retained after device restart")
            return True
        else:
            print(f"❌ Failed to save plant profile to NVS (status: {resp['status']})")
            return False

    async def disconnect(self):
        """切断"""
        if self.client and self.client.is_connected:
            try:
                await self.client.disconnect()
                print("\n👋 Disconnected")
            except Exception as e:
                # 切断エラーは無視（既に切断済みの可能性がある）
                pass


async def main():
    parser = argparse.ArgumentParser(
        description='Plant Profile Test for PlantMonitor ESP32-C6',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # 植物プロファイルを取得
  python3 test_plant_profile.py

  # 植物プロファイルを取得してNVSに保存
  python3 test_plant_profile.py --save

  # 対話式で植物プロファイルを設定
  python3 test_plant_profile.py --set

  # 特定のデバイスに接続
  python3 test_plant_profile.py --address "AA:BB:CC:DD:EE:FF"

  # デバイス名プレフィックスを指定
  python3 test_plant_profile.py --device-name "MyPlant"
        """
    )

    parser.add_argument('--address', type=str, help='Device BLE address (if known)')
    parser.add_argument('--device-name', type=str, default='PlantMonitor',
                       help='Device name prefix (default: PlantMonitor)')
    parser.add_argument('--set', action='store_true',
                       help='Set plant profile (interactive mode)')
    parser.add_argument('--save', action='store_true',
                       help='Save current profile to NVS after reading')

    args = parser.parse_args()

    tester = PlantProfileTester(device_name_prefix=args.device_name)

    try:
        # 接続
        await tester.connect(address=args.address)

        if args.set:
            # 対話式でプロファイルを設定
            print("\n" + "="*60)
            print("🔧 Plant Profile Setup (Interactive Mode)")
            print("="*60)

            # 現在のプロファイルを取得
            current_profile = await tester.get_plant_profile()

            # 新しいプロファイルを入力
            print("\nEnter new plant profile (press Enter to keep current value):")

            plant_name = input(f"Plant Name [{current_profile['plant_name'] if current_profile else 'Succulent Plant'}]: ").strip()
            if not plant_name:
                plant_name = current_profile['plant_name'] if current_profile else 'Succulent Plant'

            soil_dry_str = input(f"Soil Dry Threshold (mV) [{current_profile['soil_dry_threshold'] if current_profile else 2500.0}]: ").strip()
            soil_dry = float(soil_dry_str) if soil_dry_str else (current_profile['soil_dry_threshold'] if current_profile else 2500.0)

            soil_wet_str = input(f"Soil Wet Threshold (mV) [{current_profile['soil_wet_threshold'] if current_profile else 1000.0}]: ").strip()
            soil_wet = float(soil_wet_str) if soil_wet_str else (current_profile['soil_wet_threshold'] if current_profile else 1000.0)

            dry_days_str = input(f"Dry Days for Watering [{current_profile['soil_dry_days_for_watering'] if current_profile else 3}]: ").strip()
            dry_days = int(dry_days_str) if dry_days_str else (current_profile['soil_dry_days_for_watering'] if current_profile else 3)

            temp_high_str = input(f"Temperature High Limit (°C) [{current_profile['temp_high_limit'] if current_profile else 35.0}]: ").strip()
            temp_high = float(temp_high_str) if temp_high_str else (current_profile['temp_high_limit'] if current_profile else 35.0)

            temp_low_str = input(f"Temperature Low Limit (°C) [{current_profile['temp_low_limit'] if current_profile else 5.0}]: ").strip()
            temp_low = float(temp_low_str) if temp_low_str else (current_profile['temp_low_limit'] if current_profile else 5.0)

            watering_threshold_str = input(f"Watering Detection Threshold (mV) [{current_profile.get('watering_threshold_mv', 200.0) if current_profile else 200.0}]: ").strip()
            watering_threshold = float(watering_threshold_str) if watering_threshold_str else (current_profile.get('watering_threshold_mv', 200.0) if current_profile else 200.0)

            new_profile = {
                "plant_name": plant_name,
                "soil_dry_threshold": soil_dry,
                "soil_wet_threshold": soil_wet,
                "soil_dry_days_for_watering": dry_days,
                "temp_high_limit": temp_high,
                "temp_low_limit": temp_low,
                "watering_threshold_mv": watering_threshold
            }

            # 確認
            print("\n" + "="*60)
            print("📝 New Profile Confirmation")
            print("="*60)
            print(f"   Plant Name: {new_profile['plant_name']}")
            print(f"   Soil Dry Threshold: {new_profile['soil_dry_threshold']:.1f} mV")
            print(f"   Soil Wet Threshold: {new_profile['soil_wet_threshold']:.1f} mV")
            print(f"   Soil Dry Days for Watering: {new_profile['soil_dry_days_for_watering']} days")
            print(f"   Temperature High Limit: {new_profile['temp_high_limit']:.1f} °C")
            print(f"   Temperature Low Limit: {new_profile['temp_low_limit']:.1f} °C")
            print(f"   Watering Detection Threshold: {new_profile['watering_threshold_mv']:.1f} mV")

            confirm = input("\nApply this profile? (y/n): ").lower()
            if confirm == 'y':
                if await tester.set_plant_profile(new_profile):
                    # 設定後、再度取得して確認
                    await asyncio.sleep(0.5)
                    await tester.get_plant_profile()

                    # NVS保存を提案
                    save_now = input("\nSave profile to NVS? (retained after restart) (y/n): ").lower()
                    if save_now == 'y':
                        await tester.save_plant_profile()

                    print("\n🎉 SUCCESS: Plant profile updated!")
                    return 0
                else:
                    print("\n❌ FAILED: Could not set plant profile")
                    return 1
            else:
                print("\n❌ Cancelled")
                return 1
        else:
            # 植物プロファイルを取得
            profile = await tester.get_plant_profile()

            if profile:
                # --saveオプションが指定されている場合、NVSに保存
                if args.save:
                    await asyncio.sleep(0.5)
                    await tester.save_plant_profile()

                print("\n🎉 SUCCESS: Plant profile retrieved successfully!")
                return 0
            else:
                print("\n❌ FAILED: Could not retrieve plant profile")
                return 1

    except Exception as e:
        print(f"\n❌ Error: {e}")
        import traceback
        traceback.print_exc()
        return 1

    finally:
        await tester.disconnect()


if __name__ == "__main__":
    exit_code = asyncio.run(main())
    sys.exit(exit_code)
