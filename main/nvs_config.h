#pragma once

#include "esp_err.h"
#include "components/plant_logic/plant_manager.h"
#include "esp_wifi.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * NVS設定管理システムを初期化
 * @return ESP_OK on success
 */
esp_err_t nvs_config_init(void);

/**
 * 植物プロファイルをNVSに保存
 * @param profile 保存する植物プロファイル
 * @return ESP_OK on success
 */
esp_err_t nvs_config_save_plant_profile(const plant_profile_t *profile);

/**
 * 植物プロファイルをNVSから読み込み
 * @param profile 読み込み先の植物プロファイル
 * @return ESP_OK on success
 */
esp_err_t nvs_config_load_plant_profile(plant_profile_t *profile);

/**
 * デフォルトの植物プロファイル設定（多肉植物向け）
 * @param profile 設定先の植物プロファイル
 */
void nvs_config_set_default_plant_profile(plant_profile_t *profile);

/**
 * WiFi設定をNVSに保存
 * @param wifi_config 保存するWiFi設定
 * @return ESP_OK on success
 */
esp_err_t nvs_config_save_wifi_config(const wifi_config_t *wifi_config);

/**
 * WiFi設定をNVSから読み込み
 * @param wifi_config 読み込み先のWiFi設定
 * @return ESP_OK on success, ESP_ERR_NVS_NOT_FOUND if not found
 */
esp_err_t nvs_config_load_wifi_config(wifi_config_t *wifi_config);

#ifdef __cplusplus
}
#endif