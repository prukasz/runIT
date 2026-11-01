#include "cmd.h"
#include "sdkconfig.h"
#include "sys_data_connector_internal.h"
#include "sys_session.h"
#include "utils.h"

#define CONN_BYTE      1u     // a subscriber's frame is [connector][payload]: the payload is the frame less this byte
#define PAYLOAD_MAX    (CMD_DATA_MAX - CONN_BYTE)

static void conn_task(void* arg);

U_MUTEX_DEFINE(sys_data_connector_mutex);
U_RINGBUFFER_DEFINE(sys_data_connector_rb, CONFIG_SYS_DATA_CONNECTOR_QUEUE_SIZE, RINGBUF_TYPE_NOSPLIT);
U_TASK_DEFINE(sys_data_connector_task_h, CONFIG_SYS_DATA_CONNECTOR_TASK_STACK);

// A connector: LIST's view of it, its frame counter and, per target, whether its last send failed (logged once).
typedef struct {
  sys_data_connector_info_t info;
  uint16_t seq;  // frames sent to its subscribers: a gap in their seq is frames lost
  bool sub_failing[CONFIG_SYS_DATA_CONNECTOR_SUBSCRIBERS];
  bool bind_failing[CONFIG_SYS_DATA_CONNECTOR_BINDINGS];
} conn_t;

static struct {
  conn_t conns[CONFIG_SYS_DATA_CONNECTOR_MAX];  // conns[0..count-1]: the connectors, in no order
  uint8_t count;
} s_conn;

#define LOCK()   U_MUTEX_LOCK(sys_data_connector_mutex, WAIT_FOREVER)
#define UNLOCK() U_MUTEX_UNLOCK(sys_data_connector_mutex)

bool sys_data_connector_is_task(void) {
  return sys_data_connector_task_h != NULL && xTaskGetCurrentTaskHandle() == sys_data_connector_task_h;
}

static bool same_id(sys_data_provider_id_t a, sys_data_provider_id_t b) {
  return a.link == b.link && a.channel == b.channel;
}

/* ----------------------------------------------------------------------------
 * Registry
 * ------------------------------------------------------------------------- */

/* Connector `id`, NULL when it does not exist. Under the lock. */
static conn_t* find(uint8_t id) {
  for (uint8_t i = 0; i < s_conn.count; i++) {
    if (s_conn.conns[i].info.head.id == id) {
      return &s_conn.conns[i];
    }
  }
  return NULL;
}

/* Takes the lock and sets *c to `id`. On an error the lock is not held. */
static err_h lock_conn(uint8_t id, conn_t** c) {
  LOCK();
  *c = find(id);
  if (*c == NULL) {
    UNLOCK();
    return ERR_NEW(ERR_CONN_NOT_FOUND, .connector = id);
  }
  return NULL;
}

static err_h create(uint8_t id, uint8_t flags, bool system) {
  LOCK();
  const bool exists = find(id) != NULL || id == SYS_DATA_CONNECTOR_WATCHES;
  const bool full = s_conn.count >= CONFIG_SYS_DATA_CONNECTOR_MAX;
  if (!exists && !full) {
    s_conn.conns[s_conn.count++] = (conn_t){
        .info.head = {.id = id, .flags = flags, .state = system ? SYS_DATA_CONNECTOR_STATE_SYSTEM : 0},
    };
  }
  UNLOCK();
  ERR_CHECK(!exists, ERR_CONN_EXISTS, .connector = id);
  ERR_CHECK(!full, ERR_CONN_FULL, .connector = id);
  return NULL;
}

// A peer gone: its subscriptions end. BROADCAST (the parent's session ended): every peer not below this board.
static void on_gone(uint8_t peer, uint8_t via) {
  const bool outside = peer == SYS_SESSION_ID_BROADCAST && via == SYS_SESSION_PARENT;
  LOCK();
  for (uint8_t i = 0; i < s_conn.count; i++) {
    conn_t* c = &s_conn.conns[i];
    for (uint8_t k = 0; k < c->info.head.subscribers;) {
      const uint8_t p = c->info.subs[k].peer;
      if (p == peer || (outside && !sys_session_below(p))) {
        const uint8_t last = --c->info.head.subscribers;
        c->info.subs[k] = c->info.subs[last];
        c->sub_failing[k] = c->sub_failing[last];
      } else {
        k++;
      }
    }
  }
  UNLOCK();
}

