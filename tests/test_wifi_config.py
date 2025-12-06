#!/usr/bin/env python3
"""
WiFi Configuration Test Script for PlantMonitor ESP32-C6
Raspberry Pi用のBLE WiFi設定テストスクリプト

必要なパッケージ:
pip3 install bleak

使用方法:
python3 test_wifi_config.py --ssid "YourSSID" --password "YourPassword"
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
CMD_SET_WIFI_CONFIG = 0x0D
CMD_GET_WIFI_CONFIG = 0x0E
CMD_WIFI_CONNECT = 0x0F
CMD_GET_SYSTEM_STATUS = 0x02
CMD_GET_TIMEZONE = 0x10

# Response Status
RESP_STATUS_SUCCESS = 0x00
RESP_STATUS_ERROR = 0x01

class PlantMonitorWiFiTester:
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

    async def set_wifi_config(self, ssid, password):
        """WiFi設定を送信"""
        print(f"\n📡 Setting WiFi configuration...")
        print(f"   SSID: {ssid}")
        print(f"   Password: {'*' * len(password)}")

        # wifi_config_data_t構造体を構築（96バイト）
        ssid_bytes = ssid.encode('utf-8')[:31]  # 最大31文字 + NULL
        password_bytes = password.encode('utf-8')[:63]  # 最大63文字 + NULL

        data = bytearray(96)
        data[0:len(ssid_bytes)] = ssid_bytes
        data[32:32+len(password_bytes)] = password_bytes

        resp = await self.send_command(CMD_SET_WIFI_CONFIG, bytes(data))

        if resp["status"] == RESP_STATUS_SUCCESS:
            print("✅ WiFi configuration set successfully")
            return True
        else:
            print(f"❌ Failed to set WiFi configuration (status: {resp['status']})")
            return False

    async def get_wifi_config(self):
        """WiFi設定を取得"""
        print(f"\n📡 Getting WiFi configuration...")

        resp = await self.send_command(CMD_GET_WIFI_CONFIG)

        if resp["status"] != RESP_STATUS_SUCCESS:
            print(f"❌ Failed to get WiFi configuration (status: {resp['status']})")
            return None

        # WiFi設定をパース
        ssid = resp["data"][:32].decode('utf-8').rstrip('\x00')
        password_masked = resp["data"][32:96].decode('utf-8').rstrip('\x00')

        print(f"✅ Current WiFi configuration:")
        print(f"   SSID: {ssid}")
        print(f"   Password (masked): {password_masked}")

        return {
            "ssid": ssid,
            "password": password_masked
        }

    async def wifi_connect(self):
        """WiFi接続を実行"""
        print(f"\n📡 Starting WiFi connection...")

        resp = await self.send_command(CMD_WIFI_CONNECT)

        if resp["status"] == RESP_STATUS_SUCCESS:
            print("✅ WiFi connection started successfully")
            print("⏳ Note: Actual connection is asynchronous. Check status after a few seconds.")
            return True
        else:
            print(f"❌ Failed to start WiFi connection (status: {resp['status']})")
            return False

    async def get_system_status(self):
        """システムステータスを取得"""
        print(f"\n📊 Getting system status...")

        resp = await self.send_command(CMD_GET_SYSTEM_STATUS)

        if resp["status"] != RESP_STATUS_SUCCESS:
            print(f"❌ Failed to get system status (status: {resp['status']})")
            return None

        # システムステータスをパース（system_status_t構造体）
        # struct: uptime(4), heap_free(4), heap_min(4), task_count(4), current_time(4), wifi_connected(1), ble_connected(1), padding(2)
        if len(resp["data"]) >= 24:
            uptime, heap_free, heap_min, task_count, current_time, wifi_connected, ble_connected = \
                struct.unpack('<IIIIIBBxx', resp["data"][:24])

            # UNIXタイムスタンプを日時に変換
            from datetime import datetime
            if current_time > 0:
                device_time = datetime.fromtimestamp(current_time).strftime('%Y-%m-%d %H:%M:%S')
            else:
                device_time = "Not set"

            print(f"✅ System Status:")
            print(f"   Uptime: {uptime} seconds")
            print(f"   Device time: {device_time}")
            print(f"   Free heap: {heap_free} bytes")
            print(f"   Min heap: {heap_min} bytes")
            print(f"   Task count: {task_count}")
            print(f"   WiFi connected: {'Yes' if wifi_connected else 'No'}")
            print(f"   BLE connected: {'Yes' if ble_connected else 'No'}")

            return {
                "uptime": uptime,
                "current_time": current_time,
                "device_time": device_time,
                "heap_free": heap_free,
                "heap_min": heap_min,
                "task_count": task_count,
                "wifi_connected": bool(wifi_connected),
                "ble_connected": bool(ble_connected)
            }

        return None

    async def get_timezone(self):
        """タイムゾーンを取得"""
        print(f"\n🌏 Getting timezone...")

        resp = await self.send_command(CMD_GET_TIMEZONE)

        if resp["status"] != RESP_STATUS_SUCCESS:
            print(f"❌ Failed to get timezone (status: {resp['status']})")
            return None

        # タイムゾーン文字列をパース
        timezone = resp["data"].decode('utf-8').rstrip('\x00')

        print(f"✅ Device timezone: {timezone}")
        return timezone

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
        description='WiFi Configuration Test for PlantMonitor ESP32-C6',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # WiFi設定のみ（接続は実行しない）
  python3 test_wifi_config.py --ssid "MyNetwork" --password "MyPassword" --no-connect

  # WiFi設定と接続
  python3 test_wifi_config.py --ssid "MyNetwork" --password "MyPassword"

  # 現在の設定を確認のみ
  python3 test_wifi_config.py --get-only

  # タイムゾーン情報を取得
  python3 test_wifi_config.py --get-timezone

  # 特定のデバイスに接続
  python3 test_wifi_config.py --address "AA:BB:CC:DD:EE:FF" --ssid "MyNetwork" --password "MyPassword"
        """
    )

    parser.add_argument('--ssid', type=str, help='WiFi SSID')
    parser.add_argument('--password', type=str, help='WiFi Password')
    parser.add_argument('--address', type=str, help='Device BLE address (if known)')
    parser.add_argument('--device-name', type=str, default='PlantMonitor',
                       help='Device name prefix (default: PlantMonitor)')
    parser.add_argument('--no-connect', action='store_true',
                       help='Set WiFi config but do not connect')
    parser.add_argument('--get-only', action='store_true',
                       help='Only get current WiFi config, do not set')
    parser.add_argument('--check-status', action='store_true',
                       help='Check system status after operations')
    parser.add_argument('--get-timezone', action='store_true',
                       help='Get device timezone information')

    args = parser.parse_args()

    # パラメータチェック
    if not args.get_only and not args.get_timezone and (args.ssid is None or args.password is None):
        parser.error("--ssid and --password are required unless --get-only or --get-timezone is specified")

    tester = PlantMonitorWiFiTester(device_name_prefix=args.device_name)

    try:
        # 接続
        await tester.connect(address=args.address)

        if args.get_timezone:
            # タイムゾーン情報を取得
            await tester.get_timezone()
            return 0
        elif args.get_only:
            # 現在の設定を取得のみ
            await tester.get_wifi_config()
        else:
            # WiFi設定を送信
            success = await tester.set_wifi_config(args.ssid, args.password)

            if not success:
                print("\n❌ WiFi configuration failed")
                return 1

            # 設定を確認
            await asyncio.sleep(0.5)
            await tester.get_wifi_config()

            # WiFi接続を実行
            if not args.no_connect:
                await asyncio.sleep(0.5)
                success = await tester.wifi_connect()

                if success:
                    print("\n⏳ WiFi connection started. Checking connection status...")

                    # 初期待機
                    await asyncio.sleep(10)

                    # 最大5回、5秒間隔でステータスをチェック
                    max_retries = 5
                    wifi_connected = False

                    for retry in range(max_retries):
                        print(f"\n📊 Checking connection status ({retry + 1}/{max_retries})...")
                        status = await tester.get_system_status()

                        if status and status.get('wifi_connected'):
                            wifi_connected = True
                            break

                        if retry < max_retries - 1:
                            print("⏳ Waiting for connection... Rechecking in 5 seconds")
                            await asyncio.sleep(5)

                    if wifi_connected:
                        print("\n🎉 SUCCESS: Device is connected to WiFi!")
                        return 0
                    else:
                        print("\n⚠️  WARNING: Device is not connected to WiFi")
                        print("   Possible causes:")
                        print("   - Incorrect SSID or password")
                        print("   - WiFi router out of range")
                        print("   - Router is 5GHz (ESP32-C6 only supports 2.4GHz)")
                        return 1

        # システムステータスをチェック（--check-statusオプション使用時）
        if args.check_status and args.no_connect:
            await asyncio.sleep(0.5)
            status = await tester.get_system_status()

            if status and status.get('wifi_connected'):
                print("\n✅ Device is connected to WiFi")
                return 0
            else:
                print("\n❌ Device is not connected to WiFi")
                return 1

        return 0

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
