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
  sys_cb_register_route(SYS_CB_ROUTE_BLE, sys_ble_cb_dummy_log);
}

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
        if (g_ble_ctx.tx_slot_count < CONFIG_SYS_BLE_MAX_TOTAL_TX_SLOTS) {
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
  sys_buff_free(&c->rx_buff);
  for (int i = 0; i < c->tx_slot_count; i++) {
    sys_buff_free(&c->tx_slots[i].tx_buff);
  }
  free(c);
}

/*****************************************************************************************/
/* Public Application API                                                                */
/*****************************************************************************************/

#define OWNER OWNER_SYS_BLE_CREATE
err_h sys_ble_init(void) {
  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
  if (g_ble_ctx.initialized) {
    R_MUTEX_UNLOCK(sys_ble_mutex);
    return NULL;
  }
  g_ble_ctx.initialized = true;
  R_MUTEX_UNLOCK(sys_ble_mutex);

  ESP_LOGI(TAG, "BLE Manager initialized successfully");
  return NULL;
}

err_h sys_ble_add_callback(sys_ble_events_e on_event, uint16_t route_mask, uint64_t action_mask) {
  SE_CHECK_IN_RANGE((uint32_t)on_event, 0, SYS_BLE_EVENT_MAX - 1);

  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
  g_ble_ctx.route_masks[on_event] = route_mask;
  g_ble_ctx.action_masks[on_event] = action_mask;
  R_MUTEX_UNLOCK(sys_ble_mutex);

  return NULL;
}
#undef OWNER

#define OWNER OWNER_SYS_BLE_SERVICE_CREATE
err_h sys_ble_service_create(const sys_ble_svc_cfg_t* cfg) {
  SE_CHECK_NOT_NULL(cfg);

  if (sys_ble_find_svc_by_uuid(cfg->uuid)) {
    SE_RET_ERR(ERR_DEV_ALREADY_EXIST, cfg->uuid);
  }

  sys_ble_svc_node_t* new_svc = calloc(1, sizeof(sys_ble_svc_node_t));
  SE_CHECK_IF_ALLOCATED(new_svc);

  new_svc->cfg = *cfg;
  new_svc->registered = false;
  new_svc->compiled_def = NULL;

  LL_APPEND(g_ble_ctx.services, new_svc);

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

    int rc = ble_gatts_delete_svc((const ble_uuid_t*)&temp_uuid);
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

  sys_ble_rebuild_active_tx_slots();

  R_MUTEX_UNLOCK(sys_ble_mutex);
  ESP_LOGI(TAG, "Removed BLE service UUID 0x%04X", svc_uuid);
  return NULL;
}
#undef OWNER

#define OWNER OWNER_SYS_BLE_CHAR_CREATE
err_h sys_ble_char_create(uint16_t svc_uuid, const sys_ble_char_create_t* cfg) {
  SE_CHECK_NOT_NULL(cfg);
  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);

  sys_ble_svc_node_t* svc = NULL;
  CHECK_BLE_SVC_FIND(svc, svc_uuid, true);

  if (sys_ble_find_char_by_uuid(cfg->info.uuid)) {
    R_MUTEX_UNLOCK(sys_ble_mutex);
    SE_RET_ERR(ERR_DEV_ALREADY_EXIST, cfg->info.uuid);
  }

  sys_ble_char_node_t* new_char = calloc(1, sizeof(sys_ble_char_node_t));
  if (!new_char) {
    R_MUTEX_UNLOCK(sys_ble_mutex);
    SE_RET_ERR(ERR_BASE_NO_MEM, cfg->info.uuid);
  }

  new_char->cfg = cfg->info;

  if (cfg->rx_buffer_size > 0) {
    err_h rx_buf_err = sys_buff_init(&new_char->rx_buff, 0, cfg->rx_buffer_size);
    if (SE_IS_ERR(rx_buf_err)) {
      sys_buff_free(&new_char->rx_buff);
      free(new_char);
      R_MUTEX_UNLOCK(sys_ble_mutex);
      SE_RET_ERR(ERR_BASE_NO_MEM, cfg->info.uuid);
    }
    new_char->rx_notify_sem = cfg->rx_notify_sem;
  }

  new_char->pending_add = true;
  LL_APPEND(svc->chars, new_char);

  if (svc->registered) {
    svc->dirty = true;
  }

  R_MUTEX_UNLOCK(sys_ble_mutex);
  ESP_LOGI(TAG, "Created characteristic UUID 0x%04X under service UUID 0x%04X", cfg->info.uuid, svc_uuid);
  return NULL;
}
#undef OWNER

