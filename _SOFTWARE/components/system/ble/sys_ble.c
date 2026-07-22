#include "sys_ble_priv.h"

static const char* TAG = __FILE_NAME__;

sys_ble_ctx_t g_ble_ctx = {.mtu_size = 527};

R_MUTEX_DEFINE(sys_ble_mutex);
R_BINARY_SEM_DEFINE(sys_ble_tx_sem);

/*****************************************************************************************/
/* Helper Data Structure Management                                                      */
/*****************************************************************************************/

sys_ble_char_node_t* sys_ble_find_char_by_uuid(uint16_t char_uuid) {
  sys_ble_svc_node_t* s;
  LL_FOREACH(g_ble_ctx.services, s) {
    sys_ble_char_node_t* ch;
    LL_FOREACH(s->chars, ch) {
      if (ch->cfg.uuid == char_uuid) return ch;
    }
  }
  return NULL;
}

sys_ble_svc_node_t* sys_ble_find_svc_by_uuid(uint16_t svc_uuid) {
  sys_ble_svc_node_t* s;
  LL_FOREACH(g_ble_ctx.services, s) {
    if (s->cfg.uuid == svc_uuid) return s;
  }
  return NULL;
}

void sys_ble_rebuild_active_tx_slots(void) {
  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
  g_ble_ctx.tx_slot_count = 0;

  sys_ble_svc_node_t* s;
  LL_FOREACH(g_ble_ctx.services, s) {
    sys_ble_char_node_t* c;
    LL_FOREACH(s->chars, c) {
      for (int i = 0; i < c->tx_slot_count; i++) {
        if (g_ble_ctx.tx_slot_count < MAX_TOTAL_TX_SLOTS) {
          g_ble_ctx.tx_slots[g_ble_ctx.tx_slot_count].slot = &c->tx_slots[i];
          g_ble_ctx.tx_slots[g_ble_ctx.tx_slot_count].chr = c;
          g_ble_ctx.tx_slot_count++;
        }
      }
    }
  }
  R_MUTEX_UNLOCK(sys_ble_mutex);
}

void sys_ble_free_char_node(sys_ble_char_node_t* c) {
  if (!c) return;
  if (c->rx_buff) vRingbufferDelete(c->rx_buff);
  if (c->rx_sem) vSemaphoreDelete(c->rx_sem);
  for (int i = 0; i < c->tx_slot_count; i++) {
    if (c->tx_slots[i].tx_buff.buff) {
      vRingbufferDelete(c->tx_slots[i].tx_buff.buff);
    }
  }
  free(c);
}

/*****************************************************************************************/
/* Public Application API                                                                */
/*****************************************************************************************/

#define OWNER OWNER_SYS_BLE_CREATE
status_rep_t sys_ble_init(void) {
  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
  if (g_ble_ctx.initialized) {
    R_MUTEX_UNLOCK(sys_ble_mutex);
    return STA_OK;
  }
  g_ble_ctx.initialized = true;
  R_MUTEX_UNLOCK(sys_ble_mutex);

  ESP_LOGI(TAG, "BLE Manager initialized successfully");
  return STA_OK;
}

status_rep_t sys_ble_add_callback(sys_ble_events_e on_event, uint16_t route_mask) {
  if (on_event >= SYS_BLE_EVENT_MAX) {
    return STA_C(ERR_INVALID_ARG, OWNER, 0, STATUS_PAYLOAD_UNKNOWN);
  }

  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
  g_ble_ctx.route_masks[on_event] = route_mask;
  R_MUTEX_UNLOCK(sys_ble_mutex);

  return STA_OK;
}
#undef OWNER

#define OWNER OWNER_SYS_BLE_SERVICE_CREATE
status_rep_t sys_ble_service_create(const sys_ble_svc_cfg_t* cfg) {
  CHECK_NOT_NULL_R(cfg);

  if (sys_ble_find_svc_by_uuid(cfg->uuid)) {
    return STA_C(ERR_DEV_ALREADY_EXISTS, OWNER, cfg->uuid, STATUS_PAYLOAD_UNKNOWN);
  }

  sys_ble_svc_node_t* new_svc = calloc(1, sizeof(sys_ble_svc_node_t));
  if (!new_svc) {
    return STA_C(ERR_NO_MEM, OWNER, cfg->uuid, STATUS_PAYLOAD_UNKNOWN);
  }

  new_svc->cfg = *cfg;
  new_svc->registered = false;
  new_svc->compiled_def = NULL;

  LL_APPEND(g_ble_ctx.services, new_svc);

  ESP_LOGI(TAG, "Created BLE service UUID 0x%04X", cfg->uuid);
  return STA_OK;
}
#undef OWNER

