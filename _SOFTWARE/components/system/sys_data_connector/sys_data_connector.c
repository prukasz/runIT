#include "sys_data_connector.h"
#include <esp_log.h>
#include <string.h>
#include "utils.h"

#undef OWNER
#define OWNER OWNER_SYS_INTERFACE_CLASS

static const char* TAG = "sys_data_connector";

// -----------------------------------------------------------------------------
// Registries
// -----------------------------------------------------------------------------
static const sys_data_provider_driver_t* s_providers[SYS_DATA_PROVIDER_MAX] = {0};
static sys_data_connector_t              s_connectors[SYS_DATA_CONNECTOR_MAX] = {0};

static const sys_data_provider_driver_t* find_provider(uint8_t provider_id) {
  if (provider_id == SYS_DATA_PROVIDER_NONE) return NULL;
  for (size_t i = 0; i < SYS_DATA_PROVIDER_MAX; i++) {
    if (s_providers[i] && s_providers[i]->provider_id == provider_id) {
      return s_providers[i];
    }
  }
  return NULL;
}

// -----------------------------------------------------------------------------
// Provider Registration
// -----------------------------------------------------------------------------

err_h sys_data_connector_register_provider(const sys_data_provider_driver_t* driver) {
  SE_CHECK_NOT_NULL(driver);
  if (driver->provider_id == SYS_DATA_PROVIDER_NONE) {
    SE_RET_ERR(ERR_INVALID_VAL_UI32, .val = driver->provider_id);
  }

  // Update if already registered
  for (size_t i = 0; i < SYS_DATA_PROVIDER_MAX; i++) {
    if (s_providers[i] && s_providers[i]->provider_id == driver->provider_id) {
      s_providers[i] = driver;
      ESP_LOGI(TAG, "Provider updated: %s (id=%u)", driver->name ? driver->name : "unnamed", driver->provider_id);
      return NULL;
    }
  }

  // Insert into first empty slot
  for (size_t i = 0; i < SYS_DATA_PROVIDER_MAX; i++) {
    if (s_providers[i] == NULL) {
      s_providers[i] = driver;
      ESP_LOGI(TAG, "Provider registered: %s (id=%u, slot=%u)",
               driver->name ? driver->name : "unnamed", driver->provider_id, (unsigned)i);
      return NULL;
    }
  }

  SE_RET_ERR(ERR_BASE_NO_MEM, driver->provider_id);
}

// -----------------------------------------------------------------------------
// Connector Lifecycle
// -----------------------------------------------------------------------------

sys_data_connector_t* sys_data_connector_create(uint8_t id, const char* name) {
  if (id >= SYS_DATA_CONNECTOR_MAX) {
    ESP_LOGE(TAG, "Invalid connector ID: %u (max %d)", id, SYS_DATA_CONNECTOR_MAX - 1);
    return NULL;
  }

  sys_data_connector_t* conn = &s_connectors[id];
  if (conn->allocated) {
    if (name && name[0] != '\0') {
      strncpy(conn->name, name, sizeof(conn->name) - 1);
      conn->name[sizeof(conn->name) - 1] = '\0';
    }
    return conn;
  }

  memset(conn, 0, sizeof(*conn));
  conn->id             = id;
  conn->allocated      = true;
  conn->max_packet_len = SYS_DATA_CONNECTOR_MAX_PACKET_LEN;
  if (name && name[0] != '\0') {
    strncpy(conn->name, name, sizeof(conn->name) - 1);
    conn->name[sizeof(conn->name) - 1] = '\0';
  } else {
    snprintf(conn->name, sizeof(conn->name), "conn_%u", id);
  }

  conn->data_present = xSemaphoreCreateBinary();
  if (conn->data_present == NULL) {
    conn->allocated = false;
    ESP_LOGE(TAG, "Failed to create data_present semaphore for connector %u", id);
    return NULL;
  }

  ESP_LOGI(TAG, "Created data connector: %s (id=%u)", conn->name, id);
  return conn;
}

sys_data_connector_t* sys_data_connector_get(uint8_t id) {
  if (id >= SYS_DATA_CONNECTOR_MAX) return NULL;
  if (!s_connectors[id].allocated) return NULL;
  return &s_connectors[id];
}

// -----------------------------------------------------------------------------
// Topology Binding & Unbinding (TX & RX)
// -----------------------------------------------------------------------------

err_h sys_data_connector_bind_tx(sys_data_connector_t* conn, uint8_t provider_id, void* arg) {
  SE_CHECK_NOT_NULL(conn);
  const sys_data_provider_driver_t* prov = find_provider(provider_id);
  if (!prov || !prov->send) {
    SE_RET_ERR(ERR_INVALID_VAL_UI32, .val = provider_id);
  }

  // Update arg if already bound
  for (uint8_t i = 0; i < conn->tx_count; i++) {
    if (conn->tx_provider_id[i] == provider_id) {
      conn->tx_provider_arg[i] = arg;
      ESP_LOGI(TAG, "Connector %s: updated TX provider %s (id=%u)", conn->name, prov->name, provider_id);
      return NULL;
    }
  }

  if (conn->tx_count >= SYS_DATA_CONNECTOR_PROVIDERS_MAX) {
    SE_RET_ERR(ERR_BASE_NO_MEM, provider_id);
  }

  conn->tx_provider_id[conn->tx_count]  = provider_id;
  conn->tx_provider_arg[conn->tx_count] = arg;
  conn->tx_count++;

  ESP_LOGI(TAG, "Connector %s: bound TX provider %s (id=%u, tx_count=%u)",
           conn->name, prov->name, provider_id, conn->tx_count);
  return NULL;
}

