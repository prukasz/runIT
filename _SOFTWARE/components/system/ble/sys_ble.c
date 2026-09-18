#include "sys_ble_priv.h"

static const char* TAG = __FILE_NAME__;

sys_ble_ctx_t g_ble_ctx = {.mtu_size = 527};

R_MUTEX_DEFINE(sys_ble_mutex);
R_BINARY_SEM_DEFINE(sys_ble_tx_sem);

// Dummy callback-event handler for SYS_CB_ROUTE_BLE - logs and nothing else,
// a placeholder until ble has something real to route BLE stack events to.
static void sys_ble_cb_dummy_log(const cb_event_t* event) {
  if (event->head.callback_type != CALLBACK_BLE) return;
  ESP_LOGI(TAG, "BLE event: event %lu, val %ld", (unsigned long)event->event.ble.event, (long)event->event.ble.value);
}

__attribute__((constructor)) static void sys_ble_cb_route_register(void) {
  SE_release(sys_cb_register_route(SYS_CB_ROUTE_BLE, sys_ble_cb_dummy_log));
}

/*****************************************************************************************/
/* Helper Data Structure Management                                                      */
/*****************************************************************************************/

static sys_ble_char_node_t* sys_ble_find_char_by_uuid(uint16_t char_uuid) {
  sys_ble_svc_node_t* s;
  LL_FOREACH(g_ble_ctx.services, s) {
    sys_ble_char_node_t* ch;
    LL_FOREACH(s->chars, ch) {
      if (ch->cfg.uuid == char_uuid) return ch;
    }
  }
  return NULL;
}

static sys_ble_svc_node_t* sys_ble_find_svc_by_uuid(uint16_t svc_uuid) {
  sys_ble_svc_node_t* s;
  LL_FOREACH(g_ble_ctx.services, s) {
    if (s->cfg.uuid == svc_uuid) return s;
  }
  return NULL;
}

static void sys_ble_free_char_node(sys_ble_char_node_t* c) {
  if (!c) return;
  SE_release(sys_buff_free(&c->rx_buff));
  SE_release(sys_buff_free(&c->tx_buff));
  free(c);
}

/*****************************************************************************************/
/* Public Application API                                                                */
/*****************************************************************************************/

#define OWNER OWNER_SYS_BLE_SERVICE_CREATE
err_h sys_ble_service_create(const sys_ble_svc_cfg_t* cfg) {
  SE_CHECK_NOT_NULL(cfg);
  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);

  if (sys_ble_find_svc_by_uuid(cfg->uuid)) {
    R_MUTEX_UNLOCK(sys_ble_mutex);
    SE_RET_ERR(ERR_DEV_ALREADY_EXIST, cfg->uuid);
  }

  sys_ble_svc_node_t* new_svc = calloc(1, sizeof(sys_ble_svc_node_t));
  if (!new_svc) {
    R_MUTEX_UNLOCK(sys_ble_mutex);
    SE_RET_ERR(ERR_BASE_NO_MEM, cfg->uuid);
  }

  new_svc->cfg = *cfg;

  LL_APPEND(g_ble_ctx.services, new_svc);
  R_MUTEX_UNLOCK(sys_ble_mutex);

  ESP_LOGI(TAG, "Created BLE service UUID 0x%04X", cfg->uuid);
  return NULL;
}
#undef OWNER

