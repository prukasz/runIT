#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Outbound data streams. A **connector** is a logical stream (logs,
 * errors, telemetry, user streams) with a stream byte; it is bound to one or
 * more **providers** (transports: UART, BLE, ...) at an endpoint (BLE: a
 * characteristic UUID). Producers name only the connector.
 *
 * sys_connector_send() never blocks: it copies the payload into one queue;
 * the connector task prepends the stream byte and hands the frame to every
 * binding. So any task (also one holding a transport's lock, or logging) can
 * send. Commands and their answers do not go through connectors: they use the
 * router links (ROUTER.MD).
 */

/**
 * @brief Transport IDs - the one place they are assigned. A transport is a
 * router link (commands in, answers out) and a data connector provider
 * (streams out) under the same ID: the router takes any link ID in
 * 1..CONFIG_ROUTER_MAX_LINKS and knows nothing of this list. 0 is invalid;
 * never renumber.
 */
//#ref-enum @alias Transport
typedef enum {
  TRANSPORT_UART = 1,  //@alias UART @description Console UART / USB-Serial-JTAG, "#R:" hex lines next to the log.
  TRANSPORT_BLE = 2,   //@alias BLE @description Bluetooth LE: runIT service 0xFFE0, endpoint = characteristic UUID.
} transport_e;

//#ref-enum @alias Data Connector
typedef enum {
  SYS_CONNECTOR_LOGS = 0,       //@alias Logs @description Text log lines.
  SYS_CONNECTOR_ERRORS = 1,     //@alias Errors @description Error records: the request (if any) and the error chain.
  SYS_CONNECTOR_TELEMETRY = 2,  //@alias Telemetry @description Program object values you subscribed to.
  SYS_CONNECTOR_EVENTS = 3,     //@alias Events @description Events that matter to you (over-current, faults), sent as they happen.
  SYS_CONNECTOR_APP_BASE = 4,   //@alias First user connector @description Connectors you create use this ID or higher.
} sys_connector_id_e;

/* Stream bytes of the system connectors (0x02-0x04 as legacy; 0x05 = router answers). */
#define SYS_CONNECTOR_STREAM_TELEMETRY 0x02u
#define SYS_CONNECTOR_STREAM_LOGS 0x03u
#define SYS_CONNECTOR_STREAM_ERRORS 0x04u
#define SYS_CONNECTOR_STREAM_EVENTS 0x06u

#define SYS_CONNECTOR_NAME_MAX 16u  // including the NUL

// clang-format off
#define ERR_SYS_CONNECTOR_MAP(X)                                                                                  \
  X(ERR_CONN_BAD_ID,        ERR_TAG(ERR_MOD_SYS_CONNECTOR, 1),  ERR_SEV_HIGH,   { uint8_t id; }) /* outside 0..CONFIG_SYS_CONNECTOR_MAX-1 */ \
  X(ERR_CONN_NOT_FOUND,     ERR_TAG(ERR_MOD_SYS_CONNECTOR, 2),  ERR_SEV_MEDIUM, { uint8_t id; })              \
  X(ERR_CONN_EXISTS,        ERR_TAG(ERR_MOD_SYS_CONNECTOR, 3),  ERR_SEV_LOW,    { uint8_t id; })              \
  X(ERR_CONN_PROTECTED,     ERR_TAG(ERR_MOD_SYS_CONNECTOR, 4),  ERR_SEV_LOW,    { uint8_t id; }) /* system connector: no remove, keeps one binding */ \
  X(ERR_CONN_NO_PROVIDER,   ERR_TAG(ERR_MOD_SYS_CONNECTOR, 5),  ERR_SEV_MEDIUM, { uint8_t provider; })        \
  X(ERR_CONN_BINDINGS_FULL, ERR_TAG(ERR_MOD_SYS_CONNECTOR, 6),  ERR_SEV_MEDIUM, { uint8_t id; })              \
  X(ERR_CONN_TOO_LONG,      ERR_TAG(ERR_MOD_SYS_CONNECTOR, 7),  ERR_SEV_MEDIUM, { uint8_t id; uint8_t provider; uint16_t len; uint16_t max; }) /* frame not sent there */ \
  X(ERR_CONN_QUEUE_FULL,    ERR_TAG(ERR_MOD_SYS_CONNECTOR, 8),  ERR_SEV_LOW,    { uint8_t id; uint16_t len; }) /* payload dropped */ \
  X(ERR_CONN_BAD_ARG,       ERR_TAG(ERR_MOD_SYS_CONNECTOR, 9),  ERR_SEV_HIGH,   {})                           \
  X(ERR_CONN_SEND,          ERR_TAG(ERR_MOD_SYS_CONNECTOR, 10), ERR_SEV_NONE,   { uint8_t id; uint8_t provider; uint32_t endpoint; }) /* context: provider send failed */
// clang-format on

ERR_TAGS_DECLARE(ERR_SYS_CONNECTOR_MAP)

/**
 * @brief A transport's send side. `endpoint` is the transport's own address
 * for a binding (BLE: characteristic UUID; UART: 0). Called from the
 * connector task only.
 */
typedef struct {
  const char* name;
  /** Sends one frame (stream byte included). */
  MUST_USE err_h (*send)(uint32_t endpoint, const uint8_t* frame, uint32_t len);
  /** Largest frame for this endpoint now; NULL = CONFIG_SYS_CONNECTOR_FRAME_MAX. */
  uint32_t (*max_frame)(uint32_t endpoint);
} sys_connector_provider_t;

/** @brief Registers a transport under its ID (transport_e). Boot only. */
MUST_USE err_h sys_connector_register_provider(uint8_t provider_id, const sys_connector_provider_t* provider);

/**
 * @brief Creates the system connectors (logs, errors, telemetry, events) and starts
 * the connector task. Bind providers afterwards.
 */
MUST_USE err_h sys_connector_init(void);

/**
 * @brief Creates a user connector (id >= SYS_CONNECTOR_APP_BASE).
 * @param name Diagnostic name, cut to SYS_CONNECTOR_NAME_MAX - 1; may be NULL.
 */
MUST_USE err_h sys_connector_create(uint8_t id, uint8_t stream, const char* name);

/** @brief Removes a user connector and its bindings. System: ERR_CONN_PROTECTED. */
MUST_USE err_h sys_connector_remove(uint8_t id);

/** @brief Binds a provider at `endpoint` (again = re-address). */
MUST_USE err_h sys_connector_bind(uint8_t id, uint8_t provider_id, uint32_t endpoint);

/**
 * @brief Removes a binding (NULL when it was not bound). The last binding of a
 * system connector stays: ERR_CONN_PROTECTED (bind the new one first).
 */
MUST_USE err_h sys_connector_unbind(uint8_t id, uint8_t provider_id);

/** @brief Suspended: sends are dropped (to free a slow link). */
MUST_USE err_h sys_connector_suspend(uint8_t id);
MUST_USE err_h sys_connector_resume(uint8_t id);

/**
 * @brief Largest payload every binding carries now: the smallest provider
 * limit minus the stream byte. Producers that can size their frames (VM
 * telemetry) ask per frame. A binding that can't carry a frame skips it
 * (ERR_CONN_TOO_LONG, logged). Calls into the providers: not from a task that
 * holds a transport's lock.
 */
uint32_t sys_connector_max_payload(uint8_t id);

/**
 * @brief Queues `data` for every binding of the connector. Never blocks.
 * Suspended or without bindings: dropped, NULL. Queue full: ERR_CONN_QUEUE_FULL.
 */
MUST_USE err_h sys_connector_send(uint8_t id, const void* data, uint32_t len);

/**
 * @brief Sends every log line (ESP_LOG) also to connector `id`, without ANSI
 * colours and the line end, cut to CONFIG_SYS_CONNECTOR_LOG_LINE_MAX. The
 * console still gets every line. Lines logged by the connector task itself are
 * not sent (they would loop).
 */
MUST_USE err_h sys_connector_capture_logs(uint8_t id);

/* Snapshot for diagnostics (router INFO). */
typedef struct {
  uint8_t provider;
  uint32_t endpoint;
} sys_connector_binding_t;

typedef struct {
  uint8_t id;
  uint8_t stream;
  bool system;
  bool suspended;
  uint8_t binding_count;
  sys_connector_binding_t bindings[CONFIG_SYS_CONNECTOR_BINDINGS];
  char name[SYS_CONNECTOR_NAME_MAX];
} sys_connector_info_t;

/** @brief Info of connector `id`; ERR_CONN_NOT_FOUND when it does not exist. */
MUST_USE err_h sys_connector_info(uint8_t id, sys_connector_info_t* out);

#ifdef __cplusplus
}
#endif
