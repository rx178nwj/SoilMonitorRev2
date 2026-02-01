#include "nvs_config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "NVS_Config";

// NVSキー定義
#define NVS_NAMESPACE "plant_config"
#define NVS_KEY_PROFILE "profile"
#define NVS_KEY_WIFI "wifi_config"
#define NVS_KEY_TIMEZONE "timezone"

/**
 * デフォルトの植物プロファイル設定（多肉植物向け）
 */
void nvs_config_set_default_plant_profile(plant_profile_t *profile) {
    if (profile == NULL) {
        ESP_LOGE(TAG, "Profile pointer is NULL");
        return;
    }

    // 植物名
    strcpy(profile->plant_name, "Succulent Plant");

    // 土壌水分の条件
    profile->soil_dry_threshold = MOISTURE_DRY_THRESHOLD;
    profile->soil_wet_threshold = MOISTURE_WET_THRESHOLD;
    profile->soil_dry_days_for_watering = DRY_WARNING_DAYS;

    // 気温の限界値
    profile->temp_high_limit = TEMP_HIGH_THRESHOLD;
    profile->temp_low_limit = TEMP_LOW_THRESHOLD;

    // 灌水検出閾値
    profile->watering_threshold = WATERING_DETECTION_THRESHOLD;

    ESP_LOGI(TAG, "Default plant profile set for: %s", profile->plant_name);
}

/**
 * 植物プロファイルをNVSに保存
 */
esp_err_t nvs_config_save_plant_profile(const plant_profile_t *profile) {
    if (profile == NULL) {
        ESP_LOGE(TAG, "Profile pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err;

    // NVSハンドルを開く
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    // プロファイルをblobとして保存
    err = nvs_set_blob(nvs_handle, NVS_KEY_PROFILE, profile, sizeof(plant_profile_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving plant profile: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    // 変更をコミット
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error committing NVS: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Plant profile saved successfully: %s", profile->plant_name);
    }

    nvs_close(nvs_handle);
    return err;
}

/**
 * 植物プロファイルをNVSから読み込み
 */
esp_err_t nvs_config_load_plant_profile(plant_profile_t *profile) {
    if (profile == NULL) {
        ESP_LOGE(TAG, "Profile pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err;
    size_t required_size = sizeof(plant_profile_t);

    // NVSハンドルを開く（読み取り専用）
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "NVS partition not found, creating with default profile");
        nvs_config_set_default_plant_profile(profile);
        esp_err_t save_err = nvs_config_save_plant_profile(profile);
        if (save_err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to save default profile, continuing with defaults");
        }
        return ESP_OK;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        ESP_LOGW(TAG, "Using default profile due to NVS error");
        nvs_config_set_default_plant_profile(profile);
        return ESP_OK;
    }

    // プロファイルをblobとして読み込み
    err = nvs_get_blob(nvs_handle, NVS_KEY_PROFILE, profile, &required_size);

    if (err == ESP_ERR_NVS_NOT_FOUND || required_size != sizeof(plant_profile_t)) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "Plant profile not found in NVS, using default values");
        } else {
            ESP_LOGE(TAG, "Profile size mismatch. Expected: %zu, Got: %zu. Using defaults.", sizeof(plant_profile_t), required_size);
        }
        nvs_close(nvs_handle);

        nvs_config_set_default_plant_profile(profile);
        esp_err_t save_err = nvs_config_save_plant_profile(profile);
        if (save_err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to save default profile to NVS: %s", esp_err_to_name(save_err));
        }
        return ESP_OK;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error reading plant profile: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        ESP_LOGW(TAG, "Using default profile due to read error");
        nvs_config_set_default_plant_profile(profile);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Plant profile loaded successfully: %s", profile->plant_name);
    ESP_LOGI(TAG, "Soil: Dry >= %.0fmV, Wet <= %.0fmV, Watering after %d dry days",
                profile->soil_dry_threshold,
                profile->soil_wet_threshold,
                profile->soil_dry_days_for_watering);
    ESP_LOGI(TAG, "Temp Limits: High >= %.1f C, Low <= %.1f C",
                profile->temp_high_limit,
                profile->temp_low_limit);
    ESP_LOGI(TAG, "Watering Detection: %.2f decrease threshold",
                profile->watering_threshold);

    nvs_close(nvs_handle);
    return ESP_OK;
}

/**
 * WiFi設定をNVSに保存
 */
esp_err_t nvs_config_save_wifi_config(const wifi_config_t *wifi_config) {
    if (wifi_config == NULL) {
        ESP_LOGE(TAG, "WiFi config pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err;

    // NVSハンドルを開く
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    // WiFi設定をblobとして保存
    err = nvs_set_blob(nvs_handle, NVS_KEY_WIFI, wifi_config, sizeof(wifi_config_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving WiFi config: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    // 変更をコミット
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error committing NVS: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "WiFi config saved successfully: SSID=%s", wifi_config->sta.ssid);
    }

    nvs_close(nvs_handle);
    return err;
}

/**
 * WiFi設定をNVSから読み込み
 */
esp_err_t nvs_config_load_wifi_config(wifi_config_t *wifi_config) {
    if (wifi_config == NULL) {
        ESP_LOGE(TAG, "WiFi config pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err;
    size_t required_size = sizeof(wifi_config_t);

    // NVSハンドルを開く（読み取り専用）
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "NVS partition not found for WiFi config");
        } else {
            ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        }
        return err;
    }

    // WiFi設定をblobとして読み込み
    err = nvs_get_blob(nvs_handle, NVS_KEY_WIFI, wifi_config, &required_size);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "WiFi config not found in NVS");
        nvs_close(nvs_handle);
        return err;
    } else if (required_size != sizeof(wifi_config_t)) {
        ESP_LOGE(TAG, "WiFi config size mismatch. Expected: %zu, Got: %zu", sizeof(wifi_config_t), required_size);
        nvs_close(nvs_handle);
        return ESP_ERR_INVALID_SIZE;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error reading WiFi config: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    ESP_LOGI(TAG, "WiFi config loaded successfully: SSID=%s", wifi_config->sta.ssid);

    nvs_close(nvs_handle);
    return ESP_OK;
}

/**
 * タイムゾーン設定をNVSに保存
 */
esp_err_t nvs_config_save_timezone(const char *timezone) {
    if (timezone == NULL) {
        ESP_LOGE(TAG, "Timezone pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err;

    // NVSハンドルを開く
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    // タイムゾーン文字列を保存
    err = nvs_set_str(nvs_handle, NVS_KEY_TIMEZONE, timezone);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving timezone: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    // 変更をコミット
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error committing NVS: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Timezone saved successfully: %s", timezone);
    }

    nvs_close(nvs_handle);
    return err;
}

/**
 * タイムゾーン設定をNVSから読み込み
 */
esp_err_t nvs_config_load_timezone(char *timezone, size_t max_len) {
    if (timezone == NULL || max_len == 0) {
        ESP_LOGE(TAG, "Invalid timezone buffer");
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err;

    // NVSハンドルを開く（読み取り専用）
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "NVS partition not found for timezone");
        } else {
            ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        }
        return err;
    }

    // タイムゾーン文字列を読み込み
    size_t required_size = max_len;
    err = nvs_get_str(nvs_handle, NVS_KEY_TIMEZONE, timezone, &required_size);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Timezone not found in NVS");
        nvs_close(nvs_handle);
        return err;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error reading timezone: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    ESP_LOGI(TAG, "Timezone loaded successfully: %s", timezone);

    nvs_close(nvs_handle);
    return ESP_OK;
}
