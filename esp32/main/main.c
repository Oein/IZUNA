/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_bt_main.h"

#include "gps_parser.h"
#include "gps_storage.h"
#include "ble_service.h"
#include "log_buffer.h"

#define GPS_UART_NUM UART_NUM_1
#define GPS_UART_BAUD_RATE 115200
#define GPS_UART_TX_PIN UART_PIN_NO_CHANGE
#define GPS_UART_RX_PIN GPIO_NUM_9
#define GPS_RX_BUF_SIZE 1024
#define GPS_SAVE_INTERVAL_SECONDS 30ULL

static const char *CONN_TAG = "GEORECORD";
static const char *GPS_TAG = "GPS";

static portMUX_TYPE gps_payload_mux = portMUX_INITIALIZER_UNLOCKED;
static gps_ble_payload_t gps_payload = {0};

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

void app_main(void)
{
    esp_err_t ret;

    log_buffer_init();

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

    ret = ble_service_init(&gps_payload, &gps_payload_mux);
    if (ret)
    {
        return;
    }

    ret = storage_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(CONN_TAG, "LittleFS init failed, storage commands disabled");
    }

    xTaskCreate(gps_uart_task, "gps_uart_task", 4096, NULL, 5, NULL);
    xTaskCreate(ble_status_notify_task, "ble_status_notify_task", 3072, NULL, 5, NULL);
}
