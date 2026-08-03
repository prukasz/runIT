#pragma once
#include <stdint.h>
#include <stdio.h>

#define SYS_ACTIONS_OWNER_MAP(X) \
  X(OWNER_SYS_ACTIONS_BASE, 0xAA00, "OWNER_SYS_ACTIONS_BASE")

#define SYS_ERROR_ACTIONS_MAP(X)                                      \
  X(ERR_ACTION_NOT_FOUND, struct { uint8_t action_id; })              \
  X(ERR_ACTION_RECORDING_BUSY, struct { uint8_t action_id; })

/** @brief Human-readable descriptions for the sys_actions tags - see SE_describe_payload() in sys_error.h. */
#define SYS_ERROR_ACTIONS_LOGGER_MAP(X) \
  X(ERR_ACTION_NOT_FOUND)               \
  X(ERR_ACTION_RECORDING_BUSY)

#define LOG_BODY_ERR_ACTION_NOT_FOUND(p, out, out_size) snprintf((out), (out_size), "action %u has neither a bound static function nor anything stored", (p)->action_id)
#define LOG_BODY_ERR_ACTION_RECORDING_BUSY(p, out, out_size) snprintf((out), (out_size), "action %u: a different action is already recording", (p)->action_id)
