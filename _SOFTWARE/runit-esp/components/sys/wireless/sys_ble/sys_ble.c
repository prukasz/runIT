#include <stdlib.h>
#include <string.h>

#include "host/ble_hs.h"
#include "sdkconfig.h"
#include "sys_ble_internal.h"
#include "utils.h"

ERR_MODULE_DEFINE(MODULE_ID_BLE, ERR_SYS_BLE_MAP)

#define ATT_HDR        3u   // a notification's opcode and handle
#define SEND_POLL_MS   2u   // sys_ble_send: poll interval while the TX buffer is full
#define NOMEM_RETRY_MS 10u  // NimBLE out of buffers: the packet again

ble_t g_ble = {.mtu = BLE_ATT_MTU_DFLT};
U_MUTEX_DEFINE(g_ble_lock);
// Held while a hook is called, taken before g_ble_lock: a hook let go is never called after (sys_ble_hook waits on
// it). Not g_ble_lock itself: a hook may send (sys_ble_send takes that).
U_MUTEX_DEFINE(g_ble_calls);
U_BINARY_SEM_DEFINE(g_ble_wake);
U_TASK_DEFINE(ble_task_h, CONFIG_SYS_BLE_TASK_STACK);

static uint8_t s_item[CMD_FRAME_MAX];   // BLE task only: the item being sent
static uint8_t s_pkt[BLE_ATT_MTU_MAX];  // BLE task only: one packet of it

static void lock(void) {
  xSemaphoreTake(g_ble_lock, portMAX_DELAY);
}

static void unlock(void) {
  xSemaphoreGive(g_ble_lock);
}

static void wake(void) {
  xSemaphoreGive(g_ble_wake);
}

/* ----------------------------------------------------------------------------
 * Registry (the caller holds the lock). Lookups skip what is being removed: its UUIDs are free for the same batch.
 * ------------------------------------------------------------------------- */

static ble_svc_t* find_svc(uint16_t uuid) {
  ble_svc_t* s;
  LL_FOREACH(g_ble.services, s) {
    if (!s->removing && s->uuid == uuid) {
      return s;
    }
  }
  return NULL;
}

static ble_char_t* find_char(uint16_t uuid, ble_svc_t** svc) {
  ble_svc_t* s;
  LL_FOREACH(g_ble.services, s) {
    ble_char_t* c;
    LL_FOREACH(s->chars, c) {
      if (!s->removing && !c->removing && c->uuid == uuid) {
        if (svc != NULL) {
          *svc = s;
        }
        return c;
      }
    }
  }
  return NULL;
}

static void free_char(ble_char_t* c) {
  if (c->tx != NULL) {
    vRingbufferDelete(c->tx);
  }
  free(c->rx);
  free(c->name);
  free(c);
}

static void free_svc(ble_svc_t* s) {
  sys_ble_free_defs(s->def);
  ble_char_t *c, *tmp;
  LL_FOREACH_SAFE(s->chars, c, tmp) {
    free_char(c);
  }
  LL_DELETE(g_ble.services, s);
  free(s);
}

static void mark_registered(ble_svc_t* s) {
  s->registered = true;
  s->dirty = false;
  ble_char_t* c;
  LL_FOREACH(s->chars, c) {
    c->adding = false;
  }
}

/* ----------------------------------------------------------------------------
 * Hooks
 * ------------------------------------------------------------------------- */

/* Tells each hook the link's state it has not heard yet, one at a time, outside the lock and under g_ble_calls: up
 * while a client is connected, down once it leaves or the characteristic goes; a gone characteristic's hook is let
 * go. */
