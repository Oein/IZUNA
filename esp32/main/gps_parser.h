#ifndef GPS_PARSER_H
#define GPS_PARSER_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/portmacro.h"

#define GPS_LINE_BUF_SIZE 128

typedef struct
{
    uint64_t compact_time;
    double latitude;
    double longitude;
} gps_ble_payload_t;

_Static_assert(sizeof(gps_ble_payload_t) == 24, "GPS BLE payload must be exactly 24 bytes");

bool parse_rmc_sentence(const char *line,
                        uint64_t *compact_time,
                        double *latitude,
                        double *longitude);

gps_ble_payload_t gps_payload_get(const gps_ble_payload_t *payload,
                                  portMUX_TYPE *mux);

#endif
