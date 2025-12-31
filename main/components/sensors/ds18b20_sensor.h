#ifndef DS18B20_SENSOR_H
#define DS18B20_SENSOR_H

#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

// DS18B20定数定義
#define DS18B20_PIN             GPIO_NUM_4   // GPIO4を1-Wire通信に使用
#define DS18B20_RESOLUTION      DS18B20_RESOLUTION_12_BIT  // 12ビット分解能
#define MAX_DS18B20_DEVICES     4            // 最大デバイス数

// DS18B20分解能設定
typedef enum {
    DS18B20_RESOLUTION_9_BIT = 0,   // 9-bit (0.5°C, 93.75ms)
    DS18B20_RESOLUTION_10_BIT = 1,  // 10-bit (0.25°C, 187.5ms)
    DS18B20_RESOLUTION_11_BIT = 2,  // 11-bit (0.125°C, 375ms)
    DS18B20_RESOLUTION_12_BIT = 3   // 12-bit (0.0625°C, 750ms)
} ds18b20_resolution_t;

// DS18B20センサーデータ構造体
typedef struct {
    float temperature;      // 温度 [°C]
    uint64_t device_addr;   // デバイスアドレス（ROM code）
    bool valid;             // データ有効フラグ
    bool error;             // エラーフラグ
} ds18b20_data_t;

// DS18B20センサー情報構造体
typedef struct {
    int device_count;                           // 検出されたデバイス数
    uint64_t device_addrs[MAX_DS18B20_DEVICES]; // デバイスアドレスリスト
} ds18b20_info_t;

// DS18B20初期化
esp_err_t ds18b20_init(void);

// DS18B20終了処理
void ds18b20_deinit(void);

// デバイス検索
esp_err_t ds18b20_scan_devices(ds18b20_info_t *info);

// 単一デバイスから温度読み取り
esp_err_t ds18b20_read_temperature(uint64_t device_addr, float *temperature);

// 全デバイスから温度読み取り
esp_err_t ds18b20_read_all_temperatures(ds18b20_data_t *data_array, int *count);

// 最初のデバイスから温度読み取り（単一センサー用）
esp_err_t ds18b20_read_single_temperature(float *temperature);

// 分解能設定
esp_err_t ds18b20_set_resolution(uint64_t device_addr, ds18b20_resolution_t resolution);

#ifdef __cplusplus
}
#endif

#endif // DS18B20_SENSOR_H