err_h sys_data_connector_init(void) {
  ERR_TRY(create(SYS_DATA_CONNECTOR_LOGS, 0, true));
  ERR_TRY(create(SYS_DATA_CONNECTOR_ERRORS, SYS_DATA_CONNECTOR_FLAG_URGENT, true));
  ERR_TRY(create(SYS_DATA_CONNECTOR_TELEMETRY, 0, true));
  ERR_TRY(create(SYS_DATA_CONNECTOR_EVENTS, SYS_DATA_CONNECTOR_FLAG_URGENT, true));
  ERR_TRY(sys_session_on_gone(on_gone));
  if (sys_data_connector_task_h == NULL) {
    U_TASK_START(sys_data_connector_task_h, conn_task, NULL, CONFIG_SYS_DATA_CONNECTOR_TASK_PRIO);
  }
  return NULL;
}

err_h sys_data_connector_create(uint8_t id, uint8_t flags) {
  ERR_CHECK(id != 0 && (flags & ~SYS_DATA_CONNECTOR_FLAG_URGENT) == 0, ERR_CONN_BAD_ARG);
  return create(id, flags, false);
}

err_h sys_data_connector_remove(uint8_t id) {
  sys_data_connector_binding_t binds[CONFIG_SYS_DATA_CONNECTOR_BINDINGS];
  uint8_t bound = 0;
  conn_t* c;
  ERR_TRY(lock_conn(id, &c));
  const bool system = c->info.head.state & SYS_DATA_CONNECTOR_STATE_SYSTEM;
  if (!system) {
    bound = c->info.head.bindings;
    memcpy(binds, c->info.binds, sizeof(binds));
    *c = s_conn.conns[--s_conn.count];  // the last one takes its place
  }
  UNLOCK();
  ERR_CHECK(!system, ERR_CONN_SYSTEM, .connector = id);
  for (uint8_t i = 0; i < bound; i++) {
    err_report(sys_data_provider_close(binds[i].provider));
  }
  return NULL;
}

static int find_binding(const conn_t* c, sys_data_provider_id_t provider) {
  for (int i = 0; i < c->info.head.bindings; i++) {
    if (same_id(c->info.binds[i].provider, provider)) {
      return i;
    }
  }
  return -1;
}

err_h sys_data_connector_bind(uint8_t id, sys_data_provider_id_t provider, uint8_t flags) {
  ERR_CHECK(flags == SYS_DATA_CONNECTOR_BIND_TX, ERR_CONN_BAD_ARG);
  conn_t* c;
  ERR_TRY(lock_conn(id, &c));
  int i = find_binding(c, provider);
  if (i >= 0) {
    c->info.binds[i].flags = flags;  // bound already: only its flags change
    UNLOCK();
    return NULL;
  }
  const bool full = c->info.head.bindings >= CONFIG_SYS_DATA_CONNECTOR_BINDINGS;
  UNLOCK();
  ERR_CHECK(!full, ERR_CONN_TARGETS_FULL, .connector = id);

  // The provider is opened outside the lock, then the binding added if the connector is still there with room.
  ERR_TRY(sys_data_provider_open(provider, NULL, NULL));
  err_h err = lock_conn(id, &c);
  if (err == NULL) {
    if (c->info.head.bindings < CONFIG_SYS_DATA_CONNECTOR_BINDINGS) {
      i = c->info.head.bindings++;
      c->info.binds[i] = (sys_data_connector_binding_t){.provider = provider, .flags = flags};
      c->bind_failing[i] = false;
    } else {
      err = ERR_NEW(ERR_CONN_TARGETS_FULL, .connector = id);
    }
    UNLOCK();
  }
  if (err != NULL) {
    err_report(sys_data_provider_close(provider));
  }
  return err;
}

