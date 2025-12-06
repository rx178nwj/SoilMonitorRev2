// main/components/plant_logic/plant_manager.c

#include "plant_manager.h"
#include "../../nvs_config.h"
#include "data_buffer.h"
#include "esp_log.h"
#include "esp_random.h"
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include "../../common_types.h"

static const char *TAG = "PlantManager";

// プライベート変数
static plant_profile_t g_plant_profile;
static bool g_initialized = false;
static plant_condition_t g_last_plant_condition = SOIL_WET; // 初期状態は湿潤と仮定

// プライベート関数の宣言
static plant_condition_t determine_plant_condition(const plant_profile_t *profile, const minute_data_t *latest_data);
static bool detect_watering_event(float current_moisture, float threshold_mv);

/**
 * 植物管理システムを初期化
 */
esp_err_t plant_manager_init(void) {
    esp_err_t ret;

    ESP_LOGI(TAG, "Initializing plant management system");

    // データバッファシステムを初期化
    ret = data_buffer_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize data buffer");
        return ret;
    }

    // 植物プロファイルを読み込み
    ret = nvs_config_load_plant_profile(&g_plant_profile);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load plant profile");
        return ret;
    }

    g_initialized = true;
    ESP_LOGI(TAG, "Plant management system initialized successfully");
    ESP_LOGI(TAG, "Plant: %s", g_plant_profile.plant_name);

    return ESP_OK;
}

/**
 * センサーデータを処理（データバッファに保存）
 */
void plant_manager_process_sensor_data(const soil_data_t *sensor_data) {
    if (!g_initialized) {
        ESP_LOGE(TAG, "Plant manager not initialized");
        return;
    }

    if (sensor_data == NULL) {
        ESP_LOGE(TAG, "Sensor data is NULL");
        return;
    }

    // データバッファにセンサーデータを追加
    esp_err_t ret = data_buffer_add_minute_data(sensor_data);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add sensor data to buffer: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Sensor data added to buffer successfully. Soil Moisture: %.0fmV", sensor_data->soil_moisture);
    }
}

/**
 * 植物の状態を総合的に判断（データバッファの過去データを使用）
 */
plant_status_result_t plant_manager_determine_status(const minute_data_t *latest_data) {
    plant_status_result_t result = {0};

    if (!g_initialized) {
        ESP_LOGE(TAG, "Plant manager not initialized");
        result.plant_condition = ERROR_CONDITION;
        return result;
    }

    if (latest_data == NULL || !latest_data->valid) {
        ESP_LOGW(TAG, "Invalid or NULL data passed to determine_status");
        result.plant_condition = ERROR_CONDITION;
        return result;
    }

    result.plant_condition = determine_plant_condition(&g_plant_profile, latest_data);
    g_last_plant_condition = result.plant_condition;

    return result;
}

/**
 * 植物状態の文字列表現を取得
 */
const char* plant_manager_get_plant_condition_string(plant_condition_t condition) {
    switch (condition) {
        case SOIL_DRY:              return "乾燥";
        case SOIL_WET:              return "湿潤";
        case NEEDS_WATERING:        return "灌水要求";
        case WATERING_COMPLETED:    return "灌水完了";
        case TEMP_TOO_HIGH:         return "高温限界";
        case TEMP_TOO_LOW:          return "低温限界";
        case ERROR_CONDITION:       return "エラー";
        default:                    return "不明";
    }
}

/**
 * 現在の植物プロファイルを取得
 */
const plant_profile_t* plant_manager_get_profile(void) {
    if (!g_initialized) {
        ESP_LOGE(TAG, "Plant manager not initialized");
        return NULL;
    }
    return &g_plant_profile;
}

/**
 * 現在実行中の植物プロファイルを更新
 */
void plant_manager_update_profile(const plant_profile_t *new_profile) {
    if (!g_initialized || new_profile == NULL) {
        ESP_LOGE(TAG, "Cannot update profile: Not initialized or new profile is NULL");
        return;
    }
    memcpy(&g_plant_profile, new_profile, sizeof(plant_profile_t));
    ESP_LOGI(TAG, "Plant profile updated in memory: %s", g_plant_profile.plant_name);
}

/**
 * システム全体の状態情報をログ出力
 */
void plant_manager_print_system_status(void) {
    if (!g_initialized) {
        ESP_LOGE(TAG, "Plant manager not initialized");
        return;
    }

    ESP_LOGI(TAG, "=== Plant Management System Status ===");
    ESP_LOGI(TAG, "Plant: %s", g_plant_profile.plant_name);

    // データバッファの状態を出力
    data_buffer_print_status();

    // 最新のセンサーデータを表示
    minute_data_t latest_data;
    if (data_buffer_get_latest_minute_data(&latest_data) == ESP_OK) {
        ESP_LOGI(TAG, "Latest sensor data: temp=%.1f C, soil=%.0fmV", latest_data.temperature, latest_data.soil_moisture);
    }
}

// プライベート関数の実装

/**
 * 植物の状態を判断
 */
