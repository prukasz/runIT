#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_log.h"
#include "sys_error.h"
#include "sys_data_connector.h"

/** @brief Stack buffer the log hook renders one line into, defined by connector packet limit. */
#define SE_LOG_LINE_MAX   SYS_DATA_CONNECTOR_MAX_PACKET_LEN

/** @brief Compile-time ceiling for binary error packet buffer, defined by connector packet limit. */
#define SE_ERR_PACKET_MAX SYS_DATA_CONNECTOR_MAX_PACKET_LEN

/** @brief Static system connector ID for outbound text logs. */
#define SE_CONNECTOR_ID_LOGS   0

/** @brief Static system connector ID for outbound binary error telemetry. */
#define SE_CONNECTOR_ID_ERRORS 1

/**
 * @brief Logs an error chain, expanding each node's owner, tag, and payload.
 * Formatted lines are dispatched directly over the static log connector
 * (SE_CONNECTOR_ID_LOGS) and mirrored to serial when configured.
 *
 * @param chain Head of the error chain to print.
 */
void se_log_error_chain(err_h chain);

/**
 * @brief Initialize the global log hook for sys_errors.
 * Attaches the vprintf interceptor so all ESP_LOG lines flow through
 * SE_CONNECTOR_ID_LOGS.
 */
void se_log_init(void);

/**
 * @brief Sets all logging parameters in one call (suitable for direct interface/contract dispatch).
 *
 * @param level ESP log level (ESP_LOG_NONE .. ESP_LOG_VERBOSE).
 * @param mirror_serial Whether to mirror logs to UART/stdout.
 * @param trace_errors Whether to expand error chains on the console (se_log_error_chain).
 * @return err_h NULL on success, or ERR_INVALID_VAL_UI32 if level is invalid.
 */
err_h SE_set_logging(esp_log_level_t level, bool mirror_serial, bool trace_errors);

/**
 * @brief Encodes an error chain into a binary packet and transmits it directly over
 * the static error telemetry connector (SE_CONNECTOR_ID_ERRORS).
 *
 * @param chain Head of the error chain to encode and transmit.
 * @return err_h NULL on success or encoding error.
 */
err_h SE_send_error_raw(err_h chain);

/**
 * @brief Dispatches an error chain across all diagnostics channels.
 *
 * Invokes both se_log_error_chain(chain) (text logging via SE_CONNECTOR_ID_LOGS)
 * and SE_send_error_raw(chain) (binary telemetry over SE_CONNECTOR_ID_ERRORS).
 *
 * @param chain Head of the error chain to send.
 * @return err_h NULL on encoding success or an owned encoding error (input is borrowed).
 * Delivery is best-effort: the connector/provider send API returns void.
 */
err_h SE_send(err_h chain);