#define OWNER OWNER_SYS_BLE_SERVICE_REMOVE
err_h sys_ble_service_remove(uint16_t svc_uuid) {
  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);

  sys_ble_svc_node_t* target = NULL;
  CHECK_BLE_SVC_FIND(target, svc_uuid, true);

  if (g_ble_ctx.driver_started && target->registered) {
    ble_uuid16_t temp_uuid;
    temp_uuid.u.type = BLE_UUID_TYPE_16;
    temp_uuid.value = target->cfg.uuid;

    /* GATT access callbacks take sys_ble_mutex while the host is locked. */
    R_MUTEX_UNLOCK(sys_ble_mutex);
    int rc = ble_gatts_delete_svc((const ble_uuid_t*)&temp_uuid);
    R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
    if (rc != 0) {
      ESP_LOGE(TAG, "Failed to delete service 0x%04X from NimBLE: %d", svc_uuid, rc);
      R_MUTEX_UNLOCK(sys_ble_mutex);
      SE_RET_ERR(ERR_BASE_NOT_SUPPORTED, rc);
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

  R_MUTEX_UNLOCK(sys_ble_mutex);
  ESP_LOGI(TAG, "Removed BLE service UUID 0x%04X", svc_uuid);
  return NULL;
}
#undef OWNER

#define OWNER OWNER_SYS_BLE_CHAR_CREATE
err_h sys_ble_char_create(uint16_t svc_uuid, const sys_ble_char_cfg_t* cfg) {
  SE_CHECK_NOT_NULL(cfg);
  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);

  sys_ble_svc_node_t* svc = sys_ble_find_svc_by_uuid(svc_uuid);
  if (!svc && svc_uuid == SYS_BLE_SVC_DEFAULT_AUTO) {
    svc = calloc(1, sizeof(*svc));
    if (!svc) {
      R_MUTEX_UNLOCK(sys_ble_mutex);
      SE_RET_ERR(ERR_BASE_NO_MEM, svc_uuid);
    }
    svc->cfg = (sys_ble_svc_cfg_t){.uuid = svc_uuid, .is_primary = true};
    LL_APPEND(g_ble_ctx.services, svc);
  }
  if (!svc) {
    R_MUTEX_UNLOCK(sys_ble_mutex);
    SE_RET_ERR(ERR_BASE_NOT_FOUND, svc_uuid);
  }

  if (sys_ble_find_char_by_uuid(cfg->uuid)) {
    R_MUTEX_UNLOCK(sys_ble_mutex);
    SE_RET_ERR(ERR_DEV_ALREADY_EXIST, cfg->uuid);
  }

  sys_ble_char_node_t* new_char = calloc(1, sizeof(*new_char));
  if (!new_char) {
    R_MUTEX_UNLOCK(sys_ble_mutex);
    SE_RET_ERR(ERR_BASE_NO_MEM, cfg->uuid);
  }
  new_char->cfg = *cfg;

  err_h err = NULL;
  if (cfg->rx_buffer_size > 0) {
    err = sys_buff_init(&new_char->rx_buff, cfg->rx_buffer_size);
  }
  if (SE_IS_OK(err) && cfg->tx_buffer_size > 0) {
    err = sys_buff_init(&new_char->tx_buff, cfg->tx_buffer_size);
  }
  if (SE_IS_ERR(err)) {
    sys_ble_free_char_node(new_char);
    R_MUTEX_UNLOCK(sys_ble_mutex);
    return err;
  }

  new_char->pending_add = true;
  LL_APPEND(svc->chars, new_char);
  if (svc->registered) svc->dirty = true;

  R_MUTEX_UNLOCK(sys_ble_mutex);
  ESP_LOGI(TAG, "Created characteristic UUID 0x%04X under service UUID 0x%04X", cfg->uuid, svc_uuid);
  return NULL;
}
#undef OWNER

#define OWNER OWNER_SYS_BLE_CHAR_REMOVE
err_h sys_ble_char_remove(uint16_t svc_uuid, uint16_t char_uuid) {
  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);

  sys_ble_svc_node_t* svc = NULL;
  CHECK_BLE_SVC_FIND(svc, svc_uuid, true);

  sys_ble_char_node_t* target = NULL;
  LL_FOREACH(svc->chars, target) {
    if (target->cfg.uuid == char_uuid) break;
  }
  if (!target) {
    R_MUTEX_UNLOCK(sys_ble_mutex);
    SE_RET_ERR(ERR_BASE_NOT_FOUND, char_uuid);
  }

  if (svc->registered) {
    /* NimBLE may still reference this node's val_handle/arg until the service
       is actually deleted+recompiled - defer the free to sys_ble_database_sync(). */
    target->pending_remove = true;
    svc->dirty = true;
  } else {
    LL_DELETE(svc->chars, target);
    sys_ble_free_char_node(target);
  }

  R_MUTEX_UNLOCK(sys_ble_mutex);
  ESP_LOGI(TAG, "Removed BLE characteristic UUID 0x%04X", char_uuid);
  return NULL;
}
#undef OWNER

