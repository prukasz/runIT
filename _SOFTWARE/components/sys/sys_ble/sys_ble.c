

#include "host/ble_hs.h"
#include "sys_ble_priv.h"

ERR_MODULE_DEFINE(ERR_MOD_SYS_BLE, "sys_ble", ERR_SYS_BLE_MAP)

#define SEND_RETRY_MS 2u  // sys_ble_char_send: poll interval while the TX buffer is full

sys_ble_ctx_t g_ble = {.mtu = BLE_ATT_MTU_DFLT};
U_MUTEX_DEFINE(sys_ble_mutex);
U_BINARY_SEM_DEFINE(sys_ble_tx_sem);

static sys_ble_conn_f s_conn_listeners[CONFIG_SYS_BLE_MAX_CONN_LISTENERS];
static uint8_t s_pkt[BLE_ATT_MTU_MAX];  // BLE task only: one outgoing packet

static void wake_task(void) {
  xSemaphoreGive(sys_ble_tx_sem);
}

/* ----------------------------------------------------------------------------
 * Registry (caller holds the lock)
 * ------------------------------------------------------------------------- */

// Lookups skip a service being removed: its UUIDs are free for the same batch.
static sys_ble_char_node_t* find_char(uint16_t uuid) {
  sys_ble_svc_node_t* s;
  LL_FOREACH(g_ble.services, s) {
    if (s->removing) {
      continue;
    }
    sys_ble_char_node_t* c;
    LL_FOREACH(s->chars, c) {
      if (c->cfg.uuid == uuid) {
        return c;
      }
    }
  }
  return NULL;
}

static sys_ble_svc_node_t* find_svc(uint16_t uuid) {
  sys_ble_svc_node_t* s;
  LL_FOREACH(g_ble.services, s) {
    if (!s->removing && s->cfg.uuid == uuid) {
      return s;
    }
  }
  return NULL;
}

sys_ble_char_node_t* sys_ble_find_char_by_handle(uint16_t val_handle) {
  sys_ble_svc_node_t* s;
  LL_FOREACH(g_ble.services, s) {
    sys_ble_char_node_t* c;
    LL_FOREACH(s->chars, c) {
      if (c->val_handle == val_handle) {
        return c;
      }
    }
  }
  return NULL;
}

static void free_char(sys_ble_char_node_t* c) {
  if (c->rx_rb != NULL) {
    vRingbufferDelete(c->rx_rb);
  }
  if (c->tx_rb != NULL) {
    vRingbufferDelete(c->tx_rb);
  }
  free(c->desc);
  free(c);
}

static void free_svc(sys_ble_svc_node_t* s) {
  sys_ble_free_svc_defs(s->compiled_def);
  sys_ble_char_node_t *c, *tmp;
  LL_FOREACH_SAFE(s->chars, c, tmp) {
    free_char(c);
  }
  LL_DELETE(g_ble.services, s);
  free(s);
}

static void drain(RingbufHandle_t rb) {
  size_t size;
  void* item;
  while ((item = xRingbufferReceive(rb, &size, NO_WAIT)) != NULL) {
    vRingbufferReturnItem(rb, item);
  }
}

/* ----------------------------------------------------------------------------
 * GATT table
 * ------------------------------------------------------------------------- */

err_h sys_ble_service_create(const sys_ble_svc_cfg_t* cfg) {
  ERR_CHECK(cfg != NULL && cfg->uuid != 0, ERR_BLE_BAD_ARG);
  BLE_LOCK();
  bool taken = find_svc(cfg->uuid) != NULL;
  sys_ble_svc_node_t* s = taken ? NULL : calloc(1, sizeof(*s));
  if (s != NULL) {
    s->cfg = *cfg;
    LL_APPEND(g_ble.services, s);
  }
  BLE_UNLOCK();
  ERR_CHECK(!taken, ERR_BLE_UUID_TAKEN, .uuid = cfg->uuid);
  ERR_CHECK(s != NULL, ERR_BLE_NO_MEM, .uuid = cfg->uuid, .size = sizeof(*s));
  return NULL;
}

