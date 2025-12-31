#include "ds18b20_sensor.h"
#include "onewire_bus.h"
#include "onewire_cmd.h"
#include "onewire_crc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DS18B20";

// DS18B20コマンド定義
#define DS18B20_CMD_CONVERT_TEMP    0x44  // 温度変換開始
#define DS18B20_CMD_READ_SCRATCHPAD 0xBE  // スクラッチパッド読み取り
#define DS18B20_CMD_WRITE_SCRATCHPAD 0x4E // スクラッチパッド書き込み
#define DS18B20_CMD_COPY_SCRATCHPAD 0x48  // スクラッチパッドコピー
#define DS18B20_FAMILY_CODE         0x28  // DS18B20ファミリーコード

// グローバル変数
static onewire_bus_handle_t bus_handle = NULL;
static onewire_bus_config_t bus_config = {
    .bus_gpio_num = DS18B20_PIN,
};
static onewire_bus_rmt_config_t rmt_config = {
    .max_rx_bytes = 10, // 最大受信バイト数
};

// デバイス情報
static ds18b20_info_t device_info = {0};

/**
 * @brief DS18B20センサー初期化
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t ds18b20_init(void)
{
    ESP_LOGI(TAG, "🌡️  DS18B20温度センサー初期化中... (GPIO%d)", DS18B20_PIN);

    // 1-Wireバス初期化
    esp_err_t ret = onewire_new_bus_rmt(&bus_config, &rmt_config, &bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ 1-Wireバス初期化失敗: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "✅ 1-Wireバス初期化完了");

    // デバイス検索
    ret = ds18b20_scan_devices(&device_info);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "⚠️  デバイススキャン失敗: %s", esp_err_to_name(ret));
        return ret;
    }

    if (device_info.device_count == 0) {
        ESP_LOGW(TAG, "⚠️  DS18B20デバイスが見つかりません");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "✅ DS18B20初期化完了: %d個のデバイスを検出", device_info.device_count);

    // 各デバイスのアドレスを表示
    for (int i = 0; i < device_info.device_count; i++) {
        ESP_LOGI(TAG, "  デバイス%d: 0x%016llX", i + 1, (unsigned long long)device_info.device_addrs[i]);
    }

    // デフォルトで12ビット分解能に設定
    for (int i = 0; i < device_info.device_count; i++) {
        ds18b20_set_resolution(device_info.device_addrs[i], DS18B20_RESOLUTION_12_BIT);
    }

    return ESP_OK;
}

/**
 * @brief DS18B20センサー終了処理
 */
void ds18b20_deinit(void)
{
    if (bus_handle != NULL) {
        onewire_del_bus(bus_handle);
        bus_handle = NULL;
        ESP_LOGI(TAG, "DS18B20終了処理完了");
    }
}

