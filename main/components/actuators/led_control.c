#include "../actuators/led_control.h"
#include "../actuators/ws2812_control.h"
#include "../../common_types.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "LED_CTRL";

// グローバル変数
static led_control_t g_led_control = {0};

/**
 * @brief LED制御システム初期化
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t led_control_init(void)
{
    ESP_LOGI(TAG, "🔆 LED制御システム初期化中...");
    
    if (g_led_control.initialized) {
        ESP_LOGW(TAG, "LED制御システムは既に初期化されています");
        return ESP_OK;
    }
    
    // 青色LED初期化
    gpio_config_t blue_led_config = {
        .pin_bit_mask = 1ULL << BLUE_LED_PIN,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&blue_led_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "青色LED GPIO設定失敗: %s", esp_err_to_name(ret));
        return ret;
    }
    gpio_set_level(BLUE_LED_PIN, 0);
    
    // 赤色LED初期化
    gpio_config_t red_led_config = {
        .pin_bit_mask = 1ULL << RED_LED_PIN,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ret = gpio_config(&red_led_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "赤色LED GPIO設定失敗: %s", esp_err_to_name(ret));
        return ret;
    }
    gpio_set_level(RED_LED_PIN, 0);
    
    // WS2812B初期化
    ret = ws2812_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WS2812B初期化失敗: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 状態初期化
    g_led_control.blue_led_state = false;
    g_led_control.red_led_state = false;
    g_led_control.initialized = true;
    
    ESP_LOGI(TAG, "✅ LED制御システム初期化完了");
    return ESP_OK;
}

/**
 * @brief LED制御システム終了処理
 */
void led_control_deinit(void)
{
    ESP_LOGI(TAG, "🔆 LED制御システム終了処理中...");
    
    if (!g_led_control.initialized) {
        ESP_LOGW(TAG, "LED制御システムは初期化されていません");
        return;
    }
    
    // 全LED消灯
    led_control_all_off();
    
    // WS2812B終了処理
    ws2812_deinit();
    
    // 状態リセット
    g_led_control.blue_led_state = false;
    g_led_control.red_led_state = false;
    g_led_control.initialized = false;
    
    ESP_LOGI(TAG, "✅ LED制御システム終了処理完了");
}

/**
 * @brief 青色LED制御
 * @param state LED状態（true: 点灯, false: 消灯）
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t led_control_blue_set(bool state)
{
    if (!g_led_control.initialized) {
        ESP_LOGE(TAG, "LED制御システムが初期化されていません");
        return ESP_ERR_INVALID_STATE;
    }
    
    gpio_set_level(BLUE_LED_PIN, state ? 1 : 0);
    g_led_control.blue_led_state = state;
    
    ESP_LOGD(TAG, "💙 青色LED: %s", state ? "点灯" : "消灯");
    return ESP_OK;
}

/**
 * @brief 赤色LED制御
 * @param state LED状態（true: 点灯, false: 消灯）
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t led_control_red_set(bool state)
{
    if (!g_led_control.initialized) {
        ESP_LOGE(TAG, "LED制御システムが初期化されていません");
        return ESP_ERR_INVALID_STATE;
    }
    
    gpio_set_level(RED_LED_PIN, state ? 1 : 0);
    g_led_control.red_led_state = state;
    
    ESP_LOGD(TAG, "❤️  赤色LED: %s", state ? "点灯" : "消灯");
    return ESP_OK;
}

/**
 * @brief 全LED消灯
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t led_control_all_off(void)
{
    if (!g_led_control.initialized) {
        ESP_LOGE(TAG, "LED制御システムが初期化されていません");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = ESP_OK;
    
    // 個別LED消灯
    esp_err_t blue_ret = led_control_blue_set(false);
    esp_err_t red_ret = led_control_red_set(false);
    
    // WS2812B消灯
    esp_err_t ws2812_ret = ws2812_clear();
    
    // エラーチェック
    if (blue_ret != ESP_OK || red_ret != ESP_OK || ws2812_ret != ESP_OK) {
        ESP_LOGW(TAG, "⚠️  一部LED消灯に失敗");
        ret = ESP_FAIL;
    } else {
        ESP_LOGD(TAG, "🔅 全LED消灯完了");
    }
    
    return ret;
}

/**
 * @brief ウェイクアップ表示（青色LED点滅）
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t led_control_wakeup_indication(void)
{
    if (!g_led_control.initialized) {
        ESP_LOGE(TAG, "LED制御システムが初期化されていません");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "💙 Wakeup indication - Blue LED ON");
    
    esp_err_t ret = led_control_blue_set(true);
    if (ret != ESP_OK) {
        return ret;
    }
    
    vTaskDelay(pdMS_TO_TICKS(1000)); // 1秒間点灯
    
    ret = led_control_blue_set(false);
    ESP_LOGI(TAG, "💙 Blue LED OFF");
    
    return ret;
}

/**
 * @brief センサーステータスに応じたLED表示
 * @param status センサーステータス構造体
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t led_control_show_status(const sensor_status_t *status)
{
    if (!g_led_control.initialized) {
        ESP_LOGE(TAG, "LED制御システムが初期化されていません");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (status == NULL) {
        ESP_LOGE(TAG, "ステータスポインタがNULLです");
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = ESP_OK;
    bool warning = false;
    
    // 警告条件チェック
    if (status->moisture_warning || status->temp_high || status->temp_low || 
        status->light_low || status->sensor_error) {
        warning = true;
    }
    
    // 赤色LED制御（警告時に点灯）
    esp_err_t red_ret = led_control_red_set(warning);
    if (red_ret != ESP_OK) {
        ESP_LOGW(TAG, "赤色LED制御失敗: %s", esp_err_to_name(red_ret));
        ret = red_ret;
    }
    
    // WS2812B制御（詳細ステータス表示）
    esp_err_t ws2812_ret = ws2812_show_status(
        status->moisture_warning,
        status->temp_high,
        status->temp_low,
        status->light_low,
        status->all_ok
    );
    if (ws2812_ret != ESP_OK) {
        ESP_LOGW(TAG, "WS2812B制御失敗: %s", esp_err_to_name(ws2812_ret));
        ret = ws2812_ret;
    }
    
    // ステータスログ出力
    if (status->all_ok) {
        ESP_LOGI(TAG, "✅ 全センサー正常 - 緑LED表示");
    } else {
        ESP_LOGI(TAG, "⚠️  警告状態検出 - 警告LED表示");
        if (status->moisture_warning) ESP_LOGI(TAG, "  💧 水分不足");
        if (status->temp_high) ESP_LOGI(TAG, "  🔥 高温");
        if (status->temp_low) ESP_LOGI(TAG, "  🧊 低温");
        if (status->light_low) ESP_LOGI(TAG, "  🌙 照度不足");
        if (status->sensor_error) ESP_LOGI(TAG, "  ❌ センサーエラー");
    }
    
    return ret;
}

/**
 * @brief 青色LED状態取得
 * @return true: 点灯中, false: 消灯中
 */
bool led_control_is_blue_on(void)
{
    return g_led_control.blue_led_state;
}

/**
 * @brief 赤色LED状態取得
 * @return true: 点灯中, false: 消灯中
 */
bool led_control_is_red_on(void)
{
    return g_led_control.red_led_state;
}