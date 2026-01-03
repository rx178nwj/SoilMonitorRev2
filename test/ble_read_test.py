#!/usr/bin/env python3
"""
BLE Sensor Data Reader Test Script for Raspberry Pi
ESP32 Plant Monitor Rev3のBLEセンサーデータを読み取るテストスクリプト

必要なパッケージ:
    pip install bleak

使用方法:
    python3 ble_read_test.py
"""

import asyncio
import struct
import sys
from datetime import datetime
from bleak import BleakClient, BleakScanner

# UUIDs (ESP32デバイスから取得)
SERVICE_UUID = "59462f12-9543-9999-12c8-58b459a2712d"
SENSOR_DATA_CHAR_UUID = "6a3b2c01-4e5f-6a7b-8c9d-e0f123456789"

# データ構造バージョン
DATA_STRUCTURE_VERSION_1 = 1
DATA_STRUCTURE_VERSION_2 = 2

# FDC1004チャンネル数
FDC1004_CHANNEL_COUNT = 4


def parse_tm_data_t(data, offset):
    """tm_data_t構造体をパース (36バイト)"""
    tm_format = "<9i"  # 9個のint (tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst)
    tm_size = struct.calcsize(tm_format)
    tm_values = struct.unpack_from(tm_format, data, offset)

    tm_dict = {
        'tm_sec': tm_values[0],
        'tm_min': tm_values[1],
        'tm_hour': tm_values[2],
        'tm_mday': tm_values[3],
        'tm_mon': tm_values[4],
        'tm_year': tm_values[5],
        'tm_wday': tm_values[6],
        'tm_yday': tm_values[7],
        'tm_isdst': tm_values[8],
    }

    return tm_dict, offset + tm_size


def parse_sensor_data_v1(data):
    """データ構造バージョン1をパース (Rev1/Rev2)"""
    offset = 0

    # data_version (uint8_t)
    data_version = struct.unpack_from("<B", data, offset)[0]
    offset += 1

    # datetime (tm_data_t - 36バイト)
    datetime_dict, offset = parse_tm_data_t(data, offset)

    # センサーデータ (4 floats)
    lux, temperature, humidity, soil_moisture = struct.unpack_from("<4f", data, offset)
    offset += 16

    return {
        'data_version': data_version,
        'datetime': datetime_dict,
        'lux': lux,
        'temperature': temperature,
        'humidity': humidity,
        'soil_moisture': soil_moisture,
    }


def parse_sensor_data_v2(data):
    """データ構造バージョン2をパース (Rev3)"""
    offset = 0

    print(f"🔍 デバッグ: データ長 = {len(data)} バイト")

    # data_version (uint8_t) - 1バイト
    data_version = struct.unpack_from("<B", data, offset)[0]
    print(f"   offset {offset}: data_version = {data_version}")
    offset += 1

    # 構造体アライメントのため3バイトのパディングがある
    offset += 3

    # datetime (tm_data_t - 36バイト = 9 x 4バイトint)
    datetime_dict, offset = parse_tm_data_t(data, offset)
    print(f"   offset after tm_data_t: {offset}")

    # センサーデータ (4 floats = 16バイト)
    lux, temperature, humidity, soil_moisture = struct.unpack_from("<4f", data, offset)
    print(f"   offset {offset}: lux={lux}, temp={temperature}, hum={humidity}, soil_moist={soil_moisture}")
    offset += 16

    # 土壌温度 (2 floats = 8バイト)
    soil_temperature1, soil_temperature2 = struct.unpack_from("<2f", data, offset)
    print(f"   offset {offset}: soil_temp1={soil_temperature1}, soil_temp2={soil_temperature2}")
    offset += 8

    # FDC1004静電容量データ (4 floats = 16バイト)
    fdc1004_format = f"<{FDC1004_CHANNEL_COUNT}f"
    soil_moisture_capacitance = struct.unpack_from(fdc1004_format, data, offset)
    print(f"   offset {offset}: capacitance={soil_moisture_capacitance}")
    offset += 4 * FDC1004_CHANNEL_COUNT

    print(f"   final offset: {offset} / {len(data)}")

    return {
        'data_version': data_version,
        'datetime': datetime_dict,
        'lux': lux,
        'temperature': temperature,
        'humidity': humidity,
        'soil_moisture': soil_moisture,
        'soil_temperature1': soil_temperature1,
        'soil_temperature2': soil_temperature2,
        'soil_moisture_capacitance': list(soil_moisture_capacitance),
    }