err_h sys_data_connector_unbind_tx(sys_data_connector_t* conn, uint8_t provider_id) {
  SE_CHECK_NOT_NULL(conn);

  for (uint8_t i = 0; i < conn->tx_count; i++) {
    if (conn->tx_provider_id[i] == provider_id) {
      for (uint8_t j = i; j + 1 < conn->tx_count; j++) {
        conn->tx_provider_id[j]  = conn->tx_provider_id[j + 1];
        conn->tx_provider_arg[j] = conn->tx_provider_arg[j + 1];
      }
      conn->tx_count--;
      ESP_LOGI(TAG, "Connector %s: unbound TX provider id=%u", conn->name, provider_id);
      return NULL;
    }
  }

  return NULL;
}

err_h sys_data_connector_bind_rx(sys_data_connector_t* conn, uint8_t provider_id, void* arg) {
  SE_CHECK_NOT_NULL(conn);
  const sys_data_provider_driver_t* prov = find_provider(provider_id);
  if (!prov || !prov->dequeue) {
    SE_RET_ERR(ERR_INVALID_VAL_UI32, .val = provider_id);
  }

  // Update arg if already bound
  for (uint8_t i = 0; i < conn->rx_count; i++) {
    if (conn->rx_provider_id[i] == provider_id) {
      conn->rx_provider_arg[i] = arg;
      if (prov->bind_rx) {
        SE_RET_IF_ERR(prov->bind_rx(arg, conn->data_present));
      }
      ESP_LOGI(TAG, "Connector %s: updated RX provider %s (id=%u)", conn->name, prov->name, provider_id);
      return NULL;
    }
  }

  if (conn->rx_count >= SYS_DATA_CONNECTOR_PROVIDERS_MAX) {
    SE_RET_ERR(ERR_BASE_NO_MEM, provider_id);
  }

  conn->rx_provider_id[conn->rx_count]  = provider_id;
  conn->rx_provider_arg[conn->rx_count] = arg;
  conn->rx_count++;

  if (prov->bind_rx) {
    SE_RET_IF_ERR(prov->bind_rx(arg, conn->data_present));
  }

  ESP_LOGI(TAG, "Connector %s: bound RX provider %s (id=%u, rx_count=%u)",
           conn->name, prov->name, provider_id, conn->rx_count);
  return NULL;
}

err_h sys_data_connector_unbind_rx(sys_data_connector_t* conn, uint8_t provider_id) {
  SE_CHECK_NOT_NULL(conn);
  const sys_data_provider_driver_t* prov = find_provider(provider_id);

  for (uint8_t i = 0; i < conn->rx_count; i++) {
    if (conn->rx_provider_id[i] == provider_id) {
      void* arg = conn->rx_provider_arg[i];
      if (prov && prov->unbind_rx) {
        (void)prov->unbind_rx(arg, conn->data_present);
      }

      for (uint8_t j = i; j + 1 < conn->rx_count; j++) {
        conn->rx_provider_id[j]  = conn->rx_provider_id[j + 1];
        conn->rx_provider_arg[j] = conn->rx_provider_arg[j + 1];
      }
      conn->rx_count--;
      ESP_LOGI(TAG, "Connector %s: unbound RX provider id=%u", conn->name, provider_id);
      return NULL;
    }
  }

  return NULL;
}

// -----------------------------------------------------------------------------
// Data Transmission (TX)
// -----------------------------------------------------------------------------

void sys_data_connector_send(sys_data_connector_t* conn, const void* data, size_t len) {
  if (!conn || !conn->allocated || conn->suspended || !data || len == 0) {
    return;
  }

  for (uint8_t i = 0; i < conn->tx_count; i++) {
    const sys_data_provider_driver_t* prov = find_provider(conn->tx_provider_id[i]);
    if (prov && prov->send) {
      prov->send(conn->tx_provider_arg[i], data, len);
    }
  }
}

// -----------------------------------------------------------------------------
// Inbound Frame Dequeue (RX)
// -----------------------------------------------------------------------------

err_h sys_data_connector_receive(sys_data_connector_t* conn, uint8_t* buf, size_t max_len, size_t* out_len) {
  if (!conn || !conn->allocated || conn->suspended || !buf || !out_len) {
    if (out_len) *out_len = 0;
    return NULL;
  }

  *out_len = 0;

  for (uint8_t i = 0; i < conn->rx_count; i++) {
    const sys_data_provider_driver_t* prov = find_provider(conn->rx_provider_id[i]);
    if (!prov || !prov->dequeue) continue;

    err_h dq_err = prov->dequeue(conn->rx_provider_arg[i], buf, max_len, out_len);
    if (SE_IS_ERR(dq_err)) {
      SE_ORIGIN_CALL(dq_err);
      return dq_err;
    }

    if (*out_len > 0) {
      return NULL; // Frame retrieved
    }
  }

  return NULL;
}

// -----------------------------------------------------------------------------
// Flow Control & Suspension
// -----------------------------------------------------------------------------

void sys_data_connector_suspend(sys_data_connector_t* conn) {
  if (conn) conn->suspended = true;
}

void sys_data_connector_resume(sys_data_connector_t* conn) {
  if (conn) conn->suspended = false;
}

bool sys_data_connector_is_suspended(const sys_data_connector_t* conn) {
  return conn ? conn->suspended : false;
}