#define OWNER OWNER_SYS_BLE_CHAR_REMOVE
err_h sys_ble_char_remove(uint16_t svc_uuid, uint16_t char_uuid) {
  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);

  sys_ble_svc_node_t* svc = NULL;
  CHECK_BLE_SVC_FIND(svc, svc_uuid, true);

  sys_ble_char_node_t* target = NULL;
  CHECK_BLE_CHAR_FIND(target, char_uuid, true);

  if (svc->registered) {
    /* NimBLE may still reference this node's val_handle/arg until the service
       is actually deleted+recompiled - defer the free to sys_ble_database_sync(). */
    target->pending_remove = true;
    svc->dirty = true;
  } else {
    LL_DELETE(svc->chars, target);
    sys_ble_free_char_node(target);
  }

  sys_ble_rebuild_active_tx_slots();

  R_MUTEX_UNLOCK(sys_ble_mutex);
  ESP_LOGI(TAG, "Removed BLE characteristic UUID 0x%04X", char_uuid);
  return NULL;
}
#undef OWNER

#define OWNER OWNER_SYS_BLE_CHAR_ASSIGN_TX
err_h sys_ble_char_assign_tx_buffer(uint16_t char_uuid, const sys_ble_tx_buf_cfg_t* buf_cfg) {
  SE_CHECK_NOT_NULL(buf_cfg);

  sys_ble_char_node_t* c = NULL;
  CHECK_BLE_CHAR_FIND(c, char_uuid, false);

  if (c->tx_slot_count >= CONFIG_SYS_BLE_MAX_TX_BUFFERS) {
    SE_RET_ERR(ERR_BASE_NO_MEM, char_uuid);
  }

  for (int i = 0; i < c->tx_slot_count; i++) {
    if (c->tx_slots[i].tx_buff.header == buf_cfg->header) {
      SE_RET_ERR(ERR_DEV_ALREADY_EXIST, buf_cfg->header);
    }
  }

  sys_ble_tx_slot_t* slot = &c->tx_slots[c->tx_slot_count];
  slot->is_indication = buf_cfg->is_indication;

  SE_RET_IF_ERR(sys_buff_init(&slot->tx_buff, buf_cfg->header, buf_cfg->size));

  c->tx_slot_count++;

  ESP_LOGI(TAG, "Assigned TX buffer header 0x%02X to characteristic UUID 0x%04X", buf_cfg->header, char_uuid);
  return NULL;
}
#undef OWNER

#define OWNER OWNER_SYS_BLE_GET_STATUS
err_h sys_ble_char_check_rx_enabled(uint16_t char_uuid) {
  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);

  sys_ble_char_node_t* c = NULL;
  CHECK_BLE_CHAR_FIND(c, char_uuid, true);

  if (!c->rx_buff.buff) {
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

  if (!c->rx_buff.buff) {
    R_MUTEX_UNLOCK(sys_ble_mutex);
    SE_RET_ERR(ERR_BASE_INVALID_STATE, char_uuid);
  }
  sys_buff_t* rx_buff = &c->rx_buff;
  R_MUTEX_UNLOCK(sys_ble_mutex);

  err_h pop_res = sys_buff_pop_raw(rx_buff, buffer, max_len, out_len);
  if (SE_IS_ERR(pop_res)) {
    if (pop_res->tag == ERR_BASE_NOT_FOUND) {
      *out_len = 0;
      return NULL;
    }
    return pop_res;
  }

  return NULL;
}
#undef OWNER

#define OWNER OWNER_SYS_BLE_RX_INJECT
err_h sys_ble_char_rx_inject(uint16_t char_uuid, const uint8_t* data, size_t len) {
  SE_CHECK_NOT_NULL(data);
  if (len == 0) return NULL;

  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
  sys_ble_char_node_t* c = NULL;
  CHECK_BLE_CHAR_FIND(c, char_uuid, true);

  if (!c->rx_buff.buff) {
    R_MUTEX_UNLOCK(sys_ble_mutex);
    SE_RET_ERR(ERR_BASE_INVALID_STATE, char_uuid);
  }
  sys_buff_t* rx_buff = &c->rx_buff;
  SemaphoreHandle_t rx_notify_sem = c->rx_notify_sem;
  R_MUTEX_UNLOCK(sys_ble_mutex);

  SE_RET_IF_ERR(sys_buff_push(rx_buff, data, len, 0));
  ESP_LOGI(TAG, "Injected %u bytes into RX buffer of characteristic UUID 0x%04X", (unsigned)len, char_uuid);
  if (rx_notify_sem) xSemaphoreGive(rx_notify_sem);
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_BLE_SEND
err_h sys_ble_char_send(uint16_t char_uuid, uint8_t header, const uint8_t* data, size_t len, bool return_when_full) {
  SE_CHECK_NOT_NULL(data);
  if (len == 0) return NULL;

  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);

  sys_ble_char_node_t* c = NULL;
  CHECK_BLE_CHAR_FIND(c, char_uuid, true);

  sys_ble_tx_slot_t* slot = NULL;
  for (int i = 0; i < c->tx_slot_count; i++) {
    if (c->tx_slots[i].tx_buff.header == header) {
      slot = &c->tx_slots[i];
      break;
    }
  }

  if (!slot || !slot->tx_buff.buff) {
    R_MUTEX_UNLOCK(sys_ble_mutex);
    SE_RET_ERR(ERR_BASE_NOT_FOUND, header);
  }

  sys_buff_t* buff = &slot->tx_buff;
  R_MUTEX_UNLOCK(sys_ble_mutex);

  uint32_t wait_time_ms = return_when_full ? 0 : 100;
  SE_RET_IF_ERR(sys_buff_push(buff, data, len, wait_time_ms));

  xSemaphoreGive(sys_ble_tx_sem);
  return NULL;
}
#undef OWNER

