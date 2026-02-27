#include "gps_parser.h"
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"

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

bool parse_rmc_sentence(const char *line,
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

gps_ble_payload_t gps_payload_get(const gps_ble_payload_t *payload,
                                  portMUX_TYPE *mux)
{
    gps_ble_payload_t copy;
    portENTER_CRITICAL(mux);
    copy = *payload;
    portEXIT_CRITICAL(mux);
    return copy;
}
