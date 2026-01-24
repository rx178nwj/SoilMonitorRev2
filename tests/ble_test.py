#!/usr/bin/env python3
"""
BLE通信テストスクリプト
PlantMonitorデバイスに接続してセンサーデータを取得します

使用方法:
    pip install bleak
    python ble_test.py

必要なライブラリ:
    pip install bleak
"""

import asyncio
import struct
import sys
from datetime import datetime
from bleak import BleakClient, BleakScanner

# BLE UUIDs (ble_manager.cと一致)
SERVICE_UUID = "59462f12-9543-9999-12c8-58b459a2712d"
COMMAND_UUID = "6a3b2c1d-4e5f-6a7b-8c9d-e0f123456791"
RESPONSE_UUID = "6a3b2c1d-4e5f-6a7b-8c9d-e0f123456792"
SENSOR_DATA_UUID = "6a3b2c01-4e5f-6a7b-8c9d-e0f123456789"

# コマンドID
CMD_GET_SENSOR_DATA = 0x01
CMD_GET_SYSTEM_STATUS = 0x02
CMD_GET_PLANT_PROFILE = 0x0C
CMD_GET_DEVICE_INFO = 0x06
CMD_GET_SENSOR_DATA_V2 = 0x21

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


def parse_sensor_data(payload: bytes) -> dict:
    """センサーデータをパース"""
    if len(payload) < 60:
        return {'error': f'ペイロードが短すぎます: {len(payload)} bytes'}

    try:
        # soil_data_t構造体をパース
        # struct tm (9 * 4 = 36 bytes) + float fields
        offset = 0

        # data_version (uint8_t)
        data_version = payload[offset]
        offset += 1

        # パディング (3 bytes)
        offset += 3

        # struct tm (9 int32_t = 36 bytes)
        tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst = struct.unpack('<9i', payload[offset:offset+36])
        offset += 36

        # float fields
        temperature, humidity, lux, soil_moisture = struct.unpack('<4f', payload[offset:offset+16])
        offset += 16

        # sensor_error (uint8_t)
        sensor_error = payload[offset]
        offset += 1

        # パディング (3 bytes)
        offset += 3

        # soil_temperature1, soil_temperature2
        soil_temp1, soil_temp2 = struct.unpack('<2f', payload[offset:offset+8])
        offset += 8

        # soil_moisture_capacitance[4]
        capacitance = struct.unpack('<4f', payload[offset:offset+16])

        # 日時を構築
        try:
            dt = datetime(1900 + tm_year, tm_mon + 1, tm_mday, tm_hour, tm_min, tm_sec)
            datetime_str = dt.strftime('%Y-%m-%d %H:%M:%S')
        except:
            datetime_str = f'{1900+tm_year}-{tm_mon+1:02d}-{tm_mday:02d} {tm_hour:02d}:{tm_min:02d}:{tm_sec:02d}'

        return {
            'data_version': data_version,
            'datetime': datetime_str,
            'temperature': temperature,
            'humidity': humidity,
            'lux': lux,
            'soil_moisture': soil_moisture,
            'sensor_error': sensor_error,
            'soil_temperature1': soil_temp1,
            'soil_temperature2': soil_temp2,
            'capacitance': list(capacitance)
        }
    except Exception as e:
        return {'error': str(e), 'raw': payload.hex()}


def parse_device_info(payload: bytes) -> dict:
    """デバイス情報をパース"""
    try:
        # device_name (32 bytes) + firmware_version (16 bytes) + hardware_version (16 bytes)
        # + uptime_seconds (4 bytes) + total_sensor_readings (4 bytes)
        device_name = payload[0:32].decode('utf-8').rstrip('\x00')
        firmware_version = payload[32:48].decode('utf-8').rstrip('\x00')
        hardware_version = payload[48:64].decode('utf-8').rstrip('\x00')
        uptime, total_readings = struct.unpack('<II', payload[64:72])

        return {
            'device_name': device_name,
            'firmware_version': firmware_version,
            'hardware_version': hardware_version,
            'uptime_seconds': uptime,
            'total_sensor_readings': total_readings
        }
    except Exception as e:
        return {'error': str(e), 'raw': payload.hex()}


