#ifndef BLE_SERVICE_H
#define BLE_SERVICE_H

#include "esp_err.h"
#include "gps_parser.h"

esp_err_t ble_service_init(gps_ble_payload_t *payload, portMUX_TYPE *mux);
void ble_status_notify_task(void *arg);

#endif
