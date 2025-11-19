#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_pm.h"
#include "driver/gpio.h"
#include <esp_err.h>
#include "esp_system.h"
#include "esp_heap_caps.h"

/* NimBLE Includes */
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"
#include "esp_bt.h"

#include "ble_manager.h"
#include "../../common_types.h"
#include "../plant_logic/data_buffer.h"
#include "../../nvs_config.h" // nvs_config_save_plant_profile のためにインクルード

// 仮のデータバッファ (実際のプロジェクトに合わせてください)
extern soil_data_t data_buffer[24 * 60];

static const char *TAG = "BLE_MGR";

/* --- GATT Handles --- */
static uint16_t g_sensor_data_handle = 0;
static uint16_t g_data_status_handle = 0;
static uint16_t g_command_handle = 0;
static uint16_t g_response_handle = 0;
static uint16_t g_data_transfer_handle = 0;

static uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint8_t g_own_addr_type;

// 購読状態管理
static bool g_is_subscribed_sensor = false;
static bool g_is_subscribed_response = false;
static bool g_is_subscribed_data_transfer = false;

/* --- Command-Response System State --- */
static uint8_t g_last_sequence_num = 0;
static bool g_command_processing = false;
static uint32_t g_system_uptime = 0;
static uint32_t g_total_sensor_readings = 0;

/* --- Function Prototypes --- */
static int gap_event_handler(struct ble_gap_event *event, void *arg);
static void on_sync(void);
static void on_reset(int reason);

static esp_err_t process_ble_command(const ble_command_packet_t *cmd_packet, uint8_t *response_buffer, size_t *response_length);
static esp_err_t handle_get_sensor_data(uint8_t sequence_num, uint8_t *response_buffer, size_t *response_length);
static esp_err_t handle_get_system_status(uint8_t sequence_num, uint8_t *response_buffer, size_t *response_length);
static esp_err_t handle_set_plant_profile(const uint8_t *data, uint16_t data_length, uint8_t sequence_num, uint8_t *response_buffer, size_t *response_length);
static esp_err_t handle_get_device_info(uint8_t sequence_num, uint8_t *response_buffer, size_t *response_length);
static esp_err_t handle_get_time_data(const uint8_t *data, uint16_t data_length, uint8_t sequence_num, uint8_t *response_buffer, size_t *response_length);
static esp_err_t find_data_by_time(const struct tm *target_time, time_data_response_t *result);
static esp_err_t send_response_notification(const uint8_t *response_data, size_t response_length);

// Access Callback prototypes
static int gatt_svr_access_command_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
static int gatt_svr_access_response_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
static int gatt_svr_access_data_transfer_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
static int gatt_svr_access_sensor_data_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
static int gatt_svr_access_data_status_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);


/* --- GATT Service Definition --- */
// (UUID定義は変更なし)
// これらのUUID=59462f12-9543-9999-12c8-58b459a2712d
static const ble_uuid128_t gatt_svr_svc_uuid =
    BLE_UUID128_INIT(0x2d, 0x71, 0xa2, 0x59, 0xb4, 0x58, 0xc8, 0x12,
                     0x99, 0x99, 0x43, 0x95, 0x12, 0x2f, 0x46, 0x59);
static const ble_uuid128_t gatt_svr_chr_uuid_sensor_data =
    BLE_UUID128_INIT(0x89, 0x67, 0x45, 0x23, 0xf1, 0xe0, 0x9d, 0x8c,
                     0x7b, 0x6a, 0x5f, 0x4e, 0x01, 0x2c, 0x3b, 0x6a);
// Command-Response用のUUID定義
// これらのUUID=6a3b2c1d-4e5f-6a7b-8c9d-e0f123456790
static const ble_uuid128_t gatt_svr_chr_uuid_data_status =
    BLE_UUID128_INIT(0x90, 0x67, 0x45, 0x23, 0xf1, 0xe0, 0x9d, 0x8c,
                     0x7b, 0x6a, 0x5f, 0x4e, 0x1d, 0x2c, 0x3b, 0x6a);
// Command-Response用のUUID定義
// これらのUUID=6a3b2c1d-4e5f-6a7b-8c9d-e0f123456791
static const ble_uuid128_t gatt_svr_chr_uuid_command =
    BLE_UUID128_INIT(0x91, 0x67, 0x45, 0x23, 0xf1, 0xe0, 0x9d, 0x8c,
                     0x7b, 0x6a, 0x5f, 0x4e, 0x1d, 0x2c, 0x3b, 0x6a);