#define OWNER OWNER_SYS_BLE_SERVICE_REMOVE
status_rep_t sys_ble_service_remove(uint16_t svc_uuid) {
  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);

  sys_ble_svc_node_t* target = NULL;
  CHECK_BLE_SVC_FIND_R(target, svc_uuid, true);

  if (g_ble_ctx.driver_started && target->registered) {
    ble_uuid16_t temp_uuid;
    temp_uuid.u.type = BLE_UUID_TYPE_16;
    temp_uuid.value = target->cfg.uuid;

    int rc = ble_gatts_delete_svc((const ble_uuid_t*)&temp_uuid);
    if (rc != 0) {
      ESP_LOGE(TAG, "Failed to delete service 0x%04X from NimBLE: %d", svc_uuid, rc);
      R_MUTEX_UNLOCK(sys_ble_mutex);
      return STA_C(ERR_HARDWARE_FAULT, OWNER, rc, STATUS_PAYLOAD_UNKNOWN);
    }
  }

  if (target->compiled_def) {
    sys_ble_free_compiled_gatt_db(target->compiled_def);
  }

  sys_ble_char_node_t *c, *tmp;
  LL_FOREACH_SAFE(target->chars, c, tmp) {
    sys_ble_free_char_node(c);
  }

  LL_DELETE(g_ble_ctx.services, target);
  free(target);

  sys_ble_rebuild_active_tx_slots();

  R_MUTEX_UNLOCK(sys_ble_mutex);
  ESP_LOGI(TAG, "Removed BLE service UUID 0x%04X", svc_uuid);
  return STA_OK;
}
#undef OWNER

#define OWNER OWNER_SYS_BLE_CHAR_CREATE
status_rep_t sys_ble_char_create(uint16_t svc_uuid, const sys_ble_char_create_t* cfg) {
  CHECK_NOT_NULL_R(cfg);

  sys_ble_svc_node_t* svc = NULL;
  CHECK_BLE_SVC_FIND_R(svc, svc_uuid, false);

  if (sys_ble_find_char_by_uuid(cfg->info.uuid)) {
    return STA_C(ERR_DEV_ALREADY_EXISTS, OWNER, cfg->info.uuid, STATUS_PAYLOAD_BLE_CHAR);
  }

  sys_ble_char_node_t* new_char = calloc(1, sizeof(sys_ble_char_node_t));
  if (!new_char) {
    return STA_C(ERR_NO_MEM, OWNER, cfg->info.uuid, STATUS_PAYLOAD_BLE_CHAR);
  }

  new_char->cfg = cfg->info;

  if (cfg->rx_buffer_size > 0) {
    new_char->rx_buff = xRingbufferCreate(cfg->rx_buffer_size, RINGBUF_TYPE_BYTEBUF);
    new_char->rx_sem = xSemaphoreCreateBinary();
    if (!new_char->rx_buff || !new_char->rx_sem) {
      if (new_char->rx_buff) vRingbufferDelete(new_char->rx_buff);
      if (new_char->rx_sem) vSemaphoreDelete(new_char->rx_sem);
      free(new_char);
      return STA_C(ERR_NO_MEM, OWNER, cfg->info.uuid, STATUS_PAYLOAD_BLE_CHAR);
    }
  }

  LL_APPEND(svc->chars, new_char);

  ESP_LOGI(TAG, "Created characteristic UUID 0x%04X under service UUID 0x%04X", cfg->info.uuid, svc_uuid);
  return STA_OK;
}
#undef OWNER

#define OWNER OWNER_SYS_BLE_CHAR_REMOVE
status_rep_t sys_ble_char_remove(uint16_t svc_uuid, uint16_t char_uuid) {
  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);

  sys_ble_svc_node_t* svc = NULL;
  CHECK_BLE_SVC_FIND_R(svc, svc_uuid, true);

  sys_ble_char_node_t* target = NULL;
  CHECK_BLE_CHAR_FIND_R(target, char_uuid, true);

  LL_DELETE(svc->chars, target);
  sys_ble_free_char_node(target);

  sys_ble_rebuild_active_tx_slots();

  R_MUTEX_UNLOCK(sys_ble_mutex);
  ESP_LOGI(TAG, "Removed BLE characteristic UUID 0x%04X", char_uuid);
  return STA_OK;
}
#undef OWNER

