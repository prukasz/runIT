#include "sys_ble_priv.h"

static const char* TAG = __FILE_NAME__;

sys_ble_ctx_t g_ble_ctx = {.mtu_size = BLE_ATT_MTU_DFLT};

R_MUTEX_DEFINE(sys_ble_mutex);
R_BINARY_SEM_DEFINE(sys_ble_tx_sem);

/*****************************************************************************************/
/* Helper Data Structure Management                                                      */
/*****************************************************************************************/

/* Lookups skip a service being removed: its UUIDs are free for the same batch. */
static sys_ble_char_node_t* sys_ble_find_char_by_uuid(uint16_t char_uuid) {
  sys_ble_svc_node_t* s;
  LL_FOREACH(g_ble_ctx.services, s) {
    if (s->removing) continue;
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
    if (!s->removing && s->cfg.uuid == svc_uuid) return s;
  }
  return NULL;
}

static void sys_ble_free_char_node(sys_ble_char_node_t* c) {
  if (!c) return;
  SE_release(sys_buff_free(&c->rx_buff));
  SE_release(sys_buff_free(&c->tx_buff));
  free(c->desc);
  free(c);
}

/* Free a service node and its characteristics (no longer in NimBLE). */
static void sys_ble_free_svc_node(sys_ble_svc_node_t* s) {
  if (s->compiled_def) sys_ble_free_compiled_gatt_db(s->compiled_def);
  sys_ble_char_node_t *c, *tmp;
  LL_FOREACH_SAFE(s->chars, c, tmp) {
    sys_ble_free_char_node(c);
  }
  LL_DELETE(g_ble_ctx.services, s);
  free(s);
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
    SE_FAIL(ERR_BLE_UUID_TAKEN, cfg->uuid);
  }

  sys_ble_svc_node_t* new_svc = calloc(1, sizeof(sys_ble_svc_node_t));
  if (!new_svc) {
    R_MUTEX_UNLOCK(sys_ble_mutex);
    SE_FAIL(ERR_BASE_NO_MEM, cfg->uuid);
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
  if (target->locked) {
    R_MUTEX_UNLOCK(sys_ble_mutex);
    SE_FAIL(ERR_BLE_SERVICE_LOCKED, svc_uuid);
  }

  /* A live service stays in NimBLE until the next sync (sys_ble_database_apply);
     one never registered goes now. */
  if (target->registered) {
    target->removing = true;
  } else {
    sys_ble_free_svc_node(target);
  }

  R_MUTEX_UNLOCK(sys_ble_mutex);
  ESP_LOGI(TAG, "Removed BLE service UUID 0x%04X", svc_uuid);
  return NULL;
}
#undef OWNER

