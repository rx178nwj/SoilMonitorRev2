#include "time_sync_manager.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_config.h"
#include <sys/time.h>
#include <string.h>

static const char *TAG = "TIME_SYNC";

// グローバル変数
static time_sync_manager_t g_time_manager = {0};

// SNTP時刻同期コールバック
static void sntp_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "⏰ SNTP時刻同期完了");
    
    g_time_manager.sync_completed = true;
    g_time_manager.last_sync_time = tv->tv_sec;
    
    // 同期完了時刻を表示
    struct tm timeinfo;
    localtime_r(&tv->tv_sec, &timeinfo);
    ESP_LOGI(TAG, "🕐 同期時刻: %04d/%02d/%02d %02d:%02d:%02d", 
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    
    // ユーザーコールバック呼び出し
    if (g_time_manager.sync_callback) {
        g_time_manager.sync_callback(tv);
    }
}

/**
 * @brief 時刻同期管理システム初期化
 * @param callback 時刻同期完了コールバック関数（NULLでも可）
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t time_sync_manager_init(time_sync_callback_t callback)
{
    ESP_LOGI(TAG, "⏰ 時刻同期管理システム初期化中...");

    if (g_time_manager.initialized) {
        ESP_LOGW(TAG, "時刻同期管理システムは既に初期化されています");
        return ESP_OK;
    }

    // NVSからタイムゾーン設定を読み込み
    char nvs_timezone[MAX_TIMEZONE_LENGTH] = {0};
    esp_err_t nvs_ret = nvs_config_load_timezone(nvs_timezone, MAX_TIMEZONE_LENGTH);

    if (nvs_ret == ESP_OK) {
        // NVSから読み込み成功
        strncpy(g_time_manager.timezone, nvs_timezone, MAX_TIMEZONE_LENGTH - 1);
        ESP_LOGI(TAG, "タイムゾーン設定をNVSから読み込みました: %s", g_time_manager.timezone);
    } else {
        // NVSにデータがない場合、デフォルト値を使用
        strncpy(g_time_manager.timezone, TIMEZONE, MAX_TIMEZONE_LENGTH - 1);
        ESP_LOGI(TAG, "デフォルトタイムゾーン設定を使用: %s", TIMEZONE);
    }
    g_time_manager.timezone[MAX_TIMEZONE_LENGTH - 1] = '\0';

    // タイムゾーン適用
    setenv("TZ", g_time_manager.timezone, 1);
    tzset();

    // コールバック設定
    g_time_manager.sync_callback = callback;
    g_time_manager.initialized = true;
    g_time_manager.sync_completed = false;
    g_time_manager.last_sync_time = 0;

    ESP_LOGI(TAG, "✅ 時刻同期管理システム初期化完了 - タイムゾーン: %s", g_time_manager.timezone);
    return ESP_OK;
}

/**
 * @brief 時刻同期管理システム終了処理
 */
void time_sync_manager_deinit(void)
{
    ESP_LOGI(TAG, "⏰ 時刻同期管理システム終了処理中...");
    
    time_sync_manager_stop();
    
    memset(&g_time_manager, 0, sizeof(g_time_manager));
    
    ESP_LOGI(TAG, "✅ 時刻同期管理システム終了処理完了");
}

/**
 * @brief SNTP時刻同期開始
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t time_sync_manager_start(void)
{
    ESP_LOGI(TAG, "⏰ SNTP時刻同期開始...");
    
    if (!g_time_manager.initialized) {
        ESP_LOGE(TAG, "時刻同期管理システムが初期化されていません");
        return ESP_ERR_INVALID_STATE;
    }
    
    // SNTP初期化チェック
    if (esp_sntp_enabled()) {
        ESP_LOGW(TAG, "SNTP は既に開始されています");
        return ESP_OK;
    }
    
    // SNTP設定
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    
    // 複数のNTPサーバーを設定（冗長性確保）
    esp_sntp_setservername(0, SNTP_SERVER_PRIMARY);
    esp_sntp_setservername(1, SNTP_SERVER_SECONDARY);
    esp_sntp_setservername(2, SNTP_SERVER_TERTIARY);
    
    // 同期間隔設定（デフォルト1時間）
    esp_sntp_set_sync_interval(3600000); // 1時間 = 3600000ms
    
    // 同期モード設定（ESP-IDF v5.3対応）
    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    
    // 同期コールバック設定
    esp_sntp_set_time_sync_notification_cb(sntp_sync_notification_cb);
    
    // SNTP開始
    esp_sntp_init();
    
    ESP_LOGI(TAG, "⏰ SNTP開始完了 - サーバー: %s, %s, %s", 
             SNTP_SERVER_PRIMARY, SNTP_SERVER_SECONDARY, SNTP_SERVER_TERTIARY);
    
    // 状態リセット
    g_time_manager.sync_completed = false;
    
    return ESP_OK;
}

/**
 * @brief SNTP時刻同期停止
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t time_sync_manager_stop(void)
{
    ESP_LOGI(TAG, "⏰ SNTP時刻同期停止中...");
    
    if (esp_sntp_enabled()) {
        esp_sntp_stop();
        ESP_LOGI(TAG, "✅ SNTP停止完了");
    } else {
        ESP_LOGW(TAG, "SNTP は既に停止されています");
    }
    
    return ESP_OK;
}

/**
 * @brief 時刻同期待機
 * @param timeout_sec タイムアウト時間（秒）
 * @return true: 同期成功, false: タイムアウト
 */
