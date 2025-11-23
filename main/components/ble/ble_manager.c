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
#include "esp_mac.h"

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
#include "../../nvs_config.h"

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


/* --- GATT Service/Characteristic UUID Definitions --- */
static const ble_uuid128_t gatt_svr_svc_uuid =
    BLE_UUID128_INIT(0x2d, 0x71, 0xa2, 0x59, 0xb4, 0x58, 0xc8, 0x12,
                     0x99, 0x99, 0x43, 0x95, 0x12, 0x2f, 0x46, 0x59);

static const ble_uuid128_t gatt_svr_chr_uuid_sensor_data =
    BLE_UUID128_INIT(0x89, 0x67, 0x45, 0x23, 0xf1, 0xe0, 0x9d, 0x8c,
                     0x7b, 0x6a, 0x5f, 0x4e, 0x01, 0x2c, 0x3b, 0x6a);

static const ble_uuid128_t gatt_svr_chr_uuid_data_status =
    BLE_UUID128_INIT(0x90, 0x67, 0x45, 0x23, 0xf1, 0xe0, 0x9d, 0x8c,
                     0x7b, 0x6a, 0x5f, 0x4e, 0x1d, 0x2c, 0x3b, 0x6a);

static const ble_uuid128_t gatt_svr_chr_uuid_command =
    BLE_UUID128_INIT(0x91, 0x67, 0x45, 0x23, 0xf1, 0xe0, 0x9d, 0x8c,
                     0x7b, 0x6a, 0x5f, 0x4e, 0x1d, 0x2c, 0x3b, 0x6a);

static const ble_uuid128_t gatt_svr_chr_uuid_response =
    BLE_UUID128_INIT(0x92, 0x67, 0x45, 0x23, 0xf1, 0xe0, 0x9d, 0x8c,
                     0x7b, 0x6a, 0x5f, 0x4e, 0x1d, 0x2c, 0x3b, 0x6a);

static const ble_uuid128_t gatt_svr_chr_uuid_data_transfer =
    BLE_UUID128_INIT(0x93, 0x67, 0x45, 0x23, 0xf1, 0xe0, 0x9d, 0x8c,
                     0x7b, 0x6a, 0x5f, 0x4e, 0x1d, 0x2c, 0x3b, 0x6a);

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
    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR: {
        minute_data_t latest_data;
        esp_err_t ret = data_buffer_get_latest_minute_data(&latest_data);
        if (ret != ESP_OK) {
            return BLE_ATT_ERR_UNLIKELY;
        }

        soil_ble_data_t ble_data;
        ble_data.datetime.tm_sec = latest_data.timestamp.tm_sec;
        ble_data.datetime.tm_min = latest_data.timestamp.tm_min;
        ble_data.datetime.tm_hour = latest_data.timestamp.tm_hour;
        ble_data.datetime.tm_mday = latest_data.timestamp.tm_mday;
        ble_data.datetime.tm_mon = latest_data.timestamp.tm_mon;
        ble_data.datetime.tm_year = latest_data.timestamp.tm_year;
        ble_data.datetime.tm_wday = latest_data.timestamp.tm_wday;
        ble_data.datetime.tm_yday = latest_data.timestamp.tm_yday;
        ble_data.datetime.tm_isdst = latest_data.timestamp.tm_isdst;
        ble_data.temperature = latest_data.temperature;
        ble_data.humidity = latest_data.humidity;
        ble_data.lux = latest_data.lux;
        ble_data.soil_moisture = latest_data.soil_moisture;

        int rc = os_mbuf_append(ctxt->om, &ble_data, sizeof(ble_data));
        if (rc != 0) {
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        return 0;
    }
    default:
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }
}

static int gatt_svr_access_data_status_cb(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR: {
        data_buffer_stats_t stats;
        esp_err_t ret = data_buffer_get_stats(&stats);
        if (ret != ESP_OK) {
            return BLE_ATT_ERR_UNLIKELY;
        }

        ble_data_status_t status;
        status.count = stats.minute_data_count;
        status.capacity = DATA_BUFFER_MINUTES_PER_DAY;
        status.f_empty = (stats.minute_data_count == 0) ? 1 : 0;
        status.f_full = (stats.minute_data_count >= DATA_BUFFER_MINUTES_PER_DAY) ? 1 : 0;

        int rc = os_mbuf_append(ctxt->om, &status, sizeof(status));
        if (rc != 0) {
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        return 0;
    }
    default:
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }
}

