#pragma once

#include <stdint.h>

/**
 * @file provider_console.h
 * @brief The CREATE config of provider_console (PROVIDER_CONSOLE.MD): the only thing the driver publishes. Whoever
 * makes the console provider (app/runit) fills it; the driver is reached through the registry, never called.
 */

/** @brief CREATE cfg, 1 B. */
typedef struct __attribute__((packed)) {
  uint8_t rx_flags;  /**< CMD_SEND_* of each frame it hears, out of the port (CMD_SEND_NOWAIT: its RX never waits) */
} provider_console_cfg_t;
