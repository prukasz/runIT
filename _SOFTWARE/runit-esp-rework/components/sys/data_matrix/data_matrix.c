#include "data_matrix.h"

#include <string.h>

#include "cmd.h"
#include "data_matrix_internal.h"
#include "sdkconfig.h"
#include "sys_data_provider.h"
#include "sys_session.h"
#include "utils.h"

ERR_MODULE_DEFINE(ERR_MOD_DATA_MATRIX, "data_matrix", ERR_DATA_MATRIX_MAP)
MODULE_ID_DEFINE(DATA_MATRIX, 0x30)

#define STREAM_BYTE 1u  // a queued message is [stream][payload]
#define PAYLOAD_MAX (CMD_DATA_MAX - 1u)

U_RINGBUFFER_DEFINE(data_matrix_rb, CONFIG_DATA_MATRIX_QUEUE_SIZE, RINGBUF_TYPE_NOSPLIT);
U_TASK_DEFINE(data_matrix_task_h, CONFIG_DATA_MATRIX_TASK_STACK);

typedef struct {
  data_matrix_callback_f fn;
  void* ctx;
} callback_t;

/* The tables. Held for a copy or a change only, never over a send, an open or a callback. */
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static struct {
  data_matrix_role_args_t roles[CONFIG_DATA_MATRIX_ROLES];
  data_matrix_route_t routes[CONFIG_DATA_MATRIX_ROUTES];
  callback_t callbacks[CONFIG_DATA_MATRIX_CALLBACKS];
  uint8_t urgent[(DATA_MATRIX_STREAM_LAST + 8u) / 8u];  // a bit per stream: its frames go first
  uint8_t role_count;
  uint8_t route_count;
  uint8_t callback_count;
} s_dm;

bool data_matrix_is_task(void) {
  return data_matrix_task_h != NULL && xTaskGetCurrentTaskHandle() == data_matrix_task_h;
}

/* ----------------------------------------------------------------------------
 * The tables (under s_lock)
 * ------------------------------------------------------------------------- */

static int role_index(uint8_t provider) {
  for (uint8_t i = 0; i < s_dm.role_count; i++) {
    if (s_dm.roles[i].provider == provider) {
      return i;
    }
  }
  return -1;
}

static bool same_end(const data_matrix_end_t* a, const data_matrix_end_t* b) {
  return a->kind == b->kind && a->a == b->a && a->b == b->b;
}

static int route_index(const data_matrix_route_t* r) {
  for (uint8_t i = 0; i < s_dm.route_count; i++) {
    if (same_end(&s_dm.routes[i].from, &r->from) && same_end(&s_dm.routes[i].to, &r->to)) {
      return i;
    }
  }
  return -1;
}

// Whether a route's `from` takes what came out of `src`: the same end, a T from any session taking that tag of every.
static bool takes(const data_matrix_end_t* from, const data_matrix_end_t* src) {
  if (from->kind != src->kind) {
    return false;
  }
  switch (src->kind) {
    case DATA_MATRIX_END_P:
    case DATA_MATRIX_END_S:
      return from->a == src->a;
    case DATA_MATRIX_END_T:
      return from->b == src->b && (from->a == DATA_MATRIX_ANY || from->a == src->a);
    default:
      return false;
  }
}

// Removes route i: the last one takes its place.
static void drop_route(uint8_t i) {
  s_dm.routes[i] = s_dm.routes[--s_dm.route_count];
}

static bool has_provider(const data_matrix_end_t* e, uint8_t provider) {
  return (e->kind == DATA_MATRIX_END_P || e->kind == DATA_MATRIX_END_T) && e->a == provider;
}

uint8_t data_matrix_role_of(uint8_t provider) {
  portENTER_CRITICAL(&s_lock);
  const int i = role_index(provider);
  const uint8_t role = i >= 0 ? s_dm.roles[i].role : DATA_MATRIX_ROLE_NONE;
  portEXIT_CRITICAL(&s_lock);
  return role;
}

