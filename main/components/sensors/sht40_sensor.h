#ifndef SHT40_SENSOR_H
#define SHT40_SENSOR_H

#include "esp_err.h"
#include "driver/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

// SHT40定数定義
#define SHT40_ADDR_A        0x44         // SHT40のI2Cアドレス（ADDR pin = GND）
#define SHT40_ADDR_B        0x45         // SHT40のI2Cアドレス（ADDR pin = VDD）
#define SHT40_ADDR          SHT40_ADDR_A // デフォルトアドレス

// SHT40測定精度
typedef enum {
    SHT40_PRECISION_HIGH = 0xFD,         // High precision (最高精度、測定時間8.2ms)
    SHT40_PRECISION_MEDIUM = 0xF6,       // Medium precision (中精度、測定時間4.5ms)
    SHT40_PRECISION_LOW = 0xE0           // Low precision (低精度、測定時間1.7ms)
} sht40_precision_t;

// SHT40センサーデータ構造体
typedef struct {
    float temperature;
    float humidity;
    bool error;
} sht40_data_t;

// SHT40初期化
esp_err_t sht40_init(void);

// SHT40温湿度読み取り
esp_err_t sht40_read_data(sht40_data_t *data);

// SHT40温湿度読み取り（精度指定）
esp_err_t sht40_read_data_with_precision(sht40_data_t *data, sht40_precision_t precision);

// SHT40ソフトリセット
esp_err_t sht40_soft_reset(void);

// SHT40シリアルナンバー読み取り
esp_err_t sht40_read_serial(uint32_t *serial);

// CRC-8計算関数
uint8_t sht40_calculate_crc(uint8_t *data, uint8_t length);

#ifdef __cplusplus
}
#endif

#endif // SHT40_SENSOR_H