/*Compiles a single service's current characteristic list and (re)adds it to a
  running NimBLE stack via ble_gatts_add_dynamic_svcs(). Shared by the
  "brand-new service" and "dirty, already-live service" paths in
  sys_ble_database_sync() below. Must be called with sys_ble_mutex held;
  unlocks/relocks around the NimBLE call, same as the rest of this file.*/
#define OWNER OWNER_SYS_BLE_DATABASE_SYNC
static err_h sys_ble_svc_compile_and_add(sys_ble_svc_node_t* s) {
  struct ble_gatt_svc_def* svcs = calloc(2, sizeof(struct ble_gatt_svc_def));
  if (!svcs) {
    SE_RET_ERR(ERR_BASE_NO_MEM, s->cfg.uuid);
  }

  err_h pop_res = populate_svc_def(&svcs[0], s);
  if (SE_IS_ERR(pop_res)) {
    free(svcs);
    return pop_res;
  }

  R_MUTEX_UNLOCK(sys_ble_mutex);
  int rc = ble_gatts_add_dynamic_svcs(svcs);
  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);

  if (rc != 0) {
    ESP_LOGE(TAG, "Failed to dynamically add service UUID 0x%04X: %d", s->cfg.uuid, rc);
    sys_ble_free_compiled_gatt_db(svcs);
    SE_RET_ERR(ERR_BLE_GATT_FAILED, rc);
  }

  s->registered = true;
  s->compiled_def = svcs;
  s->dirty = false;

  sys_ble_char_node_t* c;
  LL_FOREACH(s->chars, c) {
    c->pending_add = false;
  }

  return NULL;
}

