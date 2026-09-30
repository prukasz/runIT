#pragma once
#include "utils.h"
#include <sdkconfig.h>
#include "sys_error.h"

/**
 * @file sys_data_connector.h
 * @brief Transport-agnostic data bus between logical streams and transports.
 *
 * A **connector** is a logical stream (logs, errors, telemetry, commands). A
 * **provider** is a transport (BLE, UART, Wi-Fi, MQTT, ...). Connectors never
 * know which transport carries them, and consumers (sys_interface, vm_sub,
 * the error sink) only ever name a connector ID.
 *
 * - **TX (fan-out):** sys_data_connector_send() prepends the connector's
 *   stream byte and hands the frame to every bound TX provider.
 *   sys_data_connector_send_to() sends to one origin only (command responses).
 * - **RX (fan-in):** providers queue whole frames and call
 *   sys_data_connector_notify_rx(); the consumer blocks in
 *   sys_data_connector_wait_rx() and drains with sys_data_connector_receive(),
 *   which also reports where each frame came from.
 * - **Frame limit:** each provider may report its current maximum frame (BLE:
 *   negotiated MTU - 3). sys_data_connector_max_payload() gives producers the
 *   smallest limit over a connector's TX bindings; a longer frame is refused
 *   with ERR_DATA_CONNECTOR_FRAME_TOO_LONG, never truncated. Buffers stay
 *   statically sized to CONFIG_SYS_DATA_CONNECTOR_FRAME_MAX.
 * - **System connectors** (created by sys_data_connector_init()) can't be
 *   removed or reconfigured and can't lose their last TX or RX binding, and
 *   one that receives can't be suspended: that would cut the channel the fix
 *   has to arrive on, or send its output nowhere. To move one to another
 *   transport, bind the new provider first. Suspending a TX-only system
 *   connector (logs, telemetry) is allowed, to free a slow link.
 *
 * All functions take a connector ID (0..CONFIG_SYS_DATA_CONNECTOR_MAX-1) and
 * are safe to call from any task, not from an ISR.
 *
 * @code
 * // Board wiring (runit): the BLE provider carries commands in and out.
 * SE_TRY(sys_ble_provider_register(RUNIT_DATA_PROVIDER_BLE));
 * SE_TRY(sys_data_connector_bind_rx(SYS_DATA_CONNECTOR_INTERFACE, RUNIT_DATA_PROVIDER_BLE, SYS_BLE_CHR_RUNIT_RX));
 * SE_TRY(sys_data_connector_bind_tx(SYS_DATA_CONNECTOR_INTERFACE, RUNIT_DATA_PROVIDER_BLE, SYS_BLE_CHR_RUNIT_TX));
 * @endcode
 */

/**
 * @brief Connector IDs (registry slots) of the system connectors, and where
 * user connectors start.
 *
 * Published to the app (enums.json) for the settings packets; the values come
 * from Kconfig ("Data Connector Registry"). Firmware code uses these names.
 */
//#ref-enum @alias Data Connector
typedef enum sys_data_connector_id_e {
  SYS_DATA_CONNECTOR_LOGS = CONFIG_SYS_DATA_CONN_ID_LOGS,           //@alias Logs @description Text log lines.
  SYS_DATA_CONNECTOR_ERRORS = CONFIG_SYS_DATA_CONN_ID_ERRORS,       //@alias Errors @description Binary error reports.
  SYS_DATA_CONNECTOR_TELEMETRY = CONFIG_SYS_DATA_CONN_ID_TELEMETRY, //@alias Telemetry @description Program object values you subscribed to.
  SYS_DATA_CONNECTOR_INTERFACE = CONFIG_SYS_DATA_CONN_ID_INTERFACE, //@alias Commands @description Commands to the board and their responses.
  SYS_DATA_CONNECTOR_APP_BASE = CONFIG_SYS_DATA_CONN_ID_APP_BASE,   //@alias First user connector @description Connectors you create use this ID or higher.
} sys_data_connector_id_e;

/** @brief Peer value meaning "every peer of the endpoint" (broadcast send, limit over all peers). */
#define SYS_DATA_CONNECTOR_PEER_ALL UINT32_MAX

