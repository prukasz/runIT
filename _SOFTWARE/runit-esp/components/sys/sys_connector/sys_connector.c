#include "sys_connector.h"
#include "sdkconfig.h"
#include "utils.h"

ERR_MODULE_DEFINE(ERR_MOD_SYS_CONNECTOR, "sys_connector", ERR_SYS_CONNECTOR_MAP)

#define ITEM_HDR 2u  // queued in front of the payload: [id][stream]

_Static_assert(CONFIG_SYS_CONNECTOR_QUEUE_SIZE % 4 == 0, "SYS_CONNECTOR_QUEUE_SIZE must be a multiple of 4");
_Static_assert(SYS_CONNECTOR_APP_BASE < CONFIG_SYS_CONNECTOR_MAX, "no room for user connectors");

typedef struct {
  bool used;
  sys_connector_info_t info;  // the connector itself, what sys_connector_info copies out
} conn_t;

static void conn_task(void* arg);

U_MUTEX_DEFINE(sys_connector_mutex);
U_RINGBUFFER_DEFINE(sys_connector_rb, CONFIG_SYS_CONNECTOR_QUEUE_SIZE, RINGBUF_TYPE_NOSPLIT);
U_TASK_DEFINE(sys_connector_task_h, CONFIG_SYS_CONNECTOR_TASK_STACK);

static struct {
  conn_t conns[CONFIG_SYS_CONNECTOR_MAX];
  const sys_connector_provider_t* providers[CONFIG_SYS_CONNECTOR_PROVIDERS_MAX + 1];  // index = ID
  uint8_t frame[CONFIG_SYS_CONNECTOR_FRAME_MAX];                                     // task only
} s_conn;

#define LOCK() U_MUTEX_LOCK(sys_connector_mutex, WAIT_FOREVER)
#define UNLOCK() U_MUTEX_UNLOCK(sys_connector_mutex)

bool sys_connector_is_task(void) {
  return sys_connector_task_h != NULL && xTaskGetCurrentTaskHandle() == sys_connector_task_h;
}

/* ----------------------------------------------------------------------------
 * Registry
 * ------------------------------------------------------------------------- */

static err_h check_id(uint8_t id) {
  ERR_CHECK(id < CONFIG_SYS_CONNECTOR_MAX, ERR_CONN_BAD_ID, .id = id);
  return NULL;
}

static bool provider_id_ok(uint8_t provider_id) {
  return provider_id > 0 && provider_id <= CONFIG_SYS_CONNECTOR_PROVIDERS_MAX;
}

err_h sys_connector_register_provider(uint8_t provider_id, const sys_connector_provider_t* provider) {
  ERR_CHECK(provider != NULL && provider->send != NULL, ERR_CONN_BAD_ARG);
  ERR_CHECK(provider_id_ok(provider_id), ERR_CONN_NO_PROVIDER, .provider = provider_id);
  LOCK();
  bool slot_free = s_conn.providers[provider_id] == NULL;
  if (slot_free) {
    s_conn.providers[provider_id] = provider;
  }
  UNLOCK();
  ERR_CHECK(slot_free, ERR_CONN_NO_PROVIDER, .provider = provider_id);
  return NULL;
}

static err_h create(uint8_t id, uint8_t stream, const char* name, bool system) {
  ERR_TRY(check_id(id));
  LOCK();
  conn_t* c = &s_conn.conns[id];
  bool exists = c->used;
  if (!exists) {
    *c = (conn_t){.used = true, .info = {.id = id, .stream = stream, .system = system}};
    if (name != NULL) {
      strncpy(c->info.name, name, SYS_CONNECTOR_NAME_MAX - 1);
    }
  }
  UNLOCK();
  ERR_CHECK(!exists, ERR_CONN_EXISTS, .id = id);
  return NULL;
}

err_h sys_connector_init(void) {
  ERR_TRY(create(SYS_CONNECTOR_LOGS, SYS_CONNECTOR_STREAM_LOGS, "logs", true));
  ERR_TRY(create(SYS_CONNECTOR_ERRORS, SYS_CONNECTOR_STREAM_ERRORS, "errors", true));
  ERR_TRY(create(SYS_CONNECTOR_TELEMETRY, SYS_CONNECTOR_STREAM_TELEMETRY, "telemetry", true));
  ERR_TRY(create(SYS_CONNECTOR_EVENTS, SYS_CONNECTOR_STREAM_EVENTS, "events", true));
  if (sys_connector_task_h == NULL) {
    U_TASK_START(sys_connector_task_h, conn_task, NULL, CONFIG_SYS_CONNECTOR_TASK_PRIO);
  }
  return NULL;
}