static void tell_hooks(void) {
  while (true) {
    sys_ble_rx_f cb = NULL;
    void* ctx = NULL;
    bool up = false;
    xSemaphoreTake(g_ble_calls, portMAX_DELAY);
    lock();
    ble_svc_t* s;
    LL_FOREACH(g_ble.services, s) {
      ble_char_t* c;
      LL_FOREACH(s->chars, c) {
        const bool gone = s->removing || c->removing;
        if (cb == NULL && c->cb != NULL && c->up != (g_ble.connected && !gone)) {
          cb = c->cb;
          ctx = c->ctx;
          up = c->up = !c->up;
          c->cb = gone ? NULL : c->cb;
        }
      }
    }
    unlock();
    if (cb != NULL) {
      cb(ctx, NULL, up ? 1u : 0u);
    }
    xSemaphoreGive(g_ble_calls);
    if (cb == NULL) {
      return;
    }
  }
}

err_h sys_ble_hook(uint16_t uuid, sys_ble_rx_f cb, void* ctx, bool* framed) {
  lock();
  ble_char_t* c = find_char(uuid, NULL);
  const bool taken = c != NULL && c->cb != NULL && c->ctx != ctx;
  if (c != NULL && !taken) {
    if (framed != NULL) {
      *framed = (c->flags & SYS_BLE_CHR_FRAMED) != 0;
    }
    c->cb = cb;
    c->ctx = ctx;
    c->up = false;
  }
  unlock();
  if (cb == NULL) {  // after the call of it in flight
    xSemaphoreTake(g_ble_calls, portMAX_DELAY);
    xSemaphoreGive(g_ble_calls);
  }
  if (cb == NULL && c == NULL) {
    return NULL;  // gone: its hook was let go then
  }
  ERR_CHECK(c != NULL, ERR_BLE_NOT_FOUND, .uuid = uuid);
  ERR_CHECK(!taken, ERR_BLE_HOOK, .uuid = uuid);
  tell_hooks();
  return NULL;
}

/* ----------------------------------------------------------------------------
 * The table (staged)
 * ------------------------------------------------------------------------- */

err_h sys_ble_service_create(uint16_t uuid, bool primary) {
  ERR_CHECK(uuid != 0, ERR_BAD_ARG);
  lock();
  const bool taken = find_svc(uuid) != NULL;
  ble_svc_t* s = taken ? NULL : calloc(1, sizeof(*s));
  if (s != NULL) {
    s->uuid = uuid;
    s->primary = primary;
    LL_APPEND(g_ble.services, s);
  }
  unlock();
  ERR_CHECK(!taken, ERR_BLE_UUID_TAKEN, .uuid = uuid);
  SYS_BLE_CHECK_MEM(s != NULL, uuid, sizeof(*s));
  return NULL;
}

err_h sys_ble_service_lock(uint16_t uuid) {
  lock();
  ble_svc_t* s = find_svc(uuid);
  if (s != NULL) {
    s->locked = true;
  }
  unlock();
  ERR_CHECK(s != NULL, ERR_BLE_NOT_FOUND, .uuid = uuid);
  return NULL;
}

// Its characteristics' hooks hear down at once; NimBLE drops it at the next sync.
err_h sys_ble_service_remove(uint16_t uuid) {
  lock();
  ble_svc_t* s = find_svc(uuid);
  const bool locked = s != NULL && s->locked;
  if (s != NULL && !locked) {
    s->removing = true;
  }
  unlock();
  ERR_CHECK(s != NULL, ERR_BLE_NOT_FOUND, .uuid = uuid);
  ERR_CHECK(!locked, ERR_BLE_SVC_LOCKED, .uuid = uuid);
  tell_hooks();
  return NULL;
}

static ble_char_t* new_char(uint16_t uuid, uint8_t flags, uint16_t tx, const char* name) {
  ble_char_t* c = calloc(1, sizeof(*c));
  if (c == NULL) {
    return NULL;
  }
  *c = (ble_char_t){.uuid = uuid, .flags = flags, .tx_size = tx, .adding = true};
  bool ok = true;
  if (name != NULL && name[0] != '\0') {
    c->name = strdup(name);
    ok = c->name != NULL;
  }
  if (ok && tx > 0) {
    c->tx = xRingbufferCreate(tx, RINGBUF_TYPE_NOSPLIT);
    ok = c->tx != NULL;
  }
  if (ok && (flags & SYS_BLE_CHR_FRAMED) && (flags & SYS_BLE_CHR_WRITE)) {
    c->rx = calloc(1, sizeof(*c->rx));
    ok = c->rx != NULL;
  }
  if (!ok) {
    free_char(c);
    return NULL;
  }
  return c;
}

