#ifndef GPS_STORAGE_H
#define GPS_STORAGE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "gps_parser.h"

#define LOCATION_PAGE_SIZE 240U

esp_err_t storage_init(void);
bool append_location_record(const gps_ble_payload_t *payload);
uint32_t get_location_file_size(void);
bool read_location_page(uint32_t page_index, uint8_t *out_page);
bool delete_all_pages(void);

#endif