#define OWNER OWNER_SYS_BLE_SERVICE_LOCK
err_h sys_ble_service_lock(uint16_t svc_uuid) {
  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
  sys_ble_svc_node_t* svc = NULL;
  CHECK_BLE_SVC_FIND(svc, svc_uuid, true);
  svc->locked = true;
  R_MUTEX_UNLOCK(sys_ble_mutex);
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
      SE_FAIL(ERR_BASE_NO_MEM, svc_uuid);
    }
    svc->cfg = (sys_ble_svc_cfg_t){.uuid = svc_uuid, .is_primary = true};
    LL_APPEND(g_ble_ctx.services, svc);
  }
  if (!svc) {
    R_MUTEX_UNLOCK(sys_ble_mutex);
    SE_FAIL(ERR_BASE_NOT_FOUND, svc_uuid);
  }
  if (svc->locked) {
    R_MUTEX_UNLOCK(sys_ble_mutex);
    SE_FAIL(ERR_BLE_SERVICE_LOCKED, svc_uuid);
  }

  if (sys_ble_find_char_by_uuid(cfg->uuid)) {
    R_MUTEX_UNLOCK(sys_ble_mutex);
    SE_FAIL(ERR_BLE_UUID_TAKEN, cfg->uuid);
  }

  sys_ble_char_node_t* new_char = calloc(1, sizeof(*new_char));
  if (!new_char) {
    R_MUTEX_UNLOCK(sys_ble_mutex);
    SE_FAIL(ERR_BASE_NO_MEM, cfg->uuid);
  }
  new_char->cfg = *cfg;
  if (cfg->desc) {
    new_char->desc = strdup(cfg->desc);
    if (!new_char->desc) {
      free(new_char);
      R_MUTEX_UNLOCK(sys_ble_mutex);
      SE_FAIL(ERR_BASE_NO_MEM, cfg->uuid);
    }
    new_char->cfg.desc = new_char->desc;
  }

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
  if (svc->locked) {
    R_MUTEX_UNLOCK(sys_ble_mutex);
    SE_FAIL(ERR_BLE_SERVICE_LOCKED, svc_uuid);
  }

  sys_ble_char_node_t* target = NULL;
  LL_FOREACH(svc->chars, target) {
    if (target->cfg.uuid == char_uuid) break;
  }
  if (!target) {
    R_MUTEX_UNLOCK(sys_ble_mutex);
    SE_FAIL(ERR_BASE_NOT_FOUND, char_uuid);
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
    SE_FAIL(ERR_BASE_INVALID_STATE, char_uuid);
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
    SE_FAIL(ERR_BASE_INVALID_STATE, char_uuid);
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
err_h sys_ble_char_link_rx_wake(uint16_t char_uuid, sys_ble_rx_wake_f wake, void* ctx) {
  SE_CHECK_NOT_NULL(wake);
  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);

  sys_ble_char_node_t* c = NULL;
  CHECK_BLE_CHAR_FIND(c, char_uuid, true);

  if (c->pending_remove || !c->rx_buff.buff) {
    R_MUTEX_UNLOCK(sys_ble_mutex);
    SE_FAIL(ERR_BASE_INVALID_STATE, char_uuid);
  }

  for (uint8_t i = 0; i < c->rx_wake_count; i++) {
    if (c->rx_wakes[i].wake == wake && c->rx_wakes[i].ctx == ctx) {
      R_MUTEX_UNLOCK(sys_ble_mutex);
      return NULL; // Already linked
    }
  }

  if (c->rx_wake_count >= CONFIG_SYS_BLE_MAX_LINKED_CONNECTORS) {
    R_MUTEX_UNLOCK(sys_ble_mutex);
    SE_FAIL(ERR_BASE_NO_MEM, char_uuid);
  }

  c->rx_wakes[c->rx_wake_count].wake = wake;
  c->rx_wakes[c->rx_wake_count].ctx = ctx;
  c->rx_wake_count++;
  R_MUTEX_UNLOCK(sys_ble_mutex);

  ESP_LOGI(TAG, "Linked RX wake to characteristic UUID 0x%04X", char_uuid);
  return NULL;
}