void data_matrix_list(data_matrix_role_args_t roles[CONFIG_DATA_MATRIX_ROLES], uint8_t* role_count,
                      data_matrix_route_t routes[CONFIG_DATA_MATRIX_ROUTES], uint8_t* route_count) {
  portENTER_CRITICAL(&s_lock);
  *role_count = s_dm.role_count;
  *route_count = s_dm.route_count;
  memcpy(roles, s_dm.roles, s_dm.role_count * sizeof(roles[0]));
  memcpy(routes, s_dm.routes, s_dm.route_count * sizeof(routes[0]));
  portEXIT_CRITICAL(&s_lock);
}

/* ----------------------------------------------------------------------------
 * Delivery: out to the providers, the queue and the callbacks
 * ------------------------------------------------------------------------- */

static bool queue_push(uint8_t stream, const void* data, uint32_t len) {
  uint8_t* item = NULL;
  if (xRingbufferSendAcquire(data_matrix_rb, (void**)&item, STREAM_BYTE + len, NO_WAIT) != pdTRUE) {
    return false;
  }
  item[0] = stream;
  if (len > 0) {
    memcpy(item + STREAM_BYTE, data, len);
  }
  xRingbufferSendComplete(data_matrix_rb, item);
  return true;
}

static err_h provider_send(uint8_t provider, const uint8_t* data, uint32_t len, uint8_t flags) {
  if (len == 0) {
    return NULL;
  }
  return sys_data_provider_send(provider, (cmd_in_t){data, len}, (uint8_t)(CMD_SEND_NOWAIT | (flags & CMD_SEND_URGENT)));
}

// T: a PUSH [tag][data] on a session, the data as they are on a raw provider.
static err_h tagged_send(uint8_t provider, uint8_t tag, const uint8_t* data, uint32_t len, uint8_t flags) {
  const uint8_t role = data_matrix_role_of(provider);
  if (role == DATA_MATRIX_ROLE_RAW) {
    return provider_send(provider, data, len, flags);
  }
  ERR_CHECK(role == DATA_MATRIX_ROLE_SESSION, ERR_DATA_MATRIX_ROLE, .provider = provider);
  uint8_t frame[STREAM_BYTE + len];
  frame[0] = tag;
  memcpy(frame + STREAM_BYTE, data, len);
  return sys_session_push(provider, (cmd_in_t){frame, sizeof(frame)}, (uint8_t)(flags & CMD_SEND_URGENT));
}

static bool is_urgent(uint8_t stream) {
  portENTER_CRITICAL(&s_lock);
  const bool urgent = (s_dm.urgent[stream / 8u] & (1u << (stream % 8u))) != 0u;
  portEXIT_CRITICAL(&s_lock);
  return urgent;
}

// What one end takes. Nothing here reports: a report goes to the errors stream, i.e. back into this task.
static err_h put(const data_matrix_end_t* to, const data_matrix_end_t* from, const uint8_t* data, uint32_t len,
                 uint8_t flags) {
  switch (to->kind) {
    case DATA_MATRIX_END_P:
      return provider_send(to->a, data, len, flags);
    case DATA_MATRIX_END_T:
      return tagged_send(to->a, to->b, data, len, flags);
    case DATA_MATRIX_END_S:
      return queue_push(to->a, data, len) ? NULL : ERR_NEW(ERR_DATA_MATRIX_QUEUE_FULL, .stream = to->a, .len = (uint16_t)len);
    case DATA_MATRIX_END_C: {
      callback_t cb = {0};
      portENTER_CRITICAL(&s_lock);
      if (to->a < s_dm.callback_count) {
        cb = s_dm.callbacks[to->a];
      }
      portEXIT_CRITICAL(&s_lock);
      if (cb.fn != NULL) {
        cb.fn(cb.ctx, from, data, len);
      }
      return NULL;
    }
    default:
      return ERR_NEW(ERR_DATA_MATRIX_PAIR, .from = from->kind, .to = to->kind);
  }
}

// What came out of `from`, to every route that takes it, NOWAIT. What a target does not take is lost.
static void deliver(const data_matrix_end_t* from, const uint8_t* data, uint32_t len, uint8_t flags) {
  data_matrix_end_t to[CONFIG_DATA_MATRIX_ROUTES];
  uint32_t n = 0;
  portENTER_CRITICAL(&s_lock);
  for (uint8_t i = 0; i < s_dm.route_count; i++) {
    if (takes(&s_dm.routes[i].from, from)) {
      to[n++] = s_dm.routes[i].to;
    }
  }
  portEXIT_CRITICAL(&s_lock);
  for (uint32_t i = 0; i < n; i++) {
    err_free(put(&to[i], from, data, len, flags));
  }
}

