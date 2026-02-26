/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_littlefs.h"

#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gatt_common_api.h"

#define APP_ID_PLACEHOLDER 0
#define GATTS_SERVICE_UUID 0x00FF
#define GATTS_CHAR_UUID 0xFF01
#define GATTS_CMD_CHAR_UUID 0xFF02
#define GATTS_NUM_HANDLE 7
#define GPS_UART_NUM UART_NUM_1
#define GPS_UART_BAUD_RATE 115200
#define GPS_UART_TX_PIN UART_PIN_NO_CHANGE
#define GPS_UART_RX_PIN GPIO_NUM_9
#define GPS_RX_BUF_SIZE 1024
#define GPS_LINE_BUF_SIZE 128
#define BLE_NOTIFY_PERIOD_MS 1000
#define GPS_SAVE_INTERVAL_SECONDS 30ULL
#define LOCATION_PAGE_SIZE 240U
#define LOCATION_MAX_PAGES 2048U
#define LOCATION_TRIM_PAGES 128U
#define LOCATION_MAX_BYTES (LOCATION_PAGE_SIZE * LOCATION_MAX_PAGES)
#define LOCATION_TRIM_BYTES (LOCATION_PAGE_SIZE * LOCATION_TRIM_PAGES)
#define LOCATION_FILE_PATH "/littlefs/location.bin"
#define LOCATION_TMP_FILE_PATH "/littlefs/location.tmp"

#define IDX_SVC 0
#define IDX_DATA_CHAR_DECL 1
#define IDX_DATA_CHAR_VAL 2
#define IDX_DATA_CCCD 3
#define IDX_CMD_CHAR_DECL 4
#define IDX_CMD_CHAR_VAL 5
#define IDX_CMD_CCCD 6

typedef struct
{
    uint64_t compact_time;
    double latitude;
    double longitude;
} gps_ble_payload_t;

_Static_assert(sizeof(gps_ble_payload_t) == 24, "GPS BLE payload must be exactly 24 bytes");

static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);
static void gps_uart_task(void *arg);
static void ble_status_notify_task(void *arg);
static esp_err_t littlefs_init(void);
static bool append_location_record(const gps_ble_payload_t *payload);
static bool compact_location_file_locked(void);
static uint32_t get_location_file_size(void);
static bool read_location_page(uint32_t page_index, uint8_t *out_page);
static bool delete_all_pages(void);
static void handle_command_request(const uint8_t *data, uint16_t len);
static void send_command_response(const uint8_t *data, uint16_t len);

static const char *CONN_TAG = "GEORECORD";
static const char *GPS_TAG = "GPS";
static const char device_name[] = "Georecord";
static volatile bool ble_connected = false;
static volatile bool ble_notify_enabled = false;
static volatile bool cmd_notify_enabled = false;
static uint16_t ble_conn_id = 0;
static esp_gatt_if_t ble_gatts_if = ESP_GATT_IF_NONE;
static uint16_t gatt_handle_table[GATTS_NUM_HANDLE];

static portMUX_TYPE gps_payload_mux = portMUX_INITIALIZER_UNLOCKED;
static gps_ble_payload_t gps_payload = {0};
static bool littlefs_ready = false;
static SemaphoreHandle_t storage_mutex = NULL;

static double nmea_to_decimal_degrees(const char *raw_coord, char hemisphere)
{
    if (raw_coord == NULL || raw_coord[0] == '\0')
    {
        return 0.0;
    }

    double coord = atof(raw_coord);
    int degrees = (int)(coord / 100.0);
    double minutes = coord - ((double)degrees * 100.0);
    double decimal = (double)degrees + (minutes / 60.0);

    if (hemisphere == 'S' || hemisphere == 'W')
    {
        decimal = -decimal;
    }

    return decimal;
}