#define OWNER OWNER_SYS_BLE_GET_STATUS
err_h sys_ble_char_check_rx_enabled(uint16_t char_uuid) {
  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);

  sys_ble_char_node_t* c = NULL;
  CHECK_BLE_CHAR_FIND(c, char_uuid, true);

  if (c->pending_remove || !c->rx_buff.buff) {
    R_MUTEX_UNLOCK(sys_ble_mutex);
    SE_RET_ERR(ERR_BASE_INVALID_STATE, char_uuid);
  }

  R_MUTEX_UNLOCK(sys_ble_mutex);
  return NULL;
}
#undef OWNER

#define OWNER OWNER_SYS_BLE_SEND
err_h sys_ble_char_rx_dequeue(uint16_t char_uuid, uint8_t* buffer, size_t max_len, size_t* out_len) {
  SE_CHECK_NOT_NULL(buffer);
  SE_CHECK_NOT_NULL(out_len);
  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);

  sys_ble_char_node_t* c = NULL;
  CHECK_BLE_CHAR_FIND(c, char_uuid, true);

  if (c->pending_remove || !c->rx_buff.buff) {
    R_MUTEX_UNLOCK(sys_ble_mutex);
    SE_RET_ERR(ERR_BASE_INVALID_STATE, char_uuid);
  }
  err_h pop_res = sys_buff_pop(&c->rx_buff, buffer, max_len, out_len);
  R_MUTEX_UNLOCK(sys_ble_mutex);
  if (SE_IS_ERR(pop_res)) {
    if (pop_res->tag == ERR_BASE_NOT_FOUND) {
      *out_len = 0;
      SE_release(pop_res);
      return NULL;
    }
    return pop_res;
  }

  return NULL;
}
#undef OWNER

#define OWNER OWNER_SYS_BLE_BASE
err_h sys_ble_char_link_connector(uint16_t char_uuid, sys_data_connector_t* conn) {
  SE_CHECK_NOT_NULL(conn);
  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);

  sys_ble_char_node_t* c = NULL;
  CHECK_BLE_CHAR_FIND(c, char_uuid, true);

  if (c->pending_remove || !c->rx_buff.buff) {
    R_MUTEX_UNLOCK(sys_ble_mutex);
    SE_RET_ERR(ERR_BASE_INVALID_STATE, char_uuid);
  }

  for (uint8_t i = 0; i < c->linked_connector_count; i++) {
    if (c->linked_connectors[i] == conn) {
      R_MUTEX_UNLOCK(sys_ble_mutex);
      return NULL; // Already linked
    }
  }

  if (c->linked_connector_count >= CONFIG_SYS_BLE_MAX_LINKED_CONNECTORS) {
    R_MUTEX_UNLOCK(sys_ble_mutex);
    SE_RET_ERR(ERR_BASE_NO_MEM, char_uuid);
  }

  c->linked_connectors[c->linked_connector_count++] = conn;
  R_MUTEX_UNLOCK(sys_ble_mutex);

  ESP_LOGI(TAG, "Linked connector %s to characteristic UUID 0x%04X", conn->name, char_uuid);
  return NULL;
}