/**
 * @brief Where a received frame came from, and where its answer goes.
 *
 * `peer` is provider-defined: a transport with one link per endpoint (BLE
 * today) reports 0; a multi-client transport (TCP) reports its session.
 */
typedef struct {
  uint8_t provider_id;
  uint32_t peer;
} sys_data_connector_origin_t;

/**
 * @brief Transport provider operations.
 *
 * `endpoint` is the provider's own address for a binding, given by the board
 * wiring or a settings packet (BLE: characteristic UUID; MQTT: topic slot). It
 * is a plain value, never a pointer.
 *
 * A provider owns its framing: dequeue() returns exactly one whole frame. A
 * stream transport (UART, TCP) de-frames its bytes before queueing; a frame
 * that doesn't fit @p max_len is dropped and reported, never cut short.
 */
typedef struct {
  const char* name;

  /** Transmit one framed item (stream byte included) to @p peer, or SYS_DATA_CONNECTOR_PEER_ALL. Required for TX. */
  err_h (*send)(uint32_t endpoint, uint32_t peer, const uint8_t* frame, size_t len);

  /** Pop one whole frame, non-blocking; *out_len = 0 when empty. Required for RX. */
  err_h (*dequeue)(uint32_t endpoint, uint8_t* buf, size_t max_len, size_t* out_len, uint32_t* out_peer);

  /** Current largest frame towards @p peer. Optional: NULL means CONFIG_SYS_DATA_CONNECTOR_FRAME_MAX. */
  size_t (*max_frame)(uint32_t endpoint, uint32_t peer);

  /** Start calling sys_data_connector_notify_rx(@p conn_id) whenever a frame is queued. Optional. */
  err_h (*bind_rx)(uint32_t endpoint, uint8_t conn_id);

  /** Stop the notifications started by bind_rx. Optional. */
  err_h (*unbind_rx)(uint32_t endpoint, uint8_t conn_id);
} sys_data_connector_provider_t;

/** @brief Connector configuration for sys_data_connector_create(). */
typedef struct {
  uint8_t id;          /**< Registry slot, 0..CONFIG_SYS_DATA_CONNECTOR_MAX-1. */
  const char* name;    /**< Diagnostic name; NULL or "" gives "conn_<id>". */
  uint8_t header;      /**< Stream byte prepended to every outbound frame. */
  uint16_t max_frame;  /**< Frame cap incl. the stream byte; 0 = CONFIG_SYS_DATA_CONNECTOR_FRAME_MAX. */
  bool system;         /**< Protected system connector (see file comment). */
} sys_data_connector_cfg_t;

// -----------------------------------------------------------------------------
// Providers
// -----------------------------------------------------------------------------

/**
 * @brief Register a transport provider under an ID. Boot only.
 *
 * @param provider_id Nonzero ID the bindings and settings packets use.
 * @param provider Operations table; must outlive the registry (static const).
 * @return NULL, ERR_INVALID_VAL_UI32 for ID 0 or an ID already taken,
 *         ERR_BASE_NO_MEM when CONFIG_SYS_DATA_PROVIDER_MAX providers exist.
 */
SE_MUST_USE err_h sys_data_connector_register_provider(uint8_t provider_id, const sys_data_connector_provider_t* provider);

// -----------------------------------------------------------------------------
// Connectors
// -----------------------------------------------------------------------------

/**
 * @brief Create the built-in system connectors (logs, errors, telemetry, interface).
 *
 * Transport-independent: the board binds providers afterwards.
 */
SE_MUST_USE err_h sys_data_connector_init(void);

/**
 * @brief Create a connector, or reconfigure an existing non-system one.
 *
 * @return NULL, ERR_INVALID_VAL_UI32 for a bad ID or max_frame,
 *         ERR_DATA_CONNECTOR_PROTECTED to reconfigure a system connector.
 */
SE_MUST_USE err_h sys_data_connector_create(const sys_data_connector_cfg_t* cfg);

/** @brief Whether connector @p id currently exists. */
bool sys_data_connector_exists(uint8_t id);

/**
 * @brief Remove a connector and all of its bindings.
 *
 * @return NULL, ERR_BASE_NOT_FOUND, or ERR_DATA_CONNECTOR_PROTECTED for a system connector.
 */