/**
 * @brief デバイス検索
 * @param info デバイス情報格納先
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t ds18b20_scan_devices(ds18b20_info_t *info)
{
    if (info == NULL) {
        ESP_LOGE(TAG, "デバイス情報ポインタがNULLです");
        return ESP_ERR_INVALID_ARG;
    }

    if (bus_handle == NULL) {
        ESP_LOGE(TAG, "1-Wireバスが初期化されていません");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "🔍 DS18B20デバイスをスキャン中...");

    onewire_device_iter_handle_t iter = NULL;
    onewire_device_t next_onewire_device;
    esp_err_t ret = ESP_OK;

    // デバイス検索開始
    ret = onewire_new_device_iter(bus_handle, &iter);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "デバイスイテレータ作成失敗: %s", esp_err_to_name(ret));
        return ret;
    }

    info->device_count = 0;

    // 全デバイスをスキャン
    do {
        ret = onewire_device_iter_get_next(iter, &next_onewire_device);
        if (ret == ESP_OK) {
            // DS18B20のファミリーコードは0x28
            uint8_t family_code = next_onewire_device.address & 0xFF;
            if (family_code == DS18B20_FAMILY_CODE) {
                if (info->device_count < MAX_DS18B20_DEVICES) {
                    info->device_addrs[info->device_count] = next_onewire_device.address;
                    info->device_count++;
                    ESP_LOGI(TAG, "  DS18B20検出: 0x%016llX", (unsigned long long)next_onewire_device.address);
                } else {
                    ESP_LOGW(TAG, "最大デバイス数(%d)に達しました", MAX_DS18B20_DEVICES);
                    break;
                }
            }
        }
    } while (ret != ESP_ERR_NOT_FOUND);

    onewire_del_device_iter(iter);

    ESP_LOGI(TAG, "✅ スキャン完了: %d個のDS18B20デバイスを検出", info->device_count);

    // グローバル変数にコピー
    device_info = *info;

    return ESP_OK;
}

/**
 * @brief 単一デバイスから温度読み取り
 * @param device_addr デバイスアドレス
 * @param temperature 温度格納先
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t ds18b20_read_temperature(uint64_t device_addr, float *temperature)
{
    if (temperature == NULL) {
        ESP_LOGE(TAG, "温度ポインタがNULLです");
        return ESP_ERR_INVALID_ARG;
    }

    if (bus_handle == NULL) {
        ESP_LOGE(TAG, "1-Wireバスが初期化されていません");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret;
    uint8_t scratchpad[9];

    // リセット
    ret = onewire_bus_reset(bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "バスリセット失敗: %s", esp_err_to_name(ret));
        return ret;
    }

    // デバイス選択
    onewire_device_t device = {.address = device_addr};
    ret = onewire_rom_match(bus_handle, device);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "デバイス選択失敗: %s", esp_err_to_name(ret));
        return ret;
    }

    // 温度変換開始
    uint8_t convert_cmd = DS18B20_CMD_CONVERT_TEMP;
    ret = onewire_bus_write_bytes(bus_handle, &convert_cmd, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "温度変換コマンド送信失敗: %s", esp_err_to_name(ret));
        return ret;
    }

    // 変換完了待機（12ビット分解能で750ms）
    vTaskDelay(pdMS_TO_TICKS(800));

    // リセット
    ret = onewire_bus_reset(bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "バスリセット失敗: %s", esp_err_to_name(ret));
        return ret;
    }

    // デバイス選択
    ret = onewire_rom_match(bus_handle, device);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "デバイス選択失敗: %s", esp_err_to_name(ret));
        return ret;
    }

    // スクラッチパッド読み取り
    uint8_t read_cmd = DS18B20_CMD_READ_SCRATCHPAD;
    ret = onewire_bus_write_bytes(bus_handle, &read_cmd, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "読み取りコマンド送信失敗: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = onewire_bus_read_bytes(bus_handle, scratchpad, 9);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "スクラッチパッド読み取り失敗: %s", esp_err_to_name(ret));
        return ret;
    }

    // CRCチェック
    uint8_t crc = onewire_crc8(scratchpad, 8);
    if (crc != scratchpad[8]) {
        ESP_LOGW(TAG, "CRCエラー: 計算値=0x%02X, 受信値=0x%02X", crc, scratchpad[8]);
        return ESP_ERR_INVALID_CRC;
    }

    // 温度データ変換
    int16_t temp_raw = (scratchpad[1] << 8) | scratchpad[0];
    *temperature = (float)temp_raw / 16.0f;

    ESP_LOGD(TAG, "温度読み取り: %.2f°C (raw=0x%04X, 0x%016llX)",
             *temperature, temp_raw, (unsigned long long)device_addr);

    return ESP_OK;
}

/**
 * @brief 全デバイスから温度読み取り
 * @param data_array データ配列
 * @param count 読み取ったデバイス数
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t ds18b20_read_all_temperatures(ds18b20_data_t *data_array, int *count)
{
    if (data_array == NULL || count == NULL) {
        ESP_LOGE(TAG, "パラメータがNULLです");
        return ESP_ERR_INVALID_ARG;
    }

    if (device_info.device_count == 0) {
        ESP_LOGW(TAG, "デバイスが検出されていません");
        *count = 0;
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGD(TAG, "全デバイスの温度読み取り開始");

    *count = 0;

    for (int i = 0; i < device_info.device_count; i++) {
        float temp;
        esp_err_t ret = ds18b20_read_temperature(device_info.device_addrs[i], &temp);

        data_array[i].device_addr = device_info.device_addrs[i];

        if (ret == ESP_OK) {
            data_array[i].temperature = temp;
            data_array[i].valid = true;
            data_array[i].error = false;
            (*count)++;
        } else {
            data_array[i].temperature = 0.0f;
            data_array[i].valid = false;
            data_array[i].error = true;
            ESP_LOGW(TAG, "デバイス%d読み取り失敗", i);
        }
    }

    ESP_LOGI(TAG, "✅ 温度読み取り完了: %d/%d デバイス成功", *count, device_info.device_count);

    return (*count > 0) ? ESP_OK : ESP_FAIL;
}

/**
 * @brief 最初のデバイスから温度読み取り（単一センサー用）
 * @param temperature 温度格納先
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t ds18b20_read_single_temperature(float *temperature)
{
    if (temperature == NULL) {
        ESP_LOGE(TAG, "温度ポインタがNULLです");
        return ESP_ERR_INVALID_ARG;
    }

    if (device_info.device_count == 0) {
        ESP_LOGW(TAG, "デバイスが検出されていません");
        return ESP_ERR_NOT_FOUND;
    }

    // 最初のデバイスから読み取り
    esp_err_t ret = ds18b20_read_temperature(device_info.device_addrs[0], temperature);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "🌡️  土壌温度: %.2f°C", *temperature);
    }

    return ret;
}

/**
 * @brief 分解能設定
 * @param device_addr デバイスアドレス
 * @param resolution 分解能
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t ds18b20_set_resolution(uint64_t device_addr, ds18b20_resolution_t resolution)
{
    if (bus_handle == NULL) {
        ESP_LOGE(TAG, "1-Wireバスが初期化されていません");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret;

    // リセット
    ret = onewire_bus_reset(bus_handle);
    if (ret != ESP_OK) {
        return ret;
    }

    // デバイス選択
    onewire_device_t device = {.address = device_addr};
    ret = onewire_rom_match(bus_handle, device);
    if (ret != ESP_OK) {
        return ret;
    }

    // スクラッチパッド書き込みコマンド
    uint8_t config_value = (resolution << 5) | 0x1F; // 分解能設定
    uint8_t write_data[4] = {
        DS18B20_CMD_WRITE_SCRATCHPAD,
        0x00,  // TH (high alarm)
        0x00,  // TL (low alarm)
        config_value
    };

    ret = onewire_bus_write_bytes(bus_handle, write_data, 4);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "分解能設定失敗: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGD(TAG, "分解能設定完了: %dビット", 9 + resolution);

    return ESP_OK;
}