err_h sys_ble_char_create(uint16_t svc, uint16_t uuid, uint8_t flags, uint16_t tx, const char* name) {
  ERR_CHECK(uuid != 0 && (tx == 0 || (flags & (SYS_BLE_CHR_NOTIFY | SYS_BLE_CHR_INDICATE))), ERR_BAD_ARG);
  ble_char_t* c = new_char(uuid, flags, tx, name);
  SYS_BLE_CHECK_MEM(c != NULL, uuid, sizeof(*c) + tx);
  lock();
  ble_svc_t* s = find_svc(svc);
  err_h err = NULL;
  if (s == NULL) {
    err = ERR_NEW(ERR_BLE_NOT_FOUND, .uuid = svc);
  } else if (s->locked) {
    err = ERR_NEW(ERR_BLE_SVC_LOCKED, .uuid = svc);
  } else if (find_char(uuid, NULL) != NULL) {
    err = ERR_NEW(ERR_BLE_UUID_TAKEN, .uuid = uuid);
  } else {
    LL_APPEND(s->chars, c);
    s->dirty = true;
  }
  unlock();
  if (err != NULL) {
    free_char(c);
  }
  return err;
}

// Its hook hears down at once; NimBLE points at it until its service is rebuilt: freed at the next sync.
err_h sys_ble_char_remove(uint16_t uuid) {
  lock();
  ble_svc_t* s = NULL;
  ble_char_t* c = find_char(uuid, &s);
  const uint16_t svc = s != NULL ? s->uuid : 0;
  const bool locked = c != NULL && s->locked;
  if (c != NULL && !locked) {
    c->removing = true;
    s->dirty = true;
  }
  unlock();
  ERR_CHECK(c != NULL, ERR_BLE_NOT_FOUND, .uuid = uuid);
  ERR_CHECK(!locked, ERR_BLE_SVC_LOCKED, .uuid = svc);
  tell_hooks();
  return NULL;
}

void sys_ble_apply(void) {
  lock();
  g_ble.apply_pending = true;
  g_ble.apply_at = xTaskGetTickCount() + (g_ble.connected ? MSEC(CONFIG_SYS_BLE_APPLY_DELAY_MS) : 0);
  unlock();
  wake();
}

void sys_ble_disconnect(void) {
  lock();
  const bool connected = g_ble.connected;
  const uint16_t conn = g_ble.conn;
  unlock();
  // Outside the lock: NimBLE takes the host's. The disconnect event then starts advertising (sys_ble_stack.c).
  const int rc = connected ? sys_ble_stack_terminate(conn) : 0;
  if (rc != 0 && rc != BLE_HS_ENOTCONN) {
    err_report(ERR_NEW(ERR_BLE_STACK, .rc = rc));  // it has no caller to answer
  }
}

err_h sys_ble_status(sys_ble_status_t* out) {
  lock();
  *out = (sys_ble_status_t){.connected = g_ble.connected, .mtu = g_ble.mtu};
  unlock();
  return NULL;
}

