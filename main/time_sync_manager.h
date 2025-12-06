#ifndef TIME_SYNC_MANAGER_H
#define TIME_SYNC_MANAGER_H

#include "esp_err.h"
#include <time.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// SNTP設定
#define SNTP_SERVER_PRIMARY      "pool.ntp.org"
#define SNTP_SERVER_SECONDARY    "time.nist.gov"
#define SNTP_SERVER_TERTIARY     "time.google.com"
#define TIMEZONE                 "JST-9"  // 日本標準時
#define SNTP_SYNC_TIMEOUT_SEC    60      // 同期タイムアウト時間

// 時刻同期コールバック関数型
typedef void (*time_sync_callback_t)(struct timeval *tv);

// タイムゾーン文字列の最大長
#define MAX_TIMEZONE_LENGTH 64

// 時刻同期管理構造体
typedef struct {
    bool initialized;
    bool sync_completed;
    time_t last_sync_time;
    time_sync_callback_t sync_callback;
    char timezone[MAX_TIMEZONE_LENGTH];  // 動的タイムゾーン
} time_sync_manager_t;

// 時刻同期管理関数
esp_err_t time_sync_manager_init(time_sync_callback_t callback);
void time_sync_manager_deinit(void);
esp_err_t time_sync_manager_start(void);
esp_err_t time_sync_manager_stop(void);
bool time_sync_manager_wait_for_sync(int timeout_sec);

// 時刻取得・確認
bool time_sync_manager_is_synced(void);
void time_sync_manager_get_current_time(struct tm *timeinfo);
esp_err_t time_sync_manager_get_sync_status(time_t *last_sync);
void time_sync_manager_check_status(void);
void time_sync_manager_print_time(void);

// 時刻文字列変換
void time_sync_manager_format_time(const struct tm *timeinfo, char *buffer, size_t buffer_size);

// タイムゾーン設定・取得
esp_err_t time_sync_manager_set_timezone(const char *timezone_str);
const char* time_sync_manager_get_timezone(void);

#ifdef __cplusplus
}
#endif

#endif // TIME_SYNC_MANAGER_H