def parse_sensor_data(data):
    """データ構造バージョンに応じてセンサーデータをパース"""
    if len(data) < 1:
        raise ValueError("データが短すぎます")

    # 最初のバイトでバージョンを確認
    data_version = data[0]

    if data_version == DATA_STRUCTURE_VERSION_1:
        return parse_sensor_data_v1(data)
    elif data_version == DATA_STRUCTURE_VERSION_2:
        return parse_sensor_data_v2(data)
    else:
        raise ValueError(f"未知のデータ構造バージョン: {data_version}")


def format_datetime(tm_dict):
    """tm_data_t辞書を読みやすい日時文字列に変換"""
    try:
        # tm_yearは1900年からの年数、tm_monは0-11
        year = tm_dict['tm_year'] + 1900
        month = tm_dict['tm_mon'] + 1
        day = tm_dict['tm_mday']
        hour = tm_dict['tm_hour']
        minute = tm_dict['tm_min']
        second = tm_dict['tm_sec']

        return f"{year:04d}-{month:02d}-{day:02d} {hour:02d}:{minute:02d}:{second:02d}"
    except:
        return "Invalid DateTime"


def print_sensor_data(sensor_data):
    """センサーデータを整形して表示"""
    print("\n" + "="*60)
    print("📊 センサーデータ読み取り結果")
    print("="*60)

    print(f"データ構造バージョン: {sensor_data['data_version']}")
    print(f"タイムスタンプ: {format_datetime(sensor_data['datetime'])}")
    print(f"気温: {sensor_data['temperature']:.1f} °C")
    print(f"湿度: {sensor_data['humidity']:.1f} %")
    print(f"照度: {sensor_data['lux']:.1f} lux")
    print(f"土壌水分: {sensor_data['soil_moisture']:.1f} mV")

    # Rev3用の追加データ
    if 'soil_temperature1' in sensor_data:
        print(f"土壌温度1: {sensor_data['soil_temperature1']:.1f} °C")

    if 'soil_temperature2' in sensor_data:
        print(f"土壌温度2: {sensor_data['soil_temperature2']:.1f} °C")

    if 'soil_moisture_capacitance' in sensor_data:
        print("\n🌱 FDC1004 土壌湿度計測用静電容量:")
        for i, cap in enumerate(sensor_data['soil_moisture_capacitance']):
            print(f"  チャンネル {i+1}: {cap:.3f} pF")

    print("="*60 + "\n")


