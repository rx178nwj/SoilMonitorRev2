// main/main.c

/**
 * @file main.c
 * @brief ESP-IDF v5.5 NimBLE Power-Saving Peripheral for Sensor Data
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_pm.h"
#include "driver/gpio.h"
#include <esp_err.h>

/* NimBLE Includes */
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"
#include "esp_bt.h"

// I2C and GPIO and ADC includes
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

// 分離されたモジュール
#include "components/ble/ble_manager.h"
#include "components/sensors/sht30_sensor.h"
#include "components/sensors/sht40_sensor.h"
#include "components/sensors/tsl2591_sensor.h"
#include "components/sensors/fdc1004_sensor.h"
#include "components/sensors/ds18b20_sensor.h"
#include "components/sensors/tc74_sensor.h"
#include "components/sensors/tmp102_sensor.h"
#include "wifi_manager.h"
#include "time_sync_manager.h"
#include "components/sensors/moisture_sensor.h"
#include "components/actuators/led_control.h"
#include "common_types.h"
#include "components/plant_logic/plant_manager.h"
#include "nvs_config.h"
#include "components/plant_logic/data_buffer.h"

static const char *TAG = "PLANTER_MONITOR";

// タスクハンドル
static TaskHandle_t g_sensor_task_handle = NULL;
static TaskHandle_t g_analysis_task_handle = NULL;

static TimerHandle_t g_notify_timer;

// 土壌温度センサー接続状態
typedef struct {
    bool tc74_connected;      // TC74センサーが接続されているか
    uint8_t tmp102_count;     // TMP102検出数 (0〜4)
    bool ds18b20_connected;   // DS18B20センサーが接続されているか
} soil_temp_sensor_state_t;

static soil_temp_sensor_state_t g_soil_temp_sensors = {
    .tc74_connected = false,
    .tmp102_count = 0,
    .ds18b20_connected = false
};

// 土壌センサー構成情報 (BLEからexternで参照)
soil_sensor_config_t g_sensor_config;

static void notify_timer_callback(TimerHandle_t xTimer);

// I2C初期化
static esp_err_t init_i2c(void) {
    i2c_config_t i2c_config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000, // 100kHz
        .clk_flags = 0,
    };
    esp_err_t ret = i2c_param_config(I2C_NUM_0, &i2c_config);
    if (ret != ESP_OK) return ret;
    ret = i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "I2C initialized successfully");
        vTaskDelay(pdMS_TO_TICKS(100)); // I2Cデバイスの安定化待機
    }
    return ret;
}
// qsortのための比較関数を追加
static int compare_floats(const void *a, const void *b) {
    float fa = *(const float *)a;
    float fb = *(const float *)b;
    return (fa > fb) - (fa < fb);
}

#if (HARDWARE_VERSION == 30 || HARDWARE_VERSION == 40)
/**
 * 静電容量(pF)から湿度パーセント(0-100)への変換テーブル
 * 0.1pF: 乾燥 (0%)
 * 16pF: 湿潤 (100%)
 * 線形補間で変換
 */
#define CAPACITANCE_DRY_PF    0.1f   // 乾燥時の静電容量 (pF)
#define CAPACITANCE_WET_PF    16.0f  // 湿潤時の静電容量 (pF)

/**
 * @brief 静電容量(pF)から湿度パーセントに変換
 * @param capacitance_pf 静電容量値 (pF)
 * @return 湿度パーセント (0-100)
 */
static uint8_t capacitance_to_humidity_percent(float capacitance_pf) {
    // 範囲外の値をクランプ
    if (capacitance_pf <= CAPACITANCE_DRY_PF) {
        return 0;
    }
    if (capacitance_pf >= CAPACITANCE_WET_PF) {
        return 100;
    }

    // 線形補間: (capacitance - dry) / (wet - dry) * 100
    float humidity = (capacitance_pf - CAPACITANCE_DRY_PF) /
                     (CAPACITANCE_WET_PF - CAPACITANCE_DRY_PF) * 100.0f;

    return (uint8_t)humidity;
}
#endif

