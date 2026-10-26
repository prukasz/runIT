#include "router.h"
#include "sdkconfig.h"
#include "sys_connector_internal.h"
#include "utils.h"

#define CONN_BYTE 1u  // a frame is [connector][payload]: the payload is the frame less this byte

// The router's answer byte (router Kconfig) is not a system connector's: the client could not tell them apart.
_Static_assert(CONFIG_ROUTER_RESPONSE_STREAM != SYS_CONNECTOR_LOGS &&
                   CONFIG_ROUTER_RESPONSE_STREAM != SYS_CONNECTOR_ERRORS &&
                   CONFIG_ROUTER_RESPONSE_STREAM != SYS_CONNECTOR_TELEMETRY &&
                   CONFIG_ROUTER_RESPONSE_STREAM != SYS_CONNECTOR_EVENTS,
               "CONFIG_ROUTER_RESPONSE_STREAM is a system connector's byte");
// Every transport is a router link: its ID within 1..CONFIG_ROUTER_MAX_LINKS (one line per transport_e member).
_Static_assert(TRANSPORT_UART <= CONFIG_ROUTER_MAX_LINKS, "TRANSPORT_UART above CONFIG_ROUTER_MAX_LINKS");
_Static_assert(TRANSPORT_BLE <= CONFIG_ROUTER_MAX_LINKS, "TRANSPORT_BLE above CONFIG_ROUTER_MAX_LINKS");

static void conn_task(void* arg);

U_MUTEX_DEFINE(sys_connector_mutex);
U_RINGBUFFER_DEFINE(sys_connector_rb, CONFIG_SYS_CONNECTOR_QUEUE_SIZE, RINGBUF_TYPE_NOSPLIT);
U_TASK_DEFINE(sys_connector_task_h, CONFIG_SYS_CONNECTOR_TASK_STACK);

static struct {
  sys_connector_info_t conns[CONFIG_SYS_CONNECTOR_MAX];  // conns[0..count-1]: the connectors, in no order
  uint8_t count;
  bool failing[CONFIG_ROUTER_MAX_LINKS + 1];  // task only, by provider: its last send failed (logged once)
} s_conn;

#define LOCK() U_MUTEX_LOCK(sys_connector_mutex, WAIT_FOREVER)
#define UNLOCK() U_MUTEX_UNLOCK(sys_connector_mutex)

bool sys_connector_is_task(void) {
  return sys_connector_task_h != NULL && xTaskGetCurrentTaskHandle() == sys_connector_task_h;
}

/* ----------------------------------------------------------------------------
 * Registry
 * ------------------------------------------------------------------------- */

/* `connector`, NULL when it does not exist. Under the lock. */
static sys_connector_info_t* find(uint8_t connector) {
  for (uint8_t i = 0; i < s_conn.count; i++) {
    if (s_conn.conns[i].connector == connector) {
      return &s_conn.conns[i];
    }
  }
  return NULL;
}

static err_h create(uint8_t connector, bool system) {
  LOCK();
  bool exists = find(connector) != NULL;
  bool full = s_conn.count >= CONFIG_SYS_CONNECTOR_MAX;
  if (!exists && !full) {
    s_conn.conns[s_conn.count++] = (sys_connector_info_t){.connector = connector, .system = system};
  }
  UNLOCK();
  ERR_CHECK(!exists, ERR_CONN_EXISTS, .connector = connector);
  ERR_CHECK(!full, ERR_CONN_FULL, .connector = connector);
  return NULL;
}

err_h sys_connector_init(void) {
  ERR_TRY(create(SYS_CONNECTOR_LOGS, true));
  ERR_TRY(create(SYS_CONNECTOR_ERRORS, true));
  ERR_TRY(create(SYS_CONNECTOR_TELEMETRY, true));
  ERR_TRY(create(SYS_CONNECTOR_EVENTS, true));
  if (sys_connector_task_h == NULL) {
    U_TASK_START(sys_connector_task_h, conn_task, NULL, CONFIG_SYS_CONNECTOR_TASK_PRIO);
  }
  return NULL;
}

