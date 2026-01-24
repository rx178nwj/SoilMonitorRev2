#!/usr/bin/env python3
"""
LED制御テストスクリプト
PlantMonitorデバイスのWS2812 LEDを制御するテストを行います

使用方法:
    pip install bleak
    python tests/test_led_control.py

必要なライブラリ:
    pip install bleak
"""

import asyncio
import struct
import sys
from bleak import BleakClient, BleakScanner

# BLE UUIDs (ble_manager.cと一致)
SERVICE_UUID = "59462f12-9543-9999-12c8-58b459a2712d"
COMMAND_UUID = "6a3b2c1d-4e5f-6a7b-8c9d-e0f123456791"
RESPONSE_UUID = "6a3b2c1d-4e5f-6a7b-8c9d-e0f123456792"

# コマンドID
CMD_CONTROL_LED = 0x18
CMD_SET_LED_BRIGHTNESS = 0x19

# レスポンスステータス
RESP_STATUS_SUCCESS = 0x00
RESP_STATUS_ERROR = 0x01

# グローバル変数
response_received = asyncio.Event()
response_data = None
sequence_num = 0


def create_command_packet(command_id: int, data: bytes = b'') -> bytes:
    """コマンドパケットを作成"""
    global sequence_num
    sequence_num = (sequence_num + 1) % 256

    # パケット構造: command_id(1) + sequence_num(1) + data_length(2) + data(n)
    data_length = len(data)
    packet = struct.pack('<BBH', command_id, sequence_num, data_length) + data
    return packet


def parse_response_packet(data: bytes) -> dict:
    """レスポンスパケットをパース"""
    if len(data) < 4:
        return {'error': 'パケットが短すぎます'}

    response_id, status_code, seq_num, data_length = struct.unpack('<BBBB', data[:4])
    payload = data[4:4+data_length] if data_length > 0 else b''

    return {
        'response_id': response_id,
        'status_code': status_code,
        'sequence_num': seq_num,
        'data_length': data_length,
        'payload': payload
    }


def notification_handler(sender, data):
    """レスポンス通知ハンドラ"""
    global response_data
    response_data = data
    response_received.set()


async def scan_devices(timeout: float = 5.0, name_filter: str = "PlantMonitor") -> list:
    """BLEデバイスをスキャン"""
    print(f"🔍 BLEデバイスをスキャン中... ({timeout}秒)")

    devices = await BleakScanner.discover(timeout=timeout)
    filtered_devices = [d for d in devices if d.name and d.name.startswith(name_filter)]
    filtered_devices.sort(key=lambda d: d.name)

    return filtered_devices


async def select_device() -> str:
    """デバイスをスキャンして選択"""
    devices = await scan_devices()

    if not devices:
        print("❌ PlantMonitorデバイスが見つかりませんでした")
        return None

    print(f"\n✅ {len(devices)} 個のデバイスが見つかりました")
    print("=" * 60)
    for i, device in enumerate(devices, 1):
        print(f"  {i:2d}. {device.name} ({device.address})")
    print("=" * 60)

    if len(devices) == 1:
        return devices[0].address

    while True:
        try:
            choice = input(f"\n接続するデバイス番号を入力 (1-{len(devices)}, 0で終了): ").strip()
            if choice == "0": return None
            idx = int(choice) - 1
            if 0 <= idx < len(devices):
                return devices[idx].address
        except ValueError:
            pass


async def send_command(client: BleakClient, command_id: int, data: bytes = b'', timeout: float = 5.0) -> dict:
    """コマンドを送信してレスポンスを待つ"""
    global response_data
    response_received.clear()
    response_data = None

    packet = create_command_packet(command_id, data)
    # print(f"📤 コマンド送信: ID=0x{command_id:02X}, データ={packet.hex()}")

    await client.write_gatt_char(COMMAND_UUID, packet)

    try:
        await asyncio.wait_for(response_received.wait(), timeout=timeout)
        if response_data:
            return parse_response_packet(response_data)
    except asyncio.TimeoutError:
        return {'error': 'タイムアウト'}

    return {'error': 'レスポンスなし'}


async def test_led_control(address: str):
    """LED制御テスト"""
    print(f"\n📡 {address} に接続中...")

    async with BleakClient(address) as client:
        print(f"✅ 接続成功!")
        await client.start_notify(RESPONSE_UUID, notification_handler)

        colors = [
            ("赤 (輝度50%)", 255, 0, 0, 50, 1000),
            ("緑 (輝度50%)", 0, 255, 0, 50, 1000),
            ("青 (輝度50%)", 0, 0, 255, 50, 1000),
            ("白 (輝度10%)", 255, 255, 255, 10, 1000),
            ("白 (輝度100%)", 255, 255, 255, 100, 1000),
            ("消灯", 0, 0, 0, 0, 0)
        ]

        print("\n🎨 LEDカラー・輝度制御テスト開始")
        print("=" * 60)

        for name, r, g, b, bright, duration in colors:
            print(f"  ➡️  {name} (R={r}, G={g}, B={b}, Bright={bright}%, Time={duration}ms) 送信...")
            
            # コマンドデータ作成 (R, G, B, Brightness, Duration_LSB, Duration_MSB)
            # Durationは uint16_t なので Little Endian でパック
            payload = struct.pack('<BBBBH', r, g, b, bright, duration)
            
            response = await send_command(client, CMD_CONTROL_LED, payload)
            
            if response.get('status_code') == RESP_STATUS_SUCCESS:
                print(f"     ✅ 成功")
            else:
                print(f"     ❌ 失敗: {response}")
            
            # 点灯時間分待機 + 少し余裕
            wait_time = (duration / 1000.0) + 0.5 if duration > 0 else 1.0
            await asyncio.sleep(wait_time)

        print("\n🔅 LED輝度変更テスト開始")
        print("=" * 60)
        
        # まず白で点灯（輝度10%）
        print(f"  ➡️  初期点灯: 白 (輝度10%)")
        payload = struct.pack('<BBBBH', 255, 255, 255, 10, 0) # 持続時間0=常時点灯
        await send_command(client, CMD_CONTROL_LED, payload)
        await asyncio.sleep(1.0)

        # 輝度を変更していく
        brightness_levels = [20, 50, 80, 100, 10, 20]
        for bright in brightness_levels:
            print(f"  ➡️  輝度変更: {bright}%")
            
            # コマンドデータ作成 (Brightness)
            payload = struct.pack('<B', bright)
            
            response = await send_command(client, CMD_SET_LED_BRIGHTNESS, payload)
            
            if response.get('status_code') == RESP_STATUS_SUCCESS:
                print(f"     ✅ 成功")
            else:
                print(f"     ❌ 失敗: {response}")
            
            await asyncio.sleep(0.5)

        print(f"\n✅ テスト完了 (LEDは輝度20%で点灯中)")

        await client.stop_notify(RESPONSE_UUID)


if __name__ == "__main__":
    try:
        print("=" * 60)
        print("💡 PlantMonitor LED制御テスト")
        print("=" * 60)
        
        # Windowsの場合のイベントループポリシー設定
        if sys.platform == 'win32':
            asyncio.set_event_loop_policy(asyncio.WindowsBluetoothEventLoopPolicy())

        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        
        address = loop.run_until_complete(select_device())
        if address:
            loop.run_until_complete(test_led_control(address))
            
    except KeyboardInterrupt:
        print("\n⏹️ 終了")
    except Exception as e:
        print(f"\n❌ エラー: {e}")
        sys.exit(1)