#define OWNER OWNER_SYS_BLE_CHAR_ASSIGN_TX
status_rep_t sys_ble_char_assign_tx_buffer(uint16_t char_uuid, const sys_ble_tx_buf_cfg_t* buf_cfg) {
  CHECK_NOT_NULL_R(buf_cfg);

  sys_ble_char_node_t* c = NULL;
  CHECK_BLE_CHAR_FIND_R(c, char_uuid, false);

  if (c->tx_slot_count >= MAX_TX_BUFFERS) {
    return STA_C(ERR_NO_MEM, OWNER, char_uuid, STATUS_PAYLOAD_BLE_CHAR);
  }

  for (int i = 0; i < c->tx_slot_count; i++) {
    if (c->tx_slots[i].buffer_id == buf_cfg->buffer_id) {
      return STA_C(ERR_DEV_ALREADY_EXISTS, OWNER, buf_cfg->buffer_id, STATUS_PAYLOAD_UNKNOWN);
    }
  }

  sys_ble_tx_slot_t* slot = &c->tx_slots[c->tx_slot_count];
  slot->buffer_id = buf_cfg->buffer_id;
  slot->is_indication = buf_cfg->is_indication;
  slot->tx_buff = buf_cfg->tx_buff;

  STA_R_ON_ERR(sys_buff_init(&slot->tx_buff, buf_cfg->size));

  c->tx_slot_count++;

  ESP_LOGI(TAG, "Assigned TX buffer ID %d to characteristic UUID 0x%04X", buf_cfg->buffer_id, char_uuid);
  return STA_OK;
}
#undef OWNER

#define OWNER OWNER_SYS_BLE_GET_STATUS
status_rep_t sys_ble_char_get_rx_semaphore(uint16_t char_uuid, SemaphoreHandle_t* out_sem) {
  CHECK_NOT_NULL_R(out_sem);
  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);

  sys_ble_char_node_t* c = NULL;
  CHECK_BLE_CHAR_FIND_R(c, char_uuid, true);

  *out_sem = c->rx_sem;
  R_MUTEX_UNLOCK(sys_ble_mutex);
  return STA_OK;
}
#undef OWNER

#define OWNER OWNER_SYS_BLE_SEND
status_rep_t sys_ble_char_rx_dequeue(uint16_t char_uuid, uint8_t* buffer, size_t max_len, size_t* out_len) {
  CHECK_NOT_NULL_R(buffer);
  CHECK_NOT_NULL_R(out_len);
  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);

  sys_ble_char_node_t* c = NULL;
  CHECK_BLE_CHAR_FIND_R(c, char_uuid, true);

  if (!c->rx_buff) {
    R_MUTEX_UNLOCK(sys_ble_mutex);
    return STA_C(ERR_INVALID_STATE, OWNER, char_uuid, STATUS_PAYLOAD_BLE_CHAR);
  }
  RingbufHandle_t rx_buff = c->rx_buff;
  R_MUTEX_UNLOCK(sys_ble_mutex);

  size_t item_size = 0;
  void* item = xRingbufferReceiveUpTo(rx_buff, &item_size, 0, max_len);
  if (!item) {
    *out_len = 0;
    return STA_OK;
  }

  memcpy(buffer, item, item_size);
  *out_len = item_size;
  vRingbufferReturnItem(rx_buff, item);

  return STA_OK;
}

status_rep_t sys_ble_char_send(uint16_t char_uuid, uint16_t buffer_id, const uint8_t* data, size_t len, bool return_when_full) {
  CHECK_NOT_NULL_R(data);
  if (len == 0) return STA_OK;

  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);

  sys_ble_char_node_t* c = NULL;
  CHECK_BLE_CHAR_FIND_R(c, char_uuid, true);

  sys_ble_tx_slot_t* slot = NULL;
  for (int i = 0; i < c->tx_slot_count; i++) {
    if (c->tx_slots[i].buffer_id == buffer_id) {
      slot = &c->tx_slots[i];
      break;
    }
  }

  if (!slot || !slot->tx_buff.buff) {
    R_MUTEX_UNLOCK(sys_ble_mutex);
    return STA_C(ERR_NOT_FOUND, OWNER, buffer_id, STATUS_PAYLOAD_UNKNOWN);
  }

  RingbufHandle_t buff = slot->tx_buff.buff;
  R_MUTEX_UNLOCK(sys_ble_mutex);

  uint32_t wait_time_ms = return_when_full ? 0 : 100;
  if (xRingbufferSend(buff, data, len, pdMS_TO_TICKS(wait_time_ms)) != pdTRUE) {
    return STA_C(ERR_NO_MEM, OWNER, buffer_id, STATUS_PAYLOAD_UNKNOWN);
  }

  xSemaphoreGive(sys_ble_tx_sem);
  return STA_OK;
}
#undef OWNER