// Command-Response用のUUID定義
// これらのUUID=6a3b2c1d-4e5f-6a7b-8c9d-e0f123456792
static const ble_uuid128_t gatt_svr_chr_uuid_response =
    BLE_UUID128_INIT(0x92, 0x67, 0x45, 0x23, 0xf1, 0xe0, 0x9d, 0x8c,
                     0x7b, 0x6a, 0x5f, 0x4e, 0x1d, 0x2c, 0x3b, 0x6a);
// Command-Response用のUUID定義
// これらのUUID=6a3b2c1d-4e5f-6a7b-8c9d-e0f123456793
static const ble_uuid128_t gatt_svr_chr_uuid_data_transfer =
    BLE_UUID128_INIT(0x93, 0x67, 0x45, 0x23, 0xf1, 0xe0, 0x9d, 0x8c,
                     0x7b, 0x6a, 0x5f, 0x4e, 0x1d, 0x2c, 0x3b, 0x6a);

// (GATTサービス定義は変更なし)
static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &gatt_svr_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &gatt_svr_chr_uuid_sensor_data.u,
                .access_cb = gatt_svr_access_sensor_data_cb,
                .val_handle = &g_sensor_data_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                .uuid = &gatt_svr_chr_uuid_data_status.u,
                .access_cb = gatt_svr_access_data_status_cb,
                .val_handle = &g_data_status_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
            },
            {
                .uuid = &gatt_svr_chr_uuid_command.u,
                .access_cb = gatt_svr_access_command_cb,
                .val_handle = &g_command_handle,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = &gatt_svr_chr_uuid_response.u,
                .access_cb = gatt_svr_access_response_cb,
                .val_handle = &g_response_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                .uuid = &gatt_svr_chr_uuid_data_transfer.u,
                .access_cb = gatt_svr_access_data_transfer_cb,
                .val_handle = &g_data_transfer_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
            },
            {0}
        },
    },
    {0}
};

/* --- Access Callback Functions --- */
static int gatt_svr_access_sensor_data_cb(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    ESP_LOGI(TAG, "Sensor Data access");
    return 0;
}

static int gatt_svr_access_data_status_cb(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    ESP_LOGI(TAG, "Data Status access");
    return 0;
}

static int gatt_svr_access_command_cb(uint16_t conn_handle, uint16_t attr_handle,
                                      struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        ESP_LOGW(TAG, "Command CB: Invalid operation %d", ctxt->op);
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }

    uint16_t data_len = OS_MBUF_PKTLEN(ctxt->om);
    ESP_LOGI(TAG, "Command received: %d bytes", data_len);

    if (g_command_processing) {
        ESP_LOGW(TAG, "Command received while another is processing. Ignoring.");
        return 0;
    }

    if (data_len < sizeof(ble_command_packet_t)) {
        ESP_LOGE(TAG, "Invalid command packet size: %d", data_len);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    // FIX: OS_MBUF_DATAマクロの誤用を修正
    ble_command_packet_t *cmd_packet = (ble_command_packet_t *)ctxt->om->om_data;

    if (data_len != sizeof(ble_command_packet_t) + cmd_packet->data_length) {
        ESP_LOGE(TAG, "Command data_length mismatch. Expected %d, got %d",
                 (int)(sizeof(ble_command_packet_t) + cmd_packet->data_length), data_len);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    g_command_processing = true;
    g_last_sequence_num = cmd_packet->sequence_num;

    uint8_t response_buffer[256];
    size_t response_length = 0;

    esp_err_t err = process_ble_command(cmd_packet, response_buffer, &response_length);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to process command 0x%02X", cmd_packet->command_id);
        ble_response_packet_t *resp = (ble_response_packet_t *)response_buffer;
        resp->response_id = cmd_packet->command_id;
        resp->status_code = RESP_STATUS_ERROR;
        resp->sequence_num = cmd_packet->sequence_num;
        resp->data_length = 0;
        response_length = sizeof(ble_response_packet_t);
    }

    send_response_notification(response_buffer, response_length);

    g_command_processing = false;
    // FIX: 成功時の戻り値を追加し、未定義定数を修正
    return 0;
}

