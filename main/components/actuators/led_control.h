#ifndef LED_CONTROL_H
#define LED_CONTROL_H

#include "esp_err.h"
#include "driver/gpio.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// LED制御構造体
typedef struct {
    bool blue_led_state;
    bool red_led_state;
    bool initialized;
} led_control_t;

// ステータス表示用構造体
typedef struct {
    bool moisture_warning;    // 水分不足警告
    bool temp_high;          // 高温警告
    bool temp_low;           // 低温警告
    bool light_low;          // 照度不足警告
    bool all_ok;             // 全て正常
    bool sensor_error;       // センサーエラー
} sensor_status_t;

// LED制御関数
esp_err_t led_control_init(void);
void led_control_deinit(void);

// 個別LED制御
esp_err_t led_control_blue_set(bool state);
esp_err_t led_control_red_set(bool state);
esp_err_t led_control_all_off(void);

// ウェイクアップ表示
esp_err_t led_control_wakeup_indication(void);

// 起動時LED動作チェック
esp_err_t led_control_startup_test(void);

// ステータス表示（赤色LED + WS2812B の組み合わせ）
esp_err_t led_control_show_status(const sensor_status_t *status);

// LED状態取得
bool led_control_is_blue_on(void);
bool led_control_is_red_on(void);

#ifdef __cplusplus
}
#endif

#endif // LED_CONTROL_H