#define OWNER OWNER_SYS_BLE_DATABASE_SYNC
status_rep_t sys_ble_database_sync(void) {
  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);

  if (!g_ble_ctx.driver_started) {
    uint16_t num_svcs = 0;
    sys_ble_svc_node_t* s;
    LL_FOREACH(g_ble_ctx.services, s) {
      num_svcs++;
    }

    struct ble_gatt_svc_def* svcs = calloc(num_svcs + 1, sizeof(struct ble_gatt_svc_def));
    if (!svcs) {
      R_MUTEX_UNLOCK(sys_ble_mutex);
      return STA_C(ERR_NO_MEM, OWNER, 0, STATUS_PAYLOAD_UNKNOWN);
    }

    int s_idx = 0;
    LL_FOREACH(g_ble_ctx.services, s) {
      status_rep_t pop_res = populate_svc_def(&svcs[s_idx], s);
      if (STA_IS_ERR(pop_res)) {
        sys_ble_free_compiled_gatt_db(svcs);
        R_MUTEX_UNLOCK(sys_ble_mutex);
        return pop_res;
      }
      s->registered = true;
      s->compiled_def = NULL;
      s_idx++;
    }

    g_ble_ctx.compiled_db = svcs;

    status_rep_t init_res = sys_ble_stack_init(svcs);
    if (STA_IS_ERR(init_res)) {
      sys_ble_free_compiled_gatt_db(svcs);
      R_MUTEX_UNLOCK(sys_ble_mutex);
      return init_res;
    }

    g_ble_ctx.driver_started = true;
    R_MUTEX_UNLOCK(sys_ble_mutex);
    sys_ble_rebuild_active_tx_slots();
    ESP_LOGI(TAG, "BLE database compilation and stack sync complete");
    return STA_OK;
  } else {
    sys_ble_svc_node_t* s;
    LL_FOREACH(g_ble_ctx.services, s) {
      if (s->registered) continue;

      struct ble_gatt_svc_def* svcs = calloc(2, sizeof(struct ble_gatt_svc_def));
      if (!svcs) {
        R_MUTEX_UNLOCK(sys_ble_mutex);
        return STA_C(ERR_NO_MEM, OWNER, s->cfg.uuid, STATUS_PAYLOAD_UNKNOWN);
      }

      status_rep_t pop_res = populate_svc_def(&svcs[0], s);
      if (STA_IS_ERR(pop_res)) {
        free(svcs);
        R_MUTEX_UNLOCK(sys_ble_mutex);
        return pop_res;
      }

      R_MUTEX_UNLOCK(sys_ble_mutex);
      int rc = ble_gatts_add_dynamic_svcs(svcs);
      R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);

      if (rc != 0) {
        ESP_LOGE(TAG, "Failed to dynamically add service UUID 0x%04X: %d", s->cfg.uuid, rc);
        sys_ble_free_compiled_gatt_db(svcs);
        R_MUTEX_UNLOCK(sys_ble_mutex);
        return STA_C(ERR_HARDWARE_FAULT, OWNER, rc, STATUS_PAYLOAD_UNKNOWN);
      }

      s->registered = true;
      s->compiled_def = svcs;
    }

    R_MUTEX_UNLOCK(sys_ble_mutex);
    sys_ble_rebuild_active_tx_slots();
    ESP_LOGI(TAG, "Dynamic BLE service registration complete");
    return STA_OK;
  }
}
#undef OWNER

#define OWNER OWNER_SYS_BLE_GET_STATUS
status_rep_t sys_ble_get_status(sys_ble_status_t* out_status) {
  CHECK_NOT_NULL_R(out_status);
  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
  out_status->is_connected = g_ble_ctx.is_connected;
  out_status->mtu_size = g_ble_ctx.mtu_size;
  out_status->rx_overflow_count = g_ble_ctx.rx_overflow_count;
  R_MUTEX_UNLOCK(sys_ble_mutex);
  return STA_OK;
}
#undef OWNER