err_h sys_ble_list(cmd_out_t* out) {
  const uint8_t none = 0;
  ERR_TRY(cmd_append(out, &none, sizeof(none)));
  err_h err = NULL;
  lock();
  ble_svc_t* s;
  LL_FOREACH(g_ble.services, s) {
    if (s->removing || err != NULL) {
      continue;
    }
    const uint8_t flags = (uint8_t)((s->primary ? SYS_BLE_SVC_PRIMARY : 0u) | (s->locked ? SYS_BLE_SVC_LOCKED : 0u));
    const uint8_t svc[4] = {(uint8_t)s->uuid, (uint8_t)(s->uuid >> 8), flags, 0};
    const uint32_t count_at = out->len + 3u;
    err = cmd_append(out, svc, sizeof(svc));
    out->data[0] += err == NULL ? 1u : 0u;
    ble_char_t* c;
    LL_FOREACH(s->chars, c) {
      if (c->removing || err != NULL) {
        continue;
      }
      const uint8_t chr[5] = {(uint8_t)c->uuid, (uint8_t)(c->uuid >> 8), c->flags, (uint8_t)c->tx_size,
                              (uint8_t)(c->tx_size >> 8)};
      const char* name = c->name != NULL ? c->name : "";
      err = cmd_append(out, chr, sizeof(chr));
      if (err == NULL) {
        err = cmd_append(out, name, (uint32_t)strlen(name) + 1u);
      }
      out->data[count_at] += err == NULL ? 1u : 0u;
    }
  }
  unlock();
  return err;
}

/* ----------------------------------------------------------------------------
 * Sync: the staged table -> NimBLE (the BLE task, under the lock)
 * ------------------------------------------------------------------------- */

// NimBLE calls that wait on the host run without the lock: the GATT callbacks take it while the host is locked.
static err_h delete_unlocked(uint16_t uuid) {
  unlock();
  err_h err = sys_ble_stack_delete(uuid);
  lock();
  return err;
}

static err_h sync_svc(ble_svc_t* s) {
  if (s->registered && !s->dirty) {
    return NULL;
  }
  if (s->registered) {
    ERR_TRY(delete_unlocked(s->uuid));
    s->registered = false;  // a failed rebuild is tried again by the next sync
    sys_ble_free_defs(s->def);
    s->def = NULL;
  }
  ble_char_t *c, *tmp;
  LL_FOREACH_SAFE(s->chars, c, tmp) {
    c->val_handle = 0;
    c->subscribed = false;
    if (c->removing) {
      LL_DELETE(s->chars, c);
      free_char(c);
    }
  }
  struct ble_gatt_svc_def* defs = calloc(2, sizeof(*defs));
  SYS_BLE_CHECK_MEM(defs != NULL, s->uuid, 2 * sizeof(*defs));
  err_h err = sys_ble_compile(&defs[0], s);
  if (err == NULL) {
    unlock();
    err = sys_ble_stack_add(s->uuid, defs);  // NimBLE sends Service Changed itself
    lock();
  }
  if (err != NULL) {
    sys_ble_free_defs(defs);
    return err;
  }
  s->def = defs;
  mark_registered(s);
  return NULL;
}

// Removals first: a service removed and created again in one batch has the same UUID.
static err_h sync(void) {
  ble_svc_t *s, *tmp;
  LL_FOREACH_SAFE(g_ble.services, s, tmp) {
    if (s->removing) {
      if (s->registered) {
        ERR_TRY(delete_unlocked(s->uuid));
      }
      free_svc(s);
    }
  }
  LL_FOREACH(g_ble.services, s) {
    ERR_TRY(sync_svc(s));
  }
  return NULL;
}

/* ----------------------------------------------------------------------------
 * TX (the BLE task sends what sys_ble_send queued)
 * ------------------------------------------------------------------------- */

// Whether a frame that may be lost (SPARE) is refused: the TX buffer holds frames of CMD_FRAME_MAX, and half of them are
// kept for what may not be lost. Counted in frames: the free bytes of a no-split buffer are only the longest run it has,
// which a half-used ring has less than half of.
static bool spare_refused(const ble_char_t* c) {
  const uint32_t frames = c->tx_size / (CMD_FRAME_MAX + 8u);
  UBaseType_t waiting = 0;
  vRingbufferGetInfo(c->tx, NULL, NULL, NULL, NULL, &waiting);
  return (waiting + 1u) * 2u > frames;
}