// The provider's rx hook of a RAW role, on the driver's RX task.
static void on_raw_rx(void* ctx, uint8_t id, const uint8_t* data, uint32_t len) {
  (void)ctx;
  if (data != NULL) {  // NULL: the provider went up or down
    deliver(&DATA_MATRIX_END_PROVIDER(id), data, len, 0);
  }
}

// A PUSH of a session, on the driver's RX task: [tag][payload].
static void on_session_push(uint8_t provider, const uint8_t* data, uint32_t len) {
  if (len > 0) {
    deliver(&DATA_MATRIX_END_TAGGED(provider, data[0]), data + 1, len - 1u, 0);
  }
}

static void matrix_task(void* arg) {
  (void)arg;
  while (true) {
    size_t size = 0;
    uint8_t* item = xRingbufferReceive(data_matrix_rb, &size, portMAX_DELAY);
    if (item != NULL) {
      deliver(&DATA_MATRIX_END_STREAM(item[0]), item + STREAM_BYTE, (uint32_t)size - STREAM_BYTE,
              is_urgent(item[0]) ? DATA_MATRIX_URGENT : 0u);  // the queue's room comes back once every target has it
      vRingbufferReturnItem(data_matrix_rb, item);
    }
  }
}

err_h data_matrix_init(void) {
  U_TASK_START(data_matrix_task_h, matrix_task, NULL, CONFIG_DATA_MATRIX_TASK_PRIO);
  sys_session_set_push(on_session_push);
  return NULL;
}

/* ----------------------------------------------------------------------------
 * The board's code
 * ------------------------------------------------------------------------- */

static bool stream_ok(uint8_t stream) {
  return stream >= 1u && stream <= DATA_MATRIX_STREAM_LAST;
}

err_h data_matrix_streams(const data_matrix_stream_t* streams, uint32_t count) {
  ERR_CHECK(streams != NULL || count == 0, ERR_DATA_MATRIX_BAD_ARG);
  for (uint32_t i = 0; i < count; i++) {
    ERR_CHECK(stream_ok(streams[i].id) && (streams[i].flags & ~DATA_MATRIX_URGENT) == 0u, ERR_DATA_MATRIX_BAD_ARG);
  }
  portENTER_CRITICAL(&s_lock);
  for (uint32_t i = 0; i < count; i++) {
    const uint8_t id = streams[i].id;
    if ((streams[i].flags & DATA_MATRIX_URGENT) != 0u) {
      s_dm.urgent[id / 8u] |= (uint8_t)(1u << (id % 8u));
    }
  }
  portEXIT_CRITICAL(&s_lock);
  return NULL;
}

err_h data_matrix_listen(data_matrix_callback_f fn, void* ctx, data_matrix_end_t* out) {
  ERR_CHECK(fn != NULL && out != NULL, ERR_DATA_MATRIX_BAD_ARG);
  portENTER_CRITICAL(&s_lock);
  int at = -1;
  for (uint8_t i = 0; i < s_dm.callback_count; i++) {
    if (s_dm.callbacks[i].fn == fn && s_dm.callbacks[i].ctx == ctx) {
      at = i;
    }
  }
  if (at < 0 && s_dm.callback_count < CONFIG_DATA_MATRIX_CALLBACKS) {
    at = s_dm.callback_count;
    s_dm.callbacks[s_dm.callback_count++] = (callback_t){fn, ctx};
  }
  portEXIT_CRITICAL(&s_lock);
  ERR_CHECK(at >= 0, ERR_DATA_MATRIX_FULL);
  *out = (data_matrix_end_t){DATA_MATRIX_END_C, (uint8_t)at, 0};
  return NULL;
}