err_h sys_data_connector_unbind(uint8_t id, sys_data_provider_id_t provider) {
  sys_data_connector_binding_t gone[CONFIG_SYS_DATA_CONNECTOR_BINDINGS];
  uint8_t count = 0;
  conn_t* c;
  ERR_TRY(lock_conn(id, &c));
  for (uint8_t i = 0; i < c->info.head.bindings;) {
    // Link 0 (no provider has it) is every binding of the connector.
    if (provider.link == 0 || same_id(c->info.binds[i].provider, provider)) {
      gone[count++] = c->info.binds[i];
      const uint8_t last = --c->info.head.bindings;
      c->info.binds[i] = c->info.binds[last];
      c->bind_failing[i] = c->bind_failing[last];
    } else {
      i++;
    }
  }
  UNLOCK();
  for (uint8_t i = 0; i < count; i++) {
    err_report(sys_data_provider_close(gone[i].provider));
  }
  return NULL;
}

err_h sys_data_connector_subscribe(uint8_t id, uint8_t peer) {
  ERR_CHECK(peer != SYS_SESSION_ID_SELF && peer != SYS_SESSION_ID_NONE && peer != SYS_SESSION_ID_BROADCAST,
            ERR_CONN_BAD_ARG);
  conn_t* c;
  ERR_TRY(lock_conn(id, &c));
  bool known = false;
  for (uint8_t i = 0; i < c->info.head.subscribers; i++) {
    known |= c->info.subs[i].peer == peer;
  }
  const bool full = !known && c->info.head.subscribers >= CONFIG_SYS_DATA_CONNECTOR_SUBSCRIBERS;
  if (!known && !full) {
    const uint8_t i = c->info.head.subscribers++;
    c->info.subs[i] = (sys_data_connector_subscriber_t){.peer = peer};
    c->sub_failing[i] = false;
  }
  UNLOCK();
  ERR_CHECK(!full, ERR_CONN_TARGETS_FULL, .connector = id);
  return NULL;
}

err_h sys_data_connector_unsubscribe(uint8_t id, uint8_t peer) {
  conn_t* c;
  ERR_TRY(lock_conn(id, &c));
  for (uint8_t i = 0; i < c->info.head.subscribers; i++) {
    if (c->info.subs[i].peer == peer) {
      const uint8_t last = --c->info.head.subscribers;
      c->info.subs[i] = c->info.subs[last];
      c->sub_failing[i] = c->sub_failing[last];
      break;
    }
  }
  UNLOCK();
  return NULL;
}

err_h sys_data_connector_set_suspended(uint8_t id, bool suspended) {
  conn_t* c;
  ERR_TRY(lock_conn(id, &c));
  if (suspended) {
    c->info.head.state |= SYS_DATA_CONNECTOR_STATE_SUSPENDED;
  } else {
    c->info.head.state &= (uint8_t)~SYS_DATA_CONNECTOR_STATE_SUSPENDED;
  }
  UNLOCK();
  return NULL;
}

uint8_t sys_data_connector_list(sys_data_connector_info_t out[CONFIG_SYS_DATA_CONNECTOR_MAX]) {
  LOCK();
  const uint8_t count = s_conn.count;
  for (uint8_t i = 0; i < count; i++) {
    out[i] = s_conn.conns[i].info;
  }
  UNLOCK();
  return count;
}

/* ----------------------------------------------------------------------------
 * Data
 * ------------------------------------------------------------------------- */

uint32_t sys_data_connector_max_payload(uint8_t id) {
  sys_data_connector_binding_t binds[CONFIG_SYS_DATA_CONNECTOR_BINDINGS];
  LOCK();
  const conn_t* c = find(id);
  const uint8_t bound = c != NULL ? c->info.head.bindings : 0;
  if (c != NULL) {
    memcpy(binds, c->info.binds, sizeof(binds));
  }
  UNLOCK();
  if (c == NULL) {
    return 0;
  }
  // A binding gets the payload as is: its provider's max_frame, when it is up.
  uint32_t max = PAYLOAD_MAX;
  for (uint8_t i = 0; i < bound; i++) {
    const uint32_t m = sys_data_provider_max_frame(binds[i].provider);
    max = m > 0 && m < max ? m : max;
  }
  return max;
}