err_h sys_connector_create(uint8_t connector) {
  // The system connectors exist from init (ERR_CONN_EXISTS); the router's answer byte is not a connector.
  ERR_CHECK(connector != CONFIG_ROUTER_RESPONSE_STREAM, ERR_CONN_BAD_ARG);
  return create(connector, false);
}

/* Takes the lock and sets *c to `connector`. On an error the lock is not held. */
static err_h lock_conn(uint8_t connector, sys_connector_info_t** c) {
  LOCK();
  *c = find(connector);
  if (*c == NULL) {
    UNLOCK();
    return ERR_NEW(ERR_CONN_NOT_FOUND, .connector = connector);
  }
  return NULL;
}

/* Copies `connector` out under the lock; false when there is none. Whoever then calls a provider (send, max_frame)
 * works on the copy, without the lock: a provider may wait for its queue or take its own lock. */
static bool copy_conn(uint8_t connector, sys_connector_info_t* out) {
  LOCK();
  const sys_connector_info_t* c = find(connector);
  if (c != NULL) {
    *out = *c;
  }
  UNLOCK();
  return c != NULL;
}

err_h sys_connector_remove(uint8_t connector) {
  LOCK();
  sys_connector_info_t* c = find(connector);
  bool system = c != NULL && c->system;
  if (c != NULL && !system) {
    *c = s_conn.conns[--s_conn.count];  // the last one takes its place
  }
  UNLOCK();
  ERR_CHECK(c != NULL, ERR_CONN_NOT_FOUND, .connector = connector);
  ERR_CHECK(!system, ERR_CONN_PROTECTED, .connector = connector);
  return NULL;
}

static int find_binding(const sys_connector_info_t* c, uint8_t provider_id) {
  for (int i = 0; i < c->binding_count; i++) {
    if (c->bindings[i].provider == provider_id) {
      return i;
    }
  }
  return -1;
}

err_h sys_connector_bind(uint8_t connector, uint8_t provider_id, uint32_t endpoint) {
  ERR_CHECK(router_link_get(provider_id) != NULL, ERR_CONN_NO_PROVIDER, .provider = provider_id);
  sys_connector_info_t* c;
  ERR_TRY(lock_conn(connector, &c));
  int i = find_binding(c, provider_id);
  bool full = i < 0 && c->binding_count >= CONFIG_SYS_CONNECTOR_BINDINGS;
  if (!full) {
    if (i < 0) {
      i = c->binding_count++;
    }
    c->bindings[i] = (sys_connector_binding_t){.provider = provider_id, .endpoint = endpoint};
  }
  UNLOCK();
  ERR_CHECK(!full, ERR_CONN_BINDINGS_FULL, .connector = connector);
  return NULL;
}

err_h sys_connector_unbind(uint8_t connector, uint8_t provider_id) {
  sys_connector_info_t* c;
  ERR_TRY(lock_conn(connector, &c));
  int i = find_binding(c, provider_id);
  bool last = i >= 0 && c->system && c->binding_count == 1;
  if (i >= 0 && !last) {
    c->bindings[i] = c->bindings[--c->binding_count];
  }
  UNLOCK();
  ERR_CHECK(!last, ERR_CONN_PROTECTED, .connector = connector);
  return NULL;
}

err_h sys_connector_set_suspended(uint8_t connector, bool suspended) {
  sys_connector_info_t* c;
  ERR_TRY(lock_conn(connector, &c));
  c->suspended = suspended;
  UNLOCK();
  return NULL;
}

uint8_t sys_connector_list(sys_connector_info_t out[CONFIG_SYS_CONNECTOR_MAX]) {
  LOCK();
  uint8_t count = s_conn.count;
  memcpy(out, s_conn.conns, count * sizeof(sys_connector_info_t));
  UNLOCK();
  return count;
}

/* ----------------------------------------------------------------------------
 * Data
 * ------------------------------------------------------------------------- */

/* The largest frame (connector byte included) the binding takes now, at most CONFIG_SYS_CONNECTOR_FRAME_MAX. Links
 * are never removed, so a bound provider exists. */