SE_MUST_USE err_h sys_data_connector_remove(uint8_t id);

/**
 * @brief Bind (or re-address) a TX provider on a connector.
 *
 * @return NULL, ERR_BASE_NOT_FOUND (connector), ERR_DATA_CONNECTOR_NO_PROVIDER,
 *         ERR_BASE_NO_MEM when CONFIG_SYS_DATA_CONNECTOR_PROVIDERS_MAX are bound.
 */
SE_MUST_USE err_h sys_data_connector_bind_tx(uint8_t id, uint8_t provider_id, uint32_t endpoint);

/**
 * @brief Remove a TX binding (NULL also when it wasn't bound).
 *
 * @return ERR_DATA_CONNECTOR_PROTECTED for the last TX binding of a system connector.
 */
SE_MUST_USE err_h sys_data_connector_unbind_tx(uint8_t id, uint8_t provider_id);

/**
 * @brief Bind (or re-address) an RX provider; the provider starts waking the connector.
 *
 * @return As sys_data_connector_bind_tx(), plus the provider's bind_rx error.
 */
SE_MUST_USE err_h sys_data_connector_bind_rx(uint8_t id, uint8_t provider_id, uint32_t endpoint);

/**
 * @brief Remove an RX binding (NULL also when it wasn't bound).
 *
 * @return ERR_DATA_CONNECTOR_PROTECTED for the last RX binding of a system connector.
 */
SE_MUST_USE err_h sys_data_connector_unbind_rx(uint8_t id, uint8_t provider_id);

/**
 * @brief Stop / restart a connector's traffic. While suspended, send drops
 * frames and receive returns nothing (RX frames wait in the providers).
 *
 * @return ERR_DATA_CONNECTOR_PROTECTED to suspend a system connector that receives.
 */
SE_MUST_USE err_h sys_data_connector_suspend(uint8_t id);
SE_MUST_USE err_h sys_data_connector_resume(uint8_t id);

// -----------------------------------------------------------------------------
// Data
// -----------------------------------------------------------------------------

/**
 * @brief Largest payload one send on this connector can carry right now.
 *
 * The smallest frame limit over the connector's TX bindings (and its own
 * max_frame), minus the stream byte. It follows the transports: for BLE it
 * changes when a client connects and negotiates its MTU. Query it per frame;
 * don't cache it. 0 for an unknown connector.
 */
size_t sys_data_connector_max_payload(uint8_t id);

/**
 * @brief Send @p data to every TX binding, framed with the connector's stream byte.
 *
 * Every binding is tried; the first failure is returned. A frame longer than a
 * binding's limit isn't sent there (ERR_DATA_CONNECTOR_FRAME_TOO_LONG).
 * Suspended or unbound connectors drop the frame and return NULL.
 */
SE_MUST_USE err_h sys_data_connector_send(uint8_t id, const void* data, size_t len);

/**
 * @brief Send @p data only to where a received frame came from.
 *
 * Uses the connector's TX binding of @p to->provider_id, addressed to
 * @p to->peer. NULL without sending when that provider has no TX binding here.
 */
SE_MUST_USE err_h sys_data_connector_send_to(uint8_t id, const sys_data_connector_origin_t* to, const void* data, size_t len);

/**
 * @brief Pop one inbound frame, non-blocking, rotating over the RX bindings.
 *
 * @param out_len 0 when nothing is pending (or the connector is suspended).
 * @param out_origin Filled with the frame's origin when *out_len > 0; may be NULL.
 */
SE_MUST_USE err_h sys_data_connector_receive(uint8_t id, uint8_t* buf, size_t max_len, size_t* out_len, sys_data_connector_origin_t* out_origin);

/**
 * @brief Block until a provider signals new RX data or @p timeout_ms passes.
 *
 * @return true when signalled. Drain with sys_data_connector_receive() either way.
 */
bool sys_data_connector_wait_rx(uint8_t id, uint32_t timeout_ms);

/** @brief Wake the connector's consumer. Called by providers when they queue a frame. */
void sys_data_connector_notify_rx(uint8_t id);
