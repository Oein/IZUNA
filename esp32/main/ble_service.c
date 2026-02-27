#include "ble_service.h"
#include "gps_storage.h"
#include "log_buffer.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "esp_gatt_common_api.h"

#define APP_ID_PLACEHOLDER 0
#define GATTS_SERVICE_UUID 0x00FF
#define GATTS_CHAR_UUID 0xFF01
#define GATTS_CMD_CHAR_UUID 0xFF02
#define GATTS_NUM_HANDLE 7
#define BLE_NOTIFY_PERIOD_MS 1000

#define IDX_SVC 0
#define IDX_DATA_CHAR_DECL 1
#define IDX_DATA_CHAR_VAL 2
#define IDX_DATA_CCCD 3
#define IDX_CMD_CHAR_DECL 4
#define IDX_CMD_CHAR_VAL 5
#define IDX_CMD_CCCD 6

static const char *TAG = "GEORECORD";
static const char device_name[] = "Georecord";

static volatile bool ble_connected = false;
static volatile bool ble_notify_enabled = false;
static volatile bool cmd_notify_enabled = false;
static uint16_t ble_conn_id = 0;
static esp_gatt_if_t ble_gatts_if = ESP_GATT_IF_NONE;
static uint16_t gatt_handle_table[GATTS_NUM_HANDLE];

static gps_ble_payload_t *s_gps_payload = NULL;
static portMUX_TYPE *s_gps_payload_mux = NULL;

static void send_command_response(const uint8_t *data, uint16_t len);
static void handle_command_request(const uint8_t *data, uint16_t len);
static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);

static const uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t character_declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t character_client_config_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
static const uint8_t char_property = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint16_t gatt_service_uuid = GATTS_SERVICE_UUID;
static const uint16_t gatt_data_char_uuid = GATTS_CHAR_UUID;
static const uint16_t gatt_cmd_char_uuid = GATTS_CMD_CHAR_UUID;
static uint8_t gatt_data_char_val[sizeof(gps_ble_payload_t)] = {0};
static uint8_t gatt_data_cccd[2] = {0x00, 0x00};
static uint8_t gatt_cmd_char_val[LOCATION_PAGE_SIZE] = {0};
static uint8_t gatt_cmd_cccd[2] = {0x00, 0x00};

static const esp_gatts_attr_db_t gatt_db[GATTS_NUM_HANDLE] = {
    [IDX_SVC] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&primary_service_uuid, ESP_GATT_PERM_READ,
         sizeof(uint16_t), sizeof(gatt_service_uuid), (uint8_t *)&gatt_service_uuid}},

    [IDX_DATA_CHAR_DECL] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, sizeof(uint8_t), sizeof(uint8_t), (uint8_t *)&char_property}},

    [IDX_DATA_CHAR_VAL] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *)&gatt_data_char_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(gatt_data_char_val), sizeof(gatt_data_char_val), gatt_data_char_val}},

    [IDX_DATA_CCCD] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(gatt_data_cccd), sizeof(gatt_data_cccd), gatt_data_cccd}},

    [IDX_CMD_CHAR_DECL] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, sizeof(uint8_t), sizeof(uint8_t), (uint8_t *)&char_property}},

    [IDX_CMD_CHAR_VAL] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *)&gatt_cmd_char_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, LOCATION_PAGE_SIZE, sizeof(gatt_cmd_char_val), gatt_cmd_char_val}},

    [IDX_CMD_CCCD] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(gatt_cmd_cccd), sizeof(gatt_cmd_cccd), gatt_cmd_cccd}},
};

