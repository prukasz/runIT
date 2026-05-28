#pragma once

int rik_log_vprintf(const char *fmt, va_list args);

void rik_log_remote_enable(bool enable);
void rik_enable_log_mirroring(bool enable);