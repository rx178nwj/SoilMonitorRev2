#ifndef WS2812_CONTROL_H
#define WS2812_CONTROL_H

#include "led_strip.h"
#include "esp_err.h"
#include "../common_types.h"

// WS2812B設定
#define WS2812B_PIN         WS2812_PIN  // フルカラーLED（common_types.hで定義）
#define WS2812B_LED_COUNT   1           // LED数
#define WS2812B_BRIGHTNESS  2          // 輝度パーセント（1-100）

// カラープリセット
typedef enum {
    WS2812_COLOR_OFF,      // 消灯
    WS2812_COLOR_RED,      // 赤
    WS2812_COLOR_GREEN,    // 緑
    WS2812_COLOR_BLUE,     // 青
    WS2812_COLOR_YELLOW,   // 黄
    WS2812_COLOR_ORANGE,   // オレンジ
    WS2812_COLOR_PURPLE,   // 紫
    WS2812_COLOR_WHITE,    // 白
    WS2812_COLOR_CUSTOM    // カスタム色
} ws2812_color_preset_t;

// WS2812制御関数
esp_err_t ws2812_init(void);
void ws2812_deinit(void);
esp_err_t ws2812_set_color(uint8_t red, uint8_t green, uint8_t blue);
esp_err_t ws2812_set_preset_color(ws2812_color_preset_t preset);
esp_err_t ws2812_set_brightness(uint8_t brightness_percent);
esp_err_t ws2812_clear(void);
esp_err_t ws2812_set_led(uint8_t led_index, uint8_t red, uint8_t green, uint8_t blue);
esp_err_t ws2812_refresh(void);

// ステータス表示用関数
esp_err_t ws2812_show_status(bool moisture_warning, bool temp_high, bool temp_low, bool light_low, bool all_ok);

/**
 * @brief 湿度パーセントに応じた色温度でLEDを表示
 * @param humidity_percent 湿度パーセント(0-100)
 *        0%: 暖色（オレンジ/赤系）- 乾燥
 *        100%: 寒色（青）- 湿潤
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t ws2812_set_color_by_humidity(uint8_t humidity_percent);

/**
 * @brief 長期乾燥ワーニング表示（橙⇔赤の交互点滅）
 * @param blink_count 点滅回数（橙→赤で1回）
 * @param interval_ms 1色あたりの表示時間(ms)
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t ws2812_show_dry_warning(uint8_t blink_count, uint16_t interval_ms);

#endif // WS2812_CONTROL_H