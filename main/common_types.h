#ifndef COMMON_TYPES_H
#define COMMON_TYPES_H

#include <time.h>
#include <stdbool.h>
#include <inttypes.h>
#include "components/sensors/sht30_sensor.h"   // sht30_data_t のために必要 (Rev1)
#include "components/sensors/sht40_sensor.h"   // sht40_data_t のために必要 (Rev2)
#include "components/sensors/tsl2591_sensor.h" // tsl2591_data_t のために必要
#include "components/sensors/tc74_sensor.h"    // tc74_data_t のために必要
#include "components/sensors/tmp102_sensor.h"  // tmp102_data_t のために必要 (Rev4)
#include "components/actuators/led_control.h"    // sensor_status_t のために必要
#include "components/actuators/ws2812_control.h" // ws2812_color_preset_t のために必要
#include "components/sensors/moisture_sensor.h" // moisture_sensor_profile_t のために必要
#include "components/actuators/switch_input.h" // switch_input_profile_t のために必要


// WiFi機能の有効化設定
#define CONFIG_WIFI_ENABLED 0

// アプリケーション名
#define APP_NAME "Plant Monitor"
// ソフトウェアバージョン
#define SOFTWARE_VERSION "3.0.0"
// ハードウェアバージョン (10: Rev1, 20: Rev2, 30: Rev3, 40: Rev4)
#define HARDWARE_VERSION 30



// 水分センサータイプ定義
#define MOISTURE_SENSOR_TYPE_ADC     0  // ADCベースの水分センサー
#define MOISTURE_SENSOR_TYPE_FDC1004 1  // FDC1004静電容量センサー

// FDC1004チャンネル数
#define FDC1004_CHANNEL_COUNT 4  // FDC1004は4チャンネル

// データ構造バージョン定義
#define DATA_STRUCTURE_VERSION_1 1  // Rev1/Rev2用（基本センサーデータのみ）
#define DATA_STRUCTURE_VERSION_2 2  // Rev3用（土壌温度2ch + FDC1004静電容量追加）
#define DATA_STRUCTURE_VERSION_3 3  // Rev4用（土壌温度4ch + FDC1004静電容量追加）

// 使用する水分センサータイプを選択
#if HARDWARE_VERSION == 30 || HARDWARE_VERSION == 40

#define MOISTURE_SENSOR_TYPE MOISTURE_SENSOR_TYPE_FDC1004  // Rev3/Rev4はFDC1004を使用

#else

#define MOISTURE_SENSOR_TYPE MOISTURE_SENSOR_TYPE_ADC      // Rev1/Rev2はADCを使用

#endif

// センシング間隔（ミリ秒）
#define SENSOR_READ_INTERVAL_MS  60000  // 1分ごとにセンサー読み取り

// センサー閾値
#define MOISTURE_DRY_THRESHOLD    1.0  // 乾燥閾値
#define MOISTURE_WET_THRESHOLD    2.0  // 湿潤閾値
#define DRY_WARNING_DAYS          3    // 長期乾燥警告日数
#define WATERING_DETECTION_THRESHOLD MOISTURE_DRY_THRESHOLD // 灌水検出閾値 (mVまたはpF)
#define TEMP_HIGH_THRESHOLD       30.0  // 高温閾値
#define TEMP_LOW_THRESHOLD        15.0  // 低温閾値
#define HUMIDITY_LOW_THRESHOLD    40.0  // 低湿度閾値
#define LIGHT_LOW_THRESHOLD       100   // 暗さ閾値


// センサータイプ定義
#define TEMPARETURE_SENSOR_TYPE_SHT30  1
#define TEMPARETURE_SENSOR_TYPE_SHT40  2
// 使用する温度センサータイプの定義
#define TEMPARETURE_SENSOR_TYPE TEMPARETURE_SENSOR_TYPE_SHT40 // Rev2 or Rev3 use SHT40

// 土壌温度センサーデバイス定義
#define SOIL_TEMPERATURE_SENSOR_DS18B20 1
#define SOIL_TEMPERATURE_SENSOR_TMP102  2
#define SOIL_TEMPERATURE_SENSOR_TC74    3
#define SOIL_TEMPERATURE_SENSOR_NONE    0

// 使用する土壌温度センサータイプの定義
#define SOIL_TEMPERATURE1_SENSOR_TYPE SOIL_TEMPERATURE_SENSOR_DS18B20
#define SOIL_TEMPERATURE2_SENSOR_TYPE SOIL_TEMPERATURE_SENSOR_NONE

// --- 土壌センサー構成情報構造体 ---