// 全センサーデータ読み取り
static void read_all_sensors(soil_data_t *data) {
    ESP_LOGI(TAG, "📊 Reading all sensors...");

    // データ構造バージョンを設定
    data->data_version = DATA_STRUCTURE_VERSION;

    struct tm datetime;
    time_sync_manager_get_current_time(&datetime);
    data->datetime = datetime;
    data->sensor_error = false; // エラーフラグを初期化

#if MOISTURE_SENSOR_TYPE == MOISTURE_SENSOR_TYPE_FDC1004
    // Rev3: FDC1004静電容量センサーを使用
    fdc1004_data_t fdc_data;
    float sum = 0.0f;
    if (fdc1004_measure_all_channels(&fdc_data, FDC1004_RATE_100HZ) == ESP_OK) {
        

        // 全チャンネルの静電容量データを配列に格納
        data->soil_moisture_capacitance[0] = fdc_data.capacitance_ch1;
        data->soil_moisture_capacitance[1] = fdc_data.capacitance_ch2;
        data->soil_moisture_capacitance[2] = fdc_data.capacitance_ch3;
        data->soil_moisture_capacitance[3] = fdc_data.capacitance_ch4;

        // 全チャンネルの平均を土壌水分値として使用
        for (int i = 0; i < FDC1004_CHANNEL_COUNT; i++) {
            sum += data->soil_moisture_capacitance[i];
        }
        data->soil_moisture = sum / FDC1004_CHANNEL_COUNT;

        ESP_LOGI(TAG, "  - FDC1004 CH1: %.3f pF (raw: %d)",
                 fdc_data.capacitance_ch1, fdc_data.raw_ch1);
        ESP_LOGI(TAG, "  - FDC1004 CH2: %.3f pF (raw: %d)",
                 fdc_data.capacitance_ch2, fdc_data.raw_ch2);
        ESP_LOGI(TAG, "  - FDC1004 CH3: %.3f pF (raw: %d)",
                 fdc_data.capacitance_ch3, fdc_data.raw_ch3);
        ESP_LOGI(TAG, "  - FDC1004 CH4: %.3f pF (raw: %d)",
                 fdc_data.capacitance_ch4, fdc_data.raw_ch4);
    } else {
        ESP_LOGE(TAG, "  - FDC1004: Failed to read data");
        data->soil_moisture = 0.0f;
        // エラー時は全チャンネルを0に設定
        for (int i = 0; i < FDC1004_CHANNEL_COUNT; i++) {
            data->soil_moisture_capacitance[i] = 0.0f;
        }
        data->sensor_error = true;
    }
#else
    // Rev1/Rev2: ADCベースの水分センサーを使用
    data->soil_moisture = (float)read_moisture_sensor();
    ESP_LOGI(TAG, "  - Soil Moisture: %.0f mV", data->soil_moisture);
#endif

#if TEMPARETURE_SENSOR_TYPE == TEMPARETURE_SENSOR_TYPE_SHT30
    // Rev1: SHT30センサーを使用
    sht30_data_t sht30;
    if (sht30_read_data(&sht30) == ESP_OK && !sht30.error) {
        data->temperature = sht30.temperature;
        data->humidity = sht30.humidity;
        ESP_LOGI(TAG, "  - SHT30: Temp=%.1f C, Hum=%.1f %%", data->temperature, data->humidity);
    } else {
        ESP_LOGE(TAG, "  - SHT30: Failed to read data");
        data->sensor_error = true;
    }
#elif TEMPARETURE_SENSOR_TYPE == TEMPARETURE_SENSOR_TYPE_SHT40// HARDWARE_VERSION == 20 or HARDWARE_VERSION == 30 or HARDWARE_VERSION == 40
    // Rev2/Rev3: SHT40センサーを使用
    sht40_data_t sht40;
    if (sht40_read_data(&sht40) == ESP_OK && !sht40.error) {
        data->temperature = sht40.temperature;
        data->humidity = sht40.humidity;
        ESP_LOGI(TAG, "  - SHT40: Temp=%.1f C, Hum=%.1f %%", data->temperature, data->humidity);
    } else {
        ESP_LOGE(TAG, "  - SHT40: Failed to read data");
        data->temperature = 0.0f;  // デフォルト値を設定
        data->humidity = 0.0f;     // デフォルト値を設定
        data->sensor_error = true;
    }
#endif

    // TSL2591から5回データを取得
    tsl2591_data_t tsl2591;
    float lux_readings[5];
    int valid_readings = 0;
    for (int i = 0; i < 5; i++) {
        if (tsl2591_read_data(&tsl2591) == ESP_OK) {
            lux_readings[valid_readings] = tsl2591.light_lux;
            valid_readings++;
        }
        vTaskDelay(pdMS_TO_TICKS(50)); // 測定間に短い待機時間を入れる
    }

    if (valid_readings >= 3) {
        // 読み取った値をソート
        qsort(lux_readings, valid_readings, sizeof(float), compare_floats);

        // 最小値と最大値を除外して平均を計算
        float sum = 0;
        // 実際に有効な読み取りが5回未満の場合も考慮
        int start_index = (valid_readings > 3) ? 1 : 0;
        int end_index = (valid_readings > 4) ? valid_readings - 1 : valid_readings;
        int count_for_avg = 0;

        for (int i = start_index; i < end_index; i++) {
            sum += lux_readings[i];
            count_for_avg++;
        }

        if (count_for_avg > 0) {
            data->lux = sum / count_for_avg;
        } else {
             // 3回しか読み取れなかった場合など
            data->lux = lux_readings[0];
        }

        ESP_LOGI(TAG, "  - TSL2591: Lux=%.1f (Avg of %d readings)", data->lux, count_for_avg);
    } else {
        ESP_LOGE(TAG, "  - TSL2591: Failed to get enough valid readings (%d)", valid_readings);
        data->sensor_error = true;
        data->lux = 0; // エラー時は0を設定
    }

#if HARDWARE_VERSION == 40
    // Rev4: TMP102 x4 の土壌温度を一括読み取り
    {
        uint8_t count = 0;
        tmp102_read_all_temperatures(data->soil_temperature, &count);
        data->soil_temperature_count = count;
        for (int i = 0; i < count; i++) {
            ESP_LOGI(TAG, "  - TMP102[%d] Soil Temperature: %.2f°C", i, data->soil_temperature[i]);
        }
        // 未使用スロットを0に初期化
        for (int i = count; i < TMP102_MAX_DEVICES; i++) {
            data->soil_temperature[i] = 0.0f;
        }
    }

    // Rev4: DS18B20 拡張温度センサー読み取り
    if (g_soil_temp_sensors.ds18b20_connected) {
        float ext_temp;
        if (ds18b20_read_single_temperature(&ext_temp) == ESP_OK) {
            data->ext_temperature = ext_temp;
            data->ext_temperature_valid = true;
            ESP_LOGI(TAG, "  - DS18B20 Ext Temperature: %.2f°C", ext_temp);
        } else {
            data->ext_temperature = 0.0f;
            data->ext_temperature_valid = false;
            ESP_LOGW(TAG, "  - DS18B20: Failed to read ext temperature");
        }
    } else {
        data->ext_temperature = 0.0f;
        data->ext_temperature_valid = false;
    }
#elif HARDWARE_VERSION == 30
    // Rev3: TC74/DS18B20 の土壌温度読み取り
    // 土壌温度センサー1: TC74優先、次にDS18B20
    if (g_soil_temp_sensors.tc74_connected) {
        float tc74_temp;
        if (tc74_read_temperature(&tc74_temp) == ESP_OK) {
            data->soil_temperature1 = tc74_temp;
            ESP_LOGI(TAG, "  - TC74 Soil Temperature 1: %.0f°C", tc74_temp);
        } else {
            data->soil_temperature1 = 0.0f;
            ESP_LOGW(TAG, "  - TC74: Failed to read temperature 1");
        }
    } else if (g_soil_temp_sensors.ds18b20_connected) {
        float ds18b20_temp;
        if (ds18b20_read_single_temperature(&ds18b20_temp) == ESP_OK) {
            data->soil_temperature1 = ds18b20_temp;
            ESP_LOGI(TAG, "  - DS18B20 Soil Temperature 1: %.2f°C", ds18b20_temp);
        } else {
            data->soil_temperature1 = 0.0f;
            ESP_LOGW(TAG, "  - DS18B20: Failed to read temperature 1");
        }
    } else {
        data->soil_temperature1 = 0.0f;
        ESP_LOGD(TAG, "  - Soil Temperature 1: No sensor connected");
    }

    // 土壌温度センサー2: TC74とDS18B20の両方が接続されている場合のみ
    if (g_soil_temp_sensors.tc74_connected && g_soil_temp_sensors.ds18b20_connected) {
        float ds18b20_temp2;
        if (ds18b20_read_single_temperature(&ds18b20_temp2) == ESP_OK) {
            data->soil_temperature2 = ds18b20_temp2;
            ESP_LOGI(TAG, "  - DS18B20 Soil Temperature 2: %.2f°C", ds18b20_temp2);
        } else {
            data->soil_temperature2 = 0.0f;
            ESP_LOGW(TAG, "  - DS18B20: Failed to read temperature 2");
        }
    } else {
        data->soil_temperature2 = 0.0f;
        ESP_LOGD(TAG, "  - Soil Temperature 2: No sensor assigned");
    }
#endif
}

