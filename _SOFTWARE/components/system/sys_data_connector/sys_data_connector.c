#include "sys_data_connector.h"
#include "utils.h"

#undef OWNER
#define OWNER OWNER_SYS_INTERFACE_CLASS

static const char* TAG = "sys_data_connector";

// -----------------------------------------------------------------------------
// Registries
// -----------------------------------------------------------------------------
static const sys_data_provider_driver_t* s_providers[CONFIG_SYS_DATA_PROVIDER_MAX] = {0};
static sys_data_connector_t              s_connectors[CONFIG_SYS_DATA_CONNECTOR_MAX] = {0};

static const sys_data_provider_driver_t* find_provider(uint8_t provider_id) {
  if (provider_id == SYS_DATA_PROVIDER_NONE) return NULL;
  for (size_t i = 0; i < CONFIG_SYS_DATA_PROVIDER_MAX; i++) {
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
  for (size_t i = 0; i < CONFIG_SYS_DATA_PROVIDER_MAX; i++) {
    if (s_providers[i] && s_providers[i]->provider_id == driver->provider_id) {
      s_providers[i] = driver;
      ESP_LOGI(TAG, "Provider updated: %s (id=%u)", driver->name ? driver->name : "unnamed", driver->provider_id);
      return NULL;
    }
  }

  // Insert into first empty slot
  for (size_t i = 0; i < CONFIG_SYS_DATA_PROVIDER_MAX; i++) {
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

err_h sys_data_connector_init(void) {
  static const sys_data_connector_cfg_t s_system_connectors[] = {
      {.id = CONN_ID_LOGS,      .name = "logs",      .header = CONFIG_SYS_DATA_HEADER_LOGS},
      {.id = CONN_ID_ERRORS,    .name = "errors",    .header = CONFIG_SYS_DATA_HEADER_ERRORS},
      {.id = CONN_ID_TELEMETRY, .name = "telemetry", .header = CONFIG_SYS_DATA_HEADER_TX},
      {.id = CONN_ID_INTERFACE, .name = "interface", .header = CONFIG_SYS_DATA_HEADER_TX},
  };

  for (size_t i = 0; i < sizeof(s_system_connectors) / sizeof(s_system_connectors[0]); i++) {
    if (!sys_data_connector_create_with_cfg(&s_system_connectors[i])) {
      SE_RET_ERR(ERR_BASE_NO_MEM, s_system_connectors[i].id);
    }
  }
  return NULL;
}

void sys_data_connector_set_wake_sem(sys_data_connector_t* conn, SemaphoreHandle_t sem) {
  if (!conn || conn->data_present == sem) return;
  if (conn->owns_data_present_sem && conn->data_present) {
    vSemaphoreDelete(conn->data_present);
  }
  conn->data_present          = sem;
  conn->owns_data_present_sem = false;
}

sys_data_connector_t* sys_data_connector_create_with_cfg(const sys_data_connector_cfg_t* cfg) {
  if (!cfg || cfg->id >= CONFIG_SYS_DATA_CONNECTOR_MAX) {
    ESP_LOGE(TAG, "Invalid connector config or ID: %u", cfg ? cfg->id : 0xFF);
    return NULL;
  }

  sys_data_connector_t* conn = &s_connectors[cfg->id];
  if (conn->allocated) {
    if (cfg->header != 0) {
      conn->header = cfg->header;
    }
    if (cfg->name && cfg->name[0] != '\0') {
      strncpy(conn->name, cfg->name, sizeof(conn->name) - 1);
      conn->name[sizeof(conn->name) - 1] = '\0';
    }
    if (cfg->max_packet_len > 0) {
      conn->max_packet_len = cfg->max_packet_len;
    }
    if (cfg->data_present) {
      sys_data_connector_set_wake_sem(conn, cfg->data_present);
    }
    return conn;
  }

  memset(conn, 0, sizeof(*conn));
  conn->id             = cfg->id;
  conn->allocated      = true;
  conn->header         = cfg->header;
  conn->max_packet_len = cfg->max_packet_len > 0 ? cfg->max_packet_len : CONFIG_SYS_DATA_CONNECTOR_MAX_PACKET_LEN;
  if (cfg->name && cfg->name[0] != '\0') {
    strncpy(conn->name, cfg->name, sizeof(conn->name) - 1);
    conn->name[sizeof(conn->name) - 1] = '\0';
  } else {
    snprintf(conn->name, sizeof(conn->name), "conn_%u", cfg->id);
  }

  if (cfg->data_present) {
    conn->data_present          = cfg->data_present;
    conn->owns_data_present_sem = false;
  } else {
    conn->data_present = xSemaphoreCreateBinary();
    conn->owns_data_present_sem = true;
    if (conn->data_present == NULL) {
      conn->allocated = false;
      ESP_LOGE(TAG, "Failed to create data_present semaphore for connector %u", cfg->id);
      return NULL;
    }
  }

  ESP_LOGI(TAG, "Created data connector: %s (id=%u, header=0x%02X)", conn->name, cfg->id, conn->header);
  return conn;
}

sys_data_connector_t* sys_data_connector_create(uint8_t id, const char* name, uint8_t header) {
  sys_data_connector_cfg_t cfg = {
      .id             = id,
      .name           = name,
      .header         = header,
      .max_packet_len = CONFIG_SYS_DATA_CONNECTOR_MAX_PACKET_LEN,
      .data_present   = NULL,
  };
  return sys_data_connector_create_with_cfg(&cfg);
}

sys_data_connector_t* sys_data_connector_get(uint8_t id) {
  if (id >= CONFIG_SYS_DATA_CONNECTOR_MAX) return NULL;
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

  if (conn->tx_count >= CONFIG_SYS_DATA_CONNECTOR_PROVIDERS_MAX) {
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
      if (prov->bind_rx) {
        SE_RET_IF_ERR(prov->bind_rx(arg, conn));
      }
      conn->rx_provider_arg[i] = arg;
      ESP_LOGI(TAG, "Connector %s: updated RX provider %s (id=%u)", conn->name, prov->name, provider_id);
      return NULL;
    }
  }

  if (conn->rx_count >= CONFIG_SYS_DATA_CONNECTOR_PROVIDERS_MAX) {
    SE_RET_ERR(ERR_BASE_NO_MEM, provider_id);
  }

  if (prov->bind_rx) {
    SE_RET_IF_ERR(prov->bind_rx(arg, conn));
  }

  conn->rx_provider_id[conn->rx_count]  = provider_id;
  conn->rx_provider_arg[conn->rx_count] = arg;
  conn->rx_count++;

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
        SE_release(prov->unbind_rx(arg, conn));
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

  size_t max_payload = sys_data_connector_get_max_len(conn);
  size_t send_len    = (len > max_payload) ? max_payload : len;

  uint8_t  frame[CONFIG_SYS_DATA_CONNECTOR_MAX_PACKET_LEN + 1];
  uint8_t* p_frame        = frame;
  bool     heap_allocated = false;

  size_t total_len = send_len + 1;
  if (total_len > sizeof(frame)) {
    p_frame = malloc(total_len);
    if (!p_frame) return;
    heap_allocated = true;
  }

  p_frame[0] = conn->header;
  memcpy(&p_frame[1], data, send_len);

  for (uint8_t i = 0; i < conn->tx_count; i++) {
    const sys_data_provider_driver_t* prov = find_provider(conn->tx_provider_id[i]);
    if (prov && prov->send) {
      prov->send(conn->tx_provider_arg[i], p_frame, total_len);
    }
  }

  if (heap_allocated) {
    free(p_frame);
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