// 土壌湿度センサー情報
typedef struct __attribute__((packed)) {
    uint8_t  sensor_type;              // センサータイプ (0:ADC, 1:FDC1004)
    uint16_t probe_length_mm;          // プローブ全体の長さ [mm]
    uint16_t sensing_length_mm;        // 湿度計測部の長さ [mm]
    uint8_t  channel_count;            // チャンネル数 (ADC:1, FDC1004:4)
    float    capacitance_min_pf;       // 最小静電容量 [pF] (FDC1004の場合)
    float    capacitance_max_pf;       // 最大静電容量 [pF] (FDC1004の場合)
    float    measurement_range_min;    // 計測範囲下限 (ADC: mV, FDC1004: pF)
    float    measurement_range_max;    // 計測範囲上限
} soil_moisture_sensor_info_t;

// 土壌温度センサー1台分の情報
#define MAX_SOIL_TEMP_SENSORS 4
typedef struct __attribute__((packed)) {
    uint8_t  device_type;              // デバイスタイプ (0:None, 1:DS18B20, 2:TMP102, 3:TC74)
    int16_t  depth_mm;                 // 地表からの深さ [mm]
    float    temp_min;                 // 計測範囲下限 [°C]
    float    temp_max;                 // 計測範囲上限 [°C]
    float    temp_resolution;          // 温度分解能 [°C]
} soil_temp_sensor_info_t;

// 拡張温度センサー情報
typedef struct __attribute__((packed)) {
    uint8_t  available;                // 拡張温度センサーの有無 (0:無, 1:有)
    uint8_t  device_type;              // デバイスタイプ (0:None, 1:DS18B20)
    float    temp_min;                 // 計測範囲下限 [°C]
    float    temp_max;                 // 計測範囲上限 [°C]
    float    temp_resolution;          // 温度分解能 [°C]
} ext_temp_sensor_info_t;

// ユニットの土壌センサー構成全体
typedef struct __attribute__((packed)) {
    uint8_t  hardware_version;         // HWバージョン (10/20/30/40)
    uint8_t  data_structure_version;   // データ構造バージョン
    soil_moisture_sensor_info_t moisture_sensor;                        // 土壌湿度センサー情報
    uint8_t  soil_temp_sensor_count;                                   // 接続中の土壌温度センサー数
    soil_temp_sensor_info_t soil_temp_sensors[MAX_SOIL_TEMP_SENSORS];  // 土壌温度センサー情報 x4
    ext_temp_sensor_info_t ext_temp_sensor;                            // 拡張温度センサー情報
} soil_sensor_config_t;

// GPIO定義
#if HARDWARE_VERSION == 10 // Rev1
#define HARDWARE_VERSION_STRING "1.0"
#define MOISTURE_AD_CHANNEL ADC_CHANNEL_2  // 水分センサー (ADC1_CH2)
#define I2C_SDA_PIN         GPIO_NUM_6   // I2C SDA
#define I2C_SCL_PIN         GPIO_NUM_7   // I2C SCL
#define SWITCH_PIN          GPIO_NUM_9   // スイッチピン
#define WS2812_PIN          GPIO_NUM_10  // フルカラーLED
#define BLUE_LED_PIN        GPIO_NUM_8   // 青色LED
#define RED_LED_PIN         GPIO_NUM_20  // 赤色LED
#define DATA_STRUCTURE_VERSION DATA_STRUCTURE_VERSION_1 // Rev2はデータ構造バージョン1を使用

#elif HARDWARE_VERSION == 20 // Rev2

#define HARDWARE_VERSION_STRING "2.0"
#define MOISTURE_AD_CHANNEL ADC_CHANNEL_3  // 水分センサー (ADC1_CH3)
#define I2C_SDA_PIN         GPIO_NUM_5   // I2C SDA
#define I2C_SCL_PIN         GPIO_NUM_6   // I2C SCL
#define SWITCH_PIN          GPIO_NUM_7   // スイッチ入力
#define WS2812_PIN          GPIO_NUM_1   // WS2812 LED制御
#define BLUE_LED_PIN        GPIO_NUM_0   // 青色LED
#define RED_LED_PIN         GPIO_NUM_2   // 赤色LED
#define DATA_STRUCTURE_VERSION DATA_STRUCTURE_VERSION_1 // Rev2はデータ構造バージョン1を使用

#elif HARDWARE_VERSION == 30 // Rev3

