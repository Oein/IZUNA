#include "log_buffer.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"

static uint8_t ring_buf[LOG_BUFFER_SIZE];
static size_t write_pos = 0;
static bool wrapped = false;
static portMUX_TYPE buf_mux = portMUX_INITIALIZER_UNLOCKED;
static vprintf_like_t s_orig_vprintf = NULL;

static int log_vprintf(const char *fmt, va_list args)
{
    char tmp[256];
    va_list args_copy;
    va_copy(args_copy, args);
    int len = vsnprintf(tmp, sizeof(tmp), fmt, args_copy);
    va_end(args_copy);

    if (len > 0)
    {
        if (len > (int)sizeof(tmp) - 1)
        {
            len = (int)sizeof(tmp) - 1;
        }

        portENTER_CRITICAL(&buf_mux);
        for (int i = 0; i < len; i++)
        {
            ring_buf[write_pos] = (uint8_t)tmp[i];
            write_pos++;
            if (write_pos >= LOG_BUFFER_SIZE)
            {
                write_pos = 0;
                wrapped = true;
            }
        }
        portEXIT_CRITICAL(&buf_mux);
    }

    if (s_orig_vprintf)
    {
        return s_orig_vprintf(fmt, args);
    }
    return len;
}

void log_buffer_init(void)
{
    memset(ring_buf, 0, sizeof(ring_buf));
    write_pos = 0;
    wrapped = false;
    s_orig_vprintf = esp_log_set_vprintf(log_vprintf);
}

void log_buffer_read_page(uint8_t page, uint8_t *out)
{
    if (page >= LOG_PAGE_COUNT || out == NULL)
    {
        if (out)
        {
            memset(out, 0, LOG_PAGE_SIZE);
        }
        return;
    }

    portENTER_CRITICAL(&buf_mux);
    if (!wrapped)
    {
        size_t offset = (size_t)page * LOG_PAGE_SIZE;
        if (offset < write_pos)
        {
            size_t avail = write_pos - offset;
            if (avail > LOG_PAGE_SIZE)
            {
                avail = LOG_PAGE_SIZE;
            }
            memcpy(out, &ring_buf[offset], avail);
            if (avail < LOG_PAGE_SIZE)
            {
                memset(out + avail, 0, LOG_PAGE_SIZE - avail);
            }
        }
        else
        {
            memset(out, 0, LOG_PAGE_SIZE);
        }
    }
    else
    {
        size_t start = (write_pos + (size_t)page * LOG_PAGE_SIZE) % LOG_BUFFER_SIZE;
        size_t first_chunk = LOG_BUFFER_SIZE - start;
        if (first_chunk >= LOG_PAGE_SIZE)
        {
            memcpy(out, &ring_buf[start], LOG_PAGE_SIZE);
        }
        else
        {
            memcpy(out, &ring_buf[start], first_chunk);
            memcpy(out + first_chunk, &ring_buf[0], LOG_PAGE_SIZE - first_chunk);
        }
    }
    portEXIT_CRITICAL(&buf_mux);
}