bool time_sync_manager_wait_for_sync(int timeout_sec)
{
    ESP_LOGI(TAG, "⏰ 時刻同期待機中... (最大%d秒)", timeout_sec);
    
    if (!g_time_manager.initialized) {
        ESP_LOGE(TAG, "時刻同期管理システムが初期化されていません");
        return false;
    }
    
    int retry = 0;
    while (!g_time_manager.sync_completed && retry < timeout_sec) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        retry++;
        
        if (retry % 10 == 0) {
            ESP_LOGI(TAG, "⏰ 時刻同期待機中... (%d秒)", retry);
        }
    }
    
    if (g_time_manager.sync_completed) {
        ESP_LOGI(TAG, "✅ 時刻同期完了!");
        return true;
    } else {
        ESP_LOGW(TAG, "⚠️  時刻同期タイムアウト");
        return false;
    }
}

/**
 * @brief 時刻同期完了確認
 * @return true: 同期済み, false: 未同期
 */
bool time_sync_manager_is_synced(void)
{
    return g_time_manager.sync_completed;
}

/**
 * @brief 現在時刻取得
 * @param timeinfo 時刻情報格納先
 */
void time_sync_manager_get_current_time(struct tm *timeinfo)
{
    if (timeinfo == NULL) {
        return;
    }
    
    time_t now;
    time(&now);
    localtime_r(&now, timeinfo);
}

/**
 * @brief 同期状態取得
 * @param last_sync 最後の同期時刻格納先（NULLでも可）
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t time_sync_manager_get_sync_status(time_t *last_sync)
{
    if (!g_time_manager.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (last_sync) {
        *last_sync = g_time_manager.last_sync_time;
    }
    
    return g_time_manager.sync_completed ? ESP_OK : ESP_ERR_NOT_FINISHED;
}

/**
 * @brief 時刻同期状態確認（ログ出力）
 */
void time_sync_manager_check_status(void)
{
    if (g_time_manager.sync_completed) {
        ESP_LOGI(TAG, "⏰ 時刻同期: 有効");
        
        // 現在時刻表示
        struct tm timeinfo;
        time_sync_manager_get_current_time(&timeinfo);
        ESP_LOGI(TAG, "⏰ 現在時刻: %04d/%02d/%02d %02d:%02d:%02d", 
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        
        // 同期間隔情報
        if (esp_sntp_enabled()) {
            uint32_t sync_interval_ms = esp_sntp_get_sync_interval();
            ESP_LOGI(TAG, "⏰ 同期間隔: %d分", (int)(sync_interval_ms / 60000));
        }
        
        // 最後の同期時刻表示
        if (g_time_manager.last_sync_time > 0) {
            struct tm last_sync_tm;
            localtime_r(&g_time_manager.last_sync_time, &last_sync_tm);
            ESP_LOGI(TAG, "⏰ 最終同期: %04d/%02d/%02d %02d:%02d:%02d",
                     last_sync_tm.tm_year + 1900, last_sync_tm.tm_mon + 1, last_sync_tm.tm_mday,
                     last_sync_tm.tm_hour, last_sync_tm.tm_min, last_sync_tm.tm_sec);
        }
    } else {
        ESP_LOGW(TAG, "⏰ 時刻同期: 無効（ローカル時刻使用）");
    }
}

/**
 * @brief 現在時刻表示
 */
void time_sync_manager_print_time(void)
{
    struct tm timeinfo;
    time_sync_manager_get_current_time(&timeinfo);
    
    const char* sync_status = g_time_manager.sync_completed ? "NTP同期済み" : "ローカル時刻";
    
    ESP_LOGI(TAG, "🕐 現在時刻: %04d/%02d/%02d %02d:%02d:%02d (%s)", 
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, sync_status);
}

/**
 * @brief 時刻文字列フォーマット
 * @param timeinfo 時刻情報
 * @param buffer 出力バッファ
 * @param buffer_size バッファサイズ
 */
void time_sync_manager_format_time(const struct tm *timeinfo, char *buffer, size_t buffer_size)
{
    if (timeinfo == NULL || buffer == NULL || buffer_size == 0) {
        return;
    }

    snprintf(buffer, buffer_size, "%04d/%02d/%02d %02d:%02d:%02d",
             timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
             timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
}

/**
 * @brief タイムゾーン設定
 * @param timezone_str タイムゾーン文字列（POSIX形式、例: "JST-9"）
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t time_sync_manager_set_timezone(const char *timezone_str)
{
    if (timezone_str == NULL) {
        ESP_LOGE(TAG, "Timezone string is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (!g_time_manager.initialized) {
        ESP_LOGE(TAG, "Time sync manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // タイムゾーン文字列のコピー
    strncpy(g_time_manager.timezone, timezone_str, MAX_TIMEZONE_LENGTH - 1);
    g_time_manager.timezone[MAX_TIMEZONE_LENGTH - 1] = '\0';

    // タイムゾーン適用
    setenv("TZ", g_time_manager.timezone, 1);
    tzset();

    ESP_LOGI(TAG, "✅ タイムゾーン変更: %s", g_time_manager.timezone);
    return ESP_OK;
}

/**
 * @brief タイムゾーン取得
 * @return タイムゾーン文字列（内部バッファへのポインタ）
 */
const char* time_sync_manager_get_timezone(void)
{
    if (!g_time_manager.initialized) {
        return TIMEZONE;  // デフォルト値を返す
    }

    return g_time_manager.timezone;
}