err_h sys_ble_send(uint16_t uuid, cmd_in_t data, uint8_t flags, uint32_t wait_ms) {
  const TickType_t start = xTaskGetTickCount();
  while (data.len > 0) {
    err_h err = NULL;
    bool full = false;
    lock();
    ble_char_t* c = find_char(uuid, NULL);
    if (!g_ble.connected) {
      data.len = 0;  // nobody to send to
    } else if (c == NULL) {
      err = ERR_NEW(ERR_BLE_NOT_FOUND, .uuid = uuid);
    } else if (c->tx == NULL) {
      err = ERR_NEW(ERR_BLE_NO_TX, .uuid = uuid);
    } else {
      size_t max = xRingbufferGetMaxItemSize(c->tx);
      max = max < CMD_FRAME_MAX ? max : CMD_FRAME_MAX;
      if (data.len > max) {
        err = ERR_NEW(ERR_BLE_TOO_LONG, .uuid = uuid, .len = (uint16_t)data.len, .max = (uint16_t)max);
      } else if ((flags & CMD_SEND_SPARE) && spare_refused(c)) {
        err = ERR_NEW(ERR_BLE_TX_FULL, .uuid = uuid, .len = (uint16_t)data.len);  // half kept for the answers
      } else {
        full = xRingbufferSend(c->tx, data.data, data.len, NO_WAIT) != pdTRUE;
      }
    }
    unlock();
    ERR_TRY(err);
    ERR_CHECK(!full || xTaskGetTickCount() - start < MSEC(wait_ms), ERR_BLE_TX_FULL, .uuid = uuid,
              .len = (uint16_t)data.len);
    wake();
    if (!full) {
      return NULL;
    }
    vTaskDelay(MSEC(SEND_POLL_MS));
  }
  return NULL;
}

typedef struct {
  uint16_t conn;
  uint16_t val_handle;
  uint16_t room;  // a packet's bytes: the MTU less the ATT header
  bool indicate;
  bool framed;
} out_t;

// One packet of s_pkt; again while NimBLE has no buffer and the link is the same. False when it can't go.
static bool send_packet(const out_t* o, uint32_t len) {
  int rc;
  while ((rc = sys_ble_stack_notify(o->conn, o->val_handle, s_pkt, len, o->indicate)) == BLE_HS_ENOMEM) {
    vTaskDelay(MSEC(NOMEM_RETRY_MS));
    lock();
    const bool same = g_ble.connected && g_ble.conn == o->conn;
    unlock();
    if (!same) {
      return false;
    }
  }
  if (rc != 0 && rc != BLE_HS_ENOTCONN) {
    err_report(ERR_NEW(ERR_BLE_STACK, .rc = rc));
  }
  return rc == 0;
}

// The item in s_item as packets: each with the fragment header on a framed characteristic.
static void send_item(const out_t* o, uint32_t len) {
  const uint32_t hdr = o->framed ? 1u : 0u;
  const uint32_t chunk = o->room - hdr;
  uint8_t count = 0;
  for (uint32_t off = 0; off < len; count++) {
    const uint32_t n = len - off < chunk ? len - off : chunk;
    s_pkt[0] = (uint8_t)((off == 0 ? SYS_BLE_FRAG_FIRST : 0u) | (off + n == len ? SYS_BLE_FRAG_LAST : 0u) |
                         (count & SYS_BLE_FRAG_COUNT_MASK));
    memcpy(s_pkt + hdr, s_item + off, n);
    if (!send_packet(o, n + hdr)) {
      return;
    }
    off += n;
  }
}