static int gatt_svr_access_command_cb(uint16_t conn_handle, uint16_t attr_handle,
                                      struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }

    uint16_t data_len = OS_MBUF_PKTLEN(ctxt->om);
    if (g_command_processing) {
        return 0;
    }

    if (data_len < sizeof(ble_command_packet_t)) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    ble_command_packet_t *cmd_packet = (ble_command_packet_t *)ctxt->om->om_data;

    if (data_len != sizeof(ble_command_packet_t) + cmd_packet->data_length) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    g_command_processing = true;
    g_last_sequence_num = cmd_packet->sequence_num;

    uint8_t response_buffer[BLE_RESPONSE_BUFFER_SIZE];
    size_t response_length = 0;

    esp_err_t err = process_ble_command(cmd_packet, response_buffer, &response_length);
    if (err != ESP_OK) {
        ble_response_packet_t *resp = (ble_response_packet_t *)response_buffer;
        resp->response_id = cmd_packet->command_id;
        resp->status_code = RESP_STATUS_ERROR;
        resp->sequence_num = cmd_packet->sequence_num;
        resp->data_length = 0;
        response_length = sizeof(ble_response_packet_t);
    }

    send_response_notification(response_buffer, response_length);

    g_command_processing = false;
    return 0;
}

static int gatt_svr_access_response_cb(uint16_t conn_handle, uint16_t attr_handle,
                                       struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }
    return 0;
}

static int gatt_svr_access_data_transfer_cb(uint16_t conn_handle, uint16_t attr_handle,
                                            struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    return 0;
}

/* --- Command Processing Engine --- */
static esp_err_t process_ble_command(const ble_command_packet_t *cmd_packet,
                                     uint8_t *response_buffer, size_t *response_length)
{
    ESP_LOGI(TAG, "Processing command: ID=0x%02X", cmd_packet->command_id);
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
        case CMD_GET_SWITCH_STATUS: {
            uint8_t switch_state = switch_input_is_pressed();
            ble_response_packet_t *resp = (ble_response_packet_t *)response_buffer;
            resp->response_id = CMD_GET_SWITCH_STATUS;
            resp->status_code = RESP_STATUS_SUCCESS;
            resp->sequence_num = cmd_packet->sequence_num;
            resp->data_length = sizeof(switch_state);
            memcpy(resp->data, &switch_state, sizeof(switch_state));
            *response_length = sizeof(ble_response_packet_t) + sizeof(switch_state);
            err = ESP_OK;
            break;
        }
        default: {
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

static esp_err_t handle_get_sensor_data(uint8_t sequence_num, uint8_t *response_buffer, size_t *response_length)
{
    soil_data_t latest_data;
    minute_data_t minute_data;

    esp_err_t ret = data_buffer_get_latest_minute_data(&minute_data);
    if (ret != ESP_OK) {
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
        esp_err_t err = nvs_config_save_plant_profile(&profile);
        if (err == ESP_OK) {
            plant_manager_update_profile(&profile);
            resp->status_code = RESP_STATUS_SUCCESS;
        } else {
            resp->status_code = RESP_STATUS_ERROR;
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
    strncpy(info.hardware_version, HARDWARE_VERSION_STRING, sizeof(info.hardware_version) - 1);
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
        resp->status_code = RESP_STATUS_SUCCESS;
        resp->data_length = sizeof(time_data_response_t);
        memcpy(resp->data, &result_data, sizeof(time_data_response_t));
        *response_length = sizeof(ble_response_packet_t) + sizeof(time_data_response_t);
    } else {
        resp->status_code = RESP_STATUS_ERROR;
        resp->data_length = 0;
        *response_length = sizeof(ble_response_packet_t);
    }

    return ESP_OK;
}

static esp_err_t send_response_notification(const uint8_t *response_data, size_t response_length)
{
    if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE || !g_is_subscribed_response) {
        return ESP_FAIL;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(response_data, response_length);
    if (!om) {
        return ESP_ERR_NO_MEM;
    }

    int rc = ble_gatts_notify_custom(g_conn_handle, g_response_handle, om);
    if (rc == 0) {
        return ESP_OK;
    } else {
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
        memcpy(result, &found_data, sizeof(time_data_response_t));
        return ESP_OK;
    }
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
        if (event->subscribe.attr_handle == g_sensor_data_handle) {
            g_is_subscribed_sensor = (event->subscribe.cur_notify != 0);
        } else if (event->subscribe.attr_handle == g_response_handle) {
            g_is_subscribed_response = (event->subscribe.cur_notify != 0);
        } else if (event->subscribe.attr_handle == g_data_transfer_handle) {
            g_is_subscribed_data_transfer = (event->subscribe.cur_notify != 0);
        }
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU update event; conn_handle=%d cid=%d mtu=%d",
                 event->mtu.conn_handle, event->mtu.channel_id,
                 event->mtu.value);
        return 0;

    // --- 追加: ADV終了イベントのハンドリング ---
    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "Advertising complete; reason=%d", event->adv_complete.reason);
        // 必要であればここで再開処理を行う
        // start_advertising();
        return 0;
    }
    return 0;
}