err_h sys_ble_char_unlink_connector(uint16_t char_uuid, sys_data_connector_t* conn) {
  SE_CHECK_NOT_NULL(conn);
  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);

  sys_ble_char_node_t* c = NULL;
  CHECK_BLE_CHAR_FIND(c, char_uuid, true);

  for (uint8_t i = 0; i < c->linked_connector_count; i++) {
    if (c->linked_connectors[i] == conn) {
      for (uint8_t j = i; j + 1 < c->linked_connector_count; j++) {
        c->linked_connectors[j] = c->linked_connectors[j + 1];
      }
      c->linked_connectors[--c->linked_connector_count] = NULL;
      R_MUTEX_UNLOCK(sys_ble_mutex);
      ESP_LOGI(TAG, "Unlinked connector %s from characteristic UUID 0x%04X", conn->name, char_uuid);
      return NULL;
    }
  }

  R_MUTEX_UNLOCK(sys_ble_mutex);
  return NULL;
}
#undef OWNER

#define OWNER OWNER_SYS_BLE_RX_INJECT
err_h sys_ble_rx_enqueue(sys_ble_char_node_t* c, const uint8_t* data, size_t len) {
  if (c->pending_remove || !c->rx_buff.buff) {
    SE_RET_ERR(ERR_BASE_INVALID_STATE, c->cfg.uuid);
  }

  err_h push_err = sys_buff_push(&c->rx_buff, data, len, 0);
  if (SE_IS_ERR(push_err)) {
    g_ble_ctx.rx_overflow_count++;
    return push_err;
  }

  for (uint8_t i = 0; i < c->linked_connector_count; i++) {
    if (c->linked_connectors[i] && c->linked_connectors[i]->data_present) {
      xSemaphoreGive(c->linked_connectors[i]->data_present);
    }
  }

  return NULL;
}

err_h sys_ble_char_rx_inject(uint16_t char_uuid, const uint8_t* data, size_t len) {
  SE_CHECK_NOT_NULL(data);
  if (len == 0) return NULL;

  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
  sys_ble_char_node_t* c = NULL;
  CHECK_BLE_CHAR_FIND(c, char_uuid, true);
  err_h err = sys_ble_rx_enqueue(c, data, len);
  R_MUTEX_UNLOCK(sys_ble_mutex);
  return err;
}
#undef OWNER

#define OWNER OWNER_SYS_BLE_SEND
err_h sys_ble_char_send(uint16_t char_uuid, const uint8_t* data, size_t len, bool return_when_full) {
  SE_CHECK_NOT_NULL(data);
  if (len == 0) return NULL;

  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);

  if (!g_ble_ctx.is_connected) {
    R_MUTEX_UNLOCK(sys_ble_mutex);
    return NULL;
  }

  sys_ble_char_node_t* c = NULL;
  CHECK_BLE_CHAR_FIND(c, char_uuid, true);

  if (c->pending_remove || !c->tx_buff.buff) {
    R_MUTEX_UNLOCK(sys_ble_mutex);
    SE_RET_ERR(ERR_BASE_INVALID_STATE, char_uuid);
  }

  sys_buff_t* buff = &c->tx_buff;
  R_MUTEX_UNLOCK(sys_ble_mutex);

  uint32_t wait_time_ms = return_when_full ? 0 : 100;
  SE_RET_IF_ERR(sys_buff_push(buff, data, len, wait_time_ms));

  xSemaphoreGive(sys_ble_tx_sem);
  return NULL;
}
#undef OWNER

/* Called with sys_ble_mutex held. */
#define OWNER OWNER_SYS_BLE_DATABASE_SYNC
static void sys_ble_svc_mark_registered(sys_ble_svc_node_t* s) {
  s->registered = true;
  s->dirty = false;
  sys_ble_char_node_t* c;
  LL_FOREACH(s->chars, c) {
    c->pending_add = false;
  }
}

