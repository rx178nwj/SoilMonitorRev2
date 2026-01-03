#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/i2c.h"

#include "moisture_sensor.h"
#include "../../common_types.h"
#include <esp_err.h>

// TAG for logging
static const char *TAG = "PLANTER_ADC";

// ADC設定
#define ADC_ATTEN           ADC_ATTEN_DB_12 // 12dBの減衰を使用
#define ADC_BITWIDTH        ADC_BITWIDTH_12 // 12ビットの分解能

#if MOISTURE_SENSOR_TYPE != MOISTURE_SENSOR_TYPE_FDC1004
// ESP32-C3 ADCチャンネル定義
// GPIO3 = ADC1_CH3 (Rev2)
// GPIO2 = ADC1_CH2 (Rev1)
#define MOISTURE_ADC_CHANNEL    MOISTURE_AD_CHANNEL  // ADCチャネルはハードウェアバージョンに依存

#endif

// グローバル変数
static adc_oneshot_unit_handle_t adc1_handle;
static adc_cali_handle_t adc1_cali_moisture_handle = NULL;

// ADC初期化
void init_adc(void)
{
#if MOISTURE_SENSOR_TYPE == MOISTURE_SENSOR_TYPE_FDC1004
    // FDC1004を使用する場合、ADCは無効化（他の用途で使用可能）
    ESP_LOGI(TAG, "ℹ️  Using FDC1004 for moisture sensing, ADC moisture sensor is disabled");
    adc1_handle = NULL;
    adc1_cali_moisture_handle = NULL;
    return;
#else
    // ADC1初期化
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    // ADC1チャンネル設定
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH,
        .atten = ADC_ATTEN,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, MOISTURE_ADC_CHANNEL, &config));

    // ADCキャリブレーション (ESP32-C3に適合するCurve Fitting方式を使用)
    ESP_LOGI(TAG, "ADC-Calibration: Using Curve Fitting for Channel %d", MOISTURE_ADC_CHANNEL);
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH,
        // Note: .chan is not a member of curve_fitting_config_t
    };
    esp_err_t ret = adc_cali_create_scheme_curve_fitting(&cali_config, &adc1_cali_moisture_handle);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ ADC calibration initialized successfully");
    } else if (ret == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "⚠️  ADC calibration scheme not supported, using raw values");
    } else {
        ESP_LOGW(TAG, "⚠️  ADC calibration failed: %s, using raw values", esp_err_to_name(ret));
    }

#if HARDWARE_VERSION == 20
    ESP_LOGI(TAG, "✅ ADC initialized: GPIO3 (ADC1_CH3) - Rev2 Moisture Sensor");
#else
    ESP_LOGI(TAG, "✅ ADC initialized: GPIO2 (ADC1_CH2) - Rev1 Moisture Sensor");
#endif
#endif // MOISTURE_ADC_CHANNEL == 0
}


// 水分センサー読み取り
uint16_t read_moisture_sensor(void)
{
#if MOISTURE_SENSOR_TYPE == MOISTURE_SENSOR_TYPE_FDC1004
    // FDC1004を使用する場合、ADCベースの水分センサーは無効化されています
    ESP_LOGD(TAG, "⚠️  Using FDC1004 for moisture, ADC sensor disabled, returning 0");
    return 0;
#else
    int adc_raw;
    int voltage = 0;
    int sample_count = 10;

    ESP_LOGD(TAG, "🌱 土壌水分センサー読み取り開始 (ADC Channel %d)", MOISTURE_ADC_CHANNEL);

    for (int i = 0; i < sample_count; i++) {
        esp_err_t ret = adc_oneshot_read(adc1_handle, MOISTURE_ADC_CHANNEL, &adc_raw);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "❌ ADC読み取りエラー (sample %d/%d): %s", i+1, sample_count, esp_err_to_name(ret));
            continue;
        }

        if (adc1_cali_moisture_handle) {
            int vol_mv;
            ret = adc_cali_raw_to_voltage(adc1_cali_moisture_handle, adc_raw, &vol_mv);
            if (ret == ESP_OK) {
                voltage += vol_mv;
                ESP_LOGD(TAG, "  Sample %d: raw=%d, voltage=%dmV", i+1, adc_raw, vol_mv);
            } else {
                ESP_LOGW(TAG, "⚠️  キャリブレーション変換失敗 (sample %d): %s", i+1, esp_err_to_name(ret));
                voltage += adc_raw; // キャリブレーション失敗時はRAW値を使用
            }
        } else {
            voltage += adc_raw; // キャリブレーション未初期化時はRAW値を使用
            ESP_LOGD(TAG, "  Sample %d: raw=%d (no calibration)", i+1, adc_raw);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    uint16_t average_voltage = voltage / sample_count;
    ESP_LOGI(TAG, "📊 土壌水分センサー: 平均電圧 = %dmV (%d samples)", average_voltage, sample_count);

    return average_voltage;
#endif // MOISTURE_SENSOR_TYPE
}