// One queued item into s_item, under the lock (no node leaves it): its length, 0 when none is ready.
static uint32_t take_item(out_t* o) {
  if (!g_ble.connected) {
    return 0;
  }
  ble_svc_t* s;
  LL_FOREACH(g_ble.services, s) {
    ble_char_t* c;
    LL_FOREACH(s->chars, c) {
      if (!s->registered || c->adding || c->removing || c->tx == NULL || c->val_handle == 0 || !c->subscribed) {
        continue;
      }
      size_t size = 0;
      void* item = xRingbufferReceive(c->tx, &size, NO_WAIT);
      if (item != NULL) {
        memcpy(s_item, item, size);  // at most CMD_FRAME_MAX: sys_ble_send
        vRingbufferReturnItem(c->tx, item);
        *o = (out_t){.conn = g_ble.conn,
                     .val_handle = c->val_handle,
                     .room = (uint16_t)(g_ble.mtu - ATT_HDR),
                     .indicate = (c->flags & SYS_BLE_CHR_INDICATE) != 0,
                     .framed = (c->flags & SYS_BLE_CHR_FRAMED) != 0};
        return (uint32_t)size;
      }
    }
  }
  return 0;
}

// Ticks until `at`, 0 when it has passed; portMAX_DELAY when nothing is due (`pending` false).
static TickType_t until(bool pending, TickType_t at) {
  const TickType_t left = at - xTaskGetTickCount();
  return !pending ? portMAX_DELAY : (int32_t)left > 0 ? left : 0;
}

void sys_ble_task(void* arg) {
  (void)arg;
  while (true) {
    lock();
    const TickType_t apply = until(g_ble.apply_pending, g_ble.apply_at);
    const TickType_t mute = until(g_ble.mute, g_ble.mute_until);
    unlock();
    xSemaphoreTake(g_ble_wake, apply < mute ? apply : mute);

    lock();
    const bool due = g_ble.apply_pending && until(true, g_ble.apply_at) == 0;
    g_ble.apply_pending = g_ble.apply_pending && !due;
    err_h err = due ? sync() : NULL;
    // A central that connected and wrote nothing: no client of the board (the PC's Bluetooth reconnecting on its own
    // for an app that is gone). Dropped, so the board advertises and the app finds it (SYS_BLE.MD "Connection").
    const bool drop = g_ble.mute && until(true, g_ble.mute_until) == 0;
    g_ble.mute = g_ble.mute && !drop;
    unlock();
    err_report(err);
    if (drop) {
      sys_ble_disconnect();
    }

    out_t o;
    uint32_t len;
    do {
      lock();
      len = take_item(&o);
      unlock();
      if (len > 0) {
        send_item(&o, len);
      }
    } while (len > 0);
  }
}

/* ----------------------------------------------------------------------------
 * RX and the connection (the host task)
 * ------------------------------------------------------------------------- */

// One packet of a framed characteristic: the frame's length once it was the last, else 0 (SYS_BLE.MD "Framing").
static uint32_t frag_rx(ble_frag_t* rx, uint16_t uuid, const uint8_t* data, uint32_t len) {
  const uint8_t hdr = data[0];
  if (hdr & SYS_BLE_FRAG_FIRST) {
    *rx = (ble_frag_t){.active = true};  // the frame's bytes are only written over
  }
  if (!rx->active || (hdr & SYS_BLE_FRAG_COUNT_MASK) != rx->expected) {
    err_report(ERR_NEW(ERR_BLE_FRAG, .uuid = uuid, .hdr = hdr, .want = rx->expected));
    rx->active = false;  // the rest of the frame is dropped up to the next FIRST
    return 0;
  }
  if (rx->len + len - 1u > sizeof(rx->buf)) {
    err_report(ERR_NEW(ERR_BLE_TOO_LONG, .uuid = uuid, .len = (uint16_t)(rx->len + len - 1u), .max = sizeof(rx->buf)));
    rx->active = false;
    return 0;
  }
  memcpy(rx->buf + rx->len, data + 1, len - 1u);
  rx->len += len - 1u;
  rx->expected = (uint8_t)((rx->expected + 1u) & SYS_BLE_FRAG_COUNT_MASK);
  rx->active = !(hdr & SYS_BLE_FRAG_LAST);
  return rx->active ? 0 : rx->len;
}

