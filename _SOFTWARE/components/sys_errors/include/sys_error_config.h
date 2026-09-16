#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_log.h"
#include "sys_error.h"
#include "sys_error_log.h"

/**
 * @file sys_error_config.h
 * @brief Outbound diagnostics/telemetry configuration.
 */

// ---------------------------------------------------------
// Telemetry Configuration
// ---------------------------------------------------------

/**
 * @brief Outbound error telemetry configuration.
 */
typedef struct {
  struct {
    /** @brief TX stream header byte identifying error packets. */
    uint8_t  tx_header;
    /** @brief Largest encoded packet; clamped to SE_ERR_PACKET_MAX. Longer chains are truncated, never split. */
    uint16_t packet_max;
  } errors;
} sys_error_cfg_t;

/**
 * @brief Boot defaults for error telemetry.
 */
#define SYS_ERROR_CFG_DEFAULT() \
  ((sys_error_cfg_t){.errors = {.tx_header = 0, .packet_max = SE_ERR_PACKET_MAX}})

/**
 * @brief Apply error telemetry configuration.
 *
 * @param cfg Configuration to apply.
 * @return err_h NULL on success, ERR_NULL_PTR for a NULL @p cfg.
 */
err_h SE_configure(const sys_error_cfg_t* cfg);

/**
 * @brief Read back the configuration currently in force (post-clamping).
 *
 * @param out_cfg Destination struct.
 * @return err_h NULL on success, or ERR_NULL_PTR.
 */
err_h SE_get_config(sys_error_cfg_t* out_cfg);