err_h sys_ble_service_remove(uint16_t svc_uuid) {
  BLE_LOCK();
  sys_ble_svc_node_t* s = find_svc(svc_uuid);
  bool locked = s != NULL && s->locked;
  if (s != NULL && !locked) {
    if (s->registered) {
      s->removing = true;  // stays in NimBLE until the next sync
    } else {
      free_svc(s);
    }
  }
  BLE_UNLOCK();
  ERR_CHECK(s != NULL, ERR_BLE_NOT_FOUND, .uuid = svc_uuid);
  ERR_CHECK(!locked, ERR_BLE_SVC_LOCKED, .uuid = svc_uuid);
  return NULL;
}

err_h sys_ble_service_lock(uint16_t svc_uuid) {
  BLE_LOCK();
  sys_ble_svc_node_t* s = find_svc(svc_uuid);
  if (s != NULL) {
    s->locked = true;
  }
  BLE_UNLOCK();
  ERR_CHECK(s != NULL, ERR_BLE_NOT_FOUND, .uuid = svc_uuid);
  return NULL;
}

// New characteristic node with its buffers; NULL when out of memory.
static sys_ble_char_node_t* new_char(const sys_ble_char_cfg_t* cfg) {
  sys_ble_char_node_t* c = calloc(1, sizeof(*c));
  if (c == NULL) {
    return NULL;
  }
  c->cfg = *cfg;
  c->cfg.desc = NULL;
  bool ok = true;
  if (cfg->desc != NULL && cfg->desc[0] != '\0') {
    c->desc = strdup(cfg->desc);
    c->cfg.desc = c->desc;
    ok = c->desc != NULL;
  }
  if (ok && cfg->rx_buffer_size > 0) {
    c->rx_rb = xRingbufferCreate(cfg->rx_buffer_size, RINGBUF_TYPE_NOSPLIT);
    ok = c->rx_rb != NULL;
  }
  if (ok && cfg->tx_buffer_size > 0) {
    c->tx_rb = xRingbufferCreate(cfg->tx_buffer_size, RINGBUF_TYPE_NOSPLIT);
    ok = c->tx_rb != NULL;
  }
  if (!ok) {
    free_char(c);
    return NULL;
  }
  c->pending_add = true;
  return c;
}

err_h sys_ble_char_create(uint16_t svc_uuid, const sys_ble_char_cfg_t* cfg) {
  ERR_CHECK(cfg != NULL && cfg->uuid != 0, ERR_BLE_BAD_ARG);
  ERR_CHECK(cfg->tx_buffer_size == 0 || cfg->is_notify || cfg->is_indicate, ERR_BLE_BAD_ARG);
  ERR_CHECK(cfg->rx_buffer_size == 0 || cfg->is_write, ERR_BLE_BAD_ARG);

  BLE_LOCK();
  sys_ble_svc_node_t* s = find_svc(svc_uuid);
  err_h err = NULL;
  if (s == NULL) {
    err = ERR_NEW(ERR_BLE_NOT_FOUND, .uuid = svc_uuid);
  } else if (s->locked) {
    err = ERR_NEW(ERR_BLE_SVC_LOCKED, .uuid = svc_uuid);
  } else if (find_char(cfg->uuid) != NULL || find_svc(cfg->uuid) != NULL) {
    err = ERR_NEW(ERR_BLE_UUID_TAKEN, .uuid = cfg->uuid);
  } else {
    sys_ble_char_node_t* c = new_char(cfg);
    if (c == NULL) {
      err = ERR_NEW(ERR_BLE_NO_MEM, .uuid = cfg->uuid,
                    .size = (uint32_t)(cfg->tx_buffer_size + cfg->rx_buffer_size));
    } else {
      LL_APPEND(s->chars, c);
      s->dirty = s->registered;
    }
  }
  BLE_UNLOCK();
  return err;
}