// The stream bytes of the system connectors and the router's answers: a user connector can't take one.
static bool stream_reserved(uint8_t stream) {
  return stream == SYS_CONNECTOR_STREAM_TELEMETRY || stream == SYS_CONNECTOR_STREAM_LOGS ||
         stream == SYS_CONNECTOR_STREAM_ERRORS || stream == SYS_CONNECTOR_STREAM_RESPONSE ||
         stream == SYS_CONNECTOR_STREAM_EVENTS;
}

err_h sys_connector_create(uint8_t id, uint8_t stream, const char* name) {
  ERR_CHECK(id >= SYS_CONNECTOR_APP_BASE, ERR_CONN_PROTECTED, .id = id);
  ERR_CHECK(!stream_reserved(stream), ERR_CONN_BAD_ARG);
  return create(id, stream, name, false);
}

/* Takes the lock and sets *c to existing connector `id`. On an error the lock
 * is not held. */
static err_h lock_conn(uint8_t id, sys_connector_info_t** c) {
  ERR_TRY(check_id(id));
  LOCK();
  if (!s_conn.conns[id].used) {
    UNLOCK();
    return ERR_NEW(ERR_CONN_NOT_FOUND, .id = id);
  }
  *c = &s_conn.conns[id].info;
  return NULL;
}

/* Copies connector `id` (a valid ID) out under the lock; false when it does not exist. */
static bool snapshot(uint8_t id, sys_connector_info_t* out) {
  LOCK();
  bool used = s_conn.conns[id].used;
  if (used) {
    *out = s_conn.conns[id].info;
  }
  UNLOCK();
  return used;
}