err_h data_matrix_send(uint8_t stream, const void* data, uint32_t len) {
  ERR_CHECK(stream_ok(stream), ERR_DATA_MATRIX_BAD_ARG);
  ERR_CHECK(len <= PAYLOAD_MAX, ERR_DATA_MATRIX_TOO_LONG, .len = (uint16_t)len, .max = PAYLOAD_MAX);
  const data_matrix_end_t from = DATA_MATRIX_END_STREAM(stream);
  bool routed = false;
  portENTER_CRITICAL(&s_lock);
  for (uint8_t i = 0; i < s_dm.route_count; i++) {
    routed = routed || takes(&s_dm.routes[i].from, &from);
  }
  portEXIT_CRITICAL(&s_lock);
  if (!routed) {
    return NULL;  // nowhere to go: nothing to queue
  }
  ERR_CHECK(queue_push(stream, data, len), ERR_DATA_MATRIX_QUEUE_FULL, .stream = stream, .len = (uint16_t)len);
  return NULL;
}

err_h data_matrix_send_to(const data_matrix_end_t* to, const void* data, uint32_t len, uint8_t flags) {
  ERR_CHECK(to != NULL && (len == 0 || data != NULL), ERR_DATA_MATRIX_BAD_ARG);
  ERR_CHECK(len <= PAYLOAD_MAX, ERR_DATA_MATRIX_TOO_LONG, .len = (uint16_t)len, .max = PAYLOAD_MAX);
  switch (to->kind) {
    case DATA_MATRIX_END_P:
      ERR_CHECK(data_matrix_role_of(to->a) == DATA_MATRIX_ROLE_RAW, ERR_DATA_MATRIX_ROLE, .provider = to->a);
      return provider_send(to->a, data, len, flags);
    case DATA_MATRIX_END_T:
      return tagged_send(to->a, to->b, data, len, flags);
    case DATA_MATRIX_END_S:
      ERR_CHECK(stream_ok(to->a), ERR_DATA_MATRIX_BAD_ARG);
      ERR_CHECK(queue_push(to->a, data, len), ERR_DATA_MATRIX_QUEUE_FULL, .stream = to->a, .len = (uint16_t)len);
      return NULL;
    default:
      return ERR_NEW(ERR_DATA_MATRIX_PAIR, .from = to->kind, .to = to->kind);
  }
}

/* ----------------------------------------------------------------------------
 * Roles
 * ------------------------------------------------------------------------- */

err_h data_matrix_set_role(const data_matrix_role_args_t* args) {
  ERR_CHECK(args != NULL && args->role <= DATA_MATRIX_ROLE_COMMANDS, ERR_DATA_MATRIX_BAD_ARG);
  ERR_CHECK(args->role == DATA_MATRIX_ROLE_SESSION || (args->deadman_ms == 0 && args->flags == 0),
            ERR_DATA_MATRIX_BAD_ARG);
  const data_matrix_role_args_t want = *args;
  portENTER_CRITICAL(&s_lock);
  const int at = role_index(want.provider);
  const bool same = at >= 0 && memcmp(&s_dm.roles[at], &want, sizeof(want)) == 0;
  const bool other = at >= 0 && !same;
  const bool full = at < 0 && s_dm.role_count >= CONFIG_DATA_MATRIX_ROLES;
  if (at < 0 && !full) {
    s_dm.roles[s_dm.role_count++] = want;
  }
  portEXIT_CRITICAL(&s_lock);
  if (same) {
    return NULL;  // a stored record runs at every start
  }
  ERR_CHECK(!other, ERR_DATA_MATRIX_ROLE, .provider = want.provider);
  ERR_CHECK(!full, ERR_DATA_MATRIX_FULL);

  err_h err = NULL;
  switch (want.role) {
    case DATA_MATRIX_ROLE_RAW:
      err = sys_data_provider_open(want.provider, on_raw_rx, NULL);
      break;
    case DATA_MATRIX_ROLE_SESSION:
      err = sys_session_attach(want.provider, want.deadman_ms, want.flags);
      break;
    default:
      err = sys_data_provider_open(want.provider, data_matrix_commands_rx, NULL);
      break;
  }
  if (err != NULL) {
    portENTER_CRITICAL(&s_lock);
    const int i = role_index(want.provider);
    if (i >= 0) {
      s_dm.roles[i] = s_dm.roles[--s_dm.role_count];
    }
    portEXIT_CRITICAL(&s_lock);
  }
  return err;
}

