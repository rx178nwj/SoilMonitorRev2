#include "fdc1004_sensor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "FDC1004";

// FDC1004レジスタ読み取り（16-bit）
esp_err_t fdc1004_read_register(uint8_t reg_addr, uint16_t *value)
{
    if (value == NULL) {
        ESP_LOGE(TAG, "値ポインタがNULLです");
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t data[2];

    // レジスタアドレスを書き込み、データを読み取り
    esp_err_t ret = i2c_master_write_read_device(
        I2C_NUM_0,
        FDC1004_ADDR,
        &reg_addr,
        1,
        data,
        2,
        pdMS_TO_TICKS(100)
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "レジスタ読み取り失敗 (0x%02X): %s", reg_addr, esp_err_to_name(ret));
        return ret;
    }

    // MSBファースト
    *value = (data[0] << 8) | data[1];

    ESP_LOGD(TAG, "レジスタ読み取り: 0x%02X = 0x%04X", reg_addr, *value);

    return ESP_OK;
}

// FDC1004レジスタ書き込み（16-bit）
esp_err_t fdc1004_write_register(uint8_t reg_addr, uint16_t value)
{
    uint8_t data[3];
    data[0] = reg_addr;
    data[1] = (value >> 8) & 0xFF;  // MSB
    data[2] = value & 0xFF;         // LSB

    esp_err_t ret = i2c_master_write_to_device(
        I2C_NUM_0,
        FDC1004_ADDR,
        data,
        3,
        pdMS_TO_TICKS(100)
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "レジスタ書き込み失敗 (0x%02X = 0x%04X): %s",
                 reg_addr, value, esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGD(TAG, "レジスタ書き込み: 0x%02X = 0x%04X", reg_addr, value);

    return ESP_OK;
}

// FDC1004デバイスID確認
esp_err_t fdc1004_check_device_id(uint16_t *device_id)
{
    if (device_id == NULL) {
        ESP_LOGE(TAG, "デバイスIDポインタがNULLです");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = fdc1004_read_register(FDC1004_REG_DEVICE_ID, device_id);
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG, "デバイスID: 0x%04X", *device_id);

    if (*device_id != FDC1004_DEVICE_ID) {
        ESP_LOGW(TAG, "デバイスIDが一致しません (期待値: 0x%04X, 実際: 0x%04X)",
                 FDC1004_DEVICE_ID, *device_id);
        return ESP_ERR_INVALID_VERSION;
    }

    return ESP_OK;
}

// FDC1004測定設定（シングルエンド測定）
esp_err_t fdc1004_configure_single_measurement(
    fdc1004_channel_t channel,
    fdc1004_input_t input,
    uint8_t capdac
)
{
    if (channel > FDC1004_CHANNEL_4) {
        ESP_LOGE(TAG, "無効なチャネル: %d", channel);
        return ESP_ERR_INVALID_ARG;
    }

    if (capdac > 31) {
        ESP_LOGE(TAG, "CAPDAC値が範囲外 (0-31): %d", capdac);
        return ESP_ERR_INVALID_ARG;
    }

    // シングルエンド測定設定
    // Bit 15-13: CHA (正入力)
    // Bit 12-10: CHB (負入力、CAPDAC=4を使用)
    // Bit 9-5: CAPDAC値
    // Bit 4-0: Reserved
    uint16_t config = 0;
    config |= (input & 0x07) << 13;      // CHA
    config |= (FDC1004_CAPDAC & 0x07) << 10;  // CHB = CAPDAC
    config |= (capdac & 0x1F) << 5;      // CAPDAC値

    uint8_t reg_addr = FDC1004_REG_CONF_MEAS1 + channel;

    ESP_LOGD(TAG, "チャネル%d設定: CIN%d, CAPDAC=%d (0x%04X)",
             channel + 1, input + 1, capdac, config);

    return fdc1004_write_register(reg_addr, config);
}

// FDC1004測定設定（差動測定）
esp_err_t fdc1004_configure_differential_measurement(
    fdc1004_channel_t channel,
    fdc1004_input_t cha,
    fdc1004_input_t chb,
    uint8_t capdac
)
{
    if (channel > FDC1004_CHANNEL_4) {
        ESP_LOGE(TAG, "無効なチャネル: %d", channel);
        return ESP_ERR_INVALID_ARG;
    }

    if (capdac > 31) {
        ESP_LOGE(TAG, "CAPDAC値が範囲外 (0-31): %d", capdac);
        return ESP_ERR_INVALID_ARG;
    }

    // 差動測定設定
    uint16_t config = 0;
    config |= (cha & 0x07) << 13;        // CHA（正入力）
    config |= (chb & 0x07) << 10;        // CHB（負入力）
    config |= (capdac & 0x1F) << 5;      // CAPDAC値

    uint8_t reg_addr = FDC1004_REG_CONF_MEAS1 + channel;

    ESP_LOGD(TAG, "チャネル%d差動設定: CIN%d - CIN%d, CAPDAC=%d (0x%04X)",
             channel + 1, cha + 1, chb + 1, capdac, config);

    return fdc1004_write_register(reg_addr, config);
}

// FDC1004測定トリガー
esp_err_t fdc1004_trigger_measurement(uint8_t channel_mask, fdc1004_rate_t rate)
{
    if (channel_mask == 0 || channel_mask > 0x0F) {
        ESP_LOGE(TAG, "無効なチャネルマスク: 0x%02X", channel_mask);
        return ESP_ERR_INVALID_ARG;
    }

    // FDC Configuration Register構造:
    // Bit 15: Reserved
    // Bit 14-12: Reserved
    // Bit 11-10: RATE (サンプルレート)
    // Bit 9: Reserved
    // Bit 8: REPEAT (0=シングルショット, 1=リピート)
    // Bit 7: MEAS_4 (チャネル4測定イネーブル)
    // Bit 6: MEAS_3 (チャネル3測定イネーブル)
    // Bit 5: MEAS_2 (チャネル2測定イネーブル)
    // Bit 4: MEAS_1 (チャネル1測定イネーブル)
    // Bit 3: Reserved
    // Bit 2: DONE_4 (チャネル4測定完了、読み取り専用)
    // Bit 1: DONE_3 (チャネル3測定完了、読み取り専用)
    // Bit 0: DONE_2 (チャネル2測定完了、読み取り専用)
    // Note: DONE_1はビット位置が異なる場合があるため要確認

    uint16_t config = 0;
    config |= (rate & 0x03) << 10;       // RATE設定
    config |= (channel_mask & 0x0F) << 4; // 測定チャネル設定

    ESP_LOGD(TAG, "測定トリガー: チャネルマスク=0x%02X, レート=%d (0x%04X)",
             channel_mask, rate, config);

    return fdc1004_write_register(FDC1004_REG_FDC_CONF, config);
}

// FDC1004測定完了待機
esp_err_t fdc1004_wait_for_measurement(uint8_t channel_mask, uint32_t timeout_ms)
{
    uint16_t status;
    uint32_t elapsed_ms = 0;
    const uint32_t poll_interval_ms = 5;

    while (elapsed_ms < timeout_ms) {
        esp_err_t ret = fdc1004_read_register(FDC1004_REG_FDC_CONF, &status);
        if (ret != ESP_OK) {
            return ret;
        }

        // DONEビットをチェック（ビット3-0）
        uint8_t done_bits = (status >> 3) & 0x0F;

        // 要求されたチャネルの測定が全て完了したか確認
        if ((done_bits & channel_mask) == channel_mask) {
            ESP_LOGD(TAG, "測定完了: チャネルマスク=0x%02X, ステータス=0x%04X, 経過時間=%lums",
                     channel_mask, status, elapsed_ms);
            return ESP_OK;
        }

        vTaskDelay(pdMS_TO_TICKS(poll_interval_ms));
        elapsed_ms += poll_interval_ms;
    }

    ESP_LOGW(TAG, "測定タイムアウト: チャネルマスク=0x%02X, 経過時間=%lums",
             channel_mask, elapsed_ms);
    return ESP_ERR_TIMEOUT;
}

// FDC1004生データ読み取り
esp_err_t fdc1004_read_raw_capacitance(fdc1004_channel_t channel, int32_t *raw_value)
{
    if (channel > FDC1004_CHANNEL_4) {
        ESP_LOGE(TAG, "無効なチャネル: %d", channel);
        return ESP_ERR_INVALID_ARG;
    }

    if (raw_value == NULL) {
        ESP_LOGE(TAG, "生データポインタがNULLです");
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t msb_reg = FDC1004_REG_MEAS1_MSB + (channel * 2);
    uint8_t lsb_reg = FDC1004_REG_MEAS1_LSB + (channel * 2);

    uint16_t msb, lsb;

    // MSB読み取り
    esp_err_t ret = fdc1004_read_register(msb_reg, &msb);
    if (ret != ESP_OK) {
        return ret;
    }

    // LSB読み取り
    ret = fdc1004_read_register(lsb_reg, &lsb);
    if (ret != ESP_OK) {
        return ret;
    }

    // 24ビット符号付き整数を構築
    // MSB: 16ビット、LSB: 上位8ビットのみ使用
    int32_t raw = ((int32_t)msb << 8) | ((lsb >> 8) & 0xFF);

    // 24ビット符号拡張（ビット23が符号ビット）
    if (raw & 0x800000) {
        raw |= 0xFF000000;  // 符号拡張
    }

    *raw_value = raw;

    ESP_LOGD(TAG, "チャネル%d生データ: MSB=0x%04X, LSB=0x%04X, Raw=0x%08lX (%ld)",
             channel + 1, msb, lsb, (long)*raw_value, (long)*raw_value);

    return ESP_OK;
}

// FDC1004静電容量値読み取り（pF単位）
esp_err_t fdc1004_read_capacitance(fdc1004_channel_t channel, float *capacitance, uint8_t capdac)
{
    if (capacitance == NULL) {
        ESP_LOGE(TAG, "静電容量ポインタがNULLです");
        return ESP_ERR_INVALID_ARG;
    }

    int32_t raw_value;
    esp_err_t ret = fdc1004_read_raw_capacitance(channel, &raw_value);
    if (ret != ESP_OK) {
        return ret;
    }

    // 静電容量計算式:
    // capacitance [pF] = (raw_value / 524288.0) + (capdac * 3.125)
    // 524288 = 2^19 (19ビット分解能)
    // 3.125 pF/LSB (CAPDAC単位)
    *capacitance = ((float)raw_value / 524288.0f) + ((float)capdac * 3.125f);

    ESP_LOGD(TAG, "チャネル%d静電容量: %.3f pF (raw=%ld, capdac=%d)",
             channel + 1, *capacitance, (long)raw_value, capdac);

    return ESP_OK;
}

// FDC1004全チャネル測定（シングルエンド）
esp_err_t fdc1004_measure_all_channels(fdc1004_data_t *data, fdc1004_rate_t rate)
{
    if (data == NULL) {
        ESP_LOGE(TAG, "データポインタがNULLです");
        return ESP_ERR_INVALID_ARG;
    }

    data->error = true;

    // デフォルトCAPDAC値（オフセット補正なし）
    uint8_t capdac = 0;

    // 全チャネルをシングルエンド測定として設定
    for (int i = 0; i < 4; i++) {
        esp_err_t ret = fdc1004_configure_single_measurement(
            (fdc1004_channel_t)i,
            (fdc1004_input_t)i,  // CIN1-CIN4
            capdac
        );
        if (ret != ESP_OK) {
            return ret;
        }
    }

    // 全チャネル測定トリガー
    uint8_t channel_mask = 0x0F;  // 全チャネル (bit 0-3)
    esp_err_t ret = fdc1004_trigger_measurement(channel_mask, rate);
    if (ret != ESP_OK) {
        return ret;
    }

    // 測定完了待機（タイムアウト: 100ms）
    ret = fdc1004_wait_for_measurement(channel_mask, 100);
    if (ret != ESP_OK) {
        return ret;
    }

    // 全チャネルのデータ読み取り
    int32_t raw_values[4];
    float capacitances[4];

    for (int i = 0; i < 4; i++) {
        ret = fdc1004_read_raw_capacitance((fdc1004_channel_t)i, &raw_values[i]);
        if (ret != ESP_OK) {
            return ret;
        }

        ret = fdc1004_read_capacitance((fdc1004_channel_t)i, &capacitances[i], capdac);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    // 構造体に格納
    data->raw_ch1 = raw_values[0];
    data->raw_ch2 = raw_values[1];
    data->raw_ch3 = raw_values[2];
    data->raw_ch4 = raw_values[3];

    data->capacitance_ch1 = capacitances[0];
    data->capacitance_ch2 = capacitances[1];
    data->capacitance_ch3 = capacitances[2];
    data->capacitance_ch4 = capacitances[3];

    data->error = false;

    ESP_LOGI(TAG, "全チャネル測定完了: CH1=%.3fpF, CH2=%.3fpF, CH3=%.3fpF, CH4=%.3fpF",
             data->capacitance_ch1, data->capacitance_ch2,
             data->capacitance_ch3, data->capacitance_ch4);

    return ESP_OK;
}

// FDC1004初期化
esp_err_t fdc1004_init(void)
{
    ESP_LOGI(TAG, "FDC1004センサー初期化中...");

    // デバイスID確認
    uint16_t device_id;
    esp_err_t ret = fdc1004_check_device_id(&device_id);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FDC1004: デバイスID確認失敗");
        return ret;
    }

    // Manufacturer ID確認（オプション）
    uint16_t manufacturer_id;
    ret = fdc1004_read_register(FDC1004_REG_MANUFACTURER_ID, &manufacturer_id);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "FDC1004: Manufacturer ID: 0x%04X", manufacturer_id);
    }

    // テスト測定を実行
    fdc1004_data_t test_data;
    ret = fdc1004_measure_all_channels(&test_data, FDC1004_RATE_100HZ);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FDC1004: テスト測定失敗");
        return ret;
    }

    ESP_LOGI(TAG, "FDC1004: 初期化成功 (ID: 0x%04X)", device_id);
    return ESP_OK;
}
