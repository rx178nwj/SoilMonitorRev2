#include "sht40_sensor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SHT40";

// デフォルト測定精度
static sht40_precision_t default_precision = SHT40_PRECISION_HIGH;

// 検出されたI2Cアドレス
static uint8_t sht40_detected_addr = SHT40_ADDR;

// SHT40温湿度センサー読み取り（精度指定）
esp_err_t sht40_read_data_with_precision(sht40_data_t *data, sht40_precision_t precision)
{
    if (data == NULL) {
        ESP_LOGE(TAG, "データポインタがNULLです");
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t cmd = precision;
    uint8_t sensor_data[6];

    ESP_LOGD(TAG, "SHT40: 測定コマンド送信 (精度: 0x%02X, アドレス: 0x%02X)", precision, sht40_detected_addr);

    // コマンド送信
    esp_err_t ret = i2c_master_write_to_device(I2C_NUM_0, sht40_detected_addr, &cmd, 1, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SHT40: コマンド書き込み失敗: %s", esp_err_to_name(ret));
        data->error = true;
        return ret;
    }

    // 測定完了まで待機（精度により異なる）
    uint32_t wait_ms;
    switch (precision) {
        case SHT40_PRECISION_HIGH:
            wait_ms = 10;  // 高精度: 8.2ms + マージン
            break;
        case SHT40_PRECISION_MEDIUM:
            wait_ms = 6;   // 中精度: 4.5ms + マージン
            break;
        case SHT40_PRECISION_LOW:
            wait_ms = 3;   // 低精度: 1.7ms + マージン
            break;
        default:
            wait_ms = 10;
            break;
    }
    vTaskDelay(pdMS_TO_TICKS(wait_ms));

    // データ読み取り（6バイト: 温度2バイト + CRC1バイト + 湿度2バイト + CRC1バイト）
    ret = i2c_master_read_from_device(I2C_NUM_0, sht40_detected_addr, sensor_data, sizeof(sensor_data), pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SHT40: データ読み取り失敗: %s", esp_err_to_name(ret));
        data->error = true;
        return ret;
    }

    ESP_LOGD(TAG, "SHT40: 生データ: %02X %02X %02X %02X %02X %02X",
             sensor_data[0], sensor_data[1], sensor_data[2], sensor_data[3], sensor_data[4], sensor_data[5]);

    // CRCチェック（温度データ）
    uint8_t temp_crc = sht40_calculate_crc(&sensor_data[0], 2);
    if (temp_crc != sensor_data[2]) {
        ESP_LOGW(TAG, "SHT40: 温度CRCミスマッチ. 期待値: 0x%02X, 実際: 0x%02X", temp_crc, sensor_data[2]);
    }

    // CRCチェック（湿度データ）
    uint8_t hum_crc = sht40_calculate_crc(&sensor_data[3], 2);
    if (hum_crc != sensor_data[5]) {
        ESP_LOGW(TAG, "SHT40: 湿度CRCミスマッチ. 期待値: 0x%02X, 実際: 0x%02X", hum_crc, sensor_data[5]);
    }

    // データ変換（SHT40データシートの公式に従う）
    uint16_t temp_raw = (sensor_data[0] << 8) | sensor_data[1];
    uint16_t hum_raw = (sensor_data[3] << 8) | sensor_data[4];

    // 温度変換: T[°C] = -45 + 175 * (ST / (2^16 - 1))
    data->temperature = -45.0f + 175.0f * ((float)temp_raw / 65535.0f);

    // 湿度変換: RH[%] = -6 + 125 * (SRH / (2^16 - 1))
    data->humidity = -6.0f + 125.0f * ((float)hum_raw / 65535.0f);

    // 湿度を0-100%の範囲に制限
    if (data->humidity < 0.0f) data->humidity = 0.0f;
    if (data->humidity > 100.0f) data->humidity = 100.0f;

    data->error = false;

    ESP_LOGD(TAG, "SHT40: 温度: %.2f°C, 湿度: %.2f%%", data->temperature, data->humidity);

    return ESP_OK;
}

// SHT40温湿度センサー読み取り（デフォルト精度）
esp_err_t sht40_read_data(sht40_data_t *data)
{
    return sht40_read_data_with_precision(data, default_precision);
}

// CRC-8計算関数（SHT40データシートの仕様に従う）
// Polynomial: 0x31 (x8 + x5 + x4 + 1)
// Initialization: 0xFF
// Final XOR: 0x00
uint8_t sht40_calculate_crc(uint8_t *data, uint8_t length)
{
    uint8_t crc = 0xFF; // Initialization: 0xFF

    for (uint8_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t bit = 8; bit > 0; --bit) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31; // Polynomial: 0x31
            } else {
                crc = (crc << 1);
            }
        }
    }

    return crc; // Final XOR: 0x00 (no XOR)
}