err_h data_matrix_clear_role(uint8_t provider) {
  uint8_t role = DATA_MATRIX_ROLE_NONE;
  portENTER_CRITICAL(&s_lock);
  const int at = role_index(provider);
  if (at >= 0) {
    role = s_dm.roles[at].role;
    s_dm.roles[at] = s_dm.roles[--s_dm.role_count];
    for (uint8_t i = 0; i < s_dm.route_count;) {
      const data_matrix_route_t* r = &s_dm.routes[i];
      const bool own = has_provider(&r->to, provider) || (r->from.a != DATA_MATRIX_ANY && has_provider(&r->from, provider));
      if (own) {
        drop_route(i);
      } else {
        i++;
      }
    }
  }
  portEXIT_CRITICAL(&s_lock);
  if (role == DATA_MATRIX_ROLE_NONE) {
    return NULL;
  }
  return role == DATA_MATRIX_ROLE_SESSION ? sys_session_detach(provider) : sys_data_provider_close(provider);
}

/* ----------------------------------------------------------------------------
 * Routes
 * ------------------------------------------------------------------------- */

// Whether the end may be a route's source (`source`) or target, with the roles that are set. Under s_lock.
static bool end_ok(const data_matrix_end_t* e, bool source) {
  switch (e->kind) {
    case DATA_MATRIX_END_P: {
      const int i = role_index(e->a);
      return i >= 0 && s_dm.roles[i].role == DATA_MATRIX_ROLE_RAW;
    }
    case DATA_MATRIX_END_T: {
      if (source && e->a == DATA_MATRIX_ANY) {
        return true;
      }
      const int i = role_index(e->a);
      const uint8_t role = i >= 0 ? s_dm.roles[i].role : DATA_MATRIX_ROLE_NONE;
      return role == DATA_MATRIX_ROLE_SESSION || (!source && role == DATA_MATRIX_ROLE_RAW);
    }
    case DATA_MATRIX_END_S:
      return stream_ok(e->a);
    case DATA_MATRIX_END_C:
      return !source && e->a < s_dm.callback_count;
    default:
      return false;
  }
}

err_h data_matrix_route(const data_matrix_route_t* route) {
  ERR_CHECK(route != NULL, ERR_DATA_MATRIX_BAD_ARG);
  const data_matrix_route_t r = *route;
  portENTER_CRITICAL(&s_lock);
  const bool pair = end_ok(&r.from, true) && end_ok(&r.to, false) &&
                    !(r.from.kind == DATA_MATRIX_END_S && r.to.kind == DATA_MATRIX_END_S);
  const bool known = pair && route_index(&r) >= 0;
  const bool full = pair && !known && s_dm.route_count >= CONFIG_DATA_MATRIX_ROUTES;
  if (pair && !known && !full) {
    s_dm.routes[s_dm.route_count++] = r;
  }
  portEXIT_CRITICAL(&s_lock);
  ERR_CHECK(pair, ERR_DATA_MATRIX_PAIR, .from = r.from.kind, .to = r.to.kind);
  ERR_CHECK(!full, ERR_DATA_MATRIX_FULL);
  return NULL;
}

err_h data_matrix_unroute(const data_matrix_route_t* route) {
  ERR_CHECK(route != NULL, ERR_DATA_MATRIX_BAD_ARG);
  portENTER_CRITICAL(&s_lock);
  const int i = route_index(route);
  if (i >= 0) {
    drop_route((uint8_t)i);
  }
  portEXIT_CRITICAL(&s_lock);
  return NULL;
}

err_h data_matrix_clear(const data_matrix_end_t* end) {
  ERR_CHECK(end != NULL, ERR_DATA_MATRIX_BAD_ARG);
  portENTER_CRITICAL(&s_lock);
  for (uint8_t i = 0; i < s_dm.route_count;) {
    if (same_end(&s_dm.routes[i].from, end) || same_end(&s_dm.routes[i].to, end)) {
      drop_route(i);
    } else {
      i++;
    }
  }
  portEXIT_CRITICAL(&s_lock);
  return NULL;
}