/* --- GPIO Initialization --- */
void init_gpio(void) {
    gpio_reset_pin(RED_LED_PIN);
    gpio_set_direction(RED_LED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(RED_LED_PIN, 0);

    gpio_reset_pin(BLUE_LED_PIN);
    gpio_set_direction(BLUE_LED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(BLUE_LED_PIN, 0);
}

// センサー読み取り専用タスク
static void sensor_read_task(void* pvParameters) {
    soil_data_t data;
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        gpio_set_level(RED_LED_PIN, 1);
        read_all_sensors(&data);
        plant_manager_process_sensor_data(&data);
        vTaskDelay(pdMS_TO_TICKS(1000));
        gpio_set_level(RED_LED_PIN, 0);
    }
}

/* --- Timer Callback for Notifications --- */
static void notify_timer_callback(TimerHandle_t xTimer) {
    if (g_sensor_task_handle != NULL) {
        // タイマーコールバックはISRではないため、通常のタスク通知を使用
        xTaskNotifyGive(g_sensor_task_handle);
    }
}

// WiFi/Timeコールバック
static void wifi_status_callback(bool connected) {
    if (connected) time_sync_manager_start();
}
static void time_sync_callback(struct timeval *tv) {
    ESP_LOGI(TAG, "⏰ システム時刻が同期されました");
}

// ネットワーク初期化
static void network_init(void) {
    wifi_manager_start();
    if (wifi_manager_wait_for_connection(WIFI_CONNECT_TIMEOUT_SEC)) {
        time_sync_manager_wait_for_sync(SNTP_SYNC_TIMEOUT_SEC);
    }
}

// センサーデータと判断結果をログ出力
static void log_sensor_data_and_status(const soil_data_t *soil_data,
                                     const plant_status_result_t *status,
                                     int loop_count) {
    ESP_LOGI(TAG, "=== 植物状態判断結果 (Loop: %d) ===", loop_count);
    ESP_LOGI(TAG, "現在気温: %.1f℃, 湿度: %.1f%%, 照度: %.0flux, 土壌水分: %.0fmV",
             soil_data->temperature, soil_data->humidity,
             soil_data->lux, soil_data->soil_moisture);
    ESP_LOGI(TAG, "状態: %s",
             plant_manager_get_plant_condition_string(status->plant_condition));
}

/**
 * 植物状態判断と結果表示を行うタスク
 */
static void status_analysis_task(void *pvParameters) {
    int analysis_count = 0;
    ESP_LOGI(TAG, "状態分析タスク開始（1分間隔）");
    vTaskDelay(pdMS_TO_TICKS(10000)); // 10秒待機

    while (1) {
        // 追加: 分析開始前にデータバッファの状態を確認
        ESP_LOGI(TAG, "Analyzing plant status...");
        data_buffer_print_status();

        plant_status_result_t status;
        minute_data_t latest_sensor;
        soil_data_t display_data = {0}; // ゼロで初期化

        // 最初に最新データを一度だけ取得
        if (data_buffer_get_latest_minute_data(&latest_sensor) == ESP_OK && latest_sensor.valid) {
            // 取得したデータを使って状態を判断
            status = plant_manager_determine_status(&latest_sensor);

            // ログ表示用に同じデータをコピー
            display_data.datetime = latest_sensor.timestamp;
            display_data.temperature = latest_sensor.temperature;
            display_data.humidity = latest_sensor.humidity;
            display_data.lux = latest_sensor.lux;
            display_data.soil_moisture = latest_sensor.soil_moisture;
        } else {
            // データ取得失敗またはデータが無効な場合
            ESP_LOGW(TAG, "最新センサーデータの取得に失敗、またはデータが無効です");
            status.plant_condition = ERROR_CONDITION;
            // display_dataはゼロのまま
        }

        // 結果をログに出力
        log_sensor_data_and_status(&display_data, &status, ++analysis_count);

#if (HARDWARE_VERSION == 30 || HARDWARE_VERSION == 40)
        // Rev3: 静電容量から湿度を計算し、色温度でLED表示
        // 温度限界のみ特別扱い
        if (status.plant_condition == TEMP_TOO_HIGH) {
            ws2812_set_preset_color(WS2812_COLOR_RED);
            ESP_LOGW(TAG, "🔥 高温限界です！");
        } else if (status.plant_condition == TEMP_TOO_LOW) {
            ws2812_set_preset_color(WS2812_COLOR_BLUE);
            ESP_LOGW(TAG, "🧊 低温限界です！");
        } else if (status.plant_condition == ERROR_CONDITION) {
            ws2812_set_preset_color(WS2812_COLOR_PURPLE);
            ESP_LOGE(TAG, "❌ エラー状態です！");
        } else {
            // 静電容量の平均値から湿度パーセントを計算
            uint8_t humidity_percent = capacitance_to_humidity_percent(latest_sensor.soil_moisture);
            ESP_LOGI(TAG, "📊 静電容量: %.2f pF → 湿度: %d%%", latest_sensor.soil_moisture, humidity_percent);
            // 湿度に応じた色温度でLED表示 (暖色:乾燥 → 青:湿潤)
            ws2812_set_color_by_humidity(humidity_percent);
        }
#else
        // Rev1/Rev2: 従来の状態別LED表示
        switch (status.plant_condition) {
            case TEMP_TOO_HIGH:
                ws2812_set_preset_color(WS2812_COLOR_RED);
                ESP_LOGW(TAG, "🔥 高温限界です！");
                break;
            case TEMP_TOO_LOW:
                ws2812_set_preset_color(WS2812_COLOR_BLUE);
                ESP_LOGW(TAG, "🧊 低温限界です！");
                break;
            case NEEDS_WATERING:
                ws2812_set_preset_color(WS2812_COLOR_YELLOW);
                ESP_LOGW(TAG, "💧 灌水が必要です！");
                break;
            case SOIL_DRY:
                ws2812_set_preset_color(WS2812_COLOR_ORANGE);
                break;
            case SOIL_WET:
                ws2812_set_preset_color(WS2812_COLOR_GREEN);
                break;
            case WATERING_COMPLETED:
                ws2812_set_preset_color(WS2812_COLOR_WHITE);
                break;
            case ERROR_CONDITION:
                ws2812_set_preset_color(WS2812_COLOR_PURPLE); // エラー時は紫色
                ESP_LOGE(TAG, "❌ エラー状態です！");
                break;
            default:
                ws2812_set_preset_color(WS2812_COLOR_OFF);
                break;
        }
#endif

        vTaskDelay(pdMS_TO_TICKS(60000)); // 1分待機
    }
}

/**
 * 植物プロファイル情報をログ出力
 */
static void log_plant_profile(void) {
    const plant_profile_t *profile = plant_manager_get_profile();
    if (profile == NULL) return;

    ESP_LOGI(TAG, "=== 植物プロファイル情報 ===");
    ESP_LOGI(TAG, "植物名: %s", profile->plant_name);
    ESP_LOGI(TAG, "土壌: 乾燥>=%.0fmV, 湿潤<=%.0fmV, 灌水要求%d日",
             profile->soil_dry_threshold,
             profile->soil_wet_threshold,
             profile->soil_dry_days_for_watering);
    ESP_LOGI(TAG, "気温限界: 高温>=%.1f℃, 低温<=%.1f℃",
             profile->temp_high_limit,
             profile->temp_low_limit);
}

// システム初期化
static esp_err_t system_init(void) {
    esp_err_t ret;
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    switch_input_init();
    init_adc();
    init_i2c();
    init_gpio();
    led_control_init();

    // 起動時LED動作チェック
    ESP_LOGI(TAG, "🔆 起動時LED動作チェック実行");
    led_control_startup_test();

#if TEMPARETURE_SENSOR_TYPE == TEMPARETURE_SENSOR_TYPE_SHT30
    sht30_init();  // Rev1: SHT30センサー初期化
#elif TEMPARETURE_SENSOR_TYPE == TEMPARETURE_SENSOR_TYPE_SHT40
    sht40_init();  // Rev2: SHT40センサー初期化
#endif
    tsl2591_init();

    // FDC1004静電容量センサー初期化
    esp_err_t fdc_ret = fdc1004_init();
    if (fdc_ret != ESP_OK) {
        ESP_LOGW(TAG, "FDC1004初期化失敗、スキップします");
    }

    // TC74土壌温度センサー初期化 (Rev3)
    ESP_LOGI(TAG, "TC74土壌温度センサー初期化を試行中...");
    esp_err_t tc74_ret = tc74_init_with_address(TC74_ADDR_A0);  // TC74A0を使用
    if (tc74_ret == ESP_OK) {
        g_soil_temp_sensors.tc74_connected = true;
        ESP_LOGI(TAG, "✅ TC74センサーが接続されました (soil_temperature1に割り当て)");
    } else {
        g_soil_temp_sensors.tc74_connected = false;
        ESP_LOGW(TAG, "⚠️  TC74センサーが検出されませんでした");
    }

    // TMP102土壌温度センサー初期化 (Rev4: 最大4台自動検出)
    ESP_LOGI(TAG, "TMP102土壌温度センサー自動検出を開始...");
    esp_err_t tmp102_ret = tmp102_init_all();
    if (tmp102_ret == ESP_OK) {
        g_soil_temp_sensors.tmp102_count = tmp102_get_device_count();
        ESP_LOGI(TAG, "✅ TMP102センサー %d台検出", g_soil_temp_sensors.tmp102_count);
    } else {
        g_soil_temp_sensors.tmp102_count = 0;
        ESP_LOGW(TAG, "⚠️  TMP102センサーが検出されませんでした");
    }

    // DS18B20温度センサー初期化 (Rev3: 土壌温度, Rev4: 拡張温度)
    ESP_LOGI(TAG, "DS18B20温度センサー初期化を試行中...");
    esp_err_t ds_ret = ds18b20_init();
    if (ds_ret == ESP_OK) {
        g_soil_temp_sensors.ds18b20_connected = true;
#if HARDWARE_VERSION == 40
        ESP_LOGI(TAG, "✅ DS18B20センサーが接続されました (ext_temperatureに割り当て)");
#else
        if (g_soil_temp_sensors.tc74_connected) {
            ESP_LOGI(TAG, "✅ DS18B20センサーが接続されました (soil_temperature2に割り当て)");
        } else {
            ESP_LOGI(TAG, "✅ DS18B20センサーが接続されました (soil_temperature1に割り当て)");
        }
#endif
    } else {
        g_soil_temp_sensors.ds18b20_connected = false;
        ESP_LOGW(TAG, "⚠️  DS18B20センサーが検出されませんでした");
    }

    // センサー接続状態のサマリー表示
    ESP_LOGI(TAG, "=== 土壌温度センサー接続状態 ===");
    ESP_LOGI(TAG, "  TC74:     %s", g_soil_temp_sensors.tc74_connected ? "接続済み" : "未接続");
    ESP_LOGI(TAG, "  TMP102:   %d台接続", g_soil_temp_sensors.tmp102_count);
    ESP_LOGI(TAG, "  DS18B20:  %s", g_soil_temp_sensors.ds18b20_connected ? "接続済み" : "未接続");

    // --- 土壌センサー構成情報の初期化 ---
    memset(&g_sensor_config, 0, sizeof(g_sensor_config));
    g_sensor_config.hardware_version = HARDWARE_VERSION;
    g_sensor_config.data_structure_version = DATA_STRUCTURE_VERSION;

    // 土壌湿度センサー情報
    g_sensor_config.moisture_sensor.sensor_type = MOISTURE_SENSOR_TYPE;
#if MOISTURE_SENSOR_TYPE == MOISTURE_SENSOR_TYPE_FDC1004
    g_sensor_config.moisture_sensor.channel_count = FDC1004_CHANNEL_COUNT;
    g_sensor_config.moisture_sensor.capacitance_min_pf = 0.1f;
    g_sensor_config.moisture_sensor.capacitance_max_pf = 16.0f;
    g_sensor_config.moisture_sensor.measurement_range_min = 0.1f;
    g_sensor_config.moisture_sensor.measurement_range_max = 16.0f;
#else
    g_sensor_config.moisture_sensor.channel_count = 1;
    g_sensor_config.moisture_sensor.measurement_range_min = 0.0f;
    g_sensor_config.moisture_sensor.measurement_range_max = 3300.0f;  // ADC mV
#endif

    // 土壌温度センサー情報
    uint8_t soil_temp_idx = 0;
#if HARDWARE_VERSION == 40
    // Rev4: TMP102 x N
    for (uint8_t i = 0; i < g_soil_temp_sensors.tmp102_count && soil_temp_idx < MAX_SOIL_TEMP_SENSORS; i++) {
        g_sensor_config.soil_temp_sensors[soil_temp_idx].device_type = SOIL_TEMPERATURE_SENSOR_TMP102;
        g_sensor_config.soil_temp_sensors[soil_temp_idx].depth_mm = 0;  // 未設定（アプリ側で設定可能）
        g_sensor_config.soil_temp_sensors[soil_temp_idx].temp_min = TMP102_TEMP_MIN;
        g_sensor_config.soil_temp_sensors[soil_temp_idx].temp_max = TMP102_TEMP_MAX;
        g_sensor_config.soil_temp_sensors[soil_temp_idx].temp_resolution = TMP102_TEMP_RESOLUTION;
        soil_temp_idx++;
    }
#else
    // Rev3以前: TC74
    if (g_soil_temp_sensors.tc74_connected && soil_temp_idx < MAX_SOIL_TEMP_SENSORS) {
        g_sensor_config.soil_temp_sensors[soil_temp_idx].device_type = SOIL_TEMPERATURE_SENSOR_TC74;
        g_sensor_config.soil_temp_sensors[soil_temp_idx].depth_mm = 0;
        g_sensor_config.soil_temp_sensors[soil_temp_idx].temp_min = -65.0f;
        g_sensor_config.soil_temp_sensors[soil_temp_idx].temp_max = 150.0f;
        g_sensor_config.soil_temp_sensors[soil_temp_idx].temp_resolution = 1.0f;
        soil_temp_idx++;
    }
    // Rev3以前: DS18B20 (土壌温度用)
    if (g_soil_temp_sensors.ds18b20_connected && soil_temp_idx < MAX_SOIL_TEMP_SENSORS) {
        g_sensor_config.soil_temp_sensors[soil_temp_idx].device_type = SOIL_TEMPERATURE_SENSOR_DS18B20;
        g_sensor_config.soil_temp_sensors[soil_temp_idx].depth_mm = 0;
        g_sensor_config.soil_temp_sensors[soil_temp_idx].temp_min = -55.0f;
        g_sensor_config.soil_temp_sensors[soil_temp_idx].temp_max = 125.0f;
        g_sensor_config.soil_temp_sensors[soil_temp_idx].temp_resolution = 0.0625f;
        soil_temp_idx++;
    }
#endif
    g_sensor_config.soil_temp_sensor_count = soil_temp_idx;

    // 拡張温度センサー情報
#if HARDWARE_VERSION == 40
    g_sensor_config.ext_temp_sensor.available = g_soil_temp_sensors.ds18b20_connected ? 1 : 0;
    g_sensor_config.ext_temp_sensor.device_type = g_soil_temp_sensors.ds18b20_connected ? SOIL_TEMPERATURE_SENSOR_DS18B20 : SOIL_TEMPERATURE_SENSOR_NONE;
    g_sensor_config.ext_temp_sensor.temp_min = -55.0f;
    g_sensor_config.ext_temp_sensor.temp_max = 125.0f;
    g_sensor_config.ext_temp_sensor.temp_resolution = 0.0625f;
#else
    g_sensor_config.ext_temp_sensor.available = 0;
    g_sensor_config.ext_temp_sensor.device_type = SOIL_TEMPERATURE_SENSOR_NONE;
#endif

    ESP_LOGI(TAG, "=== 土壌センサー構成情報 ===");
    ESP_LOGI(TAG, "  HW Version: %d, Data Version: %d", g_sensor_config.hardware_version, g_sensor_config.data_structure_version);
    ESP_LOGI(TAG, "  湿度センサー: type=%d, ch=%d", g_sensor_config.moisture_sensor.sensor_type, g_sensor_config.moisture_sensor.channel_count);
    ESP_LOGI(TAG, "  土壌温度センサー: %d台", g_sensor_config.soil_temp_sensor_count);
    for (uint8_t i = 0; i < g_sensor_config.soil_temp_sensor_count; i++) {
        ESP_LOGI(TAG, "    [%d] type=%d, depth=%dmm, range=%.1f~%.1f°C, res=%.4f°C",
                 i, g_sensor_config.soil_temp_sensors[i].device_type,
                 g_sensor_config.soil_temp_sensors[i].depth_mm,
                 g_sensor_config.soil_temp_sensors[i].temp_min,
                 g_sensor_config.soil_temp_sensors[i].temp_max,
                 g_sensor_config.soil_temp_sensors[i].temp_resolution);
    }
    ESP_LOGI(TAG, "  拡張温度センサー: %s (type=%d)",
             g_sensor_config.ext_temp_sensor.available ? "有" : "無",
             g_sensor_config.ext_temp_sensor.device_type);

    ESP_ERROR_CHECK(plant_manager_init());
    log_plant_profile();

    // WiFiと時刻同期の初期化は後で行う（BLEの後）

    data_buffer_init();
    return ESP_OK;
}

/* --- Main Application Entry --- */
void app_main(void) {
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGI(TAG, "Starting Soil Monitor Application...");
    ESP_ERROR_CHECK(system_init());

    // BLE初期化を最優先で実行（WiFiと電源管理より前）
    esp_err_t ble_ret = ble_manager_init();
    if (ble_ret == ESP_OK) {
        nimble_port_freertos_init(ble_host_task);
        ESP_LOGI(TAG, "✅ BLE initialized and host task started successfully");
    } else {
        ESP_LOGW(TAG, "⚠️  BLE initialization failed, continuing without BLE functionality");
    }

    // BLE Modem-sleepが有効な場合、自動Light-sleepを併用可能
    // Modem-sleepにより、BLEアドバタイジングを維持しながら省電力化
#ifdef CONFIG_PM_ENABLE
    esp_pm_config_t pm_config = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = 10,
        .light_sleep_enable = true  // Modem-sleepと併用で自動Light-sleep有効
    };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));
    ESP_LOGI(TAG, "✅ Power management configured (auto light-sleep with BLE modem-sleep)");