def parse_system_status(payload: bytes) -> dict:
    """システムステータスをパース"""
    try:
        # uptime_seconds(4) + heap_free(4) + heap_min(4) + task_count(4)
        # + current_time(4) + wifi_connected(1) + ble_connected(1)
        uptime, heap_free, heap_min, task_count, current_time = struct.unpack('<5I', payload[0:20])
        wifi_connected = payload[20]
        ble_connected = payload[21]

        return {
            'uptime_seconds': uptime,
            'heap_free': heap_free,
            'heap_min': heap_min,
            'task_count': task_count,
            'current_time': current_time,
            'wifi_connected': bool(wifi_connected),
            'ble_connected': bool(ble_connected)
        }
    except Exception as e:
        return {'error': str(e), 'raw': payload.hex()}


def notification_handler(sender, data):
    """レスポンス通知ハンドラ"""
    global response_data
    response_data = data
    response_received.set()


async def find_device(device_name_prefix: str = "PlantMonitor") -> str:
    """デバイスをスキャンして見つける"""
    print(f"🔍 '{device_name_prefix}'で始まるデバイスをスキャン中...")

    devices = await BleakScanner.discover(timeout=10.0)

    for device in devices:
        if device.name and device.name.startswith(device_name_prefix):
            print(f"✅ デバイス発見: {device.name} ({device.address})")
            return device.address

    print("❌ デバイスが見つかりませんでした")
    print("\n検出されたデバイス一覧:")
    for device in devices:
        print(f"  - {device.name or '(名前なし)'}: {device.address}")

    return None


async def send_command(client: BleakClient, command_id: int, data: bytes = b'', timeout: float = 5.0) -> dict:
    """コマンドを送信してレスポンスを待つ"""
    global response_data
    response_received.clear()
    response_data = None

    packet = create_command_packet(command_id, data)
    print(f"📤 コマンド送信: ID=0x{command_id:02X}, データ={packet.hex()}")

    await client.write_gatt_char(COMMAND_UUID, packet)

    try:
        await asyncio.wait_for(response_received.wait(), timeout=timeout)
        if response_data:
            return parse_response_packet(response_data)
    except asyncio.TimeoutError:
        return {'error': 'タイムアウト'}

    return {'error': 'レスポンスなし'}


async def test_ble_communication(address: str):
    """BLE通信テスト"""
    print(f"\n📡 {address} に接続中...")

    async with BleakClient(address) as client:
        print(f"✅ 接続成功!")

        # レスポンス通知を購読
        await client.start_notify(RESPONSE_UUID, notification_handler)
        print("📥 レスポンス通知を購読しました")

        # テスト1: デバイス情報取得
        print("\n" + "="*50)
        print("📋 テスト1: デバイス情報取得 (CMD_GET_DEVICE_INFO)")
        print("="*50)
        response = await send_command(client, CMD_GET_DEVICE_INFO)
        if response.get('status_code') == RESP_STATUS_SUCCESS:
            info = parse_device_info(response['payload'])
            print(f"  デバイス名: {info.get('device_name')}")
            print(f"  ファームウェア: {info.get('firmware_version')}")
            print(f"  ハードウェア: {info.get('hardware_version')}")
            print(f"  稼働時間: {info.get('uptime_seconds')} 秒")
            print(f"  センサー読み取り回数: {info.get('total_sensor_readings')}")
        else:
            print(f"  エラー: {response}")

        await asyncio.sleep(0.5)

        # テスト2: システムステータス取得
        print("\n" + "="*50)
        print("📊 テスト2: システムステータス取得 (CMD_GET_SYSTEM_STATUS)")
        print("="*50)
        response = await send_command(client, CMD_GET_SYSTEM_STATUS)
        if response.get('status_code') == RESP_STATUS_SUCCESS:
            status = parse_system_status(response['payload'])
            print(f"  稼働時間: {status.get('uptime_seconds')} 秒")
            print(f"  空きヒープ: {status.get('heap_free')} bytes")
            print(f"  最小ヒープ: {status.get('heap_min')} bytes")
            print(f"  タスク数: {status.get('task_count')}")
            print(f"  WiFi接続: {status.get('wifi_connected')}")
            print(f"  BLE接続: {status.get('ble_connected')}")
        else:
            print(f"  エラー: {response}")

        await asyncio.sleep(0.5)

        # テスト3: センサーデータ取得
        print("\n" + "="*50)
        print("🌡️  テスト3: センサーデータ取得 (CMD_GET_SENSOR_DATA)")
        print("="*50)
        response = await send_command(client, CMD_GET_SENSOR_DATA)
        if response.get('status_code') == RESP_STATUS_SUCCESS:
            sensor = parse_sensor_data(response['payload'])
            if 'error' not in sensor:
                print(f"  日時: {sensor.get('datetime')}")
                print(f"  気温: {sensor.get('temperature'):.1f} °C")
                print(f"  湿度: {sensor.get('humidity'):.1f} %")
                print(f"  照度: {sensor.get('lux'):.0f} lux")
                print(f"  土壌水分: {sensor.get('soil_moisture'):.1f}")
                print(f"  土壌温度1: {sensor.get('soil_temperature1'):.1f} °C")
                print(f"  土壌温度2: {sensor.get('soil_temperature2'):.1f} °C")
                print(f"  静電容量: {sensor.get('capacitance')} pF")
                print(f"  センサーエラー: {sensor.get('sensor_error')}")
            else:
                print(f"  パースエラー: {sensor}")
        else:
            print(f"  エラー: {response}")

        # 通知購読解除
        await client.stop_notify(RESPONSE_UUID)
        print("\n✅ テスト完了!")


