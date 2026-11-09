#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_log.h"

/* Clock stub */
void host_time_set(uint64_t ms);
void host_time_real(bool on);  // on: the PC's monotonic clock in ms (the simulator); off: the set value

/* Log spy */
void host_log_clear(void);
int host_log_count(void);
const char* host_log_line(int i);
esp_log_level_t host_log_level(int i);
void host_log_echo(bool on);  // on: every line also printed to stderr as "E tag: text"
