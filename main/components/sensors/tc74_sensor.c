#include "tc74_sensor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "TC74";

// 現在使用中のI2Cアドレス
static uint8_t tc74_current_addr = TC74_ADDR_DEFAULT;

/**
 * @brief TC74レジスタ読み取り
 * @param reg_addr レジスタアドレス
 * @param data 読み取りデータ格納先
 * @return ESP_OK: 成功, その他: エラー
 */
static esp_err_t tc74_read_register(uint8_t reg_addr, uint8_t *data)
{
    if (data == NULL) {
        ESP_LOGE(TAG, "データポインタがNULLです");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret;

    // レジスタアドレス書き込み
    ret = i2c_master_write_to_device(I2C_NUM_0, tc74_current_addr, &reg_addr, 1, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "レジスタアドレス書き込み失敗 (0x%02X): %s", reg_addr, esp_err_to_name(ret));
        return ret;
    }

    // レジスタデータ読み取り
    ret = i2c_master_read_from_device(I2C_NUM_0, tc74_current_addr, data, 1, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "レジスタ読み取り失敗 (0x%02X): %s", reg_addr, esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGD(TAG, "レジスタ読み取り成功: addr=0x%02X, data=0x%02X", reg_addr, *data);

    return ESP_OK;
}

/**
 * @brief TC74レジスタ書き込み
 * @param reg_addr レジスタアドレス
 * @param data 書き込みデータ
 * @return ESP_OK: 成功, その他: エラー
 */
static esp_err_t tc74_write_register(uint8_t reg_addr, uint8_t data)
{
    uint8_t write_data[2] = {reg_addr, data};

    esp_err_t ret = i2c_master_write_to_device(I2C_NUM_0, tc74_current_addr, write_data, 2, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "レジスタ書き込み失敗 (0x%02X): %s", reg_addr, esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGD(TAG, "レジスタ書き込み成功: addr=0x%02X, data=0x%02X", reg_addr, data);

    return ESP_OK;
}

/**
 * @brief TC74初期化 (アドレス指定)
 * @param i2c_addr TC74のI2Cアドレス
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t tc74_init_with_address(uint8_t i2c_addr)
{
    ESP_LOGI(TAG, "TC74温度センサー初期化中... (I2Cアドレス: 0x%02X)", i2c_addr);

    tc74_current_addr = i2c_addr;

    // デバイス接続確認のため、設定レジスタを読み取る
    uint8_t config;
    esp_err_t ret = tc74_read_register(TC74_REG_CONFIG, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TC74初期化失敗: デバイスが応答しません (アドレス: 0x%02X)", i2c_addr);
        return ret;
    }

    ESP_LOGI(TAG, "TC74接続確認成功 (設定レジスタ: 0x%02X)", config);

    // スタンバイモードを解除 (ノーマルモードに設定)
    ret = tc74_set_standby_mode(false);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "スタンバイモード解除に失敗しましたが続行します");
    }

    // データレディフラグを確認
    bool ready = false;
    ret = tc74_is_data_ready(&ready);
    if (ret == ESP_OK) {
        if (ready) {
            ESP_LOGI(TAG, "TC74初期化完了: データ準備完了");
        } else {
            ESP_LOGW(TAG, "TC74初期化完了: データ準備中 (初回測定待機中)");
            // 初回測定まで少し待つ (データシート: 最大250ms)
            vTaskDelay(pdMS_TO_TICKS(300));
        }
    }

    // テスト読み取り
    float temp;
    ret = tc74_read_temperature(&temp);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "TC74初期化完了: 現在温度 %.0f°C", temp);
    } else {
        ESP_LOGW(TAG, "TC74初期化完了: テスト読み取り失敗");
    }

    return ESP_OK;
}

/**
 * @brief TC74初期化 (デフォルトアドレス使用)
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t tc74_init(void)
{
    return tc74_init_with_address(TC74_ADDR_DEFAULT);
}

/**
 * @brief TC74温度読み取り
 * @param temperature 温度格納先 [°C]
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t tc74_read_temperature(float *temperature)
{
    if (temperature == NULL) {
        ESP_LOGE(TAG, "温度ポインタがNULLです");
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t temp_raw;
    esp_err_t ret = tc74_read_register(TC74_REG_TEMP, &temp_raw);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "温度読み取り失敗");
        return ret;
    }

    // TC74は8ビット符号付き整数で温度を返す
    // 1°C分解能で、-40°C～+125°Cの範囲
    int8_t temp_signed = (int8_t)temp_raw;
    *temperature = (float)temp_signed;

    ESP_LOGD(TAG, "温度読み取り: %.0f°C (raw=0x%02X)", *temperature, temp_raw);

    return ESP_OK;
}

/**
 * @brief TC74温度読み取り (データ構造体使用)
 * @param data データ構造体
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t tc74_read_data(tc74_data_t *data)
{
    if (data == NULL) {
        ESP_LOGE(TAG, "データポインタがNULLです");
        return ESP_ERR_INVALID_ARG;
    }

    // データレディ状態確認
    bool ready = false;
    esp_err_t ret = tc74_is_data_ready(&ready);
    if (ret == ESP_OK) {
        data->data_ready = ready;
    } else {
        data->data_ready = false;
    }

    // 温度読み取り
    float temp;
    ret = tc74_read_temperature(&temp);
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
 * @brief TC74設定レジスタ読み取り
 * @param config 設定値格納先
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t tc74_read_config(uint8_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "設定ポインタがNULLです");
        return ESP_ERR_INVALID_ARG;
    }

    return tc74_read_register(TC74_REG_CONFIG, config);
}

/**
 * @brief TC74設定レジスタ書き込み
 * @param config 設定値
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t tc74_write_config(uint8_t config)
{
    return tc74_write_register(TC74_REG_CONFIG, config);
}

/**
 * @brief TC74スタンバイモード設定
 * @param enable true: スタンバイモード有効, false: ノーマルモード
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t tc74_set_standby_mode(bool enable)
{
    uint8_t config;
    esp_err_t ret = tc74_read_config(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "設定レジスタ読み取り失敗");
        return ret;
    }

    if (enable) {
        config |= TC74_CONFIG_STANDBY;  // スタンバイビットをセット
    } else {
        config &= ~TC74_CONFIG_STANDBY; // スタンバイビットをクリア
    }

    ret = tc74_write_config(config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "設定レジスタ書き込み失敗");
        return ret;
    }

    ESP_LOGI(TAG, "スタンバイモード%s", enable ? "有効" : "無効");

    return ESP_OK;
}

/**
 * @brief TC74データレディ状態確認
 * @param ready データ準備完了フラグ格納先
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t tc74_is_data_ready(bool *ready)
{
    if (ready == NULL) {
        ESP_LOGE(TAG, "readyポインタがNULLです");
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t config;
    esp_err_t ret = tc74_read_config(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "設定レジスタ読み取り失敗");
        return ret;
    }

    *ready = (config & TC74_CONFIG_DATA_READY) ? true : false;

    ESP_LOGD(TAG, "データレディ状態: %s", *ready ? "準備完了" : "準備中");

    return ESP_OK;
}

/**
 * @brief TC74ウェイクアップ (スタンバイモードから復帰)
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t tc74_wakeup(void)
{
    ESP_LOGI(TAG, "TC74をスタンバイモードから復帰させます");

    esp_err_t ret = tc74_set_standby_mode(false);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ウェイクアップ失敗");
        return ret;
    }

    // データ準備完了まで待機 (データシート: 最大250ms)
    vTaskDelay(pdMS_TO_TICKS(300));

    // データレディ確認
    bool ready = false;
    ret = tc74_is_data_ready(&ready);
    if (ret == ESP_OK && ready) {
        ESP_LOGI(TAG, "TC74ウェイクアップ完了: データ準備完了");
    } else {
        ESP_LOGW(TAG, "TC74ウェイクアップ完了: データ準備待ち");
    }

    return ESP_OK;
}