#define HARDWARE_VERSION_STRING "3.0"
#define MOISTURE_AD_CHANNEL ADC_CHANNEL_3  // 水分センサー用ADCチャンネル (GPIO3 = ADC1_CH3)
#define I2C_SDA_PIN         GPIO_NUM_5   // I2C SDA
#define I2C_SCL_PIN         GPIO_NUM_6   // I2C SCL
#define SWITCH_PIN          GPIO_NUM_7   // スイッチ入力
#define WS2812_PIN          GPIO_NUM_1   // WS2812 LED制御
#define BLUE_LED_PIN        GPIO_NUM_0   // 青色LED
#define RED_LED_PIN         GPIO_NUM_2   // 赤色LED
#define DATA_STRUCTURE_VERSION DATA_STRUCTURE_VERSION_2 // Rev3はデータ構造バージョン2を使用

#elif HARDWARE_VERSION == 40 // Rev4

#define HARDWARE_VERSION_STRING "4.0"
#define MOISTURE_AD_CHANNEL ADC_CHANNEL_3  // 水分センサー用ADCチャンネル (GPIO3 = ADC1_CH3)
#define I2C_SDA_PIN         GPIO_NUM_5   // I2C SDA
#define I2C_SCL_PIN         GPIO_NUM_6   // I2C SCL
#define SWITCH_PIN          GPIO_NUM_7   // スイッチ入力
#define WS2812_PIN          GPIO_NUM_1   // WS2812 LED制御
#define BLUE_LED_PIN        GPIO_NUM_0   // 青色LED
#define RED_LED_PIN         GPIO_NUM_2   // 赤色LED
#define DATA_STRUCTURE_VERSION DATA_STRUCTURE_VERSION_3 // Rev4はデータ構造バージョン3を使用

#endif

// tm構造体の簡易版
typedef struct 
{
  int	tm_sec;
  int	tm_min;
  int	tm_hour;
  int	tm_mday;
  int	tm_mon;
  int	tm_year;
  int	tm_wday;
  int	tm_yday;
  int	tm_isdst;
} tm_data_t;

#if (HARDWARE_VERSION == 10 || HARDWARE_VERSION == 20) // Rev1/Rev2
/* --- soil data structure --- */
typedef struct {
    uint8_t data_version;    // データ構造バージョン (DATA_STRUCTURE_VERSION_1)
    struct tm datetime;
    float lux;
    float temperature;
    float humidity;
    float soil_moisture; // in mV
    bool sensor_error;
} soil_data_t;

/* --- soil BLE data structure --- */
typedef struct {
    uint8_t data_version;    // データ構造バージョン (DATA_STRUCTURE_VERSION_1)
    tm_data_t datetime;
    float lux;
    float temperature;
    float humidity;
    float soil_moisture; // in mV
} soil_ble_data_t;
#elif (HARDWARE_VERSION == 30 || HARDWARE_VERSION == 40) // Rev3/Rev4
/* --- soil data structure --- */
typedef struct {
    uint8_t data_version;    // データ構造バージョン
    struct tm datetime;
    float lux;
    float temperature;
    float humidity;
    float soil_moisture; // in pF (FDC1004平均値)
    bool sensor_error;
    float soil_temperature[TMP102_MAX_DEVICES]; // 土壌温度 x4 (TMP102) [°C]
    uint8_t soil_temperature_count;             // 有効な土壌温度センサー数
    float soil_moisture_capacitance[FDC1004_CHANNEL_COUNT]; // 土壌湿度計測用静電容量 (pF)
#if HARDWARE_VERSION == 40
    float ext_temperature;          // 拡張温度センサー (DS18B20) [°C]
    bool ext_temperature_valid;     // 拡張温度データの有効性
#endif
} soil_data_t;

/* --- soil BLE data structure --- */
typedef struct {
    uint8_t data_version;    // データ構造バージョン
    tm_data_t datetime;
    float lux;
    float temperature;
    float humidity;
    float soil_moisture; // in pF (FDC1004平均値)
    float soil_temperature[TMP102_MAX_DEVICES]; // 土壌温度 x4 (TMP102) [°C]
    uint8_t soil_temperature_count;             // 有効な土壌温度センサー数
    float soil_moisture_capacitance[FDC1004_CHANNEL_COUNT]; // 土壌湿度計測用静電容量 (pF)
#if HARDWARE_VERSION == 40
    float ext_temperature;          // 拡張温度センサー (DS18B20) [°C]
    uint8_t ext_temperature_valid;  // 拡張温度データの有効性
#endif
} soil_ble_data_t;
#endif

typedef struct {
    int count;
    int capacity;
    int f_empty;
    int f_full;
 } ble_data_status_t;


#endif // COMMON_TYPES_H