static int days_in_month(int year, int month)
{
    switch (month)
    {
    case 1:
    case 3:
    case 5:
    case 7:
    case 8:
    case 10:
    case 12:
        return 31;
    case 4:
    case 6:
    case 9:
    case 11:
        return 30;
    case 2:
    {
        bool leap = ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    default:
        return 30;
    }
}

static uint64_t utc_to_kst_compact_time(const char *raw_date, const char *raw_time)
{
    int day = (raw_date[0] - '0') * 10 + (raw_date[1] - '0');
    int month = (raw_date[2] - '0') * 10 + (raw_date[3] - '0');
    int year = 2000 + (raw_date[4] - '0') * 10 + (raw_date[5] - '0');
    int hour = (raw_time[0] - '0') * 10 + (raw_time[1] - '0');
    int minute = (raw_time[2] - '0') * 10 + (raw_time[3] - '0');
    int second = (raw_time[4] - '0') * 10 + (raw_time[5] - '0');

    hour += 9;
    while (hour >= 24)
    {
        hour -= 24;
        day += 1;

        if (day > days_in_month(year, month))
        {
            day = 1;
            month += 1;
            if (month > 12)
            {
                month = 1;
                year += 1;
            }
        }
    }

    return ((uint64_t)year * 10000000000ULL) +
           ((uint64_t)month * 100000000ULL) +
           ((uint64_t)day * 1000000ULL) +
           ((uint64_t)hour * 10000ULL) +
           ((uint64_t)minute * 100ULL) +
           (uint64_t)second;
}

static void nmea_remove_checksum(char *field)
{
    if (field == NULL)
    {
        return;
    }

    char *asterisk = strchr(field, '*');
    if (asterisk != NULL)
    {
        *asterisk = '\0';
    }
}

static bool parse_rmc_sentence(const char *line,
                               uint64_t *compact_time,
                               double *latitude,
                               double *longitude)
{
    if (line == NULL || compact_time == NULL || latitude == NULL || longitude == NULL)
    {
        return false;
    }

    if (strncmp(line, "$GPRMC", 6) != 0 && strncmp(line, "$GNRMC", 6) != 0)
    {
        return false;
    }

    char sentence[GPS_LINE_BUF_SIZE];
    strncpy(sentence, line, sizeof(sentence) - 1);
    sentence[sizeof(sentence) - 1] = '\0';

    char *save_ptr = NULL;
    char *token = NULL;
    int field_index = 0;
    char raw_time[16] = {0};
    char status = 'V';
    char raw_lat[16] = {0};
    char ns = 'N';
    char raw_lon[16] = {0};
    char ew = 'E';
    char raw_date[16] = {0};

    for (token = strtok_r(sentence, ",", &save_ptr); token != NULL; token = strtok_r(NULL, ",", &save_ptr))
    {
        nmea_remove_checksum(token);

        switch (field_index)
        {
        case 1:
            strncpy(raw_time, token, sizeof(raw_time) - 1);
            break;
        case 2:
            status = token[0];
            break;
        case 3:
            strncpy(raw_lat, token, sizeof(raw_lat) - 1);
            break;
        case 4:
            ns = token[0];
            break;
        case 5:
            strncpy(raw_lon, token, sizeof(raw_lon) - 1);
            break;
        case 6:
            ew = token[0];
            break;
        case 9:
            strncpy(raw_date, token, sizeof(raw_date) - 1);
            break;
        default:
            break;
        }

        field_index++;
    }

    if (status != 'A')
    {
        return false;
    }

    if (strlen(raw_time) < 6 || strlen(raw_date) < 6 || raw_lat[0] == '\0' || raw_lon[0] == '\0')
    {
        return false;
    }

    *compact_time = utc_to_kst_compact_time(raw_date, raw_time);

    *latitude = nmea_to_decimal_degrees(raw_lat, ns);
    *longitude = nmea_to_decimal_degrees(raw_lon, ew);

    return true;
}

static esp_err_t littlefs_init(void)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/littlefs",
        .partition_label = "storage",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK)
    {
        ESP_LOGE(GPS_TAG, "Failed to mount LittleFS: %s", esp_err_to_name(ret));
        return ret;
    }

    if (storage_mutex == NULL)
    {
        storage_mutex = xSemaphoreCreateMutex();
        if (storage_mutex == NULL)
        {
            esp_vfs_littlefs_unregister(conf.partition_label);
            return ESP_ERR_NO_MEM;
        }
    }

    littlefs_ready = true;
    return ESP_OK;
}

