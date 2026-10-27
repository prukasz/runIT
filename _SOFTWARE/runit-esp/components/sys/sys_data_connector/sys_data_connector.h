#pragma once

#include "errors.h"
#include "sys_data_connector_errors.h"
#include "sys_data_connector_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Outbound data streams. A **connector** is a logical stream (logs,
 * errors, telemetry, user streams) named by one byte, which is also the first
 * byte of every frame it sends (its stream byte); it is bound to one or more
 * **providers** (the router's links: UART, BLE, ...) at an **endpoint** (BLE: a
 * characteristic UUID). Producers name only the connector.
 *
 * sys_data_connector_send() never blocks: it queues the frame, [connector][payload],
 * in one ring buffer; the connector task hands it to every binding. So any task
 * (also one holding a transport's lock, or logging) can send. Commands and their
 * answers do not go through connectors: they use the router links (ROUTER.MD).
 * A transport registers once, as a router link (router_link_register).
 */

/**
 * @brief Creates the system connectors (SYS_DATA_CONNECTOR_LOGS, _ERRORS, _TELEMETRY, _EVENTS) and starts the
 * connector task. Bind providers afterwards.
 */
MUST_USE err_h sys_data_connector_init(void);

/** @brief Binds `connector` to the link `provider_id` (transport_e) at `endpoint` (again = re-address). */
MUST_USE err_h sys_data_connector_bind(uint8_t connector, uint8_t provider_id, uint32_t endpoint);

/**
 * @brief Largest payload every binding carries now: the smallest provider
 * limit minus the connector byte. Producers that can size their frames (VM
 * telemetry) ask per frame. A binding that can't carry a frame skips it
 * (ERR_CONN_TOO_LONG, logged). Calls into the providers: not from a task that
 * holds a transport's lock. 0 when `connector` does not exist.
 */
uint32_t sys_data_connector_max_payload(uint8_t connector);

/**
 * @brief Queues `data` for every binding of `connector`. Never blocks.
 * Suspended or without bindings: dropped, NULL. Queue full: ERR_CONN_QUEUE_FULL.
 */
MUST_USE err_h sys_data_connector_send(uint8_t connector, const void* data, uint32_t len);

/** @brief True when the caller runs on the connector task (the log hook skips its lines: they would loop). */
bool sys_data_connector_is_task(void);

#ifdef __cplusplus
}
#endif