static int gatt_svr_access_response_cb(uint16_t conn_handle, uint16_t attr_handle,
                                       struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    ESP_LOGI(TAG, "Response characteristic accessed (op: %d)", ctxt->op);
    return 0;
}

static int gatt_svr_access_data_transfer_cb(uint16_t conn_handle, uint16_t attr_handle,
                                            struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    ESP_LOGI(TAG, "Data Transfer characteristic accessed (op: %d)", ctxt->op);
    return 0;
}

/* --- Command Processing Engine --- */
static esp_err_t process_ble_command(const ble_command_packet_t *cmd_packet,
                                     uint8_t *response_buffer, size_t *response_length)
{
    ESP_LOGI(TAG, "Processing command: ID=0x%02X, Seq=%d, Len=%d",
             cmd_packet->command_id, cmd_packet->sequence_num, cmd_packet->data_length);

    esp_err_t err = ESP_OK;

    switch (cmd_packet->command_id) {
        case CMD_GET_SENSOR_DATA:
            err = handle_get_sensor_data(cmd_packet->sequence_num, response_buffer, response_length);
            break;
        case CMD_GET_SYSTEM_STATUS:
            err = handle_get_system_status(cmd_packet->sequence_num, response_buffer, response_length);
            break;
        case CMD_SET_PLANT_PROFILE:
            err = handle_set_plant_profile(cmd_packet->data, cmd_packet->data_length, cmd_packet->sequence_num, response_buffer, response_length);
            break;
        case CMD_SYSTEM_RESET: {
            ble_response_packet_t *resp = (ble_response_packet_t *)response_buffer;
            resp->response_id = CMD_SYSTEM_RESET;
            resp->status_code = RESP_STATUS_SUCCESS;
            resp->sequence_num = cmd_packet->sequence_num;
            resp->data_length = 0;
            *response_length = sizeof(ble_response_packet_t);
            send_response_notification(response_buffer, *response_length);
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
            break;
        }
        case CMD_GET_DEVICE_INFO:
            err = handle_get_device_info(cmd_packet->sequence_num, response_buffer, response_length);
            break;
        case CMD_GET_TIME_DATA:
            err = handle_get_time_data(cmd_packet->data, cmd_packet->data_length, cmd_packet->sequence_num, response_buffer, response_length);
            break;
        case CMD_GET_SWITCH_STATUS:
            err = ESP_ERR_NOT_SUPPORTED;
            ble_response_packet_t *resp = (ble_response_packet_t *)response_buffer;
            resp->response_id = CMD_GET_SWITCH_STATUS;
            resp->status_code = RESP_STATUS_INVALID_COMMAND;
            resp->sequence_num = cmd_packet->sequence_num;
            resp->data_length = 0;

            uint8_t switch_state = 0; // 仮のスイッチ状態
            switch_state = switch_input_is_pressed();
            memcpy(resp->data, &switch_state, sizeof(switch_state));
            *response_length = sizeof(ble_response_packet_t) + sizeof(switch_state);
            err = ESP_OK;
            break;
        default: {
            ESP_LOGW(TAG, "Unknown command ID: 0x%02X", cmd_packet->command_id);
            ble_response_packet_t *resp = (ble_response_packet_t *)response_buffer;
            resp->response_id = cmd_packet->command_id;
            resp->status_code = RESP_STATUS_INVALID_COMMAND;
            resp->sequence_num = cmd_packet->sequence_num;
            resp->data_length = 0;
            *response_length = sizeof(ble_response_packet_t);
            err = ESP_FAIL;
            break;
        }
    }
    return err;
}