err_h sys_ble_char_remove(uint16_t svc_uuid, uint16_t char_uuid) {
  BLE_LOCK();
  sys_ble_svc_node_t* s = find_svc(svc_uuid);
  sys_ble_char_node_t* c = NULL;
  err_h err = NULL;
  if (s == NULL) {
    err = ERR_NEW(ERR_BLE_NOT_FOUND, .uuid = svc_uuid);
  } else if (s->locked) {
    err = ERR_NEW(ERR_BLE_SVC_LOCKED, .uuid = svc_uuid);
  } else {
    LL_FOREACH(s->chars, c) {
      if (c->cfg.uuid == char_uuid && !c->pending_remove) {
        break;
      }
    }
    if (c == NULL) {
      err = ERR_NEW(ERR_BLE_NOT_FOUND, .uuid = char_uuid);
    } else if (s->registered) {
      // NimBLE references the node until the service is rebuilt: freed at the next sync.
      c->pending_remove = true;
      s->dirty = true;
    } else {
      LL_DELETE(s->chars, c);
      free_char(c);
    }
  }
  BLE_UNLOCK();
  return err;
}

/* ----------------------------------------------------------------------------
 * Sync: staged table -> NimBLE
 * ------------------------------------------------------------------------- */

static void mark_registered(sys_ble_svc_node_t* s) {
  s->registered = true;
  s->dirty = false;
  sys_ble_char_node_t* c;
  LL_FOREACH(s->chars, c) {
    c->pending_add = false;
  }
}

static err_h start_locked(void) {
  size_t count = 0;
  sys_ble_svc_node_t* s;
  LL_FOREACH(g_ble.services, s) {
    count++;
  }
  struct ble_gatt_svc_def* svcs = calloc(count + 1, sizeof(*svcs));
  ERR_CHECK(svcs != NULL, ERR_BLE_NO_MEM, .uuid = 0, .size = (uint32_t)((count + 1) * sizeof(*svcs)));

  size_t i = 0;
  err_h err = NULL;
  LL_FOREACH(g_ble.services, s) {
    err = sys_ble_compile_svc(&svcs[i++], s);
    if (err != NULL) {
      break;
    }
  }
  if (err == NULL) {
    err = sys_ble_stack_start(svcs);  // host not running yet: no lock order issue
  }
  if (err != NULL) {
    sys_ble_free_svc_defs(svcs);
    return err;
  }
  g_ble.initial_db = svcs;
  g_ble.started = true;
  LL_FOREACH(g_ble.services, s) {
    mark_registered(s);
  }
  return NULL;
}

/* NimBLE calls run without the registry lock: GATT access callbacks take it
 * while the host is locked. */
static err_h delete_locked(uint16_t uuid) {
  BLE_UNLOCK();
  err_h err = sys_ble_stack_delete_svc(uuid);
  BLE_LOCK();
  return err;
}

static err_h sync_svc_locked(sys_ble_svc_node_t* s) {
  if (s->registered && !s->dirty) {
    return NULL;
  }
  if (s->registered) {
    ERR_TRY(delete_locked(s->cfg.uuid));
    s->registered = false;  // a failed rebuild is retried by the next sync
    sys_ble_free_svc_defs(s->compiled_def);
    s->compiled_def = NULL;
  }

  sys_ble_char_node_t *c, *tmp;
  LL_FOREACH_SAFE(s->chars, c, tmp) {
    c->val_handle = 0;
    c->is_subscribed = false;
    if (c->pending_remove) {
      LL_DELETE(s->chars, c);
      free_char(c);
    }
  }

  struct ble_gatt_svc_def* svcs = calloc(2, sizeof(*svcs));
  ERR_CHECK(svcs != NULL, ERR_BLE_NO_MEM, .uuid = s->cfg.uuid, .size = 2 * sizeof(*svcs));
  err_h err = sys_ble_compile_svc(&svcs[0], s);
  if (err == NULL) {
    BLE_UNLOCK();
    err = sys_ble_stack_add_svc(s, svcs);
    BLE_LOCK();
  }
  if (err != NULL) {
    sys_ble_free_svc_defs(svcs);
    return err;
  }
  s->compiled_def = svcs;
  mark_registered(s);
  return NULL;
}

