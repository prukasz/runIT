#pragma once
#include <stdint.h>
#include "esp_log.h"

/* Clock stub */
void host_time_set(uint64_t ms);

/* Log spy */
void host_log_clear(void);
int host_log_count(void);
const char* host_log_line(int i);
esp_log_level_t host_log_level(int i);
