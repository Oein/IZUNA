#ifndef LOG_BUFFER_H
#define LOG_BUFFER_H

#include <stdint.h>

#define LOG_BUFFER_SIZE 3840U
#define LOG_PAGE_SIZE 240U
#define LOG_PAGE_COUNT (LOG_BUFFER_SIZE / LOG_PAGE_SIZE)

void log_buffer_init(void);
void log_buffer_read_page(uint8_t page, uint8_t *out);

#endif