/* --- Command Handlers --- */
static esp_err_t handle_get_sensor_data(uint8_t sequence_num, uint8_t *response_buffer, size_t *response_length)
{
    soil_data_t latest_data;
    minute_data_t minute_data;

    esp_err_t ret = data_buffer_get_latest_minute_data(&minute_data);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get latest sensor data");
        ble_response_packet_t *resp = (ble_response_packet_t *)response_buffer;
        resp->response_id = CMD_GET_SENSOR_DATA;
        resp->status_code = RESP_STATUS_ERROR;
        resp->sequence_num = sequence_num;
        resp->data_length = 0;
        *response_length = sizeof(ble_response_packet_t);
        return ret;
    }
    g_total_sensor_readings++;

    latest_data.datetime = minute_data.timestamp;
    latest_data.lux = minute_data.lux;
    latest_data.temperature = minute_data.temperature;
    latest_data.humidity = minute_data.humidity;
    latest_data.soil_moisture = minute_data.soil_moisture;

    ble_response_packet_t *resp = (ble_response_packet_t *)response_buffer;
    resp->response_id = CMD_GET_SENSOR_DATA;
    resp->status_code = RESP_STATUS_SUCCESS;
    resp->sequence_num = sequence_num;
    resp->data_length = sizeof(soil_data_t);

    memcpy(resp->data, &latest_data, sizeof(soil_data_t));
    *response_length = sizeof(ble_response_packet_t) + sizeof(soil_data_t);

    return ESP_OK;
}

static esp_err_t handle_get_system_status(uint8_t sequence_num, uint8_t *response_buffer, size_t *response_length)
{
    size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t min_free_heap = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);

    char status_str[128];
    snprintf(status_str, sizeof(status_str), "Uptime: %lu s, Free Heap: %u, Min Free: %u",
             g_system_uptime, (unsigned int)free_heap, (unsigned int)min_free_heap);

    ble_response_packet_t *resp = (ble_response_packet_t *)response_buffer;
    resp->response_id = CMD_GET_SYSTEM_STATUS;
    resp->status_code = RESP_STATUS_SUCCESS;
    resp->sequence_num = sequence_num;
    resp->data_length = strlen(status_str);

    memcpy(resp->data, status_str, resp->data_length);
    *response_length = sizeof(ble_response_packet_t) + resp->data_length;

    return ESP_OK;
}

static esp_err_t handle_set_plant_profile(const uint8_t *data, uint16_t data_length, uint8_t sequence_num, uint8_t *response_buffer, size_t *response_length)
{
    ble_response_packet_t *resp = (ble_response_packet_t *)response_buffer;
    resp->response_id = CMD_SET_PLANT_PROFILE;
    resp->sequence_num = sequence_num;
    resp->data_length = 0;

    if (data_length != sizeof(plant_profile_t)) {
        resp->status_code = RESP_STATUS_INVALID_PARAMETER;
    } else {
        plant_profile_t profile;
        memcpy(&profile, data, sizeof(plant_profile_t));
        ESP_LOGI(TAG, "New plant profile received: %s", profile.plant_name);

        esp_err_t err = nvs_config_save_plant_profile(&profile);
        if (err == ESP_OK) {
            plant_manager_update_profile(&profile); // Update in-memory profile
            resp->status_code = RESP_STATUS_SUCCESS;
            ESP_LOGI(TAG, "Plant profile saved to NVS and updated successfully.");
        } else {
            resp->status_code = RESP_STATUS_ERROR;
            ESP_LOGE(TAG, "Failed to save plant profile to NVS.");
        }
    }

    *response_length = sizeof(ble_response_packet_t);
    return ESP_OK;
}

static esp_err_t handle_get_device_info(uint8_t sequence_num, uint8_t *response_buffer, size_t *response_length)
{
    device_info_t info;
    memset(&info, 0, sizeof(device_info_t));

    strncpy(info.device_name, APP_NAME, sizeof(info.device_name) - 1);
    strncpy(info.firmware_version, SOFTWARE_VERSION, sizeof(info.firmware_version) - 1);
    strncpy(info.hardware_version, HARDWARE_VERSION_STRING, sizeof(info.hardware_version) - 1); //修正箇所
    info.uptime_seconds = g_system_uptime;
    info.total_sensor_readings = g_total_sensor_readings;

    ble_response_packet_t *resp = (ble_response_packet_t *)response_buffer;
    resp->response_id = CMD_GET_DEVICE_INFO;
    resp->status_code = RESP_STATUS_SUCCESS;
    resp->sequence_num = sequence_num;
    resp->data_length = sizeof(device_info_t);

    memcpy(resp->data, &info, sizeof(device_info_t));
    *response_length = sizeof(ble_response_packet_t) + sizeof(device_info_t);

    return ESP_OK;
}