err_h sys_connector_remove(uint8_t id) {
  sys_connector_info_t* c;
  ERR_TRY(lock_conn(id, &c));
  bool system = c->system;
  if (!system) {
    s_conn.conns[id] = (conn_t){0};
  }
  UNLOCK();
  ERR_CHECK(!system, ERR_CONN_PROTECTED, .id = id);
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

err_h sys_connector_bind(uint8_t id, uint8_t provider_id, uint32_t endpoint) {
  ERR_CHECK(provider_id_ok(provider_id) && s_conn.providers[provider_id] != NULL, ERR_CONN_NO_PROVIDER,
            .provider = provider_id);
  sys_connector_info_t* c;
  ERR_TRY(lock_conn(id, &c));
  int i = find_binding(c, provider_id);
  bool full = i < 0 && c->binding_count >= CONFIG_SYS_CONNECTOR_BINDINGS;
  if (!full) {
    if (i < 0) {
      i = c->binding_count++;
    }
    c->bindings[i] = (sys_connector_binding_t){.provider = provider_id, .endpoint = endpoint};
  }
  UNLOCK();
  ERR_CHECK(!full, ERR_CONN_BINDINGS_FULL, .id = id);
  return NULL;
}

err_h sys_connector_unbind(uint8_t id, uint8_t provider_id) {
  sys_connector_info_t* c;
  ERR_TRY(lock_conn(id, &c));
  int i = find_binding(c, provider_id);
  bool last = i >= 0 && c->system && c->binding_count == 1;
  if (i >= 0 && !last) {
    c->bindings[i] = c->bindings[--c->binding_count];
  }
  UNLOCK();
  ERR_CHECK(!last, ERR_CONN_PROTECTED, .id = id);
  return NULL;
}

static err_h set_suspended(uint8_t id, bool suspended) {
  sys_connector_info_t* c;
  ERR_TRY(lock_conn(id, &c));
  c->suspended = suspended;
  UNLOCK();
  return NULL;
}

err_h sys_connector_suspend(uint8_t id) {
  return set_suspended(id, true);
}

err_h sys_connector_resume(uint8_t id) {
  return set_suspended(id, false);
}

err_h sys_connector_info(uint8_t id, sys_connector_info_t* out) {
  ERR_CHECK(out != NULL, ERR_CONN_BAD_ARG);
  ERR_TRY(check_id(id));
  ERR_CHECK(snapshot(id, out), ERR_CONN_NOT_FOUND, .id = id);
  return NULL;
}

/* ----------------------------------------------------------------------------
 * Data
 * ------------------------------------------------------------------------- */

static uint32_t provider_max(uint8_t provider_id, uint32_t endpoint) {
  const sys_connector_provider_t* p = s_conn.providers[provider_id];
  uint32_t max = p->max_frame != NULL ? p->max_frame(endpoint) : CONFIG_SYS_CONNECTOR_FRAME_MAX;
  return max < CONFIG_SYS_CONNECTOR_FRAME_MAX ? max : CONFIG_SYS_CONNECTOR_FRAME_MAX;
}

uint32_t sys_connector_max_payload(uint8_t id) {
  sys_connector_info_t c;
  if (id >= CONFIG_SYS_CONNECTOR_MAX || !snapshot(id, &c)) {
    return 0;
  }
  // Providers are asked outside the lock: they take their own.
  uint32_t max = CONFIG_SYS_CONNECTOR_FRAME_MAX;
  for (uint8_t i = 0; i < c.binding_count; i++) {
    uint32_t m = provider_max(c.bindings[i].provider, c.bindings[i].endpoint);
    max = m < max ? m : max;
  }
  return max > 0 ? max - 1 : 0;  // stream byte
}

err_h sys_connector_send(uint8_t id, const void* data, uint32_t len) {
  ERR_CHECK(data != NULL || len == 0, ERR_CONN_BAD_ARG);
  ERR_TRY(check_id(id));
  ERR_CHECK(len + 1 <= CONFIG_SYS_CONNECTOR_FRAME_MAX, ERR_CONN_TOO_LONG, .id = id, .provider = 0,
            .len = (uint16_t)len, .max = CONFIG_SYS_CONNECTOR_FRAME_MAX - 1);
  sys_connector_info_t c;
  ERR_CHECK(snapshot(id, &c), ERR_CONN_NOT_FOUND, .id = id);
  if (c.suspended || c.binding_count == 0) {
    return NULL;
  }

  void* slot = NULL;
  if (xRingbufferSendAcquire(sys_connector_rb, &slot, ITEM_HDR + len, NO_WAIT) != pdTRUE) {
    return ERR_NEW(ERR_CONN_QUEUE_FULL, .id = id, .len = (uint16_t)len);
  }
  uint8_t* item = slot;
  item[0] = id;
  item[1] = c.stream;
  if (len > 0) {
    memcpy(item + ITEM_HDR, data, len);
  }
  xRingbufferSendComplete(sys_connector_rb, slot);
  return NULL;
}

/* Errors here are logged, never reported: a report goes to the errors
 * connector, i.e. back into this task, and could loop. */
static void send_frame(uint8_t id, const uint8_t* frame, uint32_t len) {
  sys_connector_info_t c;
  if (!snapshot(id, &c) || c.suspended) {  // may have changed since queued
    return;
  }

  for (uint8_t i = 0; i < c.binding_count; i++) {
    uint8_t pid = c.bindings[i].provider;
    uint32_t max = provider_max(pid, c.bindings[i].endpoint);
    err_h err = NULL;
    if (len > max) {
      err = ERR_NEW(ERR_CONN_TOO_LONG, .id = id, .provider = pid, .len = (uint16_t)len, .max = (uint16_t)max);
    } else {
      err = s_conn.providers[pid]->send(c.bindings[i].endpoint, frame, len);
      if (err != NULL) {
        err = ERR_WRAP(err, ERR_CONN_SEND, .id = id, .provider = pid, .endpoint = c.bindings[i].endpoint);
      }
    }
    if (err != NULL) {
      err_log(err);
      err_free(err);
    }
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
    // Frame = [stream][payload]: the item already holds it after the id byte.
    uint8_t id = item[0];
    uint32_t len = (uint32_t)(size - 1);
    memcpy(s_conn.frame, item + 1, len);
    vRingbufferReturnItem(sys_connector_rb, item);
    send_frame(id, s_conn.frame, len);
  }
}
