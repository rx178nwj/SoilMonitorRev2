#include "ws2812_control.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "WS2812_CTRL";

// グローバル変数
static led_strip_handle_t led_strip = NULL;
static uint8_t current_brightness = WS2812B_BRIGHTNESS;

// カラープリセット定義
static const struct {
    uint8_t r, g, b;
} color_presets[] = {
    [WS2812_COLOR_OFF]    = {0,   0,   0  },
    [WS2812_COLOR_RED]    = {255, 0,   0  },
    [WS2812_COLOR_GREEN]  = {0,   255, 0  },
    [WS2812_COLOR_BLUE]   = {0,   0,   255},
    [WS2812_COLOR_YELLOW] = {255, 255, 0  },
    [WS2812_COLOR_ORANGE] = {255, 100, 0  },
    [WS2812_COLOR_PURPLE] = {128, 0,   128},
    [WS2812_COLOR_WHITE]  = {255, 255, 255}
};

/**
 * @brief 輝度調整を適用
 * @param color_value 元の色値(0-255)
 * @param brightness_percent 輝度パーセント(1-100)
 * @return 調整後の色値
 */
static uint8_t apply_brightness(uint8_t color_value, uint8_t brightness_percent)
{
    if (brightness_percent > 100) brightness_percent = 100;
    return (uint8_t)((color_value * brightness_percent) / 100);
}

/**
 * @brief WS2812Bを初期化
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t ws2812_init(void)
{
    ESP_LOGI(TAG, "WS2812B初期化開始 (GPIO%d, LEDs:%d)", WS2812B_PIN, WS2812B_LED_COUNT);

    led_strip_config_t strip_config = {
        .strip_gpio_num = WS2812B_PIN,
        .max_leds = WS2812B_LED_COUNT,
        .led_model = LED_MODEL_WS2812,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
    };
    
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .flags.with_dma = false, // ESP32-C3ではDMAは使えない
    };
    
    esp_err_t ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LED strip initialization failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 初期化時は全消灯
    ret = ws2812_clear();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WS2812B初期化完了 (輝度: %d%%)", current_brightness);
    }
    
    return ret;
}

/**
 * @brief WS2812Bを終了処理
 */
void ws2812_deinit(void)
{
    if (led_strip != NULL) {
        ws2812_clear();
        led_strip_del(led_strip);
        led_strip = NULL;
        ESP_LOGI(TAG, "WS2812B終了処理完了");
    }
}