static esp_err_t handle_get_time_data(const uint8_t *data, uint16_t data_length,
                                      uint8_t sequence_num, uint8_t *response_buffer,
                                      size_t *response_length)
{
    ble_response_packet_t *resp = (ble_response_packet_t *)response_buffer;
    resp->response_id = CMD_GET_TIME_DATA;
    resp->sequence_num = sequence_num;

    if (data_length != sizeof(time_data_request_t)) {
        ESP_LOGE(TAG, "GetTimeData: Invalid data length %d", data_length);
        resp->status_code = RESP_STATUS_INVALID_PARAMETER;
        resp->data_length = 0;
        *response_length = sizeof(ble_response_packet_t);
        return ESP_FAIL;
    }

    const time_data_request_t *req = (const time_data_request_t *)data;
    time_data_response_t result_data;

    struct tm requested_time_aligned;
    memcpy(&requested_time_aligned, &req->requested_time, sizeof(struct tm));

    esp_err_t find_err = find_data_by_time(&requested_time_aligned, &result_data);

    if (find_err == ESP_OK) {
        ESP_LOGI(TAG, "GetTimeData: Data found for requested time.");
        resp->status_code = RESP_STATUS_SUCCESS;
        resp->data_length = sizeof(time_data_response_t);
        memcpy(resp->data, &result_data, sizeof(time_data_response_t));
        *response_length = sizeof(ble_response_packet_t) + sizeof(time_data_response_t);
    } else {
        ESP_LOGW(TAG, "GetTimeData: No data found for requested time.");
        resp->status_code = RESP_STATUS_ERROR;
        resp->data_length = 0;
        *response_length = sizeof(ble_response_packet_t);
    }

    return ESP_OK;
}

/* --- Helper Functions --- */
static esp_err_t send_response_notification(const uint8_t *response_data, size_t response_length)
{
    if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE || !g_is_subscribed_response) {
        ESP_LOGW(TAG, "Cannot send notification: No connection or not subscribed.");
        return ESP_FAIL;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(response_data, response_length);
    if (!om) {
        ESP_LOGE(TAG, "Failed to allocate mbuf for notification");
        return ESP_ERR_NO_MEM;
    }

    int rc = ble_gattc_notify_custom(g_conn_handle, g_response_handle, om);
    if (rc == 0) {
        ESP_LOGI(TAG, "Response notification sent successfully (%zu bytes)", response_length);
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Error sending response notification; rc=%d", rc);
        return ESP_FAIL;
    }
}

static esp_err_t find_data_by_time(const struct tm *target_time, time_data_response_t *result)
{
    esp_err_t err;

    if (!target_time || !result) {
        return ESP_ERR_INVALID_ARG;
    }

    minute_data_t found_data;
    err = data_buffer_get_minute_data(target_time, &found_data);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Data found in data_buffer for time: %04d-%02d-%02d %02d:%02d",
                 target_time->tm_year + 1900, target_time->tm_mon + 1, target_time->tm_mday,
                 target_time->tm_hour, target_time->tm_min);
        memcpy(result, &found_data, sizeof(time_data_response_t));
        return ESP_OK;
    } else if (err != ESP_ERR_NOT_FOUND) {
        ESP_LOGE(TAG, "Error retrieving data from data_buffer: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGW(TAG, "Data not found for the specified time.");
    return ESP_ERR_NOT_FOUND;
}


/* --- BLE Event Handlers --- */
static int gap_event_handler(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "Connection %s; status=%d",
                 event->connect.status == 0? "established" : "failed",
                 event->connect.status);
        if (event->connect.status == 0) {
            g_conn_handle = event->connect.conn_handle;
        } else {
            start_advertising();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnect; reason=%d", event->disconnect.reason);
        g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        g_is_subscribed_sensor = false;
        g_is_subscribed_response = false;
        g_is_subscribed_data_transfer = false;
        g_command_processing = false;
        start_advertising();
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "Subscribe event; conn_handle=%d attr_handle=%d cur_notify=%d",
                 event->subscribe.conn_handle, event->subscribe.attr_handle, event->subscribe.cur_notify);

        if (event->subscribe.attr_handle == g_sensor_data_handle) {
            g_is_subscribed_sensor = (event->subscribe.cur_notify != 0);
            ESP_LOGI(TAG, "Sensor data subscription %s.", g_is_subscribed_sensor ? "enabled" : "disabled");
        } else if (event->subscribe.attr_handle == g_response_handle) {
            g_is_subscribed_response = (event->subscribe.cur_notify != 0);
            ESP_LOGI(TAG, "Response subscription %s.", g_is_subscribed_response ? "enabled" : "disabled");
        } else if (event->subscribe.attr_handle == g_data_transfer_handle) {
            g_is_subscribed_data_transfer = (event->subscribe.cur_notify != 0);
            ESP_LOGI(TAG, "Data transfer subscription %s.", g_is_subscribed_data_transfer ? "enabled" : "disabled");
        }
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU update event; conn_handle=%d cid=%d mtu=%d",
                 event->mtu.conn_handle, event->mtu.channel_id,
                 event->mtu.value);
        return 0;
    }
    return 0;
}

