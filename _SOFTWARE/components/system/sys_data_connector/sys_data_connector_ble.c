#include "sys_data_connector_ble.h"
#include <esp_log.h>
#include "sys_ble.h"
#include "utils.h"

#undef OWNER
#define OWNER OWNER_SYS_INTERFACE_CLASS

static const char* TAG = "sys_data_connector_ble";

// -----------------------------------------------------------------------------
// BLE Provider Driver Callbacks
// -----------------------------------------------------------------------------

static void ble_provider_send(void* arg, const void* data, size_t len) {
  uint16_t char_uuid = SYS_DATA_BLE_ARG_CHAR(arg);
  uint8_t  tx_header = SYS_DATA_BLE_ARG_HEADER(arg);
  (void)sys_ble_char_send(char_uuid, tx_header, (const uint8_t*)data, len, true);
}

static err_h ble_provider_dequeue(void* arg, uint8_t* buf, size_t max_len, size_t* out_len) {
  uint16_t char_uuid = SYS_DATA_BLE_ARG_CHAR(arg);
  return sys_ble_char_rx_dequeue(char_uuid, buf, max_len, out_len);
}

static err_h ble_provider_bind_rx(void* arg, SemaphoreHandle_t data_present) {
  uint16_t char_uuid = SYS_DATA_BLE_ARG_CHAR(arg);
  return sys_ble_char_set_rx_notify_sem(char_uuid, data_present);
}

static err_h ble_provider_unbind_rx(void* arg, SemaphoreHandle_t data_present) {
  (void)data_present;
  uint16_t char_uuid = SYS_DATA_BLE_ARG_CHAR(arg);
  return sys_ble_char_set_rx_notify_sem(char_uuid, NULL);
}

static const sys_data_provider_driver_t s_ble_provider_driver = {
    .provider_id      = SYS_DATA_PROVIDER_BLE,
    .name             = "BLE",
    .send             = ble_provider_send,
    .dequeue          = ble_provider_dequeue,
    .bind_rx          = ble_provider_bind_rx,
    .unbind_rx        = ble_provider_unbind_rx,
};

err_h sys_data_connector_register_ble_provider(void) {
  return sys_data_connector_register_provider(&s_ble_provider_driver);
}

// -----------------------------------------------------------------------------
// Default System Wiring Helper
// -----------------------------------------------------------------------------

err_h sys_data_connector_bind_ble(const sys_data_connector_ble_cfg_t* cfg) {
  SE_CHECK_NOT_NULL(cfg);

  // Ensure BLE provider is registered
  SE_RET_IF_ERR(sys_data_connector_register_ble_provider());

  // 1. Logs Connector (CONN_ID_LOGS)
  if (cfg->logs_char_uuid != 0) {
    sys_data_connector_t* conn = sys_data_connector_create(CONN_ID_LOGS, "logs");
    if (conn) {
      void* arg = SYS_DATA_BLE_ARG(cfg->logs_char_uuid, cfg->logs_header);
      SE_RET_IF_ERR(sys_data_connector_bind_tx(conn, SYS_DATA_PROVIDER_BLE, arg));
    }
  }

  // 2. Errors Connector (CONN_ID_ERRORS)
  if (cfg->errors_char_uuid != 0) {
    sys_data_connector_t* conn = sys_data_connector_create(CONN_ID_ERRORS, "errors");
    if (conn) {
      void* arg = SYS_DATA_BLE_ARG(cfg->errors_char_uuid, cfg->errors_header);
      SE_RET_IF_ERR(sys_data_connector_bind_tx(conn, SYS_DATA_PROVIDER_BLE, arg));
    }
  }

  // 3. Telemetry Connector (CONN_ID_TELEMETRY)
  if (cfg->tx_char_uuid != 0) {
    sys_data_connector_t* conn = sys_data_connector_create(CONN_ID_TELEMETRY, "telemetry");
    if (conn) {
      void* arg = SYS_DATA_BLE_ARG(cfg->tx_char_uuid, cfg->tx_header);
      SE_RET_IF_ERR(sys_data_connector_bind_tx(conn, SYS_DATA_PROVIDER_BLE, arg));
    }
  }

  // 4. Interface Connector (CONN_ID_INTERFACE)
  if (cfg->rx_char_uuid != 0 || cfg->tx_char_uuid != 0) {
    sys_data_connector_t* conn = sys_data_connector_create(CONN_ID_INTERFACE, "interface");
    if (conn) {
      if (cfg->tx_char_uuid != 0) {
        void* tx_arg = SYS_DATA_BLE_ARG(cfg->tx_char_uuid, cfg->tx_header);
        SE_RET_IF_ERR(sys_data_connector_bind_tx(conn, SYS_DATA_PROVIDER_BLE, tx_arg));
      }
      if (cfg->rx_char_uuid != 0) {
        err_h rx_err = sys_ble_char_check_rx_enabled(cfg->rx_char_uuid);
        if (SE_IS_OK(rx_err)) {
          void* rx_arg = SYS_DATA_BLE_ARG(cfg->rx_char_uuid, 0);
          SE_RET_IF_ERR(sys_data_connector_bind_rx(conn, SYS_DATA_PROVIDER_BLE, rx_arg));
        } else {
          ESP_LOGW(TAG, "BLE char 0x%04X not RX-enabled, skipping RX binding", cfg->rx_char_uuid);
        }
      }
    }
  }

  return NULL;
}