static esp_ble_adv_params_t adv_params = {
    .adv_int_min = 0x20, // 20ms
    .adv_int_max = 0x20, // 20ms
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static uint8_t adv_raw_data[] = {
    0x02,
    ESP_BLE_AD_TYPE_FLAG,
    0x06,
    0x0A,
    ESP_BLE_AD_TYPE_NAME_CMPL,
    'G',
    'e',
    'o',
    'r',
    'e',
    'c',
    'o',
    'r',
    'd',
    0x03,
    ESP_BLE_AD_TYPE_16SRV_CMPL,
    (uint8_t)(GATTS_SERVICE_UUID & 0xFF),
    (uint8_t)((GATTS_SERVICE_UUID >> 8) & 0xFF),
    0x02,
    ESP_BLE_AD_TYPE_TX_PWR,
    0x09,
};

static void send_command_response(const uint8_t *data, uint16_t len)
{
    if (gatt_handle_table[IDX_CMD_CHAR_VAL] == 0 || ble_gatts_if == ESP_GATT_IF_NONE)
    {
        return;
    }

    esp_err_t ret = esp_ble_gatts_set_attr_value(gatt_handle_table[IDX_CMD_CHAR_VAL], len, data);
    if (ret != ESP_OK)
    {
        return;
    }

    if (ble_connected && cmd_notify_enabled)
    {
        esp_ble_gatts_send_indicate(ble_gatts_if, ble_conn_id, gatt_handle_table[IDX_CMD_CHAR_VAL], len, (uint8_t *)data, false);
    }
}

static void handle_command_request(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0)
    {
        return;
    }

    uint8_t response_240[LOCATION_PAGE_SIZE] = {0};
    uint8_t response_4[4] = {0};
    uint8_t response_1[1] = {0x00};

    switch (data[0])
    {
    case 0x00:
    {
        uint32_t file_size = get_location_file_size();
        response_4[0] = (uint8_t)(file_size & 0xFFU);
        response_4[1] = (uint8_t)((file_size >> 8) & 0xFFU);
        response_4[2] = (uint8_t)((file_size >> 16) & 0xFFU);
        response_4[3] = (uint8_t)((file_size >> 24) & 0xFFU);
        send_command_response(response_4, sizeof(response_4));
        break;
    }
    case 0x01:
    {
        if (len >= 5)
        {
            uint32_t page_index = ((uint32_t)data[1]) |
                                  (((uint32_t)data[2]) << 8) |
                                  (((uint32_t)data[3]) << 16) |
                                  (((uint32_t)data[4]) << 24);
            (void)read_location_page(page_index, response_240);
        }
        send_command_response(response_240, sizeof(response_240));
        break;
    }
    case 0x02:
    {
        response_1[0] = delete_all_pages() ? 0x01 : 0x00;
        send_command_response(response_1, sizeof(response_1));
        break;
    }
    case 0x03:
    {
        if (len >= 2 && data[1] < LOG_PAGE_COUNT)
        {
            log_buffer_read_page(data[1], response_240);
        }
        send_command_response(response_240, sizeof(response_240));
        break;
    }
    default:
        break;
    }
}

