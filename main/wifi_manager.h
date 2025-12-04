#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// WiFi認証情報をインクルード
// 注意: wifi_credentials.h は Git 管理外です
// wifi_credentials.h.example をコピーして作成してください
#include "wifi_credentials.h"

// WiFi状態コールバック関数型
typedef void (*wifi_status_callback_t)(bool connected);

// グローバルWiFi設定変数
extern wifi_config_t g_wifi_config;

// WiFi管理構造体
typedef struct {
    bool connected;
    int retry_count;
    wifi_ap_record_t ap_info;
    esp_netif_ip_info_t ip_info;
    wifi_status_callback_t status_callback;
} wifi_manager_t;

// WiFi管理関数
esp_err_t wifi_manager_init(wifi_status_callback_t callback);
void wifi_manager_deinit(void);
esp_err_t wifi_manager_start(void);
esp_err_t wifi_manager_stop(void);
bool wifi_manager_is_connected(void);
bool wifi_manager_wait_for_connection(int timeout_sec);

// WiFi情報取得
esp_err_t wifi_manager_get_ap_info(wifi_ap_record_t *ap_info);
esp_err_t wifi_manager_get_ip_info(esp_netif_ip_info_t *ip_info);
int8_t wifi_manager_get_rssi(void);

// WiFi状態確認・表示
void wifi_manager_check_status(void);
void wifi_manager_print_status(void);

// WiFi再接続
esp_err_t wifi_manager_reconnect(void);

#ifdef __cplusplus
}
#endif

#endif // WIFI_MANAGER_H