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

// FDC1004測定設定（シングルエンド測定、SHLD1でシールド）
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

    // シングルエンド測定設定（SHLD1でシールド）
    // Bit 15-13: CHA (正入力、CIN1-CIN4)
    // Bit 12-10: CHB (0b111=DISABLED でシングルエンド測定)
    // Bit 9-5: CAPDAC値（0x00でSHLD1/SHLD2が内部ショート）
    // Bit 4-0: Reserved
    //
    // 重要: CHB=DISABLED かつ CAPDAC=0 の場合、SHLD1とSHLD2が内部で
    // 短絡され、SHLD1のみで全チャネルのシールドが可能になる
    uint16_t config = 0;
    config |= (input & 0x07) << 13;           // CHA: 測定対象チャネル
    config |= (FDC1004_DISABLED & 0x07) << 10; // CHB: 0b111 (DISABLED)
    config |= (capdac & 0x1F) << 5;           // CAPDAC値（通常0x00）

    uint8_t reg_addr = FDC1004_REG_CONF_MEAS1 + channel;

    ESP_LOGD(TAG, "チャネル%d設定: CIN%d vs GND (SHLD1シールド), CAPDAC=%d (0x%04X)",
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

    // FDC Configuration Register構造 (0x0C):
    // Bit 15-12: Reserved
    // Bit 11-10: RATE[1:0] (サンプルレート: 00=Reserved, 01=100S/s, 10=200S/s, 11=400S/s)
    // Bit 9: Reserved
    // Bit 8: REPEAT (0=シングルショット, 1=リピート連続測定)
    // Bit 7: MEAS_4 (Measurement 4 イネーブル、書き込み)
    // Bit 6: MEAS_3 (Measurement 3 イネーブル、書き込み)
    // Bit 5: MEAS_2 (Measurement 2 イネーブル、書き込み)
    // Bit 4: MEAS_1 (Measurement 1 イネーブル、書き込み)
    // Bit 3: DONE_4 (Measurement 4 完了フラグ、読み取り専用)
    // Bit 2: DONE_3 (Measurement 3 完了フラグ、読み取り専用)
    // Bit 1: DONE_2 (Measurement 2 完了フラグ、読み取り専用)
    // Bit 0: DONE_1 (Measurement 1 完了フラグ、読み取り専用)

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

    ESP_LOGD(TAG, "測定完了待機開始: チャネルマスク=0x%02X", channel_mask);

    while (elapsed_ms < timeout_ms) {
        esp_err_t ret = fdc1004_read_register(FDC1004_REG_FDC_CONF, &status);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "FDC_CONFレジスタ読み取り失敗");
            return ret;
        }

        // DONEビットをチェック（bit[3:0]）
        // FDC Configuration Register (0x0C):
        // Bit 3: DONE_4 (Measurement 4 Complete)
        // Bit 2: DONE_3 (Measurement 3 Complete)
        // Bit 1: DONE_2 (Measurement 2 Complete)
        // Bit 0: DONE_1 (Measurement 1 Complete)
        uint8_t done_bits = status & 0x0F;  // bit[3:0]を直接取得

        ESP_LOGD(TAG, "ポーリング: ステータス=0x%04X, DONE bits=0x%02X, 経過=%lums",
                 status, done_bits, elapsed_ms);

        // 要求されたチャネルの測定が全て完了したか確認
        if ((done_bits & channel_mask) == channel_mask) {
            ESP_LOGD(TAG, "測定完了: チャネルマスク=0x%02X, DONE bits=0x%02X, 経過時間=%lums",
                     channel_mask, done_bits, elapsed_ms);
            return ESP_OK;
        }

        vTaskDelay(pdMS_TO_TICKS(poll_interval_ms));
        elapsed_ms += poll_interval_ms;
    }

    ESP_LOGW(TAG, "測定タイムアウト: チャネルマスク=0x%02X, 最終DONE bits=0x%02X, 経過時間=%lums",
             channel_mask, status & 0x0F, elapsed_ms);
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

    // 各チャネルを独立して計測（クロストーク防止のため）
    int32_t raw_values[4];
    float capacitances[4];

    for (int ch = 0; ch < 4; ch++) {
        ESP_LOGD(TAG, "========== チャネル%d 計測開始 ==========", ch + 1);

        // ステップ1: 測定構成 (Measurement Configuration)
        // 対応するレジスタ: MEAS1(0x08), MEAS2(0x09), MEAS3(0x0A), MEAS4(0x0B)
        // CHA: 計測対象ピン (CIN1=b000, CIN2=b001, CIN3=b010, CIN4=b011)
        // CHB: DISABLED (b111) でシングルエンド測定 (CINn vs GND)
        // CAPDAC: 0 でSHLD1/SHLD2内部ショート
        esp_err_t ret = fdc1004_configure_single_measurement(
            (fdc1004_channel_t)ch,
            (fdc1004_input_t)ch,  // CIN1-CIN4
            capdac
        );
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "チャネル%d 測定構成失敗", ch + 1);
            return ret;
        }
        ESP_LOGD(TAG, "ステップ1完了: 測定構成設定 (CIN%d vs GND)", ch + 1);

        // ステップ2: 測定トリガー (Triggering)
        // FDC構成レジスタ(0x0C)に書き込み
        // REPEAT=0: 単発測定
        // MEAS_x=1: 該当チャネルの測定を有効化
        // RATE: サンプリングレート設定
        uint8_t channel_mask = (1 << ch);  // 該当チャネルのみ有効化
        ret = fdc1004_trigger_measurement(channel_mask, rate);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "チャネル%d 測定トリガー失敗", ch + 1);
            return ret;
        }
        ESP_LOGD(TAG, "ステップ2完了: 測定トリガー送信");

        // ステップ3: 測定完了待機 (Wait for Completion)
        // レジスタ0x0CのDONE_xビット（bit[3:0]）をポーリング
        // DONE_x=1になるまで待機（タイムアウト: 100ms）
        ret = fdc1004_wait_for_measurement(channel_mask, 100);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "チャネル%d 測定完了待機タイムアウト", ch + 1);
            return ret;
        }
        ESP_LOGD(TAG, "ステップ3完了: 測定完了確認");

        // ステップ4: 測定結果読み取りと計算 (Read and Conversion)
        // データレジスタから24ビット値を取得
        // 読み取り順序: 必ずMSB（下位アドレス）→LSB（上位アドレス）
        // 例: MEAS1なら 0x00(MSB) → 0x01(LSB)
        ret = fdc1004_read_raw_capacitance((fdc1004_channel_t)ch, &raw_values[ch]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "チャネル%d 生データ読み取り失敗", ch + 1);
            return ret;
        }

        // 容量値計算: (24ビット値 / 2^19) + Coffset
        // Coffset = CAPDAC × 3.125pF（今回はCAPDAC=0なのでCoffset=0）
        ret = fdc1004_read_capacitance((fdc1004_channel_t)ch, &capacitances[ch], capdac);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "チャネル%d 容量値計算失敗", ch + 1);
            return ret;
        }
        ESP_LOGD(TAG, "ステップ4完了: データ読み取り (raw=%ld, %.3fpF)",
                 (long)raw_values[ch], capacitances[ch]);

        ESP_LOGI(TAG, "チャネル%d 測定完了: %.3fpF", ch + 1, capacitances[ch]);
    }

    // 測定結果を構造体に格納
    data->raw_ch1 = raw_values[0];
    data->raw_ch2 = raw_values[1];
    data->raw_ch3 = raw_values[2];
    data->raw_ch4 = raw_values[3];

    data->capacitance_ch1 = capacitances[0];
    data->capacitance_ch2 = capacitances[1];
    data->capacitance_ch3 = capacitances[2];
    data->capacitance_ch4 = capacitances[3];

    data->error = false;

    ESP_LOGI(TAG, "全チャネル独立測定完了: CH1=%.3fpF, CH2=%.3fpF, CH3=%.3fpF, CH4=%.3fpF",
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