err_h sys_data_connector_send(uint8_t id, const void* data, uint32_t len) {
  ERR_CHECK(data != NULL || len == 0, ERR_CONN_BAD_ARG);
  ERR_CHECK(len <= PAYLOAD_MAX, ERR_CONN_TOO_LONG, .connector = id, .len = (uint16_t)len, .max = PAYLOAD_MAX);
  LOCK();
  const conn_t* c = find(id);
  const bool idle = c != NULL && ((c->info.head.state & SYS_DATA_CONNECTOR_STATE_SUSPENDED) ||
                                  c->info.head.bindings + c->info.head.subscribers == 0);
  UNLOCK();
  ERR_CHECK(c != NULL, ERR_CONN_NOT_FOUND, .connector = id);
  if (idle) {
    return NULL;
  }

  void* slot = NULL;
  if (xRingbufferSendAcquire(sys_data_connector_rb, &slot, CONN_BYTE + len, NO_WAIT) != pdTRUE) {
    return ERR_NEW(ERR_CONN_QUEUE_FULL, .connector = id, .len = (uint16_t)len);
  }
  uint8_t* item = slot;
  item[0] = id;
  if (len > 0) {
    memcpy(item + CONN_BYTE, data, len);
  }
  xRingbufferSendComplete(sys_data_connector_rb, slot);
  return NULL;
}

/* One target's result: counted as dropped when it failed, logged once until it sends again. Errors here are logged,
 * never reported: a report goes to the errors connector, i.e. back into this task, and could loop. */
static uint16_t account(uint16_t dropped, bool* failing, err_h err) {
  if (err != NULL) {
    dropped = dropped < UINT16_MAX ? dropped + 1u : UINT16_MAX;
    if (!*failing) {
      err_log(err);
    }
  }
  *failing = err != NULL;
  err_free(err);
  return dropped;
}

/* One queued frame, [connector][payload], to every target, NOWAIT: a busy provider loses the frame there only. The
 * targets are copied out; the results go back to the connector by provider and by peer (it may have changed). */
static void deliver(const uint8_t* frame, uint32_t len) {
  LOCK();
  conn_t* c = find(frame[0]);
  if (c == NULL || (c->info.head.state & SYS_DATA_CONNECTOR_STATE_SUSPENDED)) {  // may have changed since queued
    UNLOCK();
    return;
  }
  const sys_data_connector_info_t info = c->info;
  const uint16_t seq = c->seq++;
  UNLOCK();
  const bool urgent = info.head.flags & SYS_DATA_CONNECTOR_FLAG_URGENT;

  err_h bind_err[CONFIG_SYS_DATA_CONNECTOR_BINDINGS];
  for (uint8_t i = 0; i < info.head.bindings; i++) {
    bind_err[i] = len > CONN_BYTE ? sys_data_provider_send(info.binds[i].provider, frame + CONN_BYTE, len - CONN_BYTE,
                                                           SYS_DATA_PROVIDER_SEND_NOWAIT |
                                                               (urgent ? SYS_DATA_PROVIDER_SEND_URGENT : 0))
                                  : NULL;
  }
  err_h sub_err[CONFIG_SYS_DATA_CONNECTOR_SUBSCRIBERS];
  for (uint8_t i = 0; i < info.head.subscribers; i++) {
    sub_err[i] = sys_session_stream(info.subs[i].peer, seq, frame, len, urgent ? ROUTER_FLAG_URGENT : 0);
  }

  LOCK();
  c = find(frame[0]);
  for (uint8_t i = 0; i < info.head.bindings; i++) {
    const int at = c != NULL ? find_binding(c, info.binds[i].provider) : -1;
    if (at >= 0) {
      c->info.binds[at].dropped = account(c->info.binds[at].dropped, &c->bind_failing[at], bind_err[i]);
    } else {
      err_free(bind_err[i]);
    }
  }
  for (uint8_t i = 0; i < info.head.subscribers; i++) {
    int at = -1;
    for (uint8_t k = 0; c != NULL && k < c->info.head.subscribers && at < 0; k++) {
      at = c->info.subs[k].peer == info.subs[i].peer ? k : -1;
    }
    if (at >= 0) {
      c->info.subs[at].dropped = account(c->info.subs[at].dropped, &c->sub_failing[at], sub_err[i]);
    } else {
      err_free(sub_err[i]);
    }
  }
  UNLOCK();
}

static void conn_task(void* arg) {
  (void)arg;
  while (1) {
    size_t size = 0;
    uint8_t* item = xRingbufferReceive(sys_data_connector_rb, &size, WAIT_FOREVER);
    if (item == NULL) {
      continue;
    }
    // Sent from the queue: its room comes back once every target has it.
    deliver(item, (uint32_t)size);
    vRingbufferReturnItem(sys_data_connector_rb, item);
  }
}