void start_advertising(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    struct ble_hs_adv_fields scan_rsp_fields;
    int rc;

    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    const char *name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error setting advertisement data; rc=%d", rc);
        return;
    }

    memset(&scan_rsp_fields, 0, sizeof(scan_rsp_fields));
    scan_rsp_fields.uuids128 = (ble_uuid128_t[]){gatt_svr_svc_uuid};
    scan_rsp_fields.num_uuids128 = 1;
    scan_rsp_fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&scan_rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error setting scan response data; rc=%d", rc);
        return;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    
    rc = ble_gap_adv_start(g_own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, gap_event_handler, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error enabling advertisement; rc=%d", rc);
        return;
    }
    ESP_LOGI(TAG, "Advertising started");
}

static void on_sync(void)
{
    // --- 追加: IDアドレスの保証 ---
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to ensure address: %d", rc);
    }

    rc = ble_hs_id_infer_auto(0, &g_own_addr_type);
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

static esp_err_t generate_ble_device_name(char *device_name, size_t max_len)
{
    if (device_name == NULL || max_len < 20) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t mac[6];
    esp_err_t ret = esp_read_mac(mac, ESP_MAC_BT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read BLE MAC address: %s", esp_err_to_name(ret));
        snprintf(device_name, max_len, "PlantMonitor_%02d_0000", HARDWARE_VERSION);
        return ret;
    }

    uint16_t device_id = (mac[4] << 8) | mac[5];
    snprintf(device_name, max_len, "PlantMonitor_%02d_%04X", HARDWARE_VERSION, device_id);

    ESP_LOGI(TAG, "Generated BLE device name: %s (MAC: %02X:%02X:%02X:%02X:%02X:%02X)",
             device_name, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    return ESP_OK;
}

esp_err_t ble_manager_init(void)
{
    esp_err_t ret;

    // 1. NimBLEポート初期化
    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init nimble port: %d", ret);
        return ret;
    }

    // 2. Bluetooth Modem-sleep有効化
    //ret = esp_bt_sleep_enable();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ Bluetooth Modem-sleep enabled");
    } else {
        ESP_LOGW(TAG, "⚠️  Bluetooth Modem-sleep enable failed: %s (continuing anyway)", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "Initializing BLE Manager");
    
    // --- 追加: ストア設定の初期化 (必須) ---
    //ble_store_config_init();

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.gatts_register_cb = NULL;
    //ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 0;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;

    ESP_LOGI(TAG, "🔄 GATT services registration...");
    int rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to count GATT services: %d", rc);
        return ESP_FAIL;
    }
    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to add GATT services: %d", rc);
        return ESP_FAIL;
    }

    char ble_device_name[32];
    ret = generate_ble_device_name(ble_device_name, sizeof(ble_device_name));
    if (ret == ESP_OK) {
        rc = ble_svc_gap_device_name_set(ble_device_name);
        assert(rc == 0);
        ESP_LOGI(TAG, "✅ BLE device name set: %s", ble_device_name);
    } else {
        rc = ble_svc_gap_device_name_set(ble_device_name);
        assert(rc == 0);
        ESP_LOGW(TAG, "⚠️  Using fallback BLE device name: %s", ble_device_name);
    }

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