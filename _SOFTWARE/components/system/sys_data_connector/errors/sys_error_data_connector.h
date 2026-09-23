#pragma once
#include <stdint.h>
#include <stdio.h>

// Error owners and tags for sys_data_connector. sys_errors aggregates these maps through its
// include dirs (sys_error_codes.h). This folder holds only error maps, so it
// doesn't expose the module's API to everything that includes sys_error.h.
#define SYS_DATA_CONNECTOR_OWNER_MAP(X) \
  X(OWNER_SYS_DATA_CONNECTOR_BASE, 0xAD00, "OWNER_SYS_DATA_CONNECTOR_BASE")                           \
  X(OWNER_SYS_DATA_CONNECTOR_REGISTER_PROVIDER, 0xAD01, "OWNER_SYS_DATA_CONNECTOR_REGISTER_PROVIDER") \
  X(OWNER_SYS_DATA_CONNECTOR_INIT, 0xAD02, "OWNER_SYS_DATA_CONNECTOR_INIT")                           \
  X(OWNER_SYS_DATA_CONNECTOR_REMOVE, 0xAD03, "OWNER_SYS_DATA_CONNECTOR_REMOVE")                       \
  X(OWNER_SYS_DATA_CONNECTOR_BIND_TX, 0xAD04, "OWNER_SYS_DATA_CONNECTOR_BIND_TX")                     \
  X(OWNER_SYS_DATA_CONNECTOR_UNBIND_TX, 0xAD05, "OWNER_SYS_DATA_CONNECTOR_UNBIND_TX")                 \
  X(OWNER_SYS_DATA_CONNECTOR_BIND_RX, 0xAD06, "OWNER_SYS_DATA_CONNECTOR_BIND_RX")                     \
  X(OWNER_SYS_DATA_CONNECTOR_UNBIND_RX, 0xAD07, "OWNER_SYS_DATA_CONNECTOR_UNBIND_RX")                 \
  X(OWNER_SYS_DATA_CONNECTOR_RECEIVE, 0xAD08, "OWNER_SYS_DATA_CONNECTOR_RECEIVE")                     \
  X(OWNER_SYS_DATA_CONNECTOR_CREATE, 0xAD09, "OWNER_SYS_DATA_CONNECTOR_CREATE")                       \
  X(OWNER_SYS_DATA_CONNECTOR_SEND, 0xAD0A, "OWNER_SYS_DATA_CONNECTOR_SEND")                           \
  X(OWNER_SYS_DATA_CONNECTOR_SUSPEND, 0xAD0B, "OWNER_SYS_DATA_CONNECTOR_SUSPEND")

#define SYS_ERROR_DATA_CONNECTOR_MAP(X)                                                                                                  \
  X(ERR_DATA_CONNECTOR_FRAME_TOO_LONG, 0xAD01, SE_LEVEL_MEDIUM, struct { uint8_t id; uint8_t provider_id; uint32_t len; uint32_t max; }) \
  X(ERR_DATA_CONNECTOR_PROTECTED, 0xAD02, SE_LEVEL_LOW, struct { uint8_t id; })                                                          \
  X(ERR_DATA_CONNECTOR_NO_PROVIDER, 0xAD03, SE_LEVEL_LOW, struct { uint8_t provider_id; })

/** @brief Human-readable descriptions for the sys_data_connector tags - see SE_describe_payload() in sys_error.h. */
#define SYS_ERROR_DATA_CONNECTOR_LOGGER_MAP(X) \
  X(ERR_DATA_CONNECTOR_FRAME_TOO_LONG)         \
  X(ERR_DATA_CONNECTOR_PROTECTED)              \
  X(ERR_DATA_CONNECTOR_NO_PROVIDER)

#define LOG_BODY_ERR_DATA_CONNECTOR_FRAME_TOO_LONG(p, out, out_size)                                                                 \
  snprintf((out), (out_size), "connector %u: %lu-byte frame not sent to provider %u, limit %lu", (p)->id, (unsigned long)(p)->len, \
           (p)->provider_id, (unsigned long)(p)->max)
#define LOG_BODY_ERR_DATA_CONNECTOR_PROTECTED(p, out, out_size) \
  snprintf((out), (out_size), "connector %u is a system connector: it can't be removed, reconfigured, lose its last binding, or be suspended while it receives", (p)->id)
#define LOG_BODY_ERR_DATA_CONNECTOR_NO_PROVIDER(p, out, out_size) \
  snprintf((out), (out_size), "no provider %u registered (or it can't do this direction)", (p)->provider_id)
