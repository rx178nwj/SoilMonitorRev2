#include "data_buffer.h"
#include "esp_log.h"
#include <string.h>
#include <math.h>
#include "../../common_types.h"

static const char *TAG = "DataBuffer";

// プライベート変数
static minute_data_t g_minute_buffer[DATA_BUFFER_MINUTES_PER_DAY];
static daily_summary_data_t g_daily_buffer[DATA_BUFFER_DAYS_PER_MONTH];
static uint16_t g_minute_write_index = 0;
static uint8_t g_daily_write_index = 0;
static bool g_initialized = false;

// プライベート関数の宣言
static esp_err_t calculate_daily_summary(const struct tm *date, daily_summary_data_t *summary);
static uint16_t get_minute_index_by_time(const struct tm *timestamp);
static uint8_t get_daily_index_by_date(const struct tm *date);
static bool is_same_day(const struct tm *tm1, const struct tm *tm2);
static bool is_same_minute(const struct tm *tm1, const struct tm *tm2);
static void copy_tm_date_only(struct tm *dest, const struct tm *src);
static void copy_tm_full(struct tm *dest, const struct tm *src);


/**
 * データバッファシステムを初期化
 */
esp_err_t data_buffer_init(void) {
    ESP_LOGI(TAG, "Initializing data buffer system");
    
    // 1分データバッファを初期化
    memset(g_minute_buffer, 0, sizeof(g_minute_buffer));
    for (int i = 0; i < DATA_BUFFER_MINUTES_PER_DAY; i++) {
        g_minute_buffer[i].valid = false;
    }
    
    // 日別データバッファを初期化
    memset(g_daily_buffer, 0, sizeof(g_daily_buffer));
    for (int i = 0; i < DATA_BUFFER_DAYS_PER_MONTH; i++) {
        g_daily_buffer[i].complete = false;
    }
    
    g_minute_write_index = 0;
    g_daily_write_index = 0;
    g_initialized = true;
    
    ESP_LOGI(TAG, "Data buffer system initialized successfully");
    ESP_LOGI(TAG, "Minute buffer size: %d entries", DATA_BUFFER_MINUTES_PER_DAY);
    ESP_LOGI(TAG, "Daily buffer size: %d entries", DATA_BUFFER_DAYS_PER_MONTH);
    
    return ESP_OK;
}

/**
 * 1分間隔のセンサーデータを追加
 */