async def continuous_monitor(address: str, interval: float = 5.0):
    """連続モニタリング"""
    print(f"\n📡 {address} に接続中...")

    async with BleakClient(address) as client:
        print(f"✅ 接続成功!")
        await client.start_notify(RESPONSE_UUID, notification_handler)

        print(f"\n🔄 {interval}秒間隔でセンサーデータを取得中... (Ctrl+C で終了)\n")

        try:
            while True:
                response = await send_command(client, CMD_GET_SENSOR_DATA)
                if response.get('status_code') == RESP_STATUS_SUCCESS:
                    sensor = parse_sensor_data(response['payload'])
                    if 'error' not in sensor:
                        print(f"[{sensor.get('datetime')}] "
                              f"気温:{sensor.get('temperature'):.1f}°C "
                              f"湿度:{sensor.get('humidity'):.1f}% "
                              f"照度:{sensor.get('lux'):.0f}lux "
                              f"土壌:{sensor.get('soil_moisture'):.1f} "
                              f"土壌温度1:{sensor.get('soil_temperature1'):.1f}°C "
                              f"土壌温度2:{sensor.get('soil_temperature2'):.1f}°C")
                    else:
                        print(f"パースエラー: {sensor}")
                else:
                    print(f"エラー: {response}")

                await asyncio.sleep(interval)
        except KeyboardInterrupt:
            print("\n⏹️ モニタリング終了")

        await client.stop_notify(RESPONSE_UUID)


async def main():
    """メイン関数"""
    print("=" * 60)
    print("🌱 PlantMonitor BLE通信テスト")
    print("=" * 60)

    # デバイスを検索
    address = await find_device()

    if address is None:
        print("\n💡 ヒント: デバイスがアドバタイジング中か確認してください")
        return

    # モード選択
    print("\n📋 テストモードを選択してください:")
    print("  1. 単発テスト（デバイス情報、システム状態、センサーデータ）")
    print("  2. 連続モニタリング（5秒間隔でセンサーデータ取得）")
    print("  3. 終了")

    try:
        choice = input("\n選択 (1-3): ").strip()
    except EOFError:
        choice = "1"

    if choice == "1":
        await test_ble_communication(address)
    elif choice == "2":
        await continuous_monitor(address)
    else:
        print("終了します")


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n⏹️ 終了")
    except Exception as e:
        print(f"\n❌ エラー: {e}")
        sys.exit(1)