#endif

#if CONFIG_WIFI_ENABLED
    ESP_LOGI(TAG, "WiFi機能を初期化中（BLE経由で設定可能）");
    // WiFi管理システムの初期化のみ（自動接続はしない）
    ESP_ERROR_CHECK(wifi_manager_init(wifi_status_callback));
    ESP_ERROR_CHECK(time_sync_manager_init(time_sync_callback));
    // 注意: wifi_manager_start()はBLE経由で呼び出されます（CMD_WIFI_CONNECT）
#else
    ESP_LOGI(TAG, "ℹ️  WiFi機能は無効化されています (CONFIG_WIFI_ENABLED=0)");
#endif

    xTaskCreate(sensor_read_task, "sensor_read", 4096, NULL, 5, &g_sensor_task_handle);
    xTaskCreate(status_analysis_task, "analysis_task", 8192, NULL, 4, &g_analysis_task_handle);

    g_notify_timer = xTimerCreate("notify_timer", pdMS_TO_TICKS(SENSOR_READ_INTERVAL_MS), pdTRUE, NULL, notify_timer_callback);
    xTimerStart(g_notify_timer, 0);

    // 起動直後に初回センサ読み取りを実行
    xTaskNotifyGive(g_sensor_task_handle);

    ESP_LOGI(TAG, "Initialization complete.");
}