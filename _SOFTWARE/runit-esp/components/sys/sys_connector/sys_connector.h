#pragma once

#include "errors.h"
#include "sys_connector_errors.h"
#include "sys_connector_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Outbound data streams. A **connector** is a logical stream (logs,
 * errors, telemetry, user streams) named by one byte, which is also the first
 * byte of every frame it sends (its stream byte); it is bound to one or more
 * **providers** (transports: UART, BLE, ...) at an **endpoint** (BLE: a
 * characteristic UUID). Producers name only the connector.
 *
 * sys_connector_send() never blocks: it queues the frame, [connector][payload],
 * in one ring buffer; the connector task hands it to every binding. So any task (also one holding a transport's lock, or logging) can
 * send. Commands and their answers do not go through connectors: they use the
 * router links (ROUTER.MD).
 */

/**
 * @brief A transport's send side. `endpoint` is the transport's own address
 * for a binding (BLE: characteristic UUID; UART: 0). Called from the
 * connector task only.
 */
typedef struct {
  /** Sends one frame (connector byte included). */
  MUST_USE err_h (*send)(uint32_t endpoint, const uint8_t* frame, uint32_t len);
  /** Largest frame for this endpoint now; NULL = CONFIG_SYS_CONNECTOR_FRAME_MAX. */
  uint32_t (*max_frame)(uint32_t endpoint);
} sys_connector_provider_t;

/** @brief Registers a transport under its ID (transport_e). Boot only. */
MUST_USE err_h sys_connector_register_provider(uint8_t provider_id, const sys_connector_provider_t* provider);

/**
 * @brief Creates the system connectors (SYS_CONNECTOR_LOGS, _ERRORS, _TELEMETRY, _EVENTS) and starts the
 * connector task. Bind providers afterwards.
 */
MUST_USE err_h sys_connector_init(void);

/**
 * @brief Creates user connector `connector`. A system one: ERR_CONN_EXISTS; the router's answer byte:
 * ERR_CONN_BAD_ARG; no free slot (CONFIG_SYS_CONNECTOR_MAX): ERR_CONN_FULL.
 */
MUST_USE err_h sys_connector_create(uint8_t connector);

/** @brief Removes a user connector and its bindings. System: ERR_CONN_PROTECTED. */
MUST_USE err_h sys_connector_remove(uint8_t connector);

/** @brief Binds a provider at `endpoint` (again = re-address). */
MUST_USE err_h sys_connector_bind(uint8_t connector, uint8_t provider_id, uint32_t endpoint);

/**
 * @brief Removes a binding (NULL when it was not bound). The last binding of a
 * system connector stays: ERR_CONN_PROTECTED (bind the new one first).
 */
MUST_USE err_h sys_connector_unbind(uint8_t connector, uint8_t provider_id);

/** @brief Suspended: sends are dropped (to free a slow link); false resumes. */
MUST_USE err_h sys_connector_set_suspended(uint8_t connector, bool suspended);

/**
 * @brief Largest payload every binding carries now: the smallest provider
 * limit minus the connector byte. Producers that can size their frames (VM
 * telemetry) ask per frame. A binding that can't carry a frame skips it
 * (ERR_CONN_TOO_LONG, logged). Calls into the providers: not from a task that
 * holds a transport's lock. 0 when `connector` does not exist.
 */
uint32_t sys_connector_max_payload(uint8_t connector);

/**
 * @brief Queues `data` for every binding of `connector`. Never blocks.
 * Suspended or without bindings: dropped, NULL. Queue full: ERR_CONN_QUEUE_FULL.
 */
MUST_USE err_h sys_connector_send(uint8_t connector, const void* data, uint32_t len);

/** @brief True when the caller runs on the connector task (the log hook skips its lines: they would loop). */
bool sys_connector_is_task(void);

/* A connector's state: the connector, its flags and bindings. */
typedef struct {
  uint8_t provider;  /**< transport_e */
  uint32_t endpoint; /**< the provider's address (BLE: characteristic UUID; UART: 0) */
} sys_connector_binding_t;

typedef struct {
  uint8_t connector;  /**< the connector: its stream byte, the first byte of its frames */
  bool system;
  bool suspended;
  uint8_t binding_count;
  sys_connector_binding_t bindings[CONFIG_SYS_CONNECTOR_BINDINGS];
} sys_connector_info_t;

/** @brief True when `connector` exists. */
bool sys_connector_exists(uint8_t connector);

/** @brief Copies every connector out (the router's INFO); returns how many. */
uint8_t sys_connector_list(sys_connector_info_t out[CONFIG_SYS_CONNECTOR_MAX]);

#ifdef __cplusplus
}
#endif
