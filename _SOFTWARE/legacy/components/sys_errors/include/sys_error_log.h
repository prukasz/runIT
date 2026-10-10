#pragma once
#include <stddef.h>
#include "sys_error.h"

/** @brief Transport send used by sys_errors output. Best effort, no result. */
typedef void (*sys_error_sink_send_f)(const void* data, size_t len);

/** @brief Current largest error packet the transport carries (it can change at runtime, e.g. BLE MTU). */
typedef size_t (*sys_error_sink_max_len_f)(void);

/**
 * @brief Where sys_errors output goes. sys_errors knows no transport: the
 * application binds one at boot with SE_register_sink(). Unbound outputs are
 * dropped (serial mirroring still works).
 */
typedef struct sys_error_sink_t {
  sys_error_sink_send_f send_log;           /**< Text lines: ESP_LOG output and expanded error chains. */
  sys_error_sink_send_f send_packet;        /**< Binary error-chain packets (enc_sys_errors). */
  sys_error_sink_max_len_f packet_max_len;  /**< Asked before every packet; NULL, 0 or above CONFIG_SYS_ERRORS_PACKET_MAX means that maximum. */
} sys_error_sink_t;

/**
 * @brief Bind the transport for log lines and error packets (copied).
 * Boot only: call before other tasks log. NULL unbinds both outputs.
 */
void SE_register_sink(const sys_error_sink_t* sink);

/**
 * @brief Logs an error chain, expanding each node's owner, tag, and payload.
 * Formatted lines go to the sink's send_log and are mirrored to serial when
 * configured.
 *
 * @param chain Head of the error chain to print.
 */
void se_log_error_chain(err_h chain);

/**
 * @brief Initialize the global log hook for sys_errors.
 * Attaches the vprintf interceptor so all ESP_LOG lines flow through the
 * sink's send_log.
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
SE_MUST_USE err_h SE_set_logging(esp_log_level_t level, bool mirror_serial, bool trace_errors);

/**
 * @brief Encodes an error chain into a binary packet and passes it to the
 * sink's send_packet.
 *
 * @param chain Head of the error chain to encode and transmit.
 * @return err_h NULL on success or encoding error.
 */
SE_MUST_USE err_h SE_send_error_raw(err_h chain);

/**
 * @brief Log and send an error chain later, from the error log task; consumes it.
 *
 * Use this for chains that only need diagnostics (responses have already
 * run). Non-blocking: when the log queue is full the chain is released and
 * counted, and the count is logged. Before the log task runs (early boot) it
 * logs synchronously.
 */
void SE_log(err_h chain);

/**
 * @brief Dispatches an error chain across all diagnostics channels, now, on
 * the caller's stack (borrows the chain). Prefer SE_log(); use this only when
 * the caller keeps the chain afterwards.
 *
 * Invokes both se_log_error_chain(chain) (text lines) and
 * SE_send_error_raw(chain) (binary packet), both through the registered sink.
 *
 * @param chain Head of the error chain to send.
 * @return err_h NULL on encoding success or an owned encoding error (input is borrowed).
 * Delivery is best-effort: the sink send functions return void.
 */
SE_MUST_USE err_h SE_send(err_h chain);
