#include "wifi_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include <string.h>

static const char *TAG = "WIFI_MGR";

// WiFi接続状態ビット
#define WIFI_CONNECTED_BIT     BIT0
#define WIFI_FAIL_BIT          BIT1

// グローバル変数
static wifi_manager_t g_wifi_manager = {0};
static EventGroupHandle_t s_wifi_event_group;
static esp_netif_t *s_sta_netif = NULL;
// WiFi設定
wifi_config_t g_wifi_config = {0};

// WiFiイベントハンドラ
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "📶 WiFi接続開始");
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (g_wifi_manager.retry_count < WIFI_MAXIMUM_RETRY) {
            esp_wifi_connect();
            g_wifi_manager.retry_count++;
            ESP_LOGI(TAG, "📶 WiFi再接続試行 %d/%d", 
                     g_wifi_manager.retry_count, WIFI_MAXIMUM_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGW(TAG, "⚠️  WiFi接続失敗 - 最大試行回数に到達");
        }
        
        g_wifi_manager.connected = false;
        if (g_wifi_manager.status_callback) {
            g_wifi_manager.status_callback(false);
        }
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "✅ WiFi接続成功 - IP: " IPSTR, IP2STR(&event->ip_info.ip));
        
        g_wifi_manager.connected = true;
        g_wifi_manager.retry_count = 0;
        g_wifi_manager.ip_info = event->ip_info;
        
        // AP情報更新
        if (esp_wifi_sta_get_ap_info(&g_wifi_manager.ap_info) != ESP_OK) {
            ESP_LOGW(TAG, "AP情報取得失敗");
        }
        
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        
        if (g_wifi_manager.status_callback) {
            g_wifi_manager.status_callback(true);
        }
    }
}

/**
 * @brief WiFi管理システム初期化
 * @param callback WiFi状態変更コールバック関数（NULLでも可）
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t wifi_manager_init(wifi_status_callback_t callback)
{
    ESP_LOGI(TAG, "📶 WiFi管理システム初期化中...");
    
    // 初期化チェック
    if (s_wifi_event_group != NULL) {
        ESP_LOGW(TAG, "WiFi管理システムは既に初期化されています");
        return ESP_OK;
    }
    
    // イベントグループ作成
    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "イベントグループ作成失敗");
        return ESP_FAIL;
    }
    
    // TCP/IPスタック初期化
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TCP/IPスタック初期化失敗: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // イベントループ作成
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "イベントループ作成失敗: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // STA netif作成
    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (s_sta_netif == NULL) {
        ESP_LOGE(TAG, "STA netif作成失敗");
        return ESP_FAIL;
    }
    
    // WiFi初期化
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi初期化失敗: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // イベントハンドラ登録
    ret = esp_event_handler_instance_register(WIFI_EVENT,
                                             ESP_EVENT_ANY_ID,
                                             &wifi_event_handler,
                                             NULL,
                                             NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFiイベントハンドラ登録失敗: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = esp_event_handler_instance_register(IP_EVENT,
                                             IP_EVENT_STA_GOT_IP,
                                             &wifi_event_handler,
                                             NULL,
                                             NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "IPイベントハンドラ登録失敗: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // WiFiモード設定
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFiモード設定失敗: %s", esp_err_to_name(ret));
        return ret;
    }

    // デフォルトのWiFi設定（wifi_credentials.hから読み込み）
    // BLE経由で設定する場合は、この設定は上書きされます
    if (strlen(WIFI_SSID) > 0) {
        strncpy((char*)g_wifi_config.sta.ssid, WIFI_SSID, sizeof(g_wifi_config.sta.ssid) - 1);
        strncpy((char*)g_wifi_config.sta.password, WIFI_PASSWORD, sizeof(g_wifi_config.sta.password) - 1);
        g_wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

        ret = esp_wifi_set_config(WIFI_IF_STA, &g_wifi_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "WiFi設定失敗: %s", esp_err_to_name(ret));
            return ret;
        }
        ESP_LOGI(TAG, "✅ WiFi管理システム初期化完了 - デフォルトSSID: %s", WIFI_SSID);
    } else {
        // SSIDが空の場合、BLE経由での設定を待つ
        g_wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        ESP_LOGI(TAG, "✅ WiFi管理システム初期化完了 - SSID未設定（BLE経由で設定してください）");
    }

    // コールバック設定
    g_wifi_manager.status_callback = callback;

    return ESP_OK;
}

/**
 * @brief WiFi管理システム終了処理
 */
void wifi_manager_deinit(void)
{
    ESP_LOGI(TAG, "WiFi管理システム終了処理中...");
    
    wifi_manager_stop();
    
    if (s_wifi_event_group) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
    }
    
    esp_wifi_deinit();
    
    memset(&g_wifi_manager, 0, sizeof(g_wifi_manager));
    
    ESP_LOGI(TAG, "✅ WiFi管理システム終了処理完了");
}

/**
 * @brief WiFi開始
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t wifi_manager_start(void)
{
    ESP_LOGI(TAG, "📶 WiFi開始...");
    
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "WiFi管理システムが初期化されていません");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 状態リセット
    g_wifi_manager.connected = false;
    g_wifi_manager.retry_count = 0;
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    
    esp_err_t ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi開始失敗: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "✅ WiFi開始完了");
    return ESP_OK;
}

/**
 * @brief WiFi停止
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t wifi_manager_stop(void)
{
    ESP_LOGI(TAG, "📶 WiFi停止中...");
    
    esp_err_t ret = esp_wifi_stop();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi停止失敗: %s", esp_err_to_name(ret));
        return ret;
    }
    
    g_wifi_manager.connected = false;
    
    ESP_LOGI(TAG, "✅ WiFi停止完了");
    return ESP_OK;
}

/**
 * @brief WiFi接続状態確認
 * @return true: 接続中, false: 未接続
 */