static uint32_t provider_frame_max(uint8_t provider_id, uint32_t endpoint) {
  const router_link_t* p = router_link_get(provider_id);
  uint32_t max = p->max_frame != NULL ? p->max_frame(endpoint) : CONFIG_SYS_CONNECTOR_FRAME_MAX;
  return max < CONFIG_SYS_CONNECTOR_FRAME_MAX ? max : CONFIG_SYS_CONNECTOR_FRAME_MAX;
}

uint32_t sys_connector_max_payload(uint8_t connector) {
  sys_connector_info_t c;
  if (!copy_conn(connector, &c)) {
    return 0;
  }
  // Providers are asked outside the lock: they take their own.
  uint32_t max = CONFIG_SYS_CONNECTOR_FRAME_MAX;
  for (uint8_t i = 0; i < c.binding_count; i++) {
    uint32_t m = provider_frame_max(c.bindings[i].provider, c.bindings[i].endpoint);
    max = m < max ? m : max;
  }
  return max > CONN_BYTE ? max - CONN_BYTE : 0;
}

err_h sys_connector_send(uint8_t connector, const void* data, uint32_t len) {
  ERR_CHECK(data != NULL || len == 0, ERR_CONN_BAD_ARG);
  ERR_CHECK(CONN_BYTE + len <= CONFIG_SYS_CONNECTOR_FRAME_MAX, ERR_CONN_TOO_LONG, .connector = connector, .provider = 0,
            .len = (uint16_t)len, .max = CONFIG_SYS_CONNECTOR_FRAME_MAX - CONN_BYTE);
  sys_connector_info_t c;
  ERR_CHECK(copy_conn(connector, &c), ERR_CONN_NOT_FOUND, .connector = connector);
  if (c.suspended || c.binding_count == 0) {
    return NULL;
  }

  void* slot = NULL;
  if (xRingbufferSendAcquire(sys_connector_rb, &slot, CONN_BYTE + len, NO_WAIT) != pdTRUE) {
    return ERR_NEW(ERR_CONN_QUEUE_FULL, .connector = connector, .len = (uint16_t)len);
  }
  uint8_t* item = slot;
  item[0] = connector;
  if (len > 0) {
    memcpy(item + CONN_BYTE, data, len);
  }
  xRingbufferSendComplete(sys_connector_rb, slot);
  return NULL;
}

/* Errors here are logged, never reported: a report goes to the errors
 * connector, i.e. back into this task, and could loop. A provider that keeps
 * failing (its queue full) is logged once, until it sends again. */
static void deliver(const uint8_t* frame, uint32_t len) {
  sys_connector_info_t c;
  if (!copy_conn(frame[0], &c) || c.suspended) {  // may have changed since queued
    return;
  }

  for (uint8_t i = 0; i < c.binding_count; i++) {
    uint8_t pid = c.bindings[i].provider;
    uint32_t max = provider_frame_max(pid, c.bindings[i].endpoint);
    err_h err = NULL;
    if (len > max) {
      err = ERR_NEW(ERR_CONN_TOO_LONG, .connector = c.connector, .provider = pid, .len = (uint16_t)len,
                    .max = (uint16_t)max);
    } else {
      err = router_link_get(pid)->send(c.bindings[i].endpoint, frame, len, false);
      if (err != NULL) {
        err = ERR_WRAP(err, ERR_CONN_SEND, .connector = c.connector, .provider = pid,
                       .endpoint = c.bindings[i].endpoint);
      }
    }
    if (err != NULL && !s_conn.failing[pid]) {
      err_log(err);
    }
    s_conn.failing[pid] = err != NULL;
    err_free(err);
  }
}

static void conn_task(void* arg) {
  (void)arg;
  while (1) {
    size_t size = 0;
    uint8_t* item = xRingbufferReceive(sys_connector_rb, &size, WAIT_FOREVER);
    if (item == NULL) {
      continue;
    }
    // The item is the frame, [connector][payload], sent from the queue: its room comes back once every binding has it.
    deliver(item, (uint32_t)size);
    vRingbufferReturnItem(sys_connector_rb, item);
  }
}