static err_h sync_locked(void) {
  // Removals first: a service removed and created again in one batch has the same UUID.
  sys_ble_svc_node_t *s, *tmp;
  LL_FOREACH_SAFE(g_ble.services, s, tmp) {
    if (s->removing) {
      ERR_TRY(delete_locked(s->cfg.uuid));
      free_svc(s);
    }
  }
  LL_FOREACH(g_ble.services, s) {
    ERR_TRY(sync_svc_locked(s));
  }
  return NULL;
}

err_h sys_ble_start(void) {
  BLE_LOCK();
  g_ble.apply_pending = false;
  err_h err = g_ble.started ? sync_locked() : start_locked();
  BLE_UNLOCK();
  wake_task();  // items queued before their characteristic became live
  return err;
}

err_h sys_ble_apply(void) {
  BLE_LOCK();
  bool later = g_ble.started && g_ble.is_connected;
  if (later) {
    g_ble.apply_pending = true;
    g_ble.apply_at = xTaskGetTickCount() + MSEC(CONFIG_SYS_BLE_APPLY_DELAY_MS);
  }
  BLE_UNLOCK();
  if (!later) {
    return sys_ble_start();
  }
  wake_task();
  return NULL;
}

/* ----------------------------------------------------------------------------
 * TX
 * ------------------------------------------------------------------------- */

// Item limit of a characteristic now; caller holds the lock.
static size_t max_payload_locked(const sys_ble_char_node_t* c) {
  if (c == NULL || c->pending_remove || c->tx_rb == NULL) {
    return 0;
  }
  size_t limit = xRingbufferGetMaxItemSize(c->tx_rb);
  if (c->cfg.is_framed) {
    return limit < CONFIG_SYS_BLE_TX_ITEM_MAX ? limit : CONFIG_SYS_BLE_TX_ITEM_MAX;
  }
  size_t link = (size_t)((g_ble.is_connected ? g_ble.mtu : BLE_ATT_MTU_MAX) - SYS_BLE_ATT_HDR_LEN);
  return limit < link ? limit : link;
}

size_t sys_ble_char_max_payload(uint16_t char_uuid) {
  BLE_LOCK();
  size_t max = max_payload_locked(find_char(char_uuid));
  BLE_UNLOCK();
  return max;
}

err_h sys_ble_char_send(uint16_t char_uuid, const uint8_t* data, size_t len, uint32_t wait_ms) {
  ERR_CHECK(data != NULL && len > 0, ERR_BLE_BAD_ARG);
  TickType_t start = xTaskGetTickCount();

  /* The item is queued under the lock (a removed characteristic can't be freed
   * meanwhile), so a full buffer is polled instead of waited on. */
  while (1) {
    BLE_LOCK();
    if (!g_ble.is_connected) {
      BLE_UNLOCK();
      return NULL;  // nobody to send to
    }
    sys_ble_char_node_t* c = find_char(char_uuid);
    size_t max = max_payload_locked(c);
    bool queued = max > 0 && len <= max && xRingbufferSend(c->tx_rb, data, len, NO_WAIT) == pdTRUE;
    BLE_UNLOCK();

    if (queued) {
      wake_task();
      return NULL;
    }
    ERR_CHECK(c != NULL, ERR_BLE_NOT_FOUND, .uuid = char_uuid);
    ERR_CHECK(max > 0, ERR_BLE_NO_TX, .uuid = char_uuid);
    ERR_CHECK(len <= max, ERR_BLE_TOO_LONG, .uuid = char_uuid, .len = (uint16_t)len, .max = (uint16_t)max);
    ERR_CHECK(xTaskGetTickCount() - start < MSEC(wait_ms), ERR_BLE_TX_FULL, .uuid = char_uuid,
              .len = (uint16_t)len);
    wake_task();
    vTaskDelay(MSEC(SEND_RETRY_MS));
  }
}

