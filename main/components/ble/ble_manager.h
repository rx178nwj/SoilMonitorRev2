#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <time.h>
#include <stdint.h>
#include "host/ble_hs.h" // ble_gap_event のためにインクルード
#include "../plant_logic/plant_manager.h" // plant_profile_t のためにインクルード

/* --- Constants --- */

// バッファサイズ定数
#define BLE_RESPONSE_BUFFER_SIZE    256     // レスポンスバッファサイズ
#define BLE_DEVICE_NAME_MAX_LEN     32      // デバイス名最大長

/* --- Command and Response Data Structures --- */

// コマンドパケット
typedef struct __attribute__((packed)) {
    uint8_t command_id;     // コマンド識別子
    uint8_t sequence_num;   // シーケンス番号
    uint16_t data_length;   // データ長
    uint8_t data[];         // コマンドデータ
} ble_command_packet_t;

// レスポンスパケット
typedef struct __attribute__((packed)) {
    uint8_t response_id;    // レスポンス識別子
    uint8_t status_code;    // ステータスコード
    uint8_t sequence_num;   // 対応するシーケンス番号
    uint16_t data_length;   // レスポンスデータ長
    uint8_t data[];         // レスポンスデータ
} ble_response_packet_t;

// 時間指定リクエスト用構造体
typedef struct __attribute__((packed)) {
    struct tm requested_time; // 要求する時間
} time_data_request_t;

// 時間指定データ取得レスポンス用構造体
typedef struct __attribute__((packed)) {
    struct tm actual_time;    // 実際に見つかったデータの時間
    float temperature;        // 気温
    float humidity;           // 湿度
    float lux;                // 照度
    float soil_moisture;      // 土壌水分
} time_data_response_t;

// デバイス情報構造体
typedef struct __attribute__((packed)) {
    char device_name[32];
    char firmware_version[16];
    char hardware_version[16];
    uint32_t uptime_seconds;
    uint32_t total_sensor_readings;
} device_info_t;

// WiFi設定構造体
typedef struct __attribute__((packed)) {
    char ssid[32];          // SSID（NULL終端文字列）
    char password[64];      // パスワード（NULL終端文字列）
} wifi_config_data_t;

// システムステータス構造体（CMD_GET_SYSTEM_STATUS用）
typedef struct __attribute__((packed)) {
    uint32_t uptime_seconds;    // 稼働時間（秒）
    uint32_t heap_free;         // 空きヒープ（バイト）
    uint32_t heap_min;          // 最小空きヒープ（バイト）
    uint32_t task_count;        // タスク数
    uint32_t current_time;      // 現在時刻（UNIXタイムスタンプ、0の場合は時刻未設定）
    uint8_t wifi_connected;     // WiFi接続状態（0:未接続, 1:接続済み）
    uint8_t ble_connected;      // BLE接続状態（0:未接続, 1:接続済み）
    uint8_t padding[2];         // アライメント用パディング
} system_status_t;

/* --- Command and Response Enums --- */

typedef enum {
    CMD_GET_SENSOR_DATA = 0x01,     // 最新センサーデータ取得
    CMD_GET_SYSTEM_STATUS = 0x02,   // システム状態取得（メモリ使用量、稼働時間等）
    CMD_SET_PLANT_PROFILE = 0x03,   // 植物プロファイル設定
    CMD_GET_HISTORY_DATA = 0x04,    // 履歴データ取得
    CMD_SYSTEM_RESET = 0x05,        // システムリセット
    CMD_GET_DEVICE_INFO = 0x06,     // デバイス情報取得（名前、FWバージョン等）
    CMD_SET_TIME = 0x07,            // 時刻設定
    CMD_GET_CONFIG = 0x08,          // 設定取得
    CMD_SET_CONFIG = 0x09,          // 設定変更
    CMD_GET_TIME_DATA = 0x0A,       // 指定時間データ取得
    CMD_GET_SWITCH_STATUS = 0x0B,   // スイッチ状態取得
    CMD_GET_PLANT_PROFILE = 0x0C,   // 植物プロファイル取得
    CMD_SET_WIFI_CONFIG = 0x0D,     // WiFi設定（SSID/パスワード）
    CMD_GET_WIFI_CONFIG = 0x0E,     // WiFi設定取得
    CMD_WIFI_CONNECT = 0x0F,        // WiFi接続実行
    CMD_GET_TIMEZONE = 0x10,        // タイムゾーン取得
    CMD_SYNC_TIME = 0x11,           // インターネット時刻同期
    CMD_WIFI_DISCONNECT = 0x12,     // WiFi切断
    CMD_SAVE_WIFI_CONFIG = 0x13,    // WiFi設定をNVSに保存
    CMD_SAVE_PLANT_PROFILE = 0x14,  // 植物プロファイルをNVSに保存
    CMD_SET_TIMEZONE = 0x15,        // タイムゾーン設定
    CMD_SAVE_TIMEZONE = 0x16,       // タイムゾーン設定をNVSに保存
    CMD_GET_SENSOR_DATA_V2 = 0x17,  // 最新センサーデータ取得（土壌温度含む）
} ble_command_id_t;

typedef enum {
    RESP_STATUS_SUCCESS = 0x00,
    RESP_STATUS_ERROR = 0x01,
    RESP_STATUS_INVALID_COMMAND = 0x02,
    RESP_STATUS_INVALID_PARAMETER = 0x03,
    RESP_STATUS_BUSY = 0x04,
    RESP_STATUS_NOT_SUPPORTED = 0x05,
} ble_response_status_t;


/* --- Public Function Prototypes --- */

esp_err_t ble_manager_init(void);    // BLEマネージャー初期化
void ble_host_task(void *param); // BLEホストタスク
void print_ble_system_info(void); // BLEシステム情報を表示
void start_advertising(void);   // 広告開始

#endif // BLE_MANAGER_H