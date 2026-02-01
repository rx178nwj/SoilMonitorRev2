#pragma once

#include <time.h>
#include "esp_err.h"
#include "../../common_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration to break circular dependency
struct minute_data_t;

/**
 * 設定値管理構造体
 */
typedef struct {
    char plant_name[32];                    // 植物の名前
    float soil_dry_threshold;               // このmV以上で「乾燥」 (例: 2500.0mV)
    float soil_wet_threshold;               // このmV以下で「湿潤」 (例: 1000.0mV)
    int soil_dry_days_for_watering;         // この日数以上乾燥が続いたら水やりを指示 (例: 3日)
    float temp_high_limit;                  // 高温限界 (これを超えると警告)
    float temp_low_limit;                   // 低温限界 (これを下回ると警告)
    float watering_threshold;               // 灌水検出閾値 (2回前から何pF減少で灌水と判定) (例: 200.0mV)
} plant_profile_t;

/**
 * 植物の状態を示す列挙型
 */
typedef enum {
    SOIL_DRY,              // 乾燥
    SOIL_WET,              // 湿潤
    NEEDS_WATERING,        // 灌水要求
    WATERING_COMPLETED,    // 灌水完了
    TEMP_TOO_HIGH,         // 高温すぎる
    TEMP_TOO_LOW,          // 低温すぎる
    ERROR_CONDITION        // エラー
} plant_condition_t;

/**
 * 植物管理システムの結果構造体
 */
typedef struct {
    plant_condition_t plant_condition;
} plant_status_result_t;

// 公開関数

/**
 * 植物管理システムを初期化
 * @return ESP_OK on success
 */
esp_err_t plant_manager_init(void);

/**
 * センサーデータを処理（データバッファに保存し、判断ロジック用データも更新）
 * @param sensor_data センサーデータ
 */
void plant_manager_process_sensor_data(const soil_data_t *sensor_data);

/**
 * 植物の状態を総合的に判断（データバッファの過去データを使用）
 * @param latest_data 判断に使用する最新のセンサーデータ
 * @return 植物状態の判断結果
 */
plant_status_result_t plant_manager_determine_status(const struct minute_data_t *latest_data);


/**
 * 植物状態の文字列表現を取得
 * @param condition 植物の状態
 * @return 文字列表現
 */
const char* plant_manager_get_plant_condition_string(plant_condition_t condition);

/**
 * 現在の植物プロファイルを取得
 * @return 植物プロファイルへのポインタ
 */
const plant_profile_t* plant_manager_get_profile(void);

/**
 * 現在実行中の植物プロファイルを更新
 * @param new_profile 新しい植物プロファイル
 */
void plant_manager_update_profile(const plant_profile_t *new_profile);

/**
 * システム全体の状態情報をログ出力
 */
void plant_manager_print_system_status(void);

#ifdef __cplusplus
}
#endif