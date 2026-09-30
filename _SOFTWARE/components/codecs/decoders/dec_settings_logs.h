#pragma once
/**
 * @file dec_settings_logs.h
 * @brief Runtime logging settings packets.
 *
 * Wire format: [class] [packet] [payload].
 */

#include <sdkconfig.h>
#include "sys_error.h"
#include "sys_error_log.h"
#include "sys_interface.h"

//@settings logs @title Logging settings @description Configure runtime log verbosity, serial mirroring, and error-chain tracing.
//@settings-class SETTINGS_LOGS

#undef OWNER
#define OWNER OWNER_DEC_SETTINGS_LOGS

#define HEADER_packet_settings_logs_set_t 0x01
typedef struct __packed {
  uint8_t level;         //@required @alias Log Level
  uint8_t mirror_serial; //@required @alias Mirror to Serial
  uint8_t trace_errors;  //@required @alias Trace Error Chains
} packet_settings_logs_set_t;

static inline SE_MUST_USE err_h dec_settings_logs_decode(const uint8_t* data, size_t len) {
  if (len == 0) {
    SE_FAIL(ERR_INTERFACE_SHORT_FRAME, .got = 0, .need = 1);
  }
  if (data[0] != HEADER_packet_settings_logs_set_t) {
    SE_FAIL(ERR_INTERFACE_UNKNOWN_PACKET, .class_header = CONFIG_RX_PACKET_CLASS_SETTINGS_LOGS, .packet_header = data[0]);
  }
  if (len < 1u + sizeof(packet_settings_logs_set_t)) {
    SE_FAIL(ERR_INTERFACE_SHORT_FRAME, .got = (uint32_t)(len - 1u), .need = sizeof(packet_settings_logs_set_t));
  }

  const packet_settings_logs_set_t* packet = (const void*)(data + 1);
  return SE_set_logging((esp_log_level_t)packet->level, packet->mirror_serial != 0, packet->trace_errors != 0);
}
