#ifndef TMP102_SENSOR_H
#define TMP102_SENSOR_H

#include "esp_err.h"
#include "driver/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

// TMP102 I2Cアドレス定義 (A0ピンの接続先により決定)
#define TMP102_ADDR_GND     0x48  // A0 = GND
#define TMP102_ADDR_VCC     0x49  // A0 = V+
#define TMP102_ADDR_SDA     0x4A  // A0 = SDA
#define TMP102_ADDR_SCL     0x4B  // A0 = SCL

// デフォルトアドレス (A0=GND)
#define TMP102_ADDR_DEFAULT TMP102_ADDR_GND

// TMP102最大デバイス数
#define TMP102_MAX_DEVICES  4

// TMP102レジスタアドレス
#define TMP102_REG_TEMP     0x00  // 温度レジスタ (読み取り専用)
#define TMP102_REG_CONFIG   0x01  // 設定レジスタ
#define TMP102_REG_TLOW     0x02  // 低温閾値レジスタ
#define TMP102_REG_THIGH    0x03  // 高温閾値レジスタ

// TMP102測定範囲
#define TMP102_TEMP_MIN       -40.0f  // 最低温度 [°C]
#define TMP102_TEMP_MAX       125.0f  // 最高温度 [°C]
#define TMP102_TEMP_RESOLUTION 0.0625f // 温度分解能 [°C] (12bit)

// TMP102センサーデータ構造体
typedef struct {
    float temperature;      // 温度 [°C]
    bool error;             // エラーフラグ
} tmp102_data_t;

/**
 * @brief 全TMP102デバイスを自動検出・初期化 (0x48〜0x4B)
 * @return ESP_OK: 1つ以上検出, ESP_ERR_NOT_FOUND: 検出なし
 */
esp_err_t tmp102_init_all(void);

/**
 * @brief 検出されたデバイス数を取得
 * @return 検出されたTMP102デバイス数 (0〜4)
 */
uint8_t tmp102_get_device_count(void);

/**
 * @brief インデックス指定で温度読み取り
 * @param index デバイスインデックス (0〜検出数-1)
 * @param temperature 温度格納先 [°C]
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t tmp102_read_temperature_by_index(uint8_t index, float *temperature);

/**
 * @brief インデックス指定でデータ読み取り
 * @param index デバイスインデックス (0〜検出数-1)
 * @param data データ構造体
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t tmp102_read_data_by_index(uint8_t index, tmp102_data_t *data);

/**
 * @brief 全デバイスの温度を一括読み取り
 * @param temperatures 温度配列格納先 (TMP102_MAX_DEVICES要素)
 * @param count 有効な温度データ数
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t tmp102_read_all_temperatures(float *temperatures, uint8_t *count);

#ifdef __cplusplus
}
#endif

#endif // TMP102_SENSOR_H