async def scan_for_device(device_name="PlantMonitor", debug=True):
    """指定した名前のBLEデバイスをスキャン"""
    print(f"🔍 '{device_name}' デバイスをスキャン中...")
    print(f"⏱️  スキャンタイムアウト: 10秒\n")

    devices = await BleakScanner.discover(timeout=10.0)

    print(f"📡 スキャン完了: {len(devices)} 個のBLEデバイスを検出\n")

    # デバッグ: すべてのデバイスを表示
    if debug:
        print("="*60)
        print("🔍 検出されたすべてのBLEデバイス:")
        print("="*60)
        for idx, device in enumerate(devices, 1):
            print(f"\n[{idx}] デバイス名: {device.name if device.name else '(名前なし)'}")
            print(f"    アドレス: {device.address}")

            # RSSI は metadata または details に含まれている可能性がある
            rssi_value = "N/A"
            if hasattr(device, 'rssi') and device.rssi is not None:
                rssi_value = f"{device.rssi} dBm"
            elif hasattr(device, 'metadata') and device.metadata:
                if 'rssi' in device.metadata:
                    rssi_value = f"{device.metadata['rssi']} dBm"
            elif hasattr(device, 'details') and device.details:
                if 'props' in device.details and 'RSSI' in device.details['props']:
                    rssi_value = f"{device.details['props']['RSSI']} dBm"

            print(f"    RSSI: {rssi_value}")

            if hasattr(device, 'metadata') and device.metadata:
                print(f"    メタデータ: {device.metadata}")
        print("\n" + "="*60 + "\n")

    # ターゲットデバイスを検索
    target_device = None
    for device in devices:
        if device.name and device_name.lower() in device.name.lower():
            target_device = device
            break

    if target_device:
        print(f"✅ ターゲットデバイス発見!")
        print(f"   名前: {target_device.name}")
        print(f"   アドレス: {target_device.address}")

        # RSSI表示の安全な処理
        rssi_info = "N/A"
        if hasattr(target_device, 'rssi') and target_device.rssi is not None:
            rssi_info = f"{target_device.rssi} dBm"
        elif hasattr(target_device, 'metadata') and target_device.metadata and 'rssi' in target_device.metadata:
            rssi_info = f"{target_device.metadata['rssi']} dBm"

        print(f"   RSSI: {rssi_info}\n")
        return target_device.address
    else:
        print(f"❌ '{device_name}' という名前のデバイスが見つかりませんでした\n")
        return None


async def read_sensor_data(address, debug=True):
    """BLE経由でセンサーデータを読み取り"""
    print(f"🔌 デバイスに接続中: {address}")
    print(f"⏱️  接続タイムアウト: 30秒\n")

    # 接続リトライ設定
    max_retries = 3
    retry_delay = 2  # 秒

    for attempt in range(max_retries):
        if attempt > 0:
            print(f"🔄 再試行 {attempt}/{max_retries - 1}...")
            await asyncio.sleep(retry_delay)

        try:
            async with BleakClient(address, timeout=30.0) as client:
                if not client.is_connected:
                    print("❌ 接続失敗")
                    continue

                print("✅ 接続成功\n")

                # サービスと特性を確認
                if debug:
                    print("="*60)
                    print("📋 利用可能なサービスと特性:")
                    print("="*60)
                    service_count = 0
                    char_count = 0
                    for service in client.services:
                        service_count += 1
                        print(f"\n[サービス {service_count}]")
                        print(f"  UUID: {service.uuid}")
                        print(f"  説明: {service.description if hasattr(service, 'description') else 'N/A'}")

                        for char in service.characteristics:
                            char_count += 1
                            print(f"\n  [特性 {char_count}]")
                            print(f"    UUID: {char.uuid}")
                            print(f"    プロパティ: {char.properties}")
                            print(f"    ハンドル: {char.handle if hasattr(char, 'handle') else 'N/A'}")

                            # ターゲット特性かどうか確認
                            if char.uuid.lower() == SENSOR_DATA_CHAR_UUID.lower():
                                print(f"    ⭐ これがターゲット特性です!")

                    print(f"\n合計: {service_count} サービス, {char_count} 特性")
                    print("="*60 + "\n")

                # センサーデータを読み取り
                print(f"📖 センサーデータ特性を読み取り中...")
                print(f"   ターゲットUUID: {SENSOR_DATA_CHAR_UUID}\n")

                try:
                    data = await client.read_gatt_char(SENSOR_DATA_CHAR_UUID)

                    print(f"✅ データ受信成功!")
                    print(f"   受信データサイズ: {len(data)} バイト")
                    print(f"   生データ (hex): {data.hex()}\n")

                    if debug:
                        # バイナリダンプ表示
                        print("📄 バイナリダンプ:")
                        for i in range(0, len(data), 16):
                            hex_part = ' '.join(f'{b:02x}' for b in data[i:i+16])
                            ascii_part = ''.join(chr(b) if 32 <= b < 127 else '.' for b in data[i:i+16])
                            print(f"   {i:04x}: {hex_part:<48} {ascii_part}")
                        print()

                    # データをパース
                    print("🔧 データ解析中...")
                    sensor_data = parse_sensor_data(data)
                    print("✅ 解析成功!\n")

                    # 結果を表示
                    print_sensor_data(sensor_data)

                    # 成功したらループを抜ける
                    return

                except Exception as e:
                    print(f"❌ データ読み取りエラー: {e}")
                    if debug:
                        import traceback
                        print("\n🐛 詳細なエラー情報:")
                        traceback.print_exc()
                    continue

        except Exception as e:
            print(f"❌ 接続エラー (試行 {attempt + 1}/{max_retries}): {e}")
            if debug and attempt == max_retries - 1:
                import traceback
                print("\n🐛 詳細なエラー情報:")
                traceback.print_exc()
            continue

    # すべての試行が失敗した場合
    print(f"\n❌ {max_retries}回の試行後も接続できませんでした")
    print("\n💡 トラブルシューティング:")
    print("  1. ESP32が他のデバイスと接続していないか確認")
    print("  2. ESP32を再起動してみる")
    print("  3. Raspberry Piを再起動してみる")
    print("  4. bluetoothctl でデバイスを削除してから再試行:")
    print(f"     bluetoothctl")
    print(f"     > remove {address}")
    print(f"     > exit")