static bool append_location_record(const gps_ble_payload_t *payload)
{
    if (!littlefs_ready || payload == NULL || storage_mutex == NULL)
    {
        return false;
    }

    if (xSemaphoreTake(storage_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        return false;
    }

    uint32_t file_size = 0;
    FILE *file = fopen(LOCATION_FILE_PATH, "rb");
    if (file != NULL)
    {
        if (fseek(file, 0, SEEK_END) == 0)
        {
            long size = ftell(file);
            if (size > 0)
            {
                file_size = (uint32_t)size;
            }
        }
        fclose(file);
    }

    while (file_size + sizeof(*payload) > LOCATION_MAX_BYTES)
    {
        if (!compact_location_file_locked())
        {
            xSemaphoreGive(storage_mutex);
            return false;
        }

        file = fopen(LOCATION_FILE_PATH, "rb");
        file_size = 0;
        if (file != NULL)
        {
            if (fseek(file, 0, SEEK_END) == 0)
            {
                long size = ftell(file);
                if (size > 0)
                {
                    file_size = (uint32_t)size;
                }
            }
            fclose(file);
        }
    }

    file = fopen(LOCATION_FILE_PATH, "ab");
    if (file == NULL)
    {
        xSemaphoreGive(storage_mutex);
        return false;
    }

    size_t written = fwrite(payload, 1, sizeof(*payload), file);
    fclose(file);
    xSemaphoreGive(storage_mutex);

    return written == sizeof(*payload);
}

static bool compact_location_file_locked(void)
{
    FILE *src = fopen(LOCATION_FILE_PATH, "rb");
    if (src == NULL)
    {
        return true;
    }

    if (fseek(src, 0, SEEK_END) != 0)
    {
        fclose(src);
        return false;
    }

    long size = ftell(src);
    if (size < 0)
    {
        fclose(src);
        return false;
    }

    if ((uint32_t)size <= LOCATION_TRIM_BYTES)
    {
        fclose(src);
        int rm_result = remove(LOCATION_FILE_PATH);
        return (rm_result == 0 || errno == ENOENT);
    }

    if (fseek(src, LOCATION_TRIM_BYTES, SEEK_SET) != 0)
    {
        fclose(src);
        return false;
    }

    FILE *dst = fopen(LOCATION_TMP_FILE_PATH, "wb");
    if (dst == NULL)
    {
        fclose(src);
        return false;
    }

    uint8_t buffer[512];
    size_t read_bytes = 0;
    bool ok = true;

    while ((read_bytes = fread(buffer, 1, sizeof(buffer), src)) > 0)
    {
        if (fwrite(buffer, 1, read_bytes, dst) != read_bytes)
        {
            ok = false;
            break;
        }
    }

    if (ferror(src) != 0)
    {
        ok = false;
    }

    fclose(src);
    fclose(dst);

    if (!ok)
    {
        (void)remove(LOCATION_TMP_FILE_PATH);
        return false;
    }

    if (remove(LOCATION_FILE_PATH) != 0)
    {
        (void)remove(LOCATION_TMP_FILE_PATH);
        return false;
    }

    if (rename(LOCATION_TMP_FILE_PATH, LOCATION_FILE_PATH) != 0)
    {
        (void)remove(LOCATION_TMP_FILE_PATH);
        return false;
    }

    return true;
}

static uint32_t get_location_file_size(void)
{
    uint32_t file_size = 0;

    if (!littlefs_ready || storage_mutex == NULL)
    {
        return 0;
    }

    if (xSemaphoreTake(storage_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        return 0;
    }

    FILE *file = fopen(LOCATION_FILE_PATH, "rb");
    if (file != NULL)
    {
        if (fseek(file, 0, SEEK_END) == 0)
        {
            long size = ftell(file);
            if (size > 0)
            {
                file_size = (uint32_t)size;
            }
        }
        fclose(file);
    }

    xSemaphoreGive(storage_mutex);
    return file_size;
}

static bool read_location_page(uint32_t page_index, uint8_t *out_page)
{
    if (!littlefs_ready || out_page == NULL || storage_mutex == NULL)
    {
        return false;
    }

    memset(out_page, 0, LOCATION_PAGE_SIZE);

    if (xSemaphoreTake(storage_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        return false;
    }

    FILE *file = fopen(LOCATION_FILE_PATH, "rb");
    if (file == NULL)
    {
        xSemaphoreGive(storage_mutex);
        return true;
    }

    uint32_t offset = page_index * LOCATION_PAGE_SIZE;
    if (fseek(file, (long)offset, SEEK_SET) == 0)
    {
        (void)fread(out_page, 1, LOCATION_PAGE_SIZE, file);
    }

    fclose(file);
    xSemaphoreGive(storage_mutex);
    return true;
}

static bool delete_all_pages(void)
{
    if (!littlefs_ready || storage_mutex == NULL)
    {
        return false;
    }

    if (xSemaphoreTake(storage_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        return false;
    }

    int result = remove(LOCATION_FILE_PATH);
    if (result != 0 && errno != ENOENT)
    {
        xSemaphoreGive(storage_mutex);
        return false;
    }

    xSemaphoreGive(storage_mutex);
    return true;
}

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
    default:
        break;
    }
}

static void gps_uart_task(void *arg)
{
    (void)arg;

    uart_config_t uart_config = {
        .baud_rate = GPS_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret;

    ret = uart_driver_install(GPS_UART_NUM, GPS_RX_BUF_SIZE * 2, 0, 0, NULL, 0);
    if (ret != ESP_OK)
    {
        ESP_LOGE(GPS_TAG, "uart_driver_install failed: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    ret = uart_param_config(GPS_UART_NUM, &uart_config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(GPS_TAG, "uart_param_config failed: %s", esp_err_to_name(ret));
        uart_driver_delete(GPS_UART_NUM);
        vTaskDelete(NULL);
        return;
    }

    ret = uart_set_pin(GPS_UART_NUM, GPS_UART_TX_PIN, GPS_UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK)
    {
        ESP_LOGE(GPS_TAG, "uart_set_pin failed: %s", esp_err_to_name(ret));
        uart_driver_delete(GPS_UART_NUM);
        vTaskDelete(NULL);
        return;
    }

    uint8_t *rx_data = (uint8_t *)malloc(GPS_RX_BUF_SIZE);
    if (rx_data == NULL)
    {
        ESP_LOGE(GPS_TAG, "Failed to allocate UART RX buffer");
        vTaskDelete(NULL);
        return;
    }

    char line[GPS_LINE_BUF_SIZE] = {0};
    size_t line_index = 0;
    uint64_t last_saved_compact_time = 0;
    while (1)
    {
        int len = uart_read_bytes(GPS_UART_NUM, rx_data, GPS_RX_BUF_SIZE, pdMS_TO_TICKS(1000));
        if (len <= 0)
        {
            continue;
        }

        for (int i = 0; i < len; i++)
        {
            char c = (char)rx_data[i];

            if (c == '\n')
            {
                line[line_index] = '\0';

                uint64_t compact_time = 0;
                double latitude = 0.0;
                double longitude = 0.0;

                if (parse_rmc_sentence(line, &compact_time,
                                       &latitude, &longitude))
                {
                    portENTER_CRITICAL(&gps_payload_mux);
                    gps_payload.compact_time = compact_time;
                    gps_payload.latitude = latitude;
                    gps_payload.longitude = longitude;
                    portEXIT_CRITICAL(&gps_payload_mux);

                    if ((last_saved_compact_time == 0) || (compact_time >= (last_saved_compact_time + GPS_SAVE_INTERVAL_SECONDS)))
                    {
                        gps_ble_payload_t payload_to_store = {
                            .compact_time = compact_time,
                            .latitude = latitude,
                            .longitude = longitude,
                        };

                        if (append_location_record(&payload_to_store))
                        {
                            last_saved_compact_time = compact_time;
                        }
                    }
                }

                line_index = 0;
            }
            else if (c != '\r')
            {
                if (line_index < (sizeof(line) - 1))
                {
                    line[line_index++] = c;
                }
                else
                {
                    line_index = 0;
                }
            }
        }
    }
}

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

static void ble_status_notify_task(void *arg)
{
    (void)arg;

    while (1)
    {
        if (ble_connected && ble_notify_enabled && ble_gatts_if != ESP_GATT_IF_NONE && gatt_handle_table[IDX_DATA_CHAR_VAL] != 0)
        {
            gps_ble_payload_t payload_copy;
            portENTER_CRITICAL(&gps_payload_mux);
            payload_copy = gps_payload;
            portEXIT_CRITICAL(&gps_payload_mux);

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

void app_main(void)
{
    esp_err_t ret;

    // initialize NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret)
    {
        ESP_LOGE(CONN_TAG, "%s initialize controller failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret)
    {
        ESP_LOGE(CONN_TAG, "%s enable controller failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_init();
    if (ret)
    {
        ESP_LOGE(CONN_TAG, "%s init bluetooth failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_enable();
    if (ret)
    {
        ESP_LOGE(CONN_TAG, "%s enable bluetooth failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_ble_gap_register_callback(esp_gap_cb);
    if (ret)
    {
        ESP_LOGE(CONN_TAG, "%s gap register failed, error code = %x", __func__, ret);
        return;
    }

    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret)
    {
        ESP_LOGE(CONN_TAG, "%s gatts register failed, error code = %x", __func__, ret);
        return;
    }

    ret = esp_ble_gatts_app_register(APP_ID_PLACEHOLDER);
    if (ret)
    {
        ESP_LOGE(CONN_TAG, "%s gatts app register failed, error code = %x", __func__, ret);
        return;
    }

    ret = esp_ble_gatt_set_local_mtu(500);
    if (ret)
    {
        ESP_LOGE(CONN_TAG, "set local  MTU failed, error code = %x", ret);
        return;
    }

    ret = esp_ble_gap_set_device_name(device_name);
    if (ret)
    {
        ESP_LOGE(CONN_TAG, "set device name failed, error code = %x", ret);
        return;
    }

    ret = esp_ble_gap_config_adv_data_raw(adv_raw_data, sizeof(adv_raw_data));
    if (ret)
    {
        ESP_LOGE(CONN_TAG, "config adv data failed, error code = %x", ret);
    }

    ret = littlefs_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(CONN_TAG, "LittleFS init failed, storage commands disabled");
    }

    xTaskCreate(gps_uart_task, "gps_uart_task", 4096, NULL, 5, NULL);
    xTaskCreate(ble_status_notify_task, "ble_status_notify_task", 3072, NULL, 5, NULL);
}

static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event)
    {
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        ESP_LOGI(CONN_TAG, "Advertising data set, status %d", param->adv_data_raw_cmpl.status);
        esp_ble_gap_start_advertising(&adv_params);
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGE(CONN_TAG, "Advertising start failed, status %d", param->adv_start_cmpl.status);
            break;
        }
        ESP_LOGI(CONN_TAG, "Advertising start successfully");
        break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGE(CONN_TAG, "Advertising stop failed, status %d", param->adv_stop_cmpl.status);
        }
        ESP_LOGI(CONN_TAG, "Advertising stop successfully");
        break;
    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
        ESP_LOGI(CONN_TAG, "Connection params update, status %d, conn_int %d, latency %d, timeout %d",
                 param->update_conn_params.status,
                 param->update_conn_params.conn_int,
                 param->update_conn_params.latency,
                 param->update_conn_params.timeout);
        break;
    default:
        break;
    }
}

// because we only have one profile table in this demo, there is only one set of gatts evemt handler
static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    switch (event)
    {
    case ESP_GATTS_REG_EVT:
        ESP_LOGI(CONN_TAG, "GATT server register, status %d, app_id %d", param->reg.status, param->reg.app_id);
        ble_gatts_if = gatts_if;
        esp_ble_gatts_create_attr_tab(gatt_db, gatts_if, GATTS_NUM_HANDLE, APP_ID_PLACEHOLDER);
        break;
    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
        if (param->add_attr_tab.status != ESP_GATT_OK)
        {
            ESP_LOGE(CONN_TAG, "Create attr table failed, error code=0x%x", param->add_attr_tab.status);
            break;
        }
        if (param->add_attr_tab.num_handle != GATTS_NUM_HANDLE)
        {
            ESP_LOGE(CONN_TAG, "Create attr table abnormally, num_handle (%d) doesn't match (%d)",
                     param->add_attr_tab.num_handle, GATTS_NUM_HANDLE);
            break;
        }
        memcpy(gatt_handle_table, param->add_attr_tab.handles, sizeof(gatt_handle_table));
        esp_ble_gatts_start_service(gatt_handle_table[IDX_SVC]);
        ESP_LOGI(CONN_TAG, "GATT service started, service_uuid=0x%04x, char_uuid=0x%04x",
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
        ESP_LOGI(CONN_TAG, "Connected, conn_id %u, remote " ESP_BD_ADDR_STR "",
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
            ESP_LOGI(CONN_TAG, "CCCD updated: 0x%04x (notify=%s)", cccd, ble_notify_enabled ? "on" : "off");
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
        ESP_LOGI(CONN_TAG, "Disconnected, remote " ESP_BD_ADDR_STR ", reason 0x%02x",
                 ESP_BD_ADDR_HEX(param->disconnect.remote_bda), param->disconnect.reason);
        ble_connected = false;
        ble_notify_enabled = false;
        esp_ble_gap_start_advertising(&adv_params);
        break;
    default:
        break;
    }
}
