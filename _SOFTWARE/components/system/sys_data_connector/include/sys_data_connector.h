#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "sys_error.h"

/**
 * @file sys_data_connector.h
 * @brief Instance-based Virtual Data Bus & Routing Switchboard for runIT.
 *
 * Provides decoupled routing pipes (connectors) between logical data
 * producers/consumers (logs, error telemetry, VM object streams, command frames)
 * and physical/virtual transports (providers: BLE, UART, LoRa, Flash Storage, etc.).
 *
 * Each connector can multicast across multiple TX providers and multiplex across
 * multiple RX providers. Each connector owns a dedicated `data_present` semaphore.
 * Consumer tasks block on `conn->data_present` and drain frames via
 * `sys_data_connector_receive()`.
 */

#define SYS_DATA_CONNECTOR_MAX           8
#define SYS_DATA_PROVIDER_MAX            8
#define SYS_DATA_CONNECTOR_PROVIDERS_MAX 4
#define SYS_DATA_CONNECTOR_NAME_MAX      16
#define SYS_DATA_CONNECTOR_MAX_PACKET_LEN 512

/* Outer framing bytes for the built-in logical connectors. */
#define SYS_DATA_HEADER_STATUS 0x01
#define SYS_DATA_HEADER_TX     0x02
#define SYS_DATA_HEADER_LOGS   0x03
#define SYS_DATA_HEADER_ERRORS 0x04

// -----------------------------------------------------------------------------
// Well-Known System Connector IDs
// -----------------------------------------------------------------------------
typedef enum {
  CONN_ID_LOGS = 0,         /**< Outbound text logs (ESP_LOG / se_log_vprintf). */
  CONN_ID_ERRORS,           /**< Outbound binary encoded error telemetry. */
  CONN_ID_TELEMETRY,        /**< Outbound VM object telemetry streams. */
  CONN_ID_INTERFACE,        /**< Bi-directional control frames (sys_interface). */
  CONN_ID_APP_BASE = 4      /**< Base ID for dynamic/custom app connectors. */
} sys_data_conn_id_e;

// -----------------------------------------------------------------------------
// Well-Known Provider IDs
// -----------------------------------------------------------------------------
typedef enum {
  SYS_DATA_PROVIDER_NONE = 0,
  SYS_DATA_PROVIDER_BLE  = 1,
  SYS_DATA_PROVIDER_UART = 2,
  SYS_DATA_PROVIDER_MAX_RESERVED = 16
} sys_data_provider_id_e;

// -----------------------------------------------------------------------------
// Forward Declarations
// -----------------------------------------------------------------------------
typedef struct sys_data_connector sys_data_connector_t;

// -----------------------------------------------------------------------------
// Provider Driver Interface
// -----------------------------------------------------------------------------
/**
 * @brief Transport provider driver operations table.
 *
 * Implemented by concrete transport modules (e.g. BLE, UART, loopback).
 * Transports do not process data; they only transmit outbound buffers (send),
 * drain inbound buffers (dequeue), and attach/detach connector wake semaphores.
 */
typedef struct {
  uint8_t     provider_id;
  const char* name;

  /**
   * @brief Outbound transmit callback.
   *
   * @param arg Provider-specific argument (e.g. packed slot header and char UUID).
   * @param data Buffer to transmit.
   * @param len Buffer length in bytes.
   */
  void (*send)(void* arg, const void* data, size_t len);

  /**
   * @brief Non-blocking inbound dequeue callback.
   *
   * @param arg Provider-specific argument.
   * @param buf Destination buffer.
   * @param max_len Destination capacity.
   * @param out_len Set to number of bytes read, or 0 if empty.
   * @return err_h NULL on success (even if len=0), or transport error.
   */
  err_h (*dequeue)(void* arg, uint8_t* buf, size_t max_len, size_t* out_len);

  /**
   * @brief Attach connector to provider's RX notification.
   *
   * @param arg Provider-specific argument.
   * @param conn Connector instance being attached.
   * @return err_h NULL on success.
   */
  err_h (*bind_rx)(void* arg, sys_data_connector_t* conn);

  /**
   * @brief Detach connector from provider.
   *
   * @param arg Provider-specific argument.
   * @param conn Connector instance being detached.
   * @return err_h NULL on success.
   */
  err_h (*unbind_rx)(void* arg, sys_data_connector_t* conn);
} sys_data_provider_driver_t;

// -----------------------------------------------------------------------------
// Connector Data Structure
// -----------------------------------------------------------------------------
struct sys_data_connector {
  uint8_t           id;
  char              name[SYS_DATA_CONNECTOR_NAME_MAX];
  bool              allocated;
  bool              suspended;
  uint8_t           header;            /**< Predefined framing header byte. */
  uint16_t          max_packet_len;

  // Outbound Destinations (TX - Multicast / Fan-out)
  uint8_t           tx_count;
  uint8_t           tx_provider_id[SYS_DATA_CONNECTOR_PROVIDERS_MAX];
  void*             tx_provider_arg[SYS_DATA_CONNECTOR_PROVIDERS_MAX];

  // Inbound Sources (RX - Multiplexing / Fan-in)
  uint8_t           rx_count;
  uint8_t           rx_provider_id[SYS_DATA_CONNECTOR_PROVIDERS_MAX];
  void*             rx_provider_arg[SYS_DATA_CONNECTOR_PROVIDERS_MAX];

  // Dedicated Event Wake Semaphore
  SemaphoreHandle_t data_present;
  bool              owns_data_present_sem;
};

typedef struct {
  uint8_t           id;
  const char*       name;
  uint8_t           header;
  uint16_t          max_packet_len;
  SemaphoreHandle_t data_present;  /**< Optional custom semaphore; if NULL, created automatically */
} sys_data_connector_cfg_t;