static plant_condition_t determine_plant_condition(const plant_profile_t *profile, const minute_data_t *latest_data) {
    // データは引数で渡されるため、ここでのデータ取得は不要
    float soil_moisture = latest_data->soil_moisture;
    float temperature = latest_data->temperature;

    // 最優先：気温の限界チェック
    if (temperature >= profile->temp_high_limit) {
        return TEMP_TOO_HIGH;
    }
    if (temperature <= profile->temp_low_limit) {
        return TEMP_TOO_LOW;
    }

    // 灌水完了判定（2つの条件のいずれかで判定）
    // 条件1: 2回前のサンプリングから設定値以上下がった場合
    if (detect_watering_event(soil_moisture, profile->watering_threshold_mv)) {
        ESP_LOGI(TAG, "💧 灌水イベント検出: 土壌水分が2回前から%.0fmV以上減少", profile->watering_threshold_mv);
        return WATERING_COMPLETED;
    }

    // 条件2: 従来の判定（乾燥状態から湿潤閾値以下になった場合）
    if ((g_last_plant_condition == SOIL_DRY || g_last_plant_condition == NEEDS_WATERING) && soil_moisture <= profile->soil_wet_threshold) {
        ESP_LOGI(TAG, "💧 灌水完了: 乾燥状態から湿潤閾値以下に");
        return WATERING_COMPLETED;
    }

    // 灌水要求判定
    daily_summary_data_t daily_summaries[7];
    uint8_t summary_count = 0;
    esp_err_t ret = data_buffer_get_recent_daily_summaries(profile->soil_dry_days_for_watering, daily_summaries, &summary_count);
    if (ret == ESP_OK && summary_count >= profile->soil_dry_days_for_watering) {
        int consecutive_dry_days = 0;
        for (int i = 0; i < summary_count; i++) {
            if (daily_summaries[i].avg_soil_moisture >= profile->soil_dry_threshold) {
                consecutive_dry_days++;
            }
        }
        if (consecutive_dry_days >= profile->soil_dry_days_for_watering) {
            ESP_LOGD(TAG, "Needs watering: consecutive_dry_days=%d >= %d",
                     consecutive_dry_days, profile->soil_dry_days_for_watering);
            return NEEDS_WATERING;
        }
    }

    // 乾燥判定
    if (soil_moisture >= profile->soil_dry_threshold) {
        ESP_LOGD(TAG, "Soil dry: %.0f >= %.0f", soil_moisture, profile->soil_dry_threshold);
        return SOIL_DRY;
    }

    // 湿潤判定
    if (soil_moisture <= profile->soil_wet_threshold) {
        ESP_LOGD(TAG, "Soil wet: %.0f <= %.0f", soil_moisture, profile->soil_wet_threshold);
        return SOIL_WET;
    }

    // 上記のいずれにも当てはまらない場合は、最後と同じ状態を維持
    return g_last_plant_condition;
}

/**
 * time_t比較関数（qsort用）
 */
static int compare_time_desc(const void *a, const void *b) {
    const minute_data_t *data_a = (const minute_data_t *)a;
    const minute_data_t *data_b = (const minute_data_t *)b;

    time_t time_a = mktime((struct tm*)&data_a->timestamp);
    time_t time_b = mktime((struct tm*)&data_b->timestamp);

    // 降順ソート（新しい順）
    if (time_a > time_b) return -1;
    if (time_a < time_b) return 1;
    return 0;
}

/**
 * 灌水イベントを検出
 * 2回前のサンプリングと比較して、土壌水分が指定閾値以上減少したか判定
 *
 * @param current_moisture 現在の土壌水分値 [mV]
 * @param threshold_mv 灌水検出閾値 [mV]
 * @return true: 灌水イベント検出, false: 検出せず
 */
static bool detect_watering_event(float current_moisture, float threshold_mv) {
    uint16_t count = 0;

    // 過去1時間分のデータを取得
    minute_data_t hour_data[60];
    esp_err_t ret = data_buffer_get_recent_minute_data(1, hour_data, &count);

    if (ret != ESP_OK || count < 3) {
        // データが3件未満の場合は判定できない
        ESP_LOGD(TAG, "灌水検出: データ不足 (count=%d)", count);
        return false;
    }

    // データを新しい順にソート
    qsort(hour_data, count, sizeof(minute_data_t), compare_time_desc);

    // 最新3件のデータを使用
    // インデックス0が最新、1が1回前、2が2回前
    // 注意: インデックス0は現在追加中のデータなので、実際は1が最新、3が2回前
    if (count < 3) {
        ESP_LOGD(TAG, "灌水検出: ソート後のデータ不足 (count=%d)", count);
        return false;
    }

    // インデックス2が2回前のデータ
    float moisture_2_samples_ago = hour_data[2].soil_moisture;

    // 土壌水分が2回前から200mV以上減少したか確認
    float moisture_decrease = moisture_2_samples_ago - current_moisture;

    ESP_LOGD(TAG, "灌水検出チェック: 2回前=%.0fmV, 現在=%.0fmV, 減少量=%.0fmV, 閾値=%.0fmV",
             moisture_2_samples_ago, current_moisture, moisture_decrease, threshold_mv);

    if (moisture_decrease >= threshold_mv) {
        ESP_LOGI(TAG, "✅ 灌水イベント検出: 土壌水分が %.0fmV 減少 (2回前: %.0fmV → 現在: %.0fmV, 閾値: %.0fmV)",
                 moisture_decrease, moisture_2_samples_ago, current_moisture, threshold_mv);
        return true;
    }

    return false;
}

