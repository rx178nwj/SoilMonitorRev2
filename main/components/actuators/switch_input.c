#include "switch_input.h"
#include "../../common_types.h"
#include "esp_log.h"

static const char *TAG = "SWITCH_INPUT";

// グローバル変数
static bool g_initialized = false;

/**
 * @brief スイッチ入力システム初期化
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t switch_input_init(void)
{
    ESP_LOGI(TAG, "🔘 スイッチ入力システム初期化中...");
    
    if (g_initialized) {
        ESP_LOGW(TAG, "スイッチ入力システムは既に初期化されています");
        return ESP_OK;
    }
    
    // スイッチ入力GPIO設定
    gpio_config_t switch_config = {
        .pin_bit_mask = 1ULL << SWITCH_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLDOWN_DISABLE,   // プルアップ抵抗を有効化
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,     // 割り込みは無効
    };
    
    esp_err_t ret = gpio_config(&switch_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "スイッチ GPIO設定失敗: %s", esp_err_to_name(ret));
        return ret;
    }
    
    g_initialized = true;
    
    ESP_LOGI(TAG, "✅ スイッチ入力システム初期化完了 (GPIO%d)", SWITCH_PIN);
    return ESP_OK;
}

/**
 * @brief スイッチが押されているかチェック
 * @return true: 押されている, false: 押されていない
 */
bool switch_input_is_pressed(void)
{
    if (!g_initialized) {
        ESP_LOGE(TAG, "スイッチ入力システムが初期化されていません");
        return false;
    }
    
    // ノーマルオープンスイッチ + プルアップ抵抗の場合
    // 押されていない時: HIGH (1)
    // 押されている時: LOW (0)
    int level = gpio_get_level(SWITCH_PIN);
    return (level == 0);
}

/**
 * @brief スイッチ入力システム終了処理
 */
void switch_input_deinit(void)
{
    if (!g_initialized) {
        ESP_LOGW(TAG, "スイッチ入力システムは初期化されていません");
        return;
    }
    
    ESP_LOGI(TAG, "🔘 スイッチ入力システム終了処理中...");
    
    // GPIOをリセット
    gpio_reset_pin(SWITCH_PIN);
    
    g_initialized = false;
    
    ESP_LOGI(TAG, "✅ スイッチ入力システム終了処理完了");
}