/**
 * @brief Get the maximum packet/frame capacity for a connector instance.
 */
static inline size_t sys_data_connector_get_max_len(const sys_data_connector_t* conn) {
  return (conn && conn->max_packet_len > 0) ? conn->max_packet_len : SYS_DATA_CONNECTOR_MAX_PACKET_LEN;
}

// -----------------------------------------------------------------------------
// Provider Registry APIs
// -----------------------------------------------------------------------------

/**
 * @brief Register a transport provider driver.
 *
 * @param driver Provider driver operations table.
 * @return err_h NULL on success.
 */
err_h sys_data_connector_register_provider(const sys_data_provider_driver_t* driver);

// -----------------------------------------------------------------------------
// Connector Lifecycle & Registry APIs
// -----------------------------------------------------------------------------

/**
 * @brief Create the built-in logical connectors.
 *
 * This creates the connector endpoints independently of any transport. Boards
 * bind their BLE, Wi-Fi, or other providers afterwards.
 */
err_h sys_data_connector_init(void);

/**
 * @brief Create or retrieve a connector instance with full configuration.
 *
 * @param cfg Connector configuration struct.
 * @return sys_data_connector_t* Pointer to connector instance, or NULL on error.
 */
sys_data_connector_t* sys_data_connector_create_with_cfg(const sys_data_connector_cfg_t* cfg);

/**
 * @brief Create or retrieve a connector instance in the registry.
 *
 * If a connector with the given ID already exists, its header and name are updated.
 * If not, a new slot is allocated and initialized with its own `data_present` semaphore.
 *
 * @param id Unique connector ID (0..SYS_DATA_CONNECTOR_MAX-1).
 * @param name Diagnostic name for logs and inspection.
 * @param header Predefined framing header byte for this connector.
 * @return sys_data_connector_t* Pointer to connector instance, or NULL if out of slots/memory.
 */
sys_data_connector_t* sys_data_connector_create(uint8_t id, const char* name, uint8_t header);

/**
 * @brief Set or replace the wake semaphore for a connector.
 *
 * @param conn Connector instance.
 * @param sem Caller-owned semaphore to signal when RX data arrives.
 */
void sys_data_connector_set_wake_sem(sys_data_connector_t* conn, SemaphoreHandle_t sem);

/**
 * @brief Look up an existing connector instance by ID.
 *
 * @param id Connector ID.
 * @return sys_data_connector_t* Pointer to connector instance, or NULL if not found.
 */
sys_data_connector_t* sys_data_connector_get(uint8_t id);

// -----------------------------------------------------------------------------
// Topology Binding & Unbinding (TX & RX)
// -----------------------------------------------------------------------------

/**
 * @brief Bind a TX provider destination to a connector.
 *
 * Subsequent calls to sys_data_connector_send(conn, ...) will forward data to this provider.
 * Multiple providers can be bound to the same connector for multicast/fan-out.
 *
 * @param conn Connector instance.
 * @param provider_id Transport provider ID.
 * @param arg Provider-specific argument.
 * @return err_h NULL on success.
 */
err_h sys_data_connector_bind_tx(sys_data_connector_t* conn, uint8_t provider_id, void* arg);

/**
 * @brief Unbind a TX provider destination from a connector.
 *
 * @param conn Connector instance.
 * @param provider_id Transport provider ID to remove.
 * @return err_h NULL on success.
 */
err_h sys_data_connector_unbind_tx(sys_data_connector_t* conn, uint8_t provider_id);

/**
 * @brief Bind an RX provider source to a connector.
 *
 * The provider's bind_rx callback will be invoked to attach conn->data_present.
 * When data arrives, the provider gives conn->data_present, waking the connector's consumer task.
 *
 * @param conn Connector instance.
 * @param provider_id Transport provider ID.
 * @param arg Provider-specific argument.
 * @return err_h NULL on success.
 */
err_h sys_data_connector_bind_rx(sys_data_connector_t* conn, uint8_t provider_id, void* arg);

/**
 * @brief Unbind an RX provider source from a connector.
 *
 * @param conn Connector instance.
 * @param provider_id Transport provider ID to remove.
 * @return err_h NULL on success.
 */
err_h sys_data_connector_unbind_rx(sys_data_connector_t* conn, uint8_t provider_id);

// -----------------------------------------------------------------------------
// Data Transmission (TX)
// -----------------------------------------------------------------------------

/**
 * @brief Transmit data over all bound TX providers on a connector.
 *
 * Iterates through all registered TX destinations and invokes provider->send().
 *
 * @param conn Connector instance.
 * @param data Outbound data buffer.
 * @param len Buffer length in bytes.
 */
void sys_data_connector_send(sys_data_connector_t* conn, const void* data, size_t len);

// -----------------------------------------------------------------------------
// Inbound Frame Dequeue (RX)
// -----------------------------------------------------------------------------

/**
 * @brief Dequeue incoming data from bound RX providers on a connector.
 *
 * Non-blocking: drains the next available frame from any bound RX provider.
 * The consumer task can block on conn->data_present and call this function in a loop.
 *
 * @param conn Connector instance.
 * @param buf Destination buffer.
 * @param max_len Destination buffer capacity.
 * @param out_len Set to number of bytes read, or 0 if empty.
 * @return err_h NULL on success (even if len=0), or provider error.
 */
err_h sys_data_connector_receive(sys_data_connector_t* conn, uint8_t* buf, size_t max_len, size_t* out_len);

// -----------------------------------------------------------------------------
// Flow Control & Suspension
// -----------------------------------------------------------------------------
void sys_data_connector_suspend(sys_data_connector_t* conn);
void sys_data_connector_resume(sys_data_connector_t* conn);
bool sys_data_connector_is_suspended(const sys_data_connector_t* conn);
