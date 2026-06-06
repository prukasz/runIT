#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include "esp_log.h"

int rik_log_vprintf(const char *fmt, va_list args);

void sys_log_remote_enable(bool enable);
void sys_log_mirror_on_serial(bool enable);

void sys_log_set_level(esp_log_level_t level);