bool wifi_manager_is_connected(void)
{
    return g_wifi_manager.connected;
}

/**
 * @brief WiFi接続待機
 * @param timeout_sec タイムアウト時間（秒）
 * @return true: 接続成功, false: タイムアウトまたは失敗
 */
bool wifi_manager_wait_for_connection(int timeout_sec)
{
    ESP_LOGI(TAG, "📶 WiFi接続待機中... (最大%d秒)", timeout_sec);
    
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "WiFi管理システムが初期化されていません");
        return false;
    }
    
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                          WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                          pdFALSE,
                                          pdFALSE,
                                          pdMS_TO_TICKS(timeout_sec * 1000));
    
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "✅ WiFi接続成功!");
        return true;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGW(TAG, "⚠️  WiFi接続失敗");
        return false;
    } else {
        ESP_LOGW(TAG, "⚠️  WiFi接続タイムアウト");
        return false;
    }
}

/**
 * @brief AP情報取得
 * @param ap_info AP情報格納先
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t wifi_manager_get_ap_info(wifi_ap_record_t *ap_info)
{
    if (ap_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!g_wifi_manager.connected) {
        return ESP_ERR_WIFI_NOT_CONNECT;
    }
    
    *ap_info = g_wifi_manager.ap_info;
    return ESP_OK;
}

/**
 * @brief IP情報取得
 * @param ip_info IP情報格納先
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t wifi_manager_get_ip_info(esp_netif_ip_info_t *ip_info)
{
    if (ip_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!g_wifi_manager.connected) {
        return ESP_ERR_WIFI_NOT_CONNECT;
    }
    
    *ip_info = g_wifi_manager.ip_info;
    return ESP_OK;
}

/**
 * @brief WiFi信号強度取得
 * @return RSSI値（dBm）、エラー時は-128
 */
int8_t wifi_manager_get_rssi(void)
{
    if (!g_wifi_manager.connected) {
        return -128;
    }
    
    return g_wifi_manager.ap_info.rssi;
}

/**
 * @brief WiFi状態確認（ログ出力）
 */
void wifi_manager_check_status(void)
{
    if (g_wifi_manager.connected) {
        ESP_LOGI(TAG, "📶 ネットワーク状態: 接続中");
        ESP_LOGI(TAG, "📡 IP: " IPSTR, IP2STR(&g_wifi_manager.ip_info.ip));
        ESP_LOGI(TAG, "📡 Gateway: " IPSTR, IP2STR(&g_wifi_manager.ip_info.gw));
        ESP_LOGI(TAG, "📡 Netmask: " IPSTR, IP2STR(&g_wifi_manager.ip_info.netmask));
        ESP_LOGI(TAG, "📶 信号強度: %d dBm", g_wifi_manager.ap_info.rssi);
    } else {
        ESP_LOGW(TAG, "📶 ネットワーク状態: 未接続");
    }
}

/**
 * @brief WiFi状態詳細表示
 */
void wifi_manager_print_status(void)
{
    ESP_LOGI(TAG, "=== WiFi状態詳細 ===");
    ESP_LOGI(TAG, "接続状態: %s", g_wifi_manager.connected ? "接続中" : "未接続");
    ESP_LOGI(TAG, "再試行回数: %d/%d", g_wifi_manager.retry_count, WIFI_MAXIMUM_RETRY);
    
    if (g_wifi_manager.connected) {
        ESP_LOGI(TAG, "SSID: %s", (char*)g_wifi_manager.ap_info.ssid);
        ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&g_wifi_manager.ip_info.ip));
        ESP_LOGI(TAG, "Gateway: " IPSTR, IP2STR(&g_wifi_manager.ip_info.gw));
        ESP_LOGI(TAG, "Netmask: " IPSTR, IP2STR(&g_wifi_manager.ip_info.netmask));
        ESP_LOGI(TAG, "RSSI: %d dBm", g_wifi_manager.ap_info.rssi);
        
        // チャンネル情報
        ESP_LOGI(TAG, "チャンネル: %d", g_wifi_manager.ap_info.primary);
        
        // 認証モード
        const char* auth_mode_str;
        switch (g_wifi_manager.ap_info.authmode) {
            case WIFI_AUTH_OPEN: auth_mode_str = "OPEN"; break;
            case WIFI_AUTH_WEP: auth_mode_str = "WEP"; break;
            case WIFI_AUTH_WPA_PSK: auth_mode_str = "WPA_PSK"; break;
            case WIFI_AUTH_WPA2_PSK: auth_mode_str = "WPA2_PSK"; break;
            case WIFI_AUTH_WPA_WPA2_PSK: auth_mode_str = "WPA_WPA2_PSK"; break;
            case WIFI_AUTH_WPA3_PSK: auth_mode_str = "WPA3_PSK"; break;
            default: auth_mode_str = "UNKNOWN"; break;
        }
        ESP_LOGI(TAG, "認証モード: %s", auth_mode_str);
    }
}

/**
 * @brief WiFi再接続
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t wifi_manager_reconnect(void)
{
    ESP_LOGI(TAG, "📶 WiFi再接続実行中...");
    
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "WiFi管理システムが初期化されていません");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 現在の接続を切断
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 再試行カウンタをリセット
    g_wifi_manager.retry_count = 0;
    g_wifi_manager.connected = false;
    
    // 再接続
    esp_err_t ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi再接続失敗: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "✅ WiFi再接続要求送信完了");
    return ESP_OK;
}