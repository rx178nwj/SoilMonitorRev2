#pragma once

#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "../../common_types.h" // 修正：plant_manager.hの代わりにcommon_types.hをインクルード

#ifdef __cplusplus
extern "C" {
#endif

// バッファサイズ定数
#define DATA_BUFFER_MINUTES_PER_DAY     (24 * 60)  // 1440分/日
#define DATA_BUFFER_DAYS_PER_MONTH      30         // 30日/月

/**
 * 1分間隔のセンサーデータ構造体
 */
// 修正： 構造体に 'minute_data_t' というタグ名を追加
typedef struct minute_data_t {
    struct tm timestamp;     // タイムスタンプ
    float temperature;       // 気温 (℃)
    float humidity;          // 湿度 (%)
    float lux;              // 照度 (lux)
    float soil_moisture;     // 土壌水分 (mV)
#if HARDWARE_VERSION == 40
    float soil_temperature[TMP102_MAX_DEVICES]; // 土壌温度 x4 (TMP102) [°C]
    uint8_t soil_temperature_count;             // 有効な土壌温度センサー数
#else
    float soil_temperature1;  // 土壌温度 (℃)
    float soil_temperature2;  // 土壌温度 (℃)
#endif
#if (HARDWARE_VERSION == 30 || HARDWARE_VERSION == 40)
    float soil_moisture_capacitance[FDC1004_CHANNEL_COUNT]; // 土壌湿度計測用静電容量 (pF)
#endif
#if HARDWARE_VERSION == 40
    float ext_temperature;          // 拡張温度センサー (DS18B20) [°C]
    bool ext_temperature_valid;     // 拡張温度データの有効性
#endif
    bool valid;             // データの有効性
} minute_data_t;

/**
 * 1日のサマリーデータ構造体
 */
// 修正： 構造体に 'daily_summary_data_t' というタグ名を追加
typedef struct daily_summary_data_t {
    struct tm date;                    // 日付
    float max_temperature;             // 最高気温
    float min_temperature;             // 最低気温
    float avg_temperature;             // 平均気温
    float avg_humidity;                // 平均湿度
    float avg_lux;                     // 平均照度
    float avg_soil_moisture;           // 平均土壌水分
    float max_soil_moisture;           // 最大土壌水分
    float min_soil_moisture;           // 最小土壌水分
    float max_soil_temperature;        // 最高土壌温度
    float min_soil_temperature;        // 最低土壌温度
    float avg_soil_temperature;        // 平均土壌温度
    uint16_t valid_samples;            // 有効サンプル数
    bool complete;                     // 1日分のデータが完全か
} daily_summary_data_t;

/**
 * データバッファの統計情報
 */
typedef struct {
    uint16_t minute_data_count;        // 1分データの有効件数
    uint16_t daily_data_count;         // 日別データの有効件数
    struct tm oldest_minute_data;      // 最古の1分データ日時
    struct tm newest_minute_data;      // 最新の1分データ日時
    struct tm oldest_daily_data;       // 最古の日別データ日付
    struct tm newest_daily_data;       // 最新の日別データ日付
} data_buffer_stats_t;

// 公開関数

/**
 * データバッファシステムを初期化
 * @return ESP_OK on success
 */
esp_err_t data_buffer_init(void);

/**
 * 1分間隔のセンサーデータを追加
 * @param sensor_data 追加するセンサーデータ
 * @return ESP_OK on success
 */
esp_err_t data_buffer_add_minute_data(const soil_data_t *sensor_data);

/**
 * 指定された時刻の1分データを取得
 * @param timestamp 取得したい時刻
 * @param data 取得したデータの格納先
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if not found
 */
esp_err_t data_buffer_get_minute_data(const struct tm *timestamp, minute_data_t *data);

/**
 * 指定された日付の日別サマリーデータを取得
 * @param date 取得したい日付
 * @param summary 取得したサマリーデータの格納先
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if not found
 */
esp_err_t data_buffer_get_daily_summary(const struct tm *date, daily_summary_data_t *summary);

/**
 * 最新の1分データを取得
 * @param data 取得したデータの格納先
 * @return ESP_OK on success
 */
esp_err_t data_buffer_get_latest_minute_data(minute_data_t *data);

/**
 * 最新の日別サマリーデータを取得
 * @param summary 取得したサマリーデータの格納先
 * @return ESP_OK on success
 */
esp_err_t data_buffer_get_latest_daily_summary(daily_summary_data_t *summary);

/**
 * 過去N日間の日別サマリーデータを取得
 * @param days 取得したい日数（最大30日）
 * @param summaries 取得したサマリーデータの配列（呼び出し側で確保）
 * @param count 実際に取得できた日数
 * @return ESP_OK on success
 */
esp_err_t data_buffer_get_recent_daily_summaries(uint8_t days, 
                                                daily_summary_data_t *summaries, 
                                                uint8_t *count);

/**
 * 過去N時間の1分データを取得
 * @param hours 取得したい時間数（最大24時間）
 * @param data 取得したデータの配列（呼び出し側で確保）
 * @param count 実際に取得できたデータ数
 * @return ESP_OK on success
 */
esp_err_t data_buffer_get_recent_minute_data(uint8_t hours, 
                                           minute_data_t *data, 
                                           uint16_t *count);

/**
 * 指定された日の1分データを取得
 * @param date 取得したい日付
 * @param data 取得したデータの配列（呼び出し側で1440要素確保）
 * @param count 実際に取得できたデータ数
 * @return ESP_OK on success
 */
esp_err_t data_buffer_get_day_minute_data(const struct tm *date, 
                                        minute_data_t *data, 
                                        uint16_t *count);

/**
 * データバッファの統計情報を取得
 * @param stats 統計情報の格納先
 * @return ESP_OK on success
 */
esp_err_t data_buffer_get_stats(data_buffer_stats_t *stats);

/**
 * 古いデータを削除してメモリを整理
 * @return ESP_OK on success
 */
esp_err_t data_buffer_cleanup_old_data(void);

/**
 * データバッファをクリア
 * @return ESP_OK on success
 */
esp_err_t data_buffer_clear_all(void);

/**
 * 日別サマリーを手動で再計算
 * @param date 再計算したい日付
 * @return ESP_OK on success
 */
esp_err_t data_buffer_recalculate_daily_summary(const struct tm *date);

/**
 * 現在のバッファ使用状況をログ出力
 */
void data_buffer_print_status(void);

/**
 * 時刻比較ユーティリティ関数
 * @param tm1 比較する時刻1
 * @param tm2 比較する時刻2
 * @return 0: 同じ, <0: tm1 < tm2, >0: tm1 > tm2
 */
int data_buffer_compare_time(const struct tm *tm1, const struct tm *tm2);

/**
 * 日付比較ユーティリティ関数（時刻は無視）
 * @param tm1 比較する日付1
 * @param tm2 比較する日付2
 * @return 0: 同じ日, <0: tm1 < tm2, >0: tm1 > tm2
 */
int data_buffer_compare_date(const struct tm *tm1, const struct tm *tm2);

#ifdef __cplusplus
}
#endif