void ble_status_notify_task(void *arg)
{
    (void)arg;

    while (1)
    {
        if (ble_connected && ble_notify_enabled && ble_gatts_if != ESP_GATT_IF_NONE && gatt_handle_table[IDX_DATA_CHAR_VAL] != 0)
        {
            gps_ble_payload_t payload_copy = gps_payload_get(s_gps_payload, s_gps_payload_mux);

            uint16_t payload_len = (uint16_t)sizeof(payload_copy);
            esp_err_t ret = esp_ble_gatts_set_attr_value(gatt_handle_table[IDX_DATA_CHAR_VAL], payload_len, (const uint8_t *)&payload_copy);
            if (ret == ESP_OK)
            {
                esp_ble_gatts_send_indicate(ble_gatts_if, ble_conn_id, gatt_handle_table[IDX_DATA_CHAR_VAL], payload_len, (uint8_t *)&payload_copy, false);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(BLE_NOTIFY_PERIOD_MS));
    }
}

static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event)
    {
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        ESP_LOGI(TAG, "Advertising data set, status %d", param->adv_data_raw_cmpl.status);
        esp_ble_gap_start_advertising(&adv_params);
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGE(TAG, "Advertising start failed, status %d", param->adv_start_cmpl.status);
            break;
        }
        ESP_LOGI(TAG, "Advertising start successfully");
        break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGE(TAG, "Advertising stop failed, status %d", param->adv_stop_cmpl.status);
        }
        ESP_LOGI(TAG, "Advertising stop successfully");
        break;
    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
        ESP_LOGI(TAG, "Connection params update, status %d, conn_int %d, latency %d, timeout %d",
                 param->update_conn_params.status,
                 param->update_conn_params.conn_int,
                 param->update_conn_params.latency,
                 param->update_conn_params.timeout);
        break;
    default:
        break;
    }
}

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    switch (event)
    {
    case ESP_GATTS_REG_EVT:
        ESP_LOGI(TAG, "GATT server register, status %d, app_id %d", param->reg.status, param->reg.app_id);
        ble_gatts_if = gatts_if;
        esp_ble_gatts_create_attr_tab(gatt_db, gatts_if, GATTS_NUM_HANDLE, APP_ID_PLACEHOLDER);
        break;
    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
        if (param->add_attr_tab.status != ESP_GATT_OK)
        {
            ESP_LOGE(TAG, "Create attr table failed, error code=0x%x", param->add_attr_tab.status);
            break;
        }
        if (param->add_attr_tab.num_handle != GATTS_NUM_HANDLE)
        {
            ESP_LOGE(TAG, "Create attr table abnormally, num_handle (%d) doesn't match (%d)",
                     param->add_attr_tab.num_handle, GATTS_NUM_HANDLE);
            break;
        }
        memcpy(gatt_handle_table, param->add_attr_tab.handles, sizeof(gatt_handle_table));
        esp_ble_gatts_start_service(gatt_handle_table[IDX_SVC]);
        ESP_LOGI(TAG, "GATT service started, service_uuid=0x%04x, char_uuid=0x%04x",
                 GATTS_SERVICE_UUID, GATTS_CHAR_UUID);
        break;
    case ESP_GATTS_CONNECT_EVT:
    {
        esp_ble_conn_update_params_t conn_params = {0};
        memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
        conn_params.latency = 0;
        conn_params.max_int = 0x20;
        conn_params.min_int = 0x10;
        conn_params.timeout = 400;
        ESP_LOGI(TAG, "Connected, conn_id %u, remote " ESP_BD_ADDR_STR "",
                 param->connect.conn_id, ESP_BD_ADDR_HEX(param->connect.remote_bda));
        ble_connected = true;
        ble_notify_enabled = false;
        ble_conn_id = param->connect.conn_id;
        ble_gatts_if = gatts_if;
        esp_ble_gap_update_conn_params(&conn_params);
        break;
    }
    case ESP_GATTS_WRITE_EVT:
        if (param->write.handle == gatt_handle_table[IDX_DATA_CCCD] && param->write.len >= 2)
        {
            uint16_t cccd = param->write.value[1] << 8 | param->write.value[0];
            ble_notify_enabled = (cccd & 0x0001U) != 0U;
            ESP_LOGI(TAG, "CCCD updated: 0x%04x (notify=%s)", cccd, ble_notify_enabled ? "on" : "off");
        }

        if (param->write.handle == gatt_handle_table[IDX_CMD_CCCD] && param->write.len >= 2)
        {
            uint16_t cccd = param->write.value[1] << 8 | param->write.value[0];
            cmd_notify_enabled = (cccd & 0x0001U) != 0U;
        }

        if (param->write.handle == gatt_handle_table[IDX_CMD_CHAR_VAL] && !param->write.is_prep)
        {
            handle_command_request(param->write.value, param->write.len);
        }

        if (param->write.need_rsp)
        {
            esp_gatt_rsp_t rsp;
            memset(&rsp, 0, sizeof(esp_gatt_rsp_t));
            rsp.attr_value.handle = param->write.handle;
            rsp.attr_value.len = param->write.len;
            if (rsp.attr_value.len > ESP_GATT_MAX_ATTR_LEN)
            {
                rsp.attr_value.len = ESP_GATT_MAX_ATTR_LEN;
            }
            memcpy(rsp.attr_value.value, param->write.value, rsp.attr_value.len);
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, &rsp);
        }
        break;
    case ESP_GATTS_DISCONNECT_EVT:
        ESP_LOGI(TAG, "Disconnected, remote " ESP_BD_ADDR_STR ", reason 0x%02x",
                 ESP_BD_ADDR_HEX(param->disconnect.remote_bda), param->disconnect.reason);
        ble_connected = false;
        ble_notify_enabled = false;
        esp_ble_gap_start_advertising(&adv_params);
        break;
    default:
        break;
    }
}

esp_err_t ble_service_init(gps_ble_payload_t *payload, portMUX_TYPE *mux)
{
    s_gps_payload = payload;
    s_gps_payload_mux = mux;

    esp_err_t ret;

    ret = esp_ble_gap_register_callback(esp_gap_cb);
    if (ret)
    {
        ESP_LOGE(TAG, "%s gap register failed, error code = %x", __func__, ret);
        return ret;
    }

    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret)
    {
        ESP_LOGE(TAG, "%s gatts register failed, error code = %x", __func__, ret);
        return ret;
    }

    ret = esp_ble_gatts_app_register(APP_ID_PLACEHOLDER);
    if (ret)
    {
        ESP_LOGE(TAG, "%s gatts app register failed, error code = %x", __func__, ret);
        return ret;
    }

    ret = esp_ble_gatt_set_local_mtu(500);
    if (ret)
    {
        ESP_LOGE(TAG, "set local  MTU failed, error code = %x", ret);
        return ret;
    }

    ret = esp_ble_gap_set_device_name(device_name);
    if (ret)
    {
        ESP_LOGE(TAG, "set device name failed, error code = %x", ret);
        return ret;
    }

    ret = esp_ble_gap_config_adv_data_raw(adv_raw_data, sizeof(adv_raw_data));
    if (ret)
    {
        ESP_LOGE(TAG, "config adv data failed, error code = %x", ret);
    }

    return ESP_OK;
}