// SHT40ソフトリセット関数
esp_err_t sht40_soft_reset(void)
{
    uint8_t cmd = 0x94; // Soft reset command

    ESP_LOGI(TAG, "SHT40: ソフトリセット実行 (アドレス: 0x%02X)", sht40_detected_addr);

    esp_err_t ret = i2c_master_write_to_device(I2C_NUM_0, sht40_detected_addr, &cmd, 1, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SHT40: ソフトリセット失敗: %s", esp_err_to_name(ret));
        return ret;
    }

    // リセット後の待機時間（データシート: 1ms）
    vTaskDelay(pdMS_TO_TICKS(2));

    ESP_LOGI(TAG, "SHT40: ソフトリセット完了");
    return ESP_OK;
}

// SHT40シリアルナンバー読み取り関数
esp_err_t sht40_read_serial(uint32_t *serial)
{
    if (serial == NULL) {
        ESP_LOGE(TAG, "シリアルナンバーポインタがNULLです");
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t cmd = 0x89; // Read serial number command
    uint8_t serial_data[6];

    ESP_LOGD(TAG, "SHT40: シリアルナンバー読み取り (アドレス: 0x%02X)", sht40_detected_addr);

    // コマンド送信
    esp_err_t ret = i2c_master_write_to_device(I2C_NUM_0, sht40_detected_addr, &cmd, 1, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SHT40: シリアルナンバーコマンド送信失敗: %s", esp_err_to_name(ret));
        return ret;
    }

    // 待機時間（データシート: 1ms）
    vTaskDelay(pdMS_TO_TICKS(2));

    // データ読み取り（6バイト: 2バイト + CRC + 2バイト + CRC）
    ret = i2c_master_read_from_device(I2C_NUM_0, sht40_detected_addr, serial_data, sizeof(serial_data), pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SHT40: シリアルナンバー読み取り失敗: %s", esp_err_to_name(ret));
        return ret;
    }

    // CRCチェック
    uint8_t crc1 = sht40_calculate_crc(&serial_data[0], 2);
    uint8_t crc2 = sht40_calculate_crc(&serial_data[3], 2);
    if (crc1 != serial_data[2] || crc2 != serial_data[5]) {
        ESP_LOGW(TAG, "SHT40: シリアルナンバーCRCミスマッチ");
    }

    // 32ビットシリアルナンバーを構築
    *serial = ((uint32_t)serial_data[0] << 24) | ((uint32_t)serial_data[1] << 16) |
              ((uint32_t)serial_data[3] << 8) | serial_data[4];

    ESP_LOGI(TAG, "SHT40: シリアルナンバー: 0x%08lX", (unsigned long)*serial);

    return ESP_OK;
}

// SHT40初期化関数
esp_err_t sht40_init(void)
{
    ESP_LOGI(TAG, "SHT40センサー初期化中...");

    // 両方のI2Cアドレスを試す
    uint8_t addresses[] = {SHT40_ADDR_A, SHT40_ADDR_B};
    esp_err_t ret = ESP_FAIL;

    for (int i = 0; i < 2; i++) {
        sht40_detected_addr = addresses[i];
        ESP_LOGI(TAG, "SHT40: アドレス 0x%02X で試行中...", sht40_detected_addr);

        // ソフトリセット実行
        ret = sht40_soft_reset();
        if (ret != ESP_OK) {
            ESP_LOGD(TAG, "SHT40: アドレス 0x%02X でソフトリセット失敗", sht40_detected_addr);
            continue;
        }

        // シリアルナンバー読み取り（接続確認）
        uint32_t serial;
        ret = sht40_read_serial(&serial);
        if (ret != ESP_OK) {
            ESP_LOGD(TAG, "SHT40: アドレス 0x%02X でシリアルナンバー読み取り失敗", sht40_detected_addr);
            continue;
        }

        // テスト測定を実行して接続確認
        sht40_data_t test_data;
        ret = sht40_read_data(&test_data);
        if (ret != ESP_OK) {
            ESP_LOGD(TAG, "SHT40: アドレス 0x%02X でテスト測定失敗", sht40_detected_addr);
            continue;
        }

        // 測定値の妥当性チェック
        if (test_data.temperature < -40.0f || test_data.temperature > 125.0f ||
            test_data.humidity < 0.0f || test_data.humidity > 100.0f) {
            ESP_LOGW(TAG, "SHT40: テスト測定値が範囲外 (T:%.1f°C, H:%.1f%%)",
                     test_data.temperature, test_data.humidity);
        }

        ESP_LOGI(TAG, "SHT40: 初期化成功 (アドレス: 0x%02X, T:%.1f°C, H:%.1f%%, S/N:0x%08lX)",
                 sht40_detected_addr, test_data.temperature, test_data.humidity, (unsigned long)serial);
        return ESP_OK;
    }

    // 両方のアドレスで失敗した場合
    ESP_LOGE(TAG, "SHT40: 全てのアドレスで初期化失敗");
    sht40_detected_addr = SHT40_ADDR; // デフォルトに戻す
    return ESP_FAIL;
}
