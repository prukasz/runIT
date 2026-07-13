#pragma once
#include "esp_log_level.h"
#include "status.h"

void sys_error_supress();
void sys_error_resume();
void sys_error_stream_config(bool stream_info, bool stream_warn, bool stream_crit);

// Log functions
int sys_log_vprintf(const char* fmt, va_list args);
void sys_log_set_level(esp_log_level_t level);
void sys_log_assign_ble_tx(uint16_t char_uuid, uint8_t buffer_id, bool mirror_on_serial);

// Initialization
void sys_error_handler_init(uint16_t error_char_uuid, uint8_t error_buffer_id, uint16_t log_char_uuid, uint8_t log_buffer_id);