int sys_ble_on_write(ble_char_t* c, const uint8_t* data, uint32_t len) {
  xSemaphoreTake(g_ble_calls, portMAX_DELAY);
  lock();
  const bool taken = !c->removing && c->cb != NULL;
  const sys_ble_rx_f cb = c->cb;
  void* ctx = c->ctx;
  unlock();
  if (taken && c->rx != NULL) {  // the host task only joins frames
    len = frag_rx(c->rx, c->uuid, data, len);
    data = c->rx->buf;
  }
  if (taken && len > 0) {
    cb(ctx, data, len);
  }
  lock();
  g_ble.mute = false;
  unlock();
  xSemaphoreGive(g_ble_calls);
  return taken ? 0 : BLE_ATT_ERR_WRITE_NOT_PERMITTED;
}

void sys_ble_on_connect(uint16_t conn, uint16_t mtu) {
  lock();
  g_ble.connected = true;
  g_ble.conn = conn;
  g_ble.mtu = mtu;
  g_ble.mute = true;
  g_ble.mute_until = xTaskGetTickCount() + MSEC(CONFIG_SYS_BLE_FIRST_WRITE_MS);
  unlock();
  wake();  // the BLE task waits for the first write from now
  tell_hooks();
}

void sys_ble_on_disconnect(void) {
  lock();
  g_ble.connected = false;
  g_ble.mute = false;
  g_ble.conn = 0;
  g_ble.mtu = BLE_ATT_MTU_DFLT;
  ble_svc_t* s;
  LL_FOREACH(g_ble.services, s) {
    ble_char_t* c;
    LL_FOREACH(s->chars, c) {
      c->subscribed = false;
      u_ringbuf_clear(c->tx);
      if (c->rx != NULL) {
        c->rx->active = false;
      }
    }
  }
  unlock();
  tell_hooks();
}

void sys_ble_on_mtu(uint16_t mtu) {
  lock();
  g_ble.mtu = mtu;
  unlock();
}

void sys_ble_on_subscribe(uint16_t val_handle, bool subscribed) {
  lock();
  ble_svc_t* s;
  LL_FOREACH(g_ble.services, s) {
    ble_char_t* c;
    LL_FOREACH(s->chars, c) {
      c->subscribed = c->val_handle == val_handle ? subscribed : c->subscribed;
    }
  }
  unlock();
  wake();
}

/* ----------------------------------------------------------------------------
 * Init
 * ------------------------------------------------------------------------- */

// Boot: each staged service goes to NimBLE as its own definition (what a later sync does for one), before the host
// runs. The characteristics were made by the providers; a service locked at the first start is never rebuilt, which
// would drop the client's subscriptions under its session.
static err_h add_boot_svc(ble_svc_t* s) {
  struct ble_gatt_svc_def* defs = calloc(2, sizeof(*defs));
  SYS_BLE_CHECK_MEM(defs != NULL, s->uuid, 2 * sizeof(*defs));
  err_h err = sys_ble_compile(&defs[0], s);
  if (err == NULL) {
    err = sys_ble_stack_add_boot(defs);
  }
  if (err != NULL) {
    sys_ble_free_defs(defs);
    return err;
  }
  s->def = defs;  // NimBLE keeps pointing at it
  mark_registered(s);
  return NULL;
}

err_h sys_ble_start(uint16_t adv_service) {
  ERR_TRY(sys_ble_stack_init(adv_service));
  lock();
  err_h err = NULL;
  ble_svc_t* s;
  LL_FOREACH(g_ble.services, s) {
    if (err == NULL && !s->removing) {
      err = add_boot_svc(s);
    }
  }
  g_ble.apply_pending = false;
  unlock();
  ERR_TRY(err);
  ERR_TRY(sys_ble_stack_run());
  U_TASK_START_ON_CORE(ble_task_h, sys_ble_task, NULL, CONFIG_SYS_BLE_TASK_PRIO, 0);
  return NULL;
}