// One packet; retries while NimBLE has no buffer. False when it can't be sent.
static bool send_packet(uint16_t conn, uint16_t val_handle, bool indicate, const uint8_t* data, size_t len) {
  int rc;
  while ((rc = sys_ble_stack_notify(conn, val_handle, data, len, indicate)) == BLE_HS_ENOMEM) {
    vTaskDelay(MSEC(10));
    BLE_LOCK();
    bool same_link = g_ble.is_connected && g_ble.conn_handle == conn;
    BLE_UNLOCK();
    if (!same_link) {
      return false;
    }
  }
  if (rc != 0 && rc != BLE_HS_ENOTCONN) {
    sys_ble_report(ERR_NEW(ERR_BLE_STACK, .rc = rc));
  }
  return rc == 0;
}

// One item: as is, or split into fragment packets on a framed characteristic.
static void send_item(uint16_t conn, uint16_t val_handle, bool indicate, bool framed, uint16_t mtu,
                      const uint8_t* data, size_t len) {
  size_t room = (size_t)(mtu - SYS_BLE_ATT_HDR_LEN);
  if (!framed) {
    if (len <= room) {
      send_packet(conn, val_handle, indicate, data, len);
    }
    return;  // longer: checked when queued; only possible if the MTU dropped
  }
  size_t chunk = room - 1;
  size_t off = 0;
  uint8_t count = 0;
  do {
    size_t n = len - off < chunk ? len - off : chunk;
    s_pkt[0] = (uint8_t)((off == 0 ? SYS_BLE_FRAG_FIRST : 0) | (off + n == len ? SYS_BLE_FRAG_LAST : 0) |
                         (count & SYS_BLE_FRAG_COUNT_MASK));
    memcpy(&s_pkt[1], data + off, n);
    if (!send_packet(conn, val_handle, indicate, s_pkt, n + 1)) {
      return;
    }
    off += n;
    count++;
  } while (off < len);
}

/* Copies one queued item into `buf` under the lock (no node pointer leaves it).
 * Returns its length, 0 when nothing is ready. */
static size_t take_item(uint8_t* buf, size_t cap, uint16_t* val_handle, bool* indicate, bool* framed) {
  sys_ble_svc_node_t* s;
  LL_FOREACH(g_ble.services, s) {
    if (!s->registered) {
      continue;
    }
    sys_ble_char_node_t* c;
    LL_FOREACH(s->chars, c) {
      if (c->pending_add || c->pending_remove || c->tx_rb == NULL || c->val_handle == 0 || !c->is_subscribed) {
        continue;
      }
      size_t size = 0;
      void* item = xRingbufferReceive(c->tx_rb, &size, NO_WAIT);
      if (item == NULL) {
        continue;
      }
      bool fits = size <= cap;
      if (fits) {
        memcpy(buf, item, size);
      }
      vRingbufferReturnItem(c->tx_rb, item);
      if (!fits) {
        sys_ble_report(ERR_NEW(ERR_BLE_TOO_LONG, .uuid = c->cfg.uuid, .len = (uint16_t)size, .max = (uint16_t)cap));
        continue;
      }
      *val_handle = c->val_handle;
      *indicate = c->cfg.is_indicate;
      *framed = c->cfg.is_framed;
      return size;
    }
  }
  return 0;
}

TickType_t sys_ble_task_wait(void) {
  BLE_LOCK();
  TickType_t wait = WAIT_FOREVER;
  if (g_ble.apply_pending) {
    TickType_t left = g_ble.apply_at - xTaskGetTickCount();
    wait = (int32_t)left > 0 ? left : 0;
  }
  BLE_UNLOCK();
  return wait;
}

void sys_ble_task_step(uint8_t* scratch, size_t scratch_len) {
  BLE_LOCK();
  bool apply_due = g_ble.apply_pending && (int32_t)(xTaskGetTickCount() - g_ble.apply_at) >= 0;
  BLE_UNLOCK();
  if (apply_due) {
    sys_ble_report(sys_ble_start());
  }

  while (1) {
    uint16_t val_handle = 0;
    bool indicate = false;
    bool framed = false;
    BLE_LOCK();
    uint16_t conn = g_ble.conn_handle;
    uint16_t mtu = g_ble.mtu;
    size_t len = g_ble.is_connected ? take_item(scratch, scratch_len, &val_handle, &indicate, &framed) : 0;
    BLE_UNLOCK();
    if (len == 0) {
      return;
    }
    send_item(conn, val_handle, indicate, framed, mtu, scratch, len);
  }
}