static err_h sys_ble_svc_sync(sys_ble_svc_node_t* s) {
  if (s->registered && !s->dirty) return NULL;

  if (s->registered) {
    ble_uuid16_t uuid = BLE_UUID16_INIT(s->cfg.uuid);
    R_MUTEX_UNLOCK(sys_ble_mutex);
    int rc = ble_gatts_delete_svc(&uuid.u);
    R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
    if (rc != 0) SE_RET_ERR(ERR_BLE_GATT_FAILED, rc);

    s->registered = false; /* A failed compile/add is retried on the next sync. */
    sys_ble_free_compiled_gatt_db(s->compiled_def);
    s->compiled_def = NULL;
  }

  /* The stack no longer references this service's removed characteristics. */
  sys_ble_char_node_t *c, *tmp;
  LL_FOREACH_SAFE(s->chars, c, tmp) {
    c->val_handle = 0;
    if (c->pending_remove) {
      LL_DELETE(s->chars, c);
      sys_ble_free_char_node(c);
    }
  }

  struct ble_gatt_svc_def* svcs = calloc(2, sizeof(*svcs));
  if (!svcs) SE_RET_ERR(ERR_BASE_NO_MEM, s->cfg.uuid);
  err_h err = populate_svc_def(&svcs[0], s);
  if (SE_IS_ERR(err)) {
    sys_ble_free_compiled_gatt_db(svcs);
    return err;
  }

  R_MUTEX_UNLOCK(sys_ble_mutex);
  int rc = ble_gatts_add_dynamic_svcs(svcs);
  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
  if (rc != 0) {
    sys_ble_free_compiled_gatt_db(svcs);
    SE_RET_ERR(ERR_BLE_GATT_FAILED, rc);
  }

  s->compiled_def = svcs;
  sys_ble_svc_mark_registered(s);
  /* NimBLE's dynamic add/delete APIs issue Service Changed themselves. */
  return NULL;
}

static err_h sys_ble_database_start(void) {
  size_t count = 0;
  sys_ble_svc_node_t* s;
  LL_FOREACH(g_ble_ctx.services, s) {
    count++;
  }

  struct ble_gatt_svc_def* svcs = calloc(count + 1, sizeof(*svcs));
  if (!svcs) SE_RET_ERR(ERR_BASE_NO_MEM, 0);
  size_t idx = 0;
  err_h err = NULL;
  LL_FOREACH(g_ble_ctx.services, s) {
    err = populate_svc_def(&svcs[idx++], s);
    if (SE_IS_ERR(err)) break;
  }
  if (SE_IS_OK(err)) err = sys_ble_stack_init(svcs);
  if (SE_IS_ERR(err)) {
    sys_ble_free_compiled_gatt_db(svcs);
    return err;
  }

  g_ble_ctx.compiled_db = svcs;
  g_ble_ctx.driver_started = true;
  LL_FOREACH(g_ble_ctx.services, s) {
    sys_ble_svc_mark_registered(s);
  }
  return NULL;
}

err_h sys_ble_database_sync(void) {
  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
  err_h err = NULL;
  if (!g_ble_ctx.driver_started) {
    err = sys_ble_database_start();
  } else {
    sys_ble_svc_node_t* s;
    LL_FOREACH(g_ble_ctx.services, s) {
      err = sys_ble_svc_sync(s);
      if (SE_IS_ERR(err)) break;
    }
  }
  R_MUTEX_UNLOCK(sys_ble_mutex);
  /* Resume packets queued before their characteristic became live. */
  xSemaphoreGive(sys_ble_tx_sem);
  return err;
}
#undef OWNER

#define OWNER OWNER_SYS_BLE_GET_STATUS
err_h sys_ble_get_status(sys_ble_status_t* out_status) {
  SE_CHECK_NOT_NULL(out_status);
  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
  out_status->is_connected = g_ble_ctx.is_connected;
  out_status->mtu_size = g_ble_ctx.mtu_size;
  out_status->rx_overflow_count = g_ble_ctx.rx_overflow_count;
  R_MUTEX_UNLOCK(sys_ble_mutex);
  return NULL;
}
#undef OWNER
