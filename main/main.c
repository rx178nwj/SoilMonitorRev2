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

// 全センサーデータ読み取り
static void read_all_sensors(soil_data_t *data) {
    ESP_LOGI(TAG, "📊 Reading all sensors...");
    struct tm datetime;
    time_sync_manager_get_current_time(&datetime);
    data->datetime = datetime;
    data->sensor_error = false; // エラーフラグを初期化

    data->soil_moisture = (float)read_moisture_sensor();
    ESP_LOGI(TAG, "  - Soil Moisture: %.0f mV", data->soil_moisture);

#if HARDWARE_VERSION == 10
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
#else // HARDWARE_VERSION == 20
    // Rev2: SHT40センサーを使用
    sht40_data_t sht40;
    if (sht40_read_data(&sht40) == ESP_OK && !sht40.error) {
        data->temperature = sht40.temperature;
        data->humidity = sht40.humidity;
        ESP_LOGI(TAG, "  - SHT40: Temp=%.1f C, Hum=%.1f %%", data->temperature, data->humidity);
    } else {
        ESP_LOGE(TAG, "  - SHT40: Failed to read data");
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
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        vTaskNotifyGiveFromISR(g_sensor_task_handle, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
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
#if HARDWARE_VERSION == 10
    sht30_init();  // Rev1: SHT30センサー初期化
#else
    sht40_init();  // Rev2: SHT40センサー初期化
#endif
    tsl2591_init();

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

    ESP_LOGI(TAG, "WiFi機能を初期化中（BLE経由で設定可能）");
    // WiFi管理システムの初期化のみ（自動接続はしない）
    ESP_ERROR_CHECK(wifi_manager_init(wifi_status_callback));
    ESP_ERROR_CHECK(time_sync_manager_init(time_sync_callback));
    // 注意: wifi_manager_start()はBLE経由で呼び出されます（CMD_WIFI_CONNECT）

    xTaskCreate(sensor_read_task, "sensor_read", 4096, NULL, 5, &g_sensor_task_handle);
    xTaskCreate(status_analysis_task, "analysis_task", 6144, NULL, 4, &g_analysis_task_handle);

    g_notify_timer = xTimerCreate("notify_timer", pdMS_TO_TICKS(SENSOR_READ_INTERVAL_MS), pdTRUE, NULL, notify_timer_callback);
    xTimerStart(g_notify_timer, 0);

    ESP_LOGI(TAG, "Initialization complete.");
}