err_h sys_ble_database_sync(void) {
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
      SE_RET_ERR(ERR_BASE_NO_MEM, 0);
    }

    int s_idx = 0;
    LL_FOREACH(g_ble_ctx.services, s) {
      err_h pop_res = populate_svc_def(&svcs[s_idx], s);
      if (SE_IS_ERR(pop_res)) {
        sys_ble_free_compiled_gatt_db(svcs);
        R_MUTEX_UNLOCK(sys_ble_mutex);
        return pop_res;
      }
      s->registered = true;
      s->dirty = false;
      s->compiled_def = NULL;
      sys_ble_char_node_t* c;
      LL_FOREACH(s->chars, c) {
        c->pending_add = false;
      }
      s_idx++;
    }

    g_ble_ctx.compiled_db = svcs;

    err_h init_res = sys_ble_stack_init(svcs);
    if (SE_IS_ERR(init_res)) {
      sys_ble_free_compiled_gatt_db(svcs);
      R_MUTEX_UNLOCK(sys_ble_mutex);
      return init_res;
    }

    g_ble_ctx.driver_started = true;
    R_MUTEX_UNLOCK(sys_ble_mutex);
    sys_ble_rebuild_active_tx_slots();
    ESP_LOGI(TAG, "BLE database compilation and stack sync complete");
    return NULL;
  } else {
    sys_ble_svc_node_t* s;
    LL_FOREACH(g_ble_ctx.services, s) {
      if (!s->registered) {
        /* Brand-new service, never added to the running stack yet. */
        err_h add_res = sys_ble_svc_compile_and_add(s);
        if (SE_IS_ERR(add_res)) {
          R_MUTEX_UNLOCK(sys_ble_mutex);
          return add_res;
        }
        continue;
      }

      if (!s->dirty) {
        /* Already live and unchanged since last sync - nothing to do. */
        continue;
      }

      /* Already live, but a characteristic was added/removed since - NimBLE's
         dynamic-add API is service-granularity only, so the only way to add
         (or truly remove) a characteristic on a live service is to delete the
         whole service and recompile+re-add it from the current node list. */
      ble_uuid16_t temp_uuid;
      temp_uuid.u.type = BLE_UUID_TYPE_16;
      temp_uuid.value = s->cfg.uuid;

      R_MUTEX_UNLOCK(sys_ble_mutex);
      int del_rc = ble_gatts_delete_svc((const ble_uuid_t*)&temp_uuid);
      R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);

      if (del_rc != 0) {
        ESP_LOGE(TAG, "Failed to delete service 0x%04X from NimBLE for recompile: %d", s->cfg.uuid, del_rc);
        R_MUTEX_UNLOCK(sys_ble_mutex);
        SE_RET_ERR(ERR_BLE_GATT_FAILED, del_rc);
      }

      if (s->compiled_def) {
        sys_ble_free_compiled_gatt_db(s->compiled_def);
        s->compiled_def = NULL;
      }

      /* Physically drop characteristics that were marked for removal while
         the service was live - safe now that NimBLE no longer references them. */
      sys_ble_char_node_t *c, *tmp;
      LL_FOREACH_SAFE(s->chars, c, tmp) {
        if (c->pending_remove) {
          LL_DELETE(s->chars, c);
          sys_ble_free_char_node(c);
        }
      }

      /* Mark unregistered so a failed re-add below leaves the service in the
         "brand-new, not yet added" state - the next sync() call retries it
         from scratch rather than getting stuck half-deleted. */
      s->registered = false;

      err_h add_res = sys_ble_svc_compile_and_add(s);
      if (SE_IS_ERR(add_res)) {
        R_MUTEX_UNLOCK(sys_ble_mutex);
        return add_res;
      }

      /* Tell already-subscribed/bonded clients the GATT db changed so they
         re-discover. Full-range invalidation is deliberately used here rather
         than tracking exact per-service handle ranges - this path is rare
         (post-boot dynamic add), not a hot path. */
      ble_svc_gatt_changed(0x0001, 0xffff);
    }

    R_MUTEX_UNLOCK(sys_ble_mutex);
    sys_ble_rebuild_active_tx_slots();
    ESP_LOGI(TAG, "Dynamic BLE service registration complete");
    return NULL;
  }
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

/*****************************************************************************************/
/* Unified Declarative Channel API                                                       */
/*****************************************************************************************/

#define OWNER OWNER_SYS_BLE_CHANNEL_CREATE
err_h sys_ble_channel_create(const sys_ble_channel_cfg_t* cfg, bool sync_now) {
  SE_CHECK_NOT_NULL(cfg);

  uint16_t svc_uuid = cfg->svc_uuid ? cfg->svc_uuid : SYS_BLE_SVC_DEFAULT_AUTO;

  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
  bool svc_exists = sys_ble_find_svc_by_uuid(svc_uuid) != NULL;
  R_MUTEX_UNLOCK(sys_ble_mutex);

  if (!svc_exists) {
    sys_ble_svc_cfg_t svc_cfg = {.uuid = svc_uuid, .is_primary = true};
    err_h svc_err = sys_ble_service_create(&svc_cfg);
    /* Tolerate a race where the service was created concurrently between the
       lookup above and this call - anything else is a real failure. */
    if (SE_IS_ERR(svc_err) && svc_err->tag != ERR_DEV_ALREADY_EXIST) {
      return svc_err;
    }
  }

  sys_ble_char_create_t chr_cfg = {.info = cfg->chr, .rx_buffer_size = cfg->rx_buffer_size, .rx_notify_sem = cfg->rx_notify_sem};
  SE_RET_IF_ERR(sys_ble_char_create(svc_uuid, &chr_cfg));

  for (uint8_t i = 0; i < cfg->tx_buf_count; i++) {
    SE_RET_IF_ERR(sys_ble_char_assign_tx_buffer(cfg->chr.uuid, &cfg->tx_bufs[i]));
  }

  if (cfg->rx_mode == SYS_BLE_RX_MODE_CALLBACK) {
    R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
    sys_ble_char_node_t* c = sys_ble_find_char_by_uuid(cfg->chr.uuid);
    if (c) {
      c->rx_handler = cfg->rx_handler;
    }
    R_MUTEX_UNLOCK(sys_ble_mutex);
  }

  if (sync_now) {
    SE_RET_IF_ERR(sys_ble_database_sync());
  }

  ESP_LOGI(TAG, "Created BLE channel UUID 0x%04X under service UUID 0x%04X", cfg->chr.uuid, svc_uuid);
  return NULL;
}
#undef OWNER
