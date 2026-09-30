#pragma once
#include <stdint.h>
#include <stdio.h>

// Error owners and tags for sys_uart. sys_errors aggregates these maps through its
// include dirs (sys_error_codes.h). This folder holds only error maps, so it
// doesn't expose the module's API to everything that includes sys_error.h.
#define SYS_UART_OWNER_MAP(X) \
  X(OWNER_SYS_UART_BASE, 0xB100, "OWNER_SYS_UART_BASE")         \
  X(OWNER_SYS_UART_INIT, 0xB101, "OWNER_SYS_UART_INIT")         \
  X(OWNER_SYS_UART_PROVIDER, 0xB102, "OWNER_SYS_UART_PROVIDER") \
  X(OWNER_SYS_UART_RX, 0xB103, "OWNER_SYS_UART_RX")

#define SYS_ERROR_UART_MAP(X)                                                                   \
  X(ERR_UART_LINE_BAD, 0xB101, SE_LEVEL_LOW, struct { uint16_t len; })                          \
  X(ERR_UART_RX_FULL, 0xB102, SE_LEVEL_MEDIUM, struct { uint16_t len; })                        \
  X(ERR_UART_FRAME_TOO_LONG, 0xB103, SE_LEVEL_MEDIUM, struct { uint16_t len; uint16_t max; })

/** @brief Human-readable descriptions for the sys_uart tags - see SE_describe_payload() in sys_error.h. */
#define SYS_ERROR_UART_LOGGER_MAP(X) \
  X(ERR_UART_LINE_BAD)               \
  X(ERR_UART_RX_FULL)                \
  X(ERR_UART_FRAME_TOO_LONG)

#define LOG_BODY_ERR_UART_LINE_BAD(p, out, out_size) \
  snprintf((out), (out_size), "UART frame line dropped: %u chars, bad hex or longer than the frame limit", (p)->len)
#define LOG_BODY_ERR_UART_RX_FULL(p, out, out_size) \
  snprintf((out), (out_size), "UART %u-byte frame dropped: decoded frame queue full", (p)->len)
#define LOG_BODY_ERR_UART_FRAME_TOO_LONG(p, out, out_size) \
  snprintf((out), (out_size), "UART %u-byte frame dropped: consumer buffer holds %u", (p)->len, (p)->max)