/* ----------------------------------------------------------------------------
 * RX
 * ------------------------------------------------------------------------- */

// Caller holds the lock; this releases it (the handler runs outside it).
static int deliver_unlock(sys_ble_char_node_t* c, const uint8_t* data, size_t len) {
  if (c->pending_remove) {
    BLE_UNLOCK();
    return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
  }
  if (c->rx_fn != NULL) {
    sys_ble_rx_f fn = c->rx_fn;
    void* ctx = c->rx_ctx;
    BLE_UNLOCK();
    fn(ctx, data, len);
    return 0;
  }
  if (c->rx_rb != NULL) {
    uint16_t uuid = c->cfg.uuid;
    bool ok = xRingbufferSend(c->rx_rb, data, len, NO_WAIT) == pdTRUE;
    if (!ok) {
      g_ble.rx_overflow_count++;
    }
    BLE_UNLOCK();
    if (!ok) {
      sys_ble_report(ERR_NEW(ERR_BLE_RX_FULL, .uuid = uuid, .len = (uint16_t)len));
    }
    return 0;
  }
  BLE_UNLOCK();
  return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
}

int sys_ble_on_write(sys_ble_char_node_t* c, const uint8_t* data, size_t len) {
  BLE_LOCK();
  return deliver_unlock(c, data, len);
}

err_h sys_ble_char_rx_inject(uint16_t char_uuid, const uint8_t* data, size_t len) {
  ERR_CHECK(data != NULL && len > 0, ERR_BLE_BAD_ARG);
  BLE_LOCK();
  sys_ble_char_node_t* c = find_char(char_uuid);
  if (c == NULL) {
    BLE_UNLOCK();
    return ERR_NEW(ERR_BLE_NOT_FOUND, .uuid = char_uuid);
  }
  int rc = deliver_unlock(c, data, len);
  ERR_CHECK(rc == 0, ERR_BLE_NO_RX, .uuid = char_uuid);
  return NULL;
}

err_h sys_ble_char_rx_dequeue(uint16_t char_uuid, uint8_t* buf, size_t max_len, size_t* out_len) {
  ERR_CHECK(buf != NULL && out_len != NULL, ERR_BLE_BAD_ARG);
  *out_len = 0;
  BLE_LOCK();
  sys_ble_char_node_t* c = find_char(char_uuid);
  RingbufHandle_t rb = c != NULL && !c->pending_remove ? c->rx_rb : NULL;
  size_t size = 0;
  void* item = rb != NULL ? xRingbufferReceive(rb, &size, NO_WAIT) : NULL;
  bool fits = item != NULL && size <= max_len;
  if (fits) {
    memcpy(buf, item, size);
    *out_len = size;
  }
  if (item != NULL) {
    vRingbufferReturnItem(rb, item);
  }
  BLE_UNLOCK();
  ERR_CHECK(c != NULL, ERR_BLE_NOT_FOUND, .uuid = char_uuid);
  ERR_CHECK(rb != NULL, ERR_BLE_NO_RX, .uuid = char_uuid);
  ERR_CHECK(item == NULL || fits, ERR_BLE_TOO_LONG, .uuid = char_uuid, .len = (uint16_t)size,
            .max = (uint16_t)max_len);
  return NULL;
}