async def main():
    """メイン処理"""
    import argparse

    # コマンドライン引数のパース
    parser = argparse.ArgumentParser(
        description='Plant Monitor BLE Sensor Data Reader',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
使用例:
  python3 ble_read_test.py                    # デバッグモード有効
  python3 ble_read_test.py --no-debug         # デバッグモード無効
  python3 ble_read_test.py --name "My Plant" # カスタムデバイス名
  python3 ble_read_test.py --address AA:BB:CC:DD:EE:FF  # 直接アドレス指定
        '''
    )
    parser.add_argument('--debug', dest='debug', action='store_true', default=True,
                        help='デバッグ情報を表示 (デフォルト)')
    parser.add_argument('--no-debug', dest='debug', action='store_false',
                        help='デバッグ情報を非表示')
    parser.add_argument('--name', type=str, default='PlantMonitor',
                        help='検索するデバイス名 (デフォルト: "PlantMonitor")')
    parser.add_argument('--address', type=str, default=None,
                        help='デバイスのMACアドレスを直接指定')
    args = parser.parse_args()

    print("="*60)
    print("🌱 Plant Monitor BLE Sensor Data Reader")
    print("="*60)
    print(f"デバッグモード: {'有効' if args.debug else '無効'}")
    print(f"Pythonバージョン: {sys.version}")
    print("="*60 + "\n")

    # アドレスが直接指定されている場合
    if args.address:
        print(f"💡 デバイスアドレスが指定されました: {args.address}")
        print("   スキャンをスキップして直接接続します\n")
        address = args.address
    else:
        # デバイスをスキャン
        address = await scan_for_device(args.name, debug=args.debug)

        if address is None:
            print("❌ デバイスが見つかりませんでした")
            print("\n💡 トラブルシューティング:")
            print("  1. ESP32デバイスの電源が入っているか確認")
            print("  2. BLEアドバタイジングが有効か確認")
            print("  3. デバイスが近くにあるか確認 (RSSI値を参考に)")
            print("  4. デバイス名が正しいか確認")
            print("\n💡 ヒント:")
            print("  - 上記のデバイスリストに目的のデバイスがある場合:")
            print("    python3 ble_read_test.py --address <MACアドレス>")
            print("  - デバイス名を変更している場合:")
            print("    python3 ble_read_test.py --name \"カスタム名\"")
            sys.exit(1)

    # センサーデータを読み取り
    try:
        await read_sensor_data(address, debug=args.debug)
    except Exception as e:
        print(f"❌ エラー: {e}")
        if args.debug:
            import traceback
            print("\n🐛 詳細なエラー情報:")
            traceback.print_exc()
        sys.exit(1)


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n\n⚠️  ユーザーによって中断されました")
        sys.exit(0)