esp_err_t data_buffer_add_minute_data(const soil_data_t *sensor_data) {
    if (!g_initialized) {
        ESP_LOGE(TAG, "Data buffer not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (sensor_data == NULL) {
        ESP_LOGE(TAG, "Sensor data is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 現在の書き込み位置にデータを格納
    minute_data_t *entry = &g_minute_buffer[g_minute_write_index];

    copy_tm_full(&entry->timestamp, &sensor_data->datetime);
    entry->temperature = sensor_data->temperature;
    entry->humidity = sensor_data->humidity;
    entry->lux = sensor_data->lux;
    entry->soil_moisture = sensor_data->soil_moisture;
    entry->soil_temperature1 = sensor_data->soil_temperature1;
    entry->soil_temperature2 = sensor_data->soil_temperature2;

#if HARDWARE_VERSION == 30
    // FDC1004静電容量データをコピー
    for (int i = 0; i < FDC1004_CHANNEL_COUNT; i++) {
        entry->soil_moisture_capacitance[i] = sensor_data->soil_moisture_capacitance[i];
    }
#endif

    entry->valid = true;

    ESP_LOGD(TAG, "Added minute data at index %d: temp=%.1f, humidity=%.1f, soil=%.0f, soil_temp1=%.1f, soil_temp2=%.1f",
             g_minute_write_index, entry->temperature, entry->humidity, entry->soil_moisture, entry->soil_temperature1, entry->soil_temperature2);

    // インデックスを更新（リングバッファ）
    g_minute_write_index = (g_minute_write_index + 1) % DATA_BUFFER_MINUTES_PER_DAY;
    
    // 日別サマリーを更新
    daily_summary_data_t summary;
    esp_err_t ret = calculate_daily_summary(&sensor_data->datetime, &summary);
    if (ret == ESP_OK) {
        // 日別バッファに格納
        uint8_t daily_index = get_daily_index_by_date(&sensor_data->datetime);
        if (daily_index < DATA_BUFFER_DAYS_PER_MONTH) {
            memcpy(&g_daily_buffer[daily_index], &summary, sizeof(daily_summary_data_t));
            ESP_LOGD(TAG, "Updated daily summary at index %d", daily_index);
        }
    }
    
    return ESP_OK;
}

/**
 * 指定された時刻の1分データを取得
 */
esp_err_t data_buffer_get_minute_data(const struct tm *timestamp, minute_data_t *data) {
    if (!g_initialized || timestamp == NULL || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 全バッファを検索
    for (int i = 0; i < DATA_BUFFER_MINUTES_PER_DAY; i++) {
        if (g_minute_buffer[i].valid && is_same_minute(timestamp, &g_minute_buffer[i].timestamp)) {
            memcpy(data, &g_minute_buffer[i], sizeof(minute_data_t));
            return ESP_OK;
        }
    }
    
    return ESP_ERR_NOT_FOUND;
}

/**
 * 指定された日付の日別サマリーデータを取得
 */
esp_err_t data_buffer_get_daily_summary(const struct tm *date, daily_summary_data_t *summary) {
    if (!g_initialized || date == NULL || summary == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 全バッファを検索
    for (int i = 0; i < DATA_BUFFER_DAYS_PER_MONTH; i++) {
        if (g_daily_buffer[i].complete && is_same_day(date, &g_daily_buffer[i].date)) {
            memcpy(summary, &g_daily_buffer[i], sizeof(daily_summary_data_t));
            return ESP_OK;
        }
    }
    
    return ESP_ERR_NOT_FOUND;
}

/**
 * 最新の1分データを取得
 */
esp_err_t data_buffer_get_latest_minute_data(minute_data_t *data) {
    if (!g_initialized || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 最新のデータは前のインデックス
    uint16_t latest_index = (g_minute_write_index == 0) ? 
                           (DATA_BUFFER_MINUTES_PER_DAY - 1) : (g_minute_write_index - 1);
    
    if (g_minute_buffer[latest_index].valid) {
        memcpy(data, &g_minute_buffer[latest_index], sizeof(minute_data_t));
        return ESP_OK;
    }
    
    return ESP_ERR_NOT_FOUND;
}

/**
 * 最新の日別サマリーデータを取得
 */
esp_err_t data_buffer_get_latest_daily_summary(daily_summary_data_t *summary) {
    if (!g_initialized || summary == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 最新の完全な日別データを検索
    time_t latest_time = 0;
    int latest_index = -1;
    
    for (int i = 0; i < DATA_BUFFER_DAYS_PER_MONTH; i++) {
        if (g_daily_buffer[i].complete) {
            time_t current_time = mktime((struct tm*)&g_daily_buffer[i].date);
            if (current_time > latest_time) {
                latest_time = current_time;
                latest_index = i;
            }
        }
    }
    
    if (latest_index >= 0) {
        memcpy(summary, &g_daily_buffer[latest_index], sizeof(daily_summary_data_t));
        return ESP_OK;
    }
    
    return ESP_ERR_NOT_FOUND;
}

/**
 * 過去N日間の日別サマリーデータを取得
 */
esp_err_t data_buffer_get_recent_daily_summaries(uint8_t days, 
                                                daily_summary_data_t *summaries, 
                                                uint8_t *count) {
    if (!g_initialized || summaries == NULL || count == NULL || days == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (days > DATA_BUFFER_DAYS_PER_MONTH) {
        days = DATA_BUFFER_DAYS_PER_MONTH;
    }
    
    // 有効な日別データを時系列順に収集
    daily_summary_data_t temp_buffer[DATA_BUFFER_DAYS_PER_MONTH];
    uint8_t valid_count = 0;
    
    for (int i = 0; i < DATA_BUFFER_DAYS_PER_MONTH; i++) {
        if (g_daily_buffer[i].complete) {
            memcpy(&temp_buffer[valid_count], &g_daily_buffer[i], sizeof(daily_summary_data_t));
            valid_count++;
        }
    }
    
    // 日付でソート（バブルソート - データ量が少ないため）
    for (int i = 0; i < valid_count - 1; i++) {
        for (int j = 0; j < valid_count - i - 1; j++) {
            if (data_buffer_compare_date(&temp_buffer[j].date, &temp_buffer[j+1].date) > 0) {
                daily_summary_data_t temp;
                memcpy(&temp, &temp_buffer[j], sizeof(daily_summary_data_t));
                memcpy(&temp_buffer[j], &temp_buffer[j+1], sizeof(daily_summary_data_t));
                memcpy(&temp_buffer[j+1], &temp, sizeof(daily_summary_data_t));
            }
        }
    }
    
    // 最新のN日分を取得
    uint8_t start_index = (valid_count > days) ? (valid_count - days) : 0;
    uint8_t result_count = valid_count - start_index;
    
    for (uint8_t i = 0; i < result_count; i++) {
        memcpy(&summaries[i], &temp_buffer[start_index + i], sizeof(daily_summary_data_t));
    }
    
    *count = result_count;
    ESP_LOGD(TAG, "Retrieved %d daily summaries out of %d requested", result_count, days);
    
    return ESP_OK;
}

/**
 * 過去N時間の1分データを取得
 */
esp_err_t data_buffer_get_recent_minute_data(uint8_t hours, 
                                           minute_data_t *data, 
                                           uint16_t *count) {
    if (!g_initialized || data == NULL || count == NULL || hours == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (hours > 24) {
        hours = 24;
    }
    
    uint16_t max_entries = hours * 60;
    uint16_t result_count = 0;
    
    // 現在時刻から過去に向かって検索
    time_t now;
    time(&now);
    time_t cutoff_time = now - (hours * 3600);
    
    for (int i = 0; i < DATA_BUFFER_MINUTES_PER_DAY; i++) {
        if (g_minute_buffer[i].valid) {
            time_t data_time = mktime((struct tm*)&g_minute_buffer[i].timestamp);
            if (data_time >= cutoff_time && result_count < max_entries) {
                memcpy(&data[result_count], &g_minute_buffer[i], sizeof(minute_data_t));
                result_count++;
            }
        }
    }
    
    *count = result_count;
    ESP_LOGD(TAG, "Retrieved %d minute data entries for past %d hours", result_count, hours);
    
    return ESP_OK;
}

/**
 * データバッファの統計情報を取得
 */
esp_err_t data_buffer_get_stats(data_buffer_stats_t *stats) {
    if (!g_initialized || stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(stats, 0, sizeof(data_buffer_stats_t));
    
    time_t oldest_minute = 0, newest_minute = 0;
    time_t oldest_daily = 0, newest_daily = 0;
    
    // 1分データの統計
    for (int i = 0; i < DATA_BUFFER_MINUTES_PER_DAY; i++) {
        if (g_minute_buffer[i].valid) {
            stats->minute_data_count++;
            time_t data_time = mktime((struct tm*)&g_minute_buffer[i].timestamp);
            
            if (oldest_minute == 0 || data_time < oldest_minute) {
                oldest_minute = data_time;
                copy_tm_full(&stats->oldest_minute_data, &g_minute_buffer[i].timestamp);
            }
            if (newest_minute == 0 || data_time > newest_minute) {
                newest_minute = data_time;
                copy_tm_full(&stats->newest_minute_data, &g_minute_buffer[i].timestamp);
            }
        }
    }
    
    // 日別データの統計
    for (int i = 0; i < DATA_BUFFER_DAYS_PER_MONTH; i++) {
        if (g_daily_buffer[i].complete) {
            stats->daily_data_count++;
            time_t data_time = mktime((struct tm*)&g_daily_buffer[i].date);
            
            if (oldest_daily == 0 || data_time < oldest_daily) {
                oldest_daily = data_time;
                copy_tm_date_only(&stats->oldest_daily_data, &g_daily_buffer[i].date);
            }
            if (newest_daily == 0 || data_time > newest_daily) {
                newest_daily = data_time;
                copy_tm_date_only(&stats->newest_daily_data, &g_daily_buffer[i].date);
            }
        }
    }
    
    return ESP_OK;
}

/**
 * 現在のバッファ使用状況をログ出力
 */
void data_buffer_print_status(void) {
    if (!g_initialized) {
        ESP_LOGE(TAG, "Data buffer not initialized");
        return;
    }
    
    data_buffer_stats_t stats;
    if (data_buffer_get_stats(&stats) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get buffer stats");
        return;
    }
    
    ESP_LOGI(TAG, "=== Data Buffer Status ===");
    ESP_LOGI(TAG, "Minute data: %d/%d entries", stats.minute_data_count, DATA_BUFFER_MINUTES_PER_DAY);
    ESP_LOGI(TAG, "Daily data: %d/%d entries", stats.daily_data_count, DATA_BUFFER_DAYS_PER_MONTH);
    
    if (stats.minute_data_count > 0) {
        ESP_LOGI(TAG, "Minute data range: %04d-%02d-%02d %02d:%02d to %04d-%02d-%02d %02d:%02d",
                 stats.oldest_minute_data.tm_year + 1900, stats.oldest_minute_data.tm_mon + 1, stats.oldest_minute_data.tm_mday,
                 stats.oldest_minute_data.tm_hour, stats.oldest_minute_data.tm_min,
                 stats.newest_minute_data.tm_year + 1900, stats.newest_minute_data.tm_mon + 1, stats.newest_minute_data.tm_mday,
                 stats.newest_minute_data.tm_hour, stats.newest_minute_data.tm_min);
    }
    
    if (stats.daily_data_count > 0) {
        ESP_LOGI(TAG, "Daily data range: %04d-%02d-%02d to %04d-%02d-%02d",
                 stats.oldest_daily_data.tm_year + 1900, stats.oldest_daily_data.tm_mon + 1, stats.oldest_daily_data.tm_mday,
                 stats.newest_daily_data.tm_year + 1900, stats.newest_daily_data.tm_mon + 1, stats.newest_daily_data.tm_mday);
    }
}

// プライベート関数の実装

/**
 * 指定された日の日別サマリーを計算
 */
static esp_err_t calculate_daily_summary(const struct tm *date, daily_summary_data_t *summary) {
    if (date == NULL || summary == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(summary, 0, sizeof(daily_summary_data_t));
    copy_tm_date_only(&summary->date, date);

    float temp_sum = 0, humidity_sum = 0, lux_sum = 0, soil_sum = 0, soil_temp_sum = 0;
    float min_temp = 999, max_temp = -999;
    float min_soil = 999999, max_soil = -999999;
    float min_soil_temp = 999, max_soil_temp = -999;
    uint16_t count = 0;

    // 指定された日の1分データを集計
    for (int i = 0; i < DATA_BUFFER_MINUTES_PER_DAY; i++) {
        if (g_minute_buffer[i].valid && is_same_day(date, &g_minute_buffer[i].timestamp)) {
            count++;

            // 温度
            temp_sum += g_minute_buffer[i].temperature;
            if (g_minute_buffer[i].temperature < min_temp) min_temp = g_minute_buffer[i].temperature;
            if (g_minute_buffer[i].temperature > max_temp) max_temp = g_minute_buffer[i].temperature;

            // その他
            humidity_sum += g_minute_buffer[i].humidity;
            lux_sum += g_minute_buffer[i].lux;
            soil_sum += g_minute_buffer[i].soil_moisture;

            // 土壌水分
            if (g_minute_buffer[i].soil_moisture < min_soil) min_soil = g_minute_buffer[i].soil_moisture;
            if (g_minute_buffer[i].soil_moisture > max_soil) max_soil = g_minute_buffer[i].soil_moisture;

            // 土壌温度（soil_temperature1を使用）
            soil_temp_sum += g_minute_buffer[i].soil_temperature1;
            if (g_minute_buffer[i].soil_temperature1 < min_soil_temp) min_soil_temp = g_minute_buffer[i].soil_temperature1;
            if (g_minute_buffer[i].soil_temperature1 > max_soil_temp) max_soil_temp = g_minute_buffer[i].soil_temperature1;
        }
    }

    if (count > 0) {
        summary->avg_temperature = temp_sum / count;
        summary->min_temperature = min_temp;
        summary->max_temperature = max_temp;
        summary->avg_humidity = humidity_sum / count;
        summary->avg_lux = lux_sum / count;
        summary->avg_soil_moisture = soil_sum / count;
        summary->min_soil_moisture = min_soil;
        summary->max_soil_moisture = max_soil;
        summary->avg_soil_temperature = soil_temp_sum / count;
        summary->min_soil_temperature = min_soil_temp;
        summary->max_soil_temperature = max_soil_temp;
        summary->valid_samples = count;
        summary->complete = (count >= 1200); // 20時間以上のデータがあれば完全とみなす

        ESP_LOGD(TAG, "Daily summary calculated: samples=%d, avg_temp=%.1f, avg_soil=%.0f, avg_soil_temp=%.1f",
                 count, summary->avg_temperature, summary->avg_soil_moisture, summary->avg_soil_temperature);

        return ESP_OK;
    }
    
    return ESP_ERR_NOT_FOUND;
}

/**
 * 時刻比較ユーティリティ関数
 */
int data_buffer_compare_time(const struct tm *tm1, const struct tm *tm2) {
    if (tm1 == NULL || tm2 == NULL) return 0;
    
    time_t time1 = mktime((struct tm*)tm1);
    time_t time2 = mktime((struct tm*)tm2);
    
    if (time1 < time2) return -1;
    if (time1 > time2) return 1;
    return 0;
}

/**
 * 日付比較ユーティリティ関数
 */
int data_buffer_compare_date(const struct tm *tm1, const struct tm *tm2) {
    if (tm1 == NULL || tm2 == NULL) return 0;
    
    if (tm1->tm_year != tm2->tm_year) return tm1->tm_year - tm2->tm_year;
    if (tm1->tm_mon != tm2->tm_mon) return tm1->tm_mon - tm2->tm_mon;
    return tm1->tm_mday - tm2->tm_mday;
}

// その他のプライベート関数

static bool is_same_day(const struct tm *tm1, const struct tm *tm2) {
    return (tm1->tm_year == tm2->tm_year && 
            tm1->tm_mon == tm2->tm_mon && 
            tm1->tm_mday == tm2->tm_mday);
}

static bool is_same_minute(const struct tm *tm1, const struct tm *tm2) {
    return (tm1->tm_year == tm2->tm_year && 
            tm1->tm_mon == tm2->tm_mon && 
            tm1->tm_mday == tm2->tm_mday &&
            tm1->tm_hour == tm2->tm_hour &&
            tm1->tm_min == tm2->tm_min);
}

static void copy_tm_date_only(struct tm *dest, const struct tm *src) {
    dest->tm_year = src->tm_year;
    dest->tm_mon = src->tm_mon;
    dest->tm_mday = src->tm_mday;
    dest->tm_hour = 0;
    dest->tm_min = 0;
    dest->tm_sec = 0;
    dest->tm_wday = src->tm_wday;
    dest->tm_yday = src->tm_yday;
    dest->tm_isdst = src->tm_isdst;
}

static void copy_tm_full(struct tm *dest, const struct tm *src) {
    memcpy(dest, src, sizeof(struct tm));
}

static uint16_t get_minute_index_by_time(const struct tm *timestamp) {
    return (timestamp->tm_hour * 60 + timestamp->tm_min) % DATA_BUFFER_MINUTES_PER_DAY;
}

static uint8_t get_daily_index_by_date(const struct tm *date) {
    // 簡易的な日付ハッシュ（月日を基準）
    return ((date->tm_mon * 31) + date->tm_mday) % DATA_BUFFER_DAYS_PER_MONTH;
}

/**
 * 指定された日の1分データを取得
 */
esp_err_t data_buffer_get_day_minute_data(const struct tm *date, 
                                        minute_data_t *data, 
                                        uint16_t *count) {
    if (!g_initialized || date == NULL || data == NULL || count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint16_t result_count = 0;
    
    // 指定された日の1分データを収集
    for (int i = 0; i < DATA_BUFFER_MINUTES_PER_DAY; i++) {
        if (g_minute_buffer[i].valid && is_same_day(date, &g_minute_buffer[i].timestamp)) {
            if (result_count < DATA_BUFFER_MINUTES_PER_DAY) {
                memcpy(&data[result_count], &g_minute_buffer[i], sizeof(minute_data_t));
                result_count++;
            }
        }
    }
    
    *count = result_count;
    ESP_LOGD(TAG, "Retrieved %d minute data entries for specified date", result_count);
    
    return ESP_OK;
}

/**
 * 古いデータを削除してメモリを整理
 */
esp_err_t data_buffer_cleanup_old_data(void) {
    if (!g_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    time_t now;
    time(&now);
    time_t cutoff_minute = now - (24 * 3600); // 24時間前
    time_t cutoff_daily = now - (30 * 24 * 3600); // 30日前
    
    uint16_t cleaned_minute = 0;
    uint8_t cleaned_daily = 0;
    
    // 古い1分データを削除
    for (int i = 0; i < DATA_BUFFER_MINUTES_PER_DAY; i++) {
        if (g_minute_buffer[i].valid) {
            time_t data_time = mktime((struct tm*)&g_minute_buffer[i].timestamp);
            if (data_time < cutoff_minute) {
                g_minute_buffer[i].valid = false;
                cleaned_minute++;
            }
        }
    }
    
    // 古い日別データを削除
    for (int i = 0; i < DATA_BUFFER_DAYS_PER_MONTH; i++) {
        if (g_daily_buffer[i].complete) {
            time_t data_time = mktime((struct tm*)&g_daily_buffer[i].date);
            if (data_time < cutoff_daily) {
                g_daily_buffer[i].complete = false;
                cleaned_daily++;
            }
        }
    }
    
    ESP_LOGI(TAG, "Cleanup completed: removed %d minute entries, %d daily entries", 
             cleaned_minute, cleaned_daily);
    
    return ESP_OK;
}

/**
 * データバッファをクリア
 */
esp_err_t data_buffer_clear_all(void) {
    if (!g_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // 1分データバッファをクリア
    for (int i = 0; i < DATA_BUFFER_MINUTES_PER_DAY; i++) {
        g_minute_buffer[i].valid = false;
    }
    
    // 日別データバッファをクリア
    for (int i = 0; i < DATA_BUFFER_DAYS_PER_MONTH; i++) {
        g_daily_buffer[i].complete = false;
    }
    
    g_minute_write_index = 0;
    g_daily_write_index = 0;
    
    ESP_LOGI(TAG, "All data buffers cleared");
    
    return ESP_OK;
}

/**
 * 日別サマリーを手動で再計算
 */
esp_err_t data_buffer_recalculate_daily_summary(const struct tm *date) {
    if (!g_initialized || date == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    daily_summary_data_t summary;
    esp_err_t ret = calculate_daily_summary(date, &summary);
    
    if (ret == ESP_OK) {
        // 該当する日別バッファエントリを更新
        uint8_t daily_index = get_daily_index_by_date(date);
        if (daily_index < DATA_BUFFER_DAYS_PER_MONTH) {
            memcpy(&g_daily_buffer[daily_index], &summary, sizeof(daily_summary_data_t));
            ESP_LOGI(TAG, "Daily summary recalculated for %04d-%02d-%02d", 
                     date->tm_year + 1900, date->tm_mon + 1, date->tm_mday);
        }
    }
    
    return ret;
}