err_h sys_ble_char_set_rx_handler(uint16_t char_uuid, sys_ble_rx_f fn, void* ctx) {
  ERR_CHECK(fn != NULL, ERR_BLE_BAD_ARG);
  BLE_LOCK();
  sys_ble_char_node_t* c = find_char(char_uuid);
  bool ok = c != NULL && c->cfg.is_write && c->rx_rb == NULL && c->rx_fn == NULL;
  if (ok) {
    c->rx_fn = fn;
    c->rx_ctx = ctx;
  }
  BLE_UNLOCK();
  ERR_CHECK(c != NULL, ERR_BLE_NOT_FOUND, .uuid = char_uuid);
  ERR_CHECK(ok, ERR_BLE_RX_HANDLER, .uuid = char_uuid);
  return NULL;
}

/* ----------------------------------------------------------------------------
 * Connection (host task)
 * ------------------------------------------------------------------------- */

err_h sys_ble_add_conn_listener(sys_ble_conn_f fn) {
  ERR_CHECK(fn != NULL, ERR_BLE_BAD_ARG);
  for (size_t i = 0; i < ARRAY_LEN(s_conn_listeners); i++) {
    if (s_conn_listeners[i] == NULL || s_conn_listeners[i] == fn) {
      s_conn_listeners[i] = fn;
      return NULL;
    }
  }
  return ERR_NEW(ERR_BLE_NO_MEM, .uuid = 0, .size = sizeof(s_conn_listeners));
}

static void notify_listeners(bool connected, int32_t reason) {
  for (size_t i = 0; i < ARRAY_LEN(s_conn_listeners) && s_conn_listeners[i] != NULL; i++) {
    s_conn_listeners[i](connected, reason);
  }
}

void sys_ble_on_connect(uint16_t conn_handle, uint16_t mtu) {
  BLE_LOCK();
  g_ble.conn_handle = conn_handle;
  g_ble.is_connected = true;
  g_ble.mtu = mtu;
  BLE_UNLOCK();
  notify_listeners(true, 0);
}

void sys_ble_on_disconnect(int32_t reason) {
  BLE_LOCK();
  g_ble.is_connected = false;
  g_ble.conn_handle = 0;
  g_ble.mtu = BLE_ATT_MTU_DFLT;
  sys_ble_svc_node_t* s;
  LL_FOREACH(g_ble.services, s) {
    sys_ble_char_node_t* c;
    LL_FOREACH(s->chars, c) {
      c->is_subscribed = false;
      if (c->tx_rb != NULL) {
        drain(c->tx_rb);
      }
    }
  }
  BLE_UNLOCK();
  notify_listeners(false, reason);
}

void sys_ble_on_mtu(uint16_t mtu) {
  BLE_LOCK();
  g_ble.mtu = mtu;
  BLE_UNLOCK();
}

void sys_ble_on_subscribe(uint16_t attr_handle, bool subscribed) {
  BLE_LOCK();
  sys_ble_char_node_t* c = sys_ble_find_char_by_handle(attr_handle);
  if (c != NULL) {
    c->is_subscribed = subscribed;
  }
  BLE_UNLOCK();
  if (subscribed) {
    wake_task();
  }
}

err_h sys_ble_table_visit(sys_ble_visit_f fn, void* ctx) {
  ERR_CHECK(fn != NULL, ERR_BLE_BAD_ARG);
  err_h err = NULL;
  BLE_LOCK();
  sys_ble_svc_node_t* s;
  LL_FOREACH(g_ble.services, s) {
    if (s->removing) {
      continue;
    }
    err = fn(ctx, &s->cfg, s->locked, NULL);
    sys_ble_char_node_t* c;
    LL_FOREACH(s->chars, c) {
      if (err != NULL) {
        break;
      }
      if (!c->pending_remove) {
        err = fn(ctx, &s->cfg, s->locked, &c->cfg);
      }
    }
    if (err != NULL) {
      break;
    }
  }
  BLE_UNLOCK();
  return err;
}

err_h sys_ble_get_status(sys_ble_status_t* out) {
  ERR_CHECK(out != NULL, ERR_BLE_BAD_ARG);
  BLE_LOCK();
  *out = (sys_ble_status_t){
      .is_connected = g_ble.is_connected,
      .mtu = g_ble.mtu,
      .rx_overflow_count = g_ble.rx_overflow_count,
  };
  BLE_UNLOCK();
  return NULL;
}