void start_advertising(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    struct ble_hs_adv_fields scan_rsp_fields; // スキャンレスポンス用
    int rc;

    /* --- アドバタイズデータの設定 (31バイト以内) --- */
    memset(&fields, 0, sizeof(fields));

    // Flags (一般発見可能モード、BR/EDR非対応)
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    // 送信電力レベル
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    // デバイス名
    const char *name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error setting advertisement data; rc=%d", rc);
        return;
    }

    /* --- スキャンレスポンスデータの設定 (31バイト以内) --- */
    memset(&scan_rsp_fields, 0, sizeof(scan_rsp_fields));

    // 128-bit サービスUUID
    scan_rsp_fields.uuids128 = (ble_uuid128_t[]){gatt_svr_svc_uuid};
    scan_rsp_fields.num_uuids128 = 1;
    scan_rsp_fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&scan_rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error setting scan response data; rc=%d", rc);
        return;
    }

    /* --- アドバタイズ開始 --- */
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND; // 接続可能
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN; // 一般発見可能モード
    rc = ble_gap_adv_start(g_own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, gap_event_handler, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error enabling advertisement; rc=%d", rc);
        return;
    }
    ESP_LOGI(TAG, "Advertising started");
}

static void on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &g_own_addr_type);
    assert(rc == 0);
    start_advertising();
}

static void on_reset(int reason)
{
    ESP_LOGE(TAG, "Resetting state; reason=%d", reason);
}

void ble_host_task(void *param)
{
    ESP_LOGI(TAG, "BLE Host Task Started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t ble_manager_init(void)
{
    esp_err_t ret;

    // Bluetooth Modem-sleepを有効化（省電力モード）
    // これにより、BLEアドバタイジング中も接続可能で、自動的に必要時のみ復帰
    ret = esp_bt_sleep_enable();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ Bluetooth Modem-sleep enabled");
    } else {
        ESP_LOGW(TAG, "⚠️  Bluetooth Modem-sleep enable failed: %s (continuing anyway)", esp_err_to_name(ret));
    }

    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init nimble port: %d", ret);
        return ret;
    }

    ESP_LOGI(TAG, "Initializing BLE Manager");
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.gatts_register_cb = NULL;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 0;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;

    ESP_LOGI(TAG, "🔄 GATT services registration...");
    int rc = ble_gatts_count_cfg(gatt_svr_svcs);
    assert(rc == 0);
    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    assert(rc == 0);

    rc = ble_svc_gap_device_name_set("SoilMonitorV1");
    assert(rc == 0);

    return ESP_OK;
}

void print_ble_system_info(void)
{
    ESP_LOGI(TAG, "✅ BLE Command-Response System initialized");
    ESP_LOGI(TAG, "📡 Available commands:");
    ESP_LOGI(TAG, "  - 0x01: Get Sensor Data");
    ESP_LOGI(TAG, "  - 0x02: Get System Status");
    ESP_LOGI(TAG, "  - 0x03: Set Plant Profile");
    ESP_LOGI(TAG, "  - 0x05: System Reset");
    ESP_LOGI(TAG, "  - 0x06: Get Device Info");
    ESP_LOGI(TAG, "  - 0x0A: Get Time-Specific Data");
    ESP_LOGI(TAG, "📡 BLE Characteristics:");
    ESP_LOGI(TAG, "  - Command: Write commands to device");
    ESP_LOGI(TAG, "  - Response: Read/Notify for command responses");
    ESP_LOGI(TAG, "  - Data Transfer: Read/Write/Notify for large data");
}