err_h sys_ble_char_unlink_rx_wake(uint16_t char_uuid, sys_ble_rx_wake_f wake, void* ctx) {
  SE_CHECK_NOT_NULL(wake);
  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);

  sys_ble_char_node_t* c = NULL;
  CHECK_BLE_CHAR_FIND(c, char_uuid, true);

  for (uint8_t i = 0; i < c->rx_wake_count; i++) {
    if (c->rx_wakes[i].wake == wake && c->rx_wakes[i].ctx == ctx) {
      for (uint8_t j = i; j + 1 < c->rx_wake_count; j++) {
        c->rx_wakes[j] = c->rx_wakes[j + 1];
      }
      c->rx_wake_count--;
      c->rx_wakes[c->rx_wake_count].wake = NULL;
      c->rx_wakes[c->rx_wake_count].ctx = NULL;
      R_MUTEX_UNLOCK(sys_ble_mutex);
      ESP_LOGI(TAG, "Unlinked RX wake from characteristic UUID 0x%04X", char_uuid);
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
    SE_FAIL(ERR_BASE_INVALID_STATE, c->cfg.uuid);
  }

  err_h push_err = sys_buff_push(&c->rx_buff, data, len, 0);
  if (SE_IS_ERR(push_err)) {
    g_ble_ctx.rx_overflow_count++;
    return push_err;
  }

  for (uint8_t i = 0; i < c->rx_wake_count; i++) {
    c->rx_wakes[i].wake(c->rx_wakes[i].ctx);
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
    SE_FAIL(ERR_BASE_INVALID_STATE, char_uuid);
  }

  sys_buff_t* buff = &c->tx_buff;
  R_MUTEX_UNLOCK(sys_ble_mutex);

  uint32_t wait_time_ms = return_when_full ? 0 : 100;
  SE_TRY(sys_buff_push(buff, data, len, wait_time_ms));

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

static SE_MUST_USE err_h sys_ble_svc_sync(sys_ble_svc_node_t* s) {
  if (s->registered && !s->dirty) return NULL;

  if (s->registered) {
    ble_uuid16_t uuid = BLE_UUID16_INIT(s->cfg.uuid);
    R_MUTEX_UNLOCK(sys_ble_mutex);
    int rc = ble_gatts_delete_svc(&uuid.u);
    R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
    if (rc != 0) SE_FAIL(ERR_BLE_GATT_FAILED, rc);

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
  if (!svcs) SE_FAIL(ERR_BASE_NO_MEM, s->cfg.uuid);
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
    SE_FAIL(ERR_BLE_GATT_FAILED, rc);
  }

  s->compiled_def = svcs;
  sys_ble_svc_mark_registered(s);
  /* NimBLE's dynamic add/delete APIs issue Service Changed themselves. */
  return NULL;
}

static SE_MUST_USE err_h sys_ble_database_start(void) {
  size_t count = 0;
  sys_ble_svc_node_t* s;
  LL_FOREACH(g_ble_ctx.services, s) {
    count++;
  }

  struct ble_gatt_svc_def* svcs = calloc(count + 1, sizeof(*svcs));
  if (!svcs) SE_FAIL(ERR_BASE_NO_MEM, 0);
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

/* Delete a removed service from NimBLE and free it. */
static SE_MUST_USE err_h sys_ble_svc_delete(sys_ble_svc_node_t* s) {
  ble_uuid16_t uuid = BLE_UUID16_INIT(s->cfg.uuid);
  /* GATT access callbacks take sys_ble_mutex while the host is locked. */
  R_MUTEX_UNLOCK(sys_ble_mutex);
  int rc = ble_gatts_delete_svc(&uuid.u);
  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
  if (rc != 0) SE_FAIL(ERR_BLE_GATT_FAILED, rc);
  sys_ble_free_svc_node(s);
  return NULL;
}

err_h sys_ble_database_sync(void) {
  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
  err_h err = NULL;
  g_ble_ctx.apply_pending = false;
  if (!g_ble_ctx.driver_started) {
    err = sys_ble_database_start();
  } else {
    /* Removals first: a service removed and created again in one batch has the same UUID. */
    sys_ble_svc_node_t *s, *tmp;
    LL_FOREACH_SAFE(g_ble_ctx.services, s, tmp) {
      if (!s->removing) continue;
      err = sys_ble_svc_delete(s);
      if (SE_IS_ERR(err)) break;
    }
    if (SE_IS_OK(err)) {
      LL_FOREACH(g_ble_ctx.services, s) {
        err = sys_ble_svc_sync(s);
        if (SE_IS_ERR(err)) break;
      }
    }
  }
  R_MUTEX_UNLOCK(sys_ble_mutex);
  /* Resume packets queued before their characteristic became live. */
  xSemaphoreGive(sys_ble_tx_sem);
  return err;
}

err_h sys_ble_database_apply(void) {
  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
  bool later = g_ble_ctx.driver_started && g_ble_ctx.is_connected;
  if (later) {
    g_ble_ctx.apply_pending = true;
    g_ble_ctx.apply_at = xTaskGetTickCount() + pdMS_TO_TICKS(SYS_BLE_APPLY_DELAY_MS);
  }
  R_MUTEX_UNLOCK(sys_ble_mutex);
  if (!later) return sys_ble_database_sync();
  xSemaphoreGive(sys_ble_tx_sem);
  return NULL;
}
#undef OWNER

size_t sys_ble_char_max_payload(uint16_t char_uuid) {
  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
  const sys_ble_char_node_t* c = sys_ble_find_char_by_uuid(char_uuid);
  size_t buffer = (c && !c->pending_remove) ? sys_buff_max_item(&c->tx_buff) : 0;
  uint16_t mtu = g_ble_ctx.is_connected ? g_ble_ctx.mtu_size : BLE_ATT_MTU_MAX;
  R_MUTEX_UNLOCK(sys_ble_mutex);
  size_t link = (size_t)(mtu - SYS_BLE_ATT_NOTIFY_HDR_LEN);
  return buffer < link ? buffer : link;
}

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
