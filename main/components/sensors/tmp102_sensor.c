#include "tmp102_sensor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "TMP102";

// 検出されたデバイス情報
typedef struct {
    uint8_t addr;       // I2Cアドレス
    bool connected;     // 接続状態
} tmp102_device_t;

// 全デバイス管理
static tmp102_device_t tmp102_devices[TMP102_MAX_DEVICES] = {0};
static uint8_t tmp102_device_count = 0;

// 検出対象アドレスリスト
static const uint8_t tmp102_scan_addrs[TMP102_MAX_DEVICES] = {
    TMP102_ADDR_GND,  // 0x48
    TMP102_ADDR_VCC,  // 0x49
    TMP102_ADDR_SDA,  // 0x4A
    TMP102_ADDR_SCL   // 0x4B
};

/**
 * @brief 指定アドレスからレジスタ読み取り (2バイト)
 */
static esp_err_t tmp102_read_register(uint8_t addr, uint8_t reg_addr, uint8_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = i2c_master_write_to_device(I2C_NUM_0, addr, &reg_addr, 1, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        return ret;
    }

    ret = i2c_master_read_from_device(I2C_NUM_0, addr, data, 2, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGD(TAG, "レジスタ読み取り: addr=0x%02X, reg=0x%02X, data=0x%02X%02X", addr, reg_addr, data[0], data[1]);
    return ESP_OK;
}

/**
 * @brief 指定アドレスから温度を読み取り
 */
static esp_err_t tmp102_read_temp_at_addr(uint8_t addr, float *temperature)
{
    uint8_t data[2];
    esp_err_t ret = tmp102_read_register(addr, TMP102_REG_TEMP, data);
    if (ret != ESP_OK) {
        return ret;
    }

    // TMP102: 12ビット左詰め [D11..D4] [D3..D0 0000]
    int16_t raw = ((int16_t)data[0] << 4) | (data[1] >> 4);

    // 負の値の符号拡張 (12ビット→16ビット)
    if (raw & 0x0800) {
        raw |= 0xF000;
    }

    *temperature = raw * TMP102_TEMP_RESOLUTION;
    return ESP_OK;
}

/**
 * @brief 全TMP102デバイスを自動検出・初期化 (0x48〜0x4B)
 */
esp_err_t tmp102_init_all(void)
{
    ESP_LOGI(TAG, "TMP102温度センサー自動検出中...");

    tmp102_device_count = 0;

    for (int i = 0; i < TMP102_MAX_DEVICES; i++) {
        uint8_t addr = tmp102_scan_addrs[i];
        tmp102_devices[i].addr = addr;
        tmp102_devices[i].connected = false;

        // 設定レジスタを読み取ってデバイス存在を確認
        uint8_t config[2];
        esp_err_t ret = tmp102_read_register(addr, TMP102_REG_CONFIG, config);
        if (ret != ESP_OK) {
            ESP_LOGD(TAG, "  0x%02X: 応答なし", addr);
            continue;
        }

        // 変換完了待ち
        vTaskDelay(pdMS_TO_TICKS(30));

        // テスト読み取りで検証
        float temp;
        ret = tmp102_read_temp_at_addr(addr, &temp);
        if (ret == ESP_OK && temp >= TMP102_TEMP_MIN && temp <= TMP102_TEMP_MAX) {
            tmp102_devices[i].connected = true;
            tmp102_device_count++;
            ESP_LOGI(TAG, "  0x%02X: 検出 (%.2f°C)", addr, temp);
        } else {
            ESP_LOGW(TAG, "  0x%02X: 応答あり、温度読み取り不正", addr);
        }
    }

    ESP_LOGI(TAG, "TMP102検出完了: %d台検出", tmp102_device_count);

    if (tmp102_device_count == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    return ESP_OK;
}

/**
 * @brief 検出されたデバイス数を取得
 */
uint8_t tmp102_get_device_count(void)
{
    return tmp102_device_count;
}

/**
 * @brief インデックス指定で温度読み取り
 * @param index 検出順インデックス (0〜検出数-1)
 */
esp_err_t tmp102_read_temperature_by_index(uint8_t index, float *temperature)
{
    if (temperature == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // index番目の接続デバイスを探す
    uint8_t found = 0;
    for (int i = 0; i < TMP102_MAX_DEVICES; i++) {
        if (tmp102_devices[i].connected) {
            if (found == index) {
                esp_err_t ret = tmp102_read_temp_at_addr(tmp102_devices[i].addr, temperature);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "温度読み取り失敗 (0x%02X)", tmp102_devices[i].addr);
                }
                return ret;
            }
            found++;
        }
    }

    ESP_LOGE(TAG, "インデックス %d のデバイスが見つかりません (検出数: %d)", index, tmp102_device_count);
    return ESP_ERR_NOT_FOUND;
}

/**
 * @brief インデックス指定でデータ読み取り
 */
esp_err_t tmp102_read_data_by_index(uint8_t index, tmp102_data_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    float temp;
    esp_err_t ret = tmp102_read_temperature_by_index(index, &temp);
    if (ret != ESP_OK) {
        data->error = true;
        data->temperature = 0.0f;
        return ret;
    }

    data->temperature = temp;
    data->error = false;
    return ESP_OK;
}

/**
 * @brief 全デバイスの温度を一括読み取り
 */
esp_err_t tmp102_read_all_temperatures(float *temperatures, uint8_t *count)
{
    if (temperatures == NULL || count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *count = 0;
    for (int i = 0; i < TMP102_MAX_DEVICES; i++) {
        if (tmp102_devices[i].connected) {
            float temp;
            esp_err_t ret = tmp102_read_temp_at_addr(tmp102_devices[i].addr, &temp);
            if (ret == ESP_OK) {
                temperatures[*count] = temp;
            } else {
                temperatures[*count] = 0.0f;
                ESP_LOGW(TAG, "TMP102 (0x%02X) 読み取り失敗", tmp102_devices[i].addr);
            }
            (*count)++;
        }
    }

    return ESP_OK;
}