/**
 * @brief 全LEDを指定色に設定
 * @param red 赤色値(0-255)
 * @param green 緑色値(0-255)
 * @param blue 青色値(0-255)
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t ws2812_set_color(uint8_t red, uint8_t green, uint8_t blue)
{
    if (led_strip == NULL) {
        ESP_LOGE(TAG, "WS2812B not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // 輝度制御を適用
    uint8_t dimmed_red = apply_brightness(red, current_brightness);
    uint8_t dimmed_green = apply_brightness(green, current_brightness);
    uint8_t dimmed_blue = apply_brightness(blue, current_brightness);
    
    // 全LEDに同じ色を設定
    for (int i = 0; i < WS2812B_LED_COUNT; i++) {
        esp_err_t ret = led_strip_set_pixel(led_strip, i, dimmed_red, dimmed_green, dimmed_blue);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set pixel %d: %s", i, esp_err_to_name(ret));
            return ret;
        }
    }
    
    // LEDに色を反映
    esp_err_t ret = led_strip_refresh(led_strip);
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "WS2812B: R=%d->%d, G=%d->%d, B=%d->%d (%d%%)", 
                 red, dimmed_red, green, dimmed_green, blue, dimmed_blue, current_brightness);
    }
    
    return ret;
}

/**
 * @brief プリセット色に設定
 * @param preset プリセット色
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t ws2812_set_preset_color(ws2812_color_preset_t preset)
{
    if (preset >= sizeof(color_presets) / sizeof(color_presets[0])) {
        ESP_LOGE(TAG, "Invalid color preset: %d", preset);
        return ESP_ERR_INVALID_ARG;
    }

    return ws2812_set_color(color_presets[preset].r, 
                           color_presets[preset].g, 
                           color_presets[preset].b);
}

/**
 * @brief 輝度を設定
 * @param brightness_percent 輝度パーセント(1-100)
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t ws2812_set_brightness(uint8_t brightness_percent)
{
    if (brightness_percent < 1 || brightness_percent > 100) {
        ESP_LOGE(TAG, "Invalid brightness: %d (must be 1-100)", brightness_percent);
        return ESP_ERR_INVALID_ARG;
    }

    current_brightness = brightness_percent;
    ESP_LOGI(TAG, "輝度設定: %d%%", current_brightness);
    
    return ESP_OK;
}

/**
 * @brief 全LED消灯
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t ws2812_clear(void)
{
    if (led_strip == NULL) {
        ESP_LOGE(TAG, "WS2812B not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = led_strip_clear(led_strip);
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "WS2812B cleared");
    }
    
    return ret;
}

/**
 * @brief 個別LEDに色を設定
 * @param led_index LED番号(0-WS2812B_LED_COUNT-1)
 * @param red 赤色値(0-255)
 * @param green 緑色値(0-255)
 * @param blue 青色値(0-255)
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t ws2812_set_led(uint8_t led_index, uint8_t red, uint8_t green, uint8_t blue)
{
    if (led_strip == NULL) {
        ESP_LOGE(TAG, "WS2812B not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (led_index >= WS2812B_LED_COUNT) {
        ESP_LOGE(TAG, "Invalid LED index: %d (max: %d)", led_index, WS2812B_LED_COUNT - 1);
        return ESP_ERR_INVALID_ARG;
    }

    // 輝度制御を適用
    uint8_t dimmed_red = apply_brightness(red, current_brightness);
    uint8_t dimmed_green = apply_brightness(green, current_brightness);
    uint8_t dimmed_blue = apply_brightness(blue, current_brightness);
    
    return led_strip_set_pixel(led_strip, led_index, dimmed_red, dimmed_green, dimmed_blue);
}

/**
 * @brief LED表示を更新
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t ws2812_refresh(void)
{
    if (led_strip == NULL) {
        ESP_LOGE(TAG, "WS2812B not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    return led_strip_refresh(led_strip);
}

/**
 * @brief 湿度パーセントに応じた色温度でLEDを表示
 * @param humidity_percent 湿度パーセント(0-100)
 *        0%: 暖色（オレンジ/赤系）- 乾燥
 *        100%: 寒色（青）- 湿潤
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t ws2812_set_color_by_humidity(uint8_t humidity_percent)
{
    // 0-100の範囲にクランプ
    if (humidity_percent > 100) humidity_percent = 100;

    // 色温度グラデーション: 暖色(乾燥) → 寒色(湿潤)
    // 0%:   オレンジ (255, 80, 0)
    // 25%:  黄色     (255, 200, 0)
    // 50%:  緑       (0, 255, 0)
    // 75%:  シアン   (0, 200, 255)
    // 100%: 青       (0, 50, 255)

    uint8_t r, g, b;

    if (humidity_percent <= 25) {
        // 0-25%: オレンジ → 黄色
        float t = humidity_percent / 25.0f;
        r = 255;
        g = (uint8_t)(80 + (200 - 80) * t);
        b = 0;
    } else if (humidity_percent <= 50) {
        // 25-50%: 黄色 → 緑
        float t = (humidity_percent - 25) / 25.0f;
        r = (uint8_t)(255 * (1.0f - t));
        g = (uint8_t)(200 + (255 - 200) * t);
        b = 0;
    } else if (humidity_percent <= 75) {
        // 50-75%: 緑 → シアン
        float t = (humidity_percent - 50) / 25.0f;
        r = 0;
        g = (uint8_t)(255 - (255 - 200) * t);
        b = (uint8_t)(255 * t);
    } else {
        // 75-100%: シアン → 青
        float t = (humidity_percent - 75) / 25.0f;
        r = 0;
        g = (uint8_t)(200 * (1.0f - t) + 50 * t);
        b = 255;
    }

    ESP_LOGI(TAG, "��️ 湿度 %d%% → LED色 R=%d, G=%d, B=%d", humidity_percent, r, g, b);
    return ws2812_set_color(r, g, b);
}

/**
 * @brief センサーステータスに応じてLED表示
 * @param moisture_warning 水分不足警告
 * @param temp_high 高温警告
 * @param temp_low 低温警告
 * @param light_low 照度不足警告
 * @param all_ok 全て正常
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t ws2812_show_status(bool moisture_warning, bool temp_high, bool temp_low, bool light_low, bool all_ok)
{
    esp_err_t ret = ESP_OK;

    if (all_ok) {
        // 全て正常 - 緑色
        ret = ws2812_set_preset_color(WS2812_COLOR_GREEN);
        ESP_LOGI(TAG, "✅ 状態良好 - 緑LED点灯");
    } else if (moisture_warning) {
        // 水分不足 - オレンジ色
        ret = ws2812_set_preset_color(WS2812_COLOR_ORANGE);
        ESP_LOGI(TAG, "⚠️  水分不足 - オレンジLED点灯");
    } else if (temp_high) {
        // 高温 - 赤色
        ret = ws2812_set_preset_color(WS2812_COLOR_RED);
        ESP_LOGI(TAG, "🔥 高温警告 - 赤LED点灯");
    } else if (temp_low) {
        // 低温 - 青色
        ret = ws2812_set_preset_color(WS2812_COLOR_BLUE);
        ESP_LOGI(TAG, "🧊 低温警告 - 青LED点灯");
    } else if (light_low) {
        // 照度不足 - 黄色
        ret = ws2812_set_preset_color(WS2812_COLOR_YELLOW);
        ESP_LOGI(TAG, "🌙 照度不足 - 黄LED点灯");
    } else {
        // その他/不明 - 紫色
        ret = ws2812_set_preset_color(WS2812_COLOR_PURPLE);
        ESP_LOGI(TAG, "❓ 状態不明 - 紫LED点灯");
    }

    return ret;
}

/**
 * @brief 長期乾燥ワーニング表示（橙⇔赤の交互点滅）
 * @param blink_count 点滅回数（橙→赤で1回）
 * @param interval_ms 1色あたりの表示時間(ms)
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t ws2812_show_dry_warning(uint8_t blink_count, uint16_t interval_ms)
{
    esp_err_t ret = ESP_OK;

    ESP_LOGI(TAG, "⚠️  長期乾燥ワーニング: 橙⇔赤 交互点滅 (%d回, %dms間隔)", blink_count, interval_ms);

    for (uint8_t i = 0; i < blink_count; i++) {
        // 橙色表示
        ret = ws2812_set_color(255, 100, 0);
        if (ret != ESP_OK) return ret;
        vTaskDelay(pdMS_TO_TICKS(interval_ms));

        // 赤色表示
        ret = ws2812_set_color(255, 0, 0);
        if (ret != ESP_OK) return ret;
        vTaskDelay(pdMS_TO_TICKS(interval_ms));
    }

    // 最後は橙色で終了（乾燥状態を示す）
    ret = ws2812_set_color(255, 100, 0);

    return ret;
}