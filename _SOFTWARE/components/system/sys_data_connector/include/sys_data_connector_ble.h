#pragma once
#include "sys_data_connector.h"
#include "sys_error.h"

/**
 * @file sys_data_connector_ble.h
 * @brief BLE transport adapter for sys_data_connector.
 * @brief BLE transport provider adapter for sys_data_connector.
 */

#define SYS_DATA_BLE_ARG(char_uuid, header) \
  ((void*)(((uintptr_t)(uint8_t)(header) << 16) | (uintptr_t)(uint16_t)(char_uuid)))

#define SYS_DATA_BLE_ARG_CHAR(arg)   ((uint16_t)((uintptr_t)(arg) & 0xFFFF))
#define SYS_DATA_BLE_ARG_HEADER(arg) ((uint8_t)(((uintptr_t)(arg) >> 16) & 0xFF))

/**
 * @brief BLE channel mapping configuration for sys_data_connector.
 * @brief BLE default topology configuration for runIT system connectors.
 */
typedef struct {
  uint16_t logs_char_uuid;    /**< Characteristic UUID carrying text logs. */
  uint8_t  logs_header;       /**< Slot header byte identifying the log stream. */
  uint16_t errors_char_uuid;  /**< Characteristic UUID carrying binary error packets. */
  uint8_t  errors_header;     /**< Slot header byte identifying the error stream. */
  uint16_t tx_char_uuid;      /**< Characteristic UUID carrying command replies and telemetry. */
  uint8_t  tx_header;         /**< Slot header byte identifying TX traffic. */
  uint16_t rx_char_uuid;      /**< Characteristic UUID receiving inbound control frames. */
  size_t   rx_frame_max;      /**< Maximum expected inbound frame size. */
} sys_data_connector_ble_cfg_t;

/**
 * @brief Binds BLE transport to the data connector channels using the supplied mapping.
 * @brief Registers the BLE transport provider driver with the data connector bus.
 *
 * @return err_h NULL on success.
 */
err_h sys_data_connector_register_ble_provider(void);

/**
 * @brief Configures standard system connectors (LOGS, ERRORS, TELEMETRY, INTERFACE) to use BLE.
 *
 * Automatically registers the BLE provider and binds standard connectors:
 * - CONN_ID_LOGS (TX)
 * - CONN_ID_ERRORS (TX)
 * - CONN_ID_TELEMETRY (TX)
 * - CONN_ID_INTERFACE (TX and RX)
 *
 * @param cfg BLE channel mapping configuration.
 * @return err_h NULL on success.
 */
err_h sys_data_connector_bind_ble(const sys_data_connector_ble_cfg_t* cfg);


