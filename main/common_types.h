#ifndef COMMON_TYPES_H
#define COMMON_TYPES_H

#include <time.h>
#include <stdbool.h>
#include <inttypes.h>
#include "components/sensors/sht30_sensor.h"   // sht30_data_t のために必要 (Rev1)
#include "components/sensors/sht40_sensor.h"   // sht40_data_t のために必要 (Rev2)
#include "components/sensors/tsl2591_sensor.h" // tsl2591_data_t のために必要
#include "components/actuators/led_control.h"    // sensor_status_t のために必要
#include "components/actuators/ws2812_control.h" // ws2812_color_preset_t のために必要
#include "components/sensors/moisture_sensor.h" // moisture_sensor_profile_t のために必要
#include "components/actuators/switch_input.h" // switch_input_profile_t のために必要


// WiFi機能の有効化設定
#define CONFIG_WIFI_ENABLED 0

// アプリケーション名
#define APP_NAME "Plant Monitor"
// ソフトウェアバージョン
#define SOFTWARE_VERSION "2.0.0"
// ハードウェアバージョン (10: Rev1, 20: Rev2)
#define HARDWARE_VERSION 20

// GPIO定義
#if HARDWARE_VERSION == 10 // Rev1
#define HARDWARE_VERSION_STRING "1.0"
#define MOISTURE_PIN        GPIO_NUM_2   // 水分センサー (ADC1_CH2)
#define I2C_SDA_PIN         GPIO_NUM_6   // I2C SDA
#define I2C_SCL_PIN         GPIO_NUM_7   // I2C SCL
#define SWITCH_PIN          GPIO_NUM_9   // スイッチピン
#define WS2812_PIN          GPIO_NUM_10  // フルカラーLED
#define BLUE_LED_PIN        GPIO_NUM_8   // 青色LED
#define RED_LED_PIN         GPIO_NUM_20  // 赤色LED

#else // Rev2
#define HARDWARE_VERSION_STRING "2.0"
#define MOISTURE_PIN        GPIO_NUM_3   // 水分センサー (ADC1_CH3)
#define I2C_SDA_PIN         GPIO_NUM_5   // I2C SDA
#define I2C_SCL_PIN         GPIO_NUM_6   // I2C SCL
#define SWITCH_PIN          GPIO_NUM_7   // スイッチ入力
#define WS2812_PIN          GPIO_NUM_1   // WS2812 LED制御
#define BLUE_LED_PIN        GPIO_NUM_0   // 青色LED
#define RED_LED_PIN         GPIO_NUM_2   // 赤色LED

#endif

// センシング間隔（ミリ秒）
#define SENSOR_READ_INTERVAL_MS  60000  // 1分ごとにセンサー読み取り

// センサー閾値
#define MOISTURE_DRY_THRESHOLD    2000  // 乾燥閾値
#define TEMP_HIGH_THRESHOLD       30.0  // 高温閾値
#define TEMP_LOW_THRESHOLD        15.0  // 低温閾値
#define HUMIDITY_LOW_THRESHOLD    40.0  // 低湿度閾値
#define LIGHT_LOW_THRESHOLD       100   // 暗さ閾値

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

/* --- soil data structure --- */
typedef struct {
    struct tm datetime;
    float lux;
    float temperature;
    float humidity;
    float soil_moisture; // in mV
    bool sensor_error;
    float soil_temperature; // DS18B20 soil temperature in °C
} soil_data_t;

/* --- soil data structure --- */
typedef struct {
    tm_data_t datetime;
    float lux;
    float temperature;
    float humidity;
    float soil_moisture; // in mV
    float soil_temperature; // DS18B20 soil temperature in °C
} soil_ble_data_t;

typedef struct {
    int count;
    int capacity;
    int f_empty;
    int f_full;
 } ble_data_status_t;


#endif // COMMON_TYPES_H