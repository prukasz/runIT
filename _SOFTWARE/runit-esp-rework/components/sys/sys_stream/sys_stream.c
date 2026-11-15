#include "cmd.h"
#include "sdkconfig.h"
#include "sys_session.h"
#include "sys_stream_internal.h"
#include "sys_stream_router.h"
#include "utils.h"

ERR_MODULE_DEFINE(ERR_MOD_SYS_STREAM, "sys_stream", ERR_SYS_STREAM_MAP)
MODULE_ID_DEFINE(STREAM, 0x29)

#define ID_BYTE 1u  // a queued message and a subscriber's frame are [stream id][payload]

_Static_assert(SYS_STREAM_PAYLOAD_MAX == CMD_DATA_MAX - ID_BYTE, "a stream's frame fits");
_Static_assert(SYS_STREAM_ID_LAST < SYS_SESSION_CHANNEL_FIRST, "a stream id is never a session channel");
_Static_assert(SYS_STREAM_FLAG_URGENT == CMD_SEND_URGENT, "SYS_STREAM_FLAG_URGENT must match CMD_SEND_URGENT");
_Static_assert(ROUTER_FLAG_URGENT == CMD_SEND_URGENT, "ROUTER_FLAG_URGENT must match CMD_SEND_URGENT");

static void stream_task(void* arg);

static StaticSemaphore_t s_mutex_buf;
static SemaphoreHandle_t sys_stream_mutex;
__attribute__((constructor)) static void init_sys_stream_mutex(void) {
  sys_stream_mutex = xSemaphoreCreateMutexStatic(&s_mutex_buf);
}
U_RINGBUFFER_DEFINE(sys_stream_rb, CONFIG_SYS_STREAM_QUEUE_SIZE, RINGBUF_TYPE_NOSPLIT);
U_TASK_DEFINE(sys_stream_task_h, CONFIG_SYS_STREAM_TASK_STACK);

// A target: LIST's view of it, the callback it is (CALLBACK), whether its last message failed (logged once).
typedef struct {
  sys_stream_target_t t;
  sys_stream_callback_f fn;
  void* ctx;
  bool failing;
} target_t;

// A stream: LIST's head and its one list of targets. PUSH seq counters live in sys_session per (peer, class).
typedef struct {
  sys_stream_head_t head;
  target_t targets[CONFIG_SYS_STREAM_TARGETS];
} stream_t;

static struct {
  stream_t streams[CONFIG_SYS_STREAM_MAX];  // streams[0..count-1], in no order
  uint8_t count;
} s_stream;

bool sys_stream_is_task(void) {
  return sys_stream_task_h != NULL && xTaskGetCurrentTaskHandle() == sys_stream_task_h;
}

/* ----------------------------------------------------------------------------
 * Registry
 * ------------------------------------------------------------------------- */

// Stream `id`, NULL when it does not exist. Under sys_stream_mutex.
static stream_t* find(uint8_t id) {
  for (uint8_t i = 0; i < s_stream.count; i++) {
    if (s_stream.streams[i].head.cfg.id == id) {
      return &s_stream.streams[i];
    }
  }
  return NULL;
}

// Takes sys_stream_mutex and sets *s to `id`. On error the mutex is released.
static err_h lock_stream(uint8_t id, stream_t** s) {
  xSemaphoreTake(sys_stream_mutex, portMAX_DELAY);
  *s = find(id);
  if (*s == NULL) {
    xSemaphoreGive(sys_stream_mutex);
    return ERR_NEW(ERR_STREAM_NOT_FOUND, .stream = id);
  }
  return NULL;
}

// Whether two targets are the same one: same source provider, same sink (peer, provider), same peer, or fn+ctx.
static bool same_target(const target_t* a, const target_t* b) {
  if (a->t.type != b->t.type) {
    return false;
  }
  switch (a->t.type) {
    case SYS_STREAM_TARGET_SOURCE:
      return a->t.provider == b->t.provider;
    case SYS_STREAM_TARGET_SINK:
      return a->t.peer == b->t.peer && a->t.provider == b->t.provider;
    case SYS_STREAM_TARGET_PEER:
      return a->t.peer == b->t.peer;
    default:
      return a->fn == b->fn && a->ctx == b->ctx;
  }
}

static int find_target(const stream_t* s, const target_t* t) {
  for (int i = 0; i < s->head.targets; i++) {
    if (same_target(&s->targets[i], t)) {
      return i;
    }
  }
  return -1;
}

static bool has_source(const stream_t* s) {
  for (uint8_t i = 0; i < s->head.targets; i++) {
    if (s->targets[i].t.type == SYS_STREAM_TARGET_SOURCE) {
      return true;
    }
  }
  return false;
}

static bool has_destination(const stream_t* s) {
  for (uint8_t i = 0; i < s->head.targets; i++) {
    if (s->targets[i].t.type != SYS_STREAM_TARGET_SOURCE) {
      return true;
    }
  }
  return false;
}

// Removes target i: the last one takes its place. Under sys_stream_mutex.
static void drop_target(stream_t* s, int i) {
  s->targets[i] = s->targets[--s->head.targets];
}

// Adds a target unless it is there; refuses SYSTEM if `forbid_system`. Takes and gives sys_stream_mutex.
static err_h add_target(uint8_t id, const target_t* t, bool forbid_system) {
  stream_t* s;
  ERR_TRY(lock_stream(id, &s));
  const bool denied = forbid_system && (s->head.state & SYS_STREAM_STATE_SYSTEM) != 0u;
  const bool known = !denied && find_target(s, t) >= 0;
  const bool full = !denied && !known && s->head.targets >= CONFIG_SYS_STREAM_TARGETS;
  if (!denied && !known && !full) {
    s->targets[s->head.targets++] = *t;
  }
  xSemaphoreGive(sys_stream_mutex);
  ERR_CHECK(!denied, ERR_STREAM_DIRECTION, .stream = id);
  ERR_CHECK(!full, ERR_STREAM_TARGETS_FULL, .stream = id);
  return NULL;
}

// Removes a target when it is there. Takes and gives sys_stream_mutex.
static err_h remove_target(uint8_t id, const target_t* t) {
  stream_t* s;
  ERR_TRY(lock_stream(id, &s));
  const int at = find_target(s, t);
  if (at >= 0) {
    drop_target(s, at);
  }
  xSemaphoreGive(sys_stream_mutex);
  return NULL;
}

// A peer gone: its PEER subscriptions end (SINK targets stay as configuration). BROADCAST (master ended): every peer.
static void on_gone(uint8_t peer, uint8_t role) {
  (void)role;
  const bool all = peer == SYS_SESSION_ID_BROADCAST;
  xSemaphoreTake(sys_stream_mutex, portMAX_DELAY);
  for (uint8_t i = 0; i < s_stream.count; i++) {
    stream_t* s = &s_stream.streams[i];
    for (int k = 0; k < s->head.targets;) {
      const sys_stream_target_t* t = &s->targets[k].t;
      if (t->type == SYS_STREAM_TARGET_PEER && (t->peer == peer || all)) {
        drop_target(s, k);
      } else {
        k++;
      }
    }
  }
  xSemaphoreGive(sys_stream_mutex);
}

err_h sys_stream_init(void) {
  if (sys_stream_task_h == NULL) {
    ERR_TRY(sys_session_on_gone(on_gone));
    U_TASK_START(sys_stream_task_h, stream_task, NULL, CONFIG_SYS_STREAM_TASK_PRIO);
  }
  return NULL;
}

err_h sys_stream_create(const sys_stream_cfg_t* cfg) {
  ERR_CHECK(cfg != NULL && cfg->id != 0 && cfg->id <= SYS_STREAM_ID_LAST && (cfg->flags & ~SYS_STREAM_FLAG_URGENT) == 0,
            ERR_STREAM_BAD_ARG);
  xSemaphoreTake(sys_stream_mutex, portMAX_DELAY);
  const bool exists = find(cfg->id) != NULL;
  const bool full = s_stream.count >= CONFIG_SYS_STREAM_MAX;
  if (!exists && !full) {
    s_stream.streams[s_stream.count++] = (stream_t){.head = {.cfg = *cfg}};
  }
  xSemaphoreGive(sys_stream_mutex);
  ERR_CHECK(!exists, ERR_STREAM_EXISTS, .stream = cfg->id);
  ERR_CHECK(!full, ERR_STREAM_FULL, .stream = cfg->id);
  return NULL;
}

void sys_stream_mark_system(void) {
  xSemaphoreTake(sys_stream_mutex, portMAX_DELAY);
  for (uint8_t i = 0; i < s_stream.count; i++) {
    s_stream.streams[i].head.state |= SYS_STREAM_STATE_SYSTEM;
  }
  xSemaphoreGive(sys_stream_mutex);
}

err_h sys_stream_remove(uint8_t id) {
  uint8_t sources[CONFIG_SYS_STREAM_TARGETS];
  uint8_t n = 0;
  stream_t* s;
  ERR_TRY(lock_stream(id, &s));
  const bool system = (s->head.state & SYS_STREAM_STATE_SYSTEM) != 0u;
  if (!system) {
    for (uint8_t i = 0; i < s->head.targets; i++) {
      if (s->targets[i].t.type == SYS_STREAM_TARGET_SOURCE) {
        sources[n++] = s->targets[i].t.provider;
      }
    }
    *s = s_stream.streams[--s_stream.count];  // the last one takes its place
  }
  xSemaphoreGive(sys_stream_mutex);
  ERR_CHECK(!system, ERR_STREAM_SYSTEM, .stream = id);
  for (uint8_t i = 0; i < n; i++) {
    err_report(sys_data_provider_close(sources[i]));
  }
  return NULL;
}

/* ----------------------------------------------------------------------------
 * Targets and RX queue
 * ------------------------------------------------------------------------- */

static bool queue_push(uint8_t id, const void* data, uint32_t len) {
  void* slot = NULL;
  if (xRingbufferSendAcquire(sys_stream_rb, &slot, ID_BYTE + len, NO_WAIT) != pdTRUE) {
    return false;
  }
  uint8_t* item = slot;
  item[0] = id;
  if (len > 0) {
    memcpy(item + ID_BYTE, data, len);
  }
  xRingbufferSendComplete(sys_stream_rb, slot);
  return true;
}

// What comes in on a SOURCE of stream `ctx` (its id), on the driver's RX task: queued for the stream's targets.
// Provider up/down events (data == NULL), oversized frames, and idle streams with no destination are dropped.
static void on_provider_rx(void* ctx, uint8_t from, const uint8_t* data, uint32_t len) {
  const uint8_t id = (uint8_t)(uintptr_t)ctx;
  if (data == NULL || len > SYS_STREAM_PAYLOAD_MAX) {
    return;
  }
  const target_t src = {.t = {.type = SYS_STREAM_TARGET_SOURCE, .provider = from}};
  xSemaphoreTake(sys_stream_mutex, portMAX_DELAY);
  const stream_t* s = find(id);
  const bool takes = s != NULL && find_target(s, &src) >= 0 && has_destination(s);
  xSemaphoreGive(sys_stream_mutex);
  if (takes) {
    (void)queue_push(id, data, len);  // a full queue drops it: the subscriber sees the gap in PUSH seq
  }
}

err_h sys_stream_source(uint8_t id, uint8_t provider) {
  const target_t t = {.t = {.type = SYS_STREAM_TARGET_SOURCE, .provider = provider}};
  stream_t* s;
  ERR_TRY(lock_stream(id, &s));
  const bool system = (s->head.state & SYS_STREAM_STATE_SYSTEM) != 0u;
  const bool known = !system && find_target(s, &t) >= 0;
  const bool full = !system && !known && s->head.targets >= CONFIG_SYS_STREAM_TARGETS;
  xSemaphoreGive(sys_stream_mutex);
  ERR_CHECK(!system, ERR_STREAM_DIRECTION, .stream = id);
  if (known) {
    return NULL;
  }
  ERR_CHECK(!full, ERR_STREAM_TARGETS_FULL, .stream = id);
  // Open outside sys_stream_mutex to avoid lock-order inversion with sys_data_provider, then commit if still valid.
  ERR_TRY(sys_data_provider_open(provider, on_provider_rx, (void*)(uintptr_t)id));
  err_h err = add_target(id, &t, true);
  if (err != NULL) {
    err_report(sys_data_provider_close(provider));
  }
  return err;
}

err_h sys_stream_sink(uint8_t id, uint8_t board, uint8_t provider) {
  ERR_CHECK(board <= SYS_SESSION_ID_CLIENT_LAST || board == SYS_SESSION_ID_SELF, ERR_STREAM_BAD_ARG);
  const uint8_t peer = (board == SYS_SESSION_ID_SELF || board == sys_session_get_board_id()) ? SYS_SESSION_ID_SELF
                                                                                             : board;
  return add_target(id, &(target_t){.t = {.type = SYS_STREAM_TARGET_SINK, .peer = peer, .provider = provider}}, false);
}

// Removes SOURCE and SINK targets matching `provider`, or all of them.
static err_h unbind(uint8_t id, bool all, uint8_t provider) {
  uint8_t sources[CONFIG_SYS_STREAM_TARGETS];
  uint8_t n = 0;
  stream_t* s;
  ERR_TRY(lock_stream(id, &s));
  for (int i = 0; i < s->head.targets;) {
    const sys_stream_target_t* t = &s->targets[i].t;
    if ((t->type == SYS_STREAM_TARGET_SOURCE || t->type == SYS_STREAM_TARGET_SINK) &&
        (all || t->provider == provider)) {
      if (t->type == SYS_STREAM_TARGET_SOURCE) {
        sources[n++] = t->provider;
      }
      drop_target(s, i);
    } else {
      i++;
    }
  }
  xSemaphoreGive(sys_stream_mutex);
  for (uint8_t i = 0; i < n; i++) {
    err_report(sys_data_provider_close(sources[i]));
  }
  return NULL;
}

err_h sys_stream_unbind(uint8_t id, uint8_t provider) {
  return unbind(id, false, provider);
}

err_h sys_stream_unbind_all(uint8_t id) {
  return unbind(id, true, 0);
}

err_h sys_stream_subscribe(uint8_t id, uint8_t peer) {
  ERR_CHECK(peer <= SYS_SESSION_ID_CLIENT_LAST, ERR_STREAM_BAD_ARG);
  return add_target(id, &(target_t){.t = {.type = SYS_STREAM_TARGET_PEER, .peer = peer}}, false);
}

err_h sys_stream_unsubscribe(uint8_t id, uint8_t peer) {
  return remove_target(id, &(target_t){.t = {.type = SYS_STREAM_TARGET_PEER, .peer = peer}});
}

err_h sys_stream_listen(uint8_t id, sys_stream_callback_f fn, void* ctx) {
  ERR_CHECK(fn != NULL, ERR_STREAM_BAD_ARG);
  return add_target(id, &(target_t){.t = {.type = SYS_STREAM_TARGET_CALLBACK}, .fn = fn, .ctx = ctx}, false);
}

err_h sys_stream_unlisten(uint8_t id, sys_stream_callback_f fn, void* ctx) {
  return remove_target(id, &(target_t){.t = {.type = SYS_STREAM_TARGET_CALLBACK}, .fn = fn, .ctx = ctx});
}

uint8_t sys_stream_list(sys_stream_info_t out[CONFIG_SYS_STREAM_MAX]) {
  xSemaphoreTake(sys_stream_mutex, portMAX_DELAY);
  const uint8_t count = s_stream.count;
  for (uint8_t i = 0; i < count; i++) {
    const stream_t* s = &s_stream.streams[i];
    out[i].head = s->head;
    for (uint8_t k = 0; k < s->head.targets; k++) {
      out[i].targets[k] = s->targets[k].t;
    }
  }
  xSemaphoreGive(sys_stream_mutex);
  return count;
}

/* ----------------------------------------------------------------------------
 * Sending and delivery (stream task)
 * ------------------------------------------------------------------------- */

err_h sys_stream_send(uint8_t id, const void* data, uint32_t len) {
  ERR_CHECK(data != NULL || len == 0, ERR_STREAM_BAD_ARG);
  ERR_CHECK(len <= SYS_STREAM_PAYLOAD_MAX, ERR_STREAM_TOO_LONG, .stream = id, .len = (uint16_t)len,
            .max = SYS_STREAM_PAYLOAD_MAX);
  stream_t* s;
  ERR_TRY(lock_stream(id, &s));
  const bool in = has_source(s);
  const bool idle = s->head.targets == 0;
  xSemaphoreGive(sys_stream_mutex);
  ERR_CHECK(!in, ERR_STREAM_DIRECTION, .stream = id);
  if (idle) {
    return NULL;  // no target: nothing to queue
  }
  ERR_CHECK(queue_push(id, data, len), ERR_STREAM_QUEUE_FULL, .stream = id, .len = (uint16_t)len);
  return NULL;
}

/* One target's result: counted as dropped when it failed, logged once until it takes one again. Errors here are
 * logged, never reported: a report goes to the errors stream, i.e. back into this task, and could loop. */
static void account(target_t* t, err_h err) {
  if (err != NULL) {
    t->t.dropped = t->t.dropped < UINT16_MAX ? (uint16_t)(t->t.dropped + 1u) : UINT16_MAX;
    if (!t->failing) {
      err_log(err);
    }
  }
  t->failing = err != NULL;
  err_free(err);
}

static err_h send_sink(uint8_t id, const target_t* t, const uint8_t* payload, uint32_t len, uint8_t urgent) {
  if (len == 0) {
    return NULL;
  }
  if (t->t.peer == SYS_SESSION_ID_SELF || t->t.peer == sys_session_get_board_id()) {
    return sys_data_provider_send(t->t.provider, (cmd_in_t){payload, len}, (uint8_t)(CMD_SEND_NOWAIT | urgent));
  }
  return sys_session_data(t->t.peer, t->t.provider, id, payload, len, urgent);
}

static void deliver(const uint8_t* frame, uint32_t len) {
  target_t targets[CONFIG_SYS_STREAM_TARGETS];
  xSemaphoreTake(sys_stream_mutex, portMAX_DELAY);
  stream_t* s = find(frame[0]);
  if (s == NULL) {  // removed since queued
    xSemaphoreGive(sys_stream_mutex);
    return;
  }
  const sys_stream_cfg_t cfg = s->head.cfg;
  const uint8_t n = s->head.targets;
  memcpy(targets, s->targets, n * sizeof(targets[0]));
  xSemaphoreGive(sys_stream_mutex);
  const uint8_t urgent = (uint8_t)(cfg.flags & CMD_SEND_URGENT);
  const uint8_t* payload = frame + ID_BYTE;
  const uint32_t payload_len = len - ID_BYTE;

  err_h errs[CONFIG_SYS_STREAM_TARGETS];
  for (uint8_t i = 0; i < n; i++) {
    const target_t* t = &targets[i];
    errs[i] = NULL;
    switch (t->t.type) {
      case SYS_STREAM_TARGET_SOURCE:
        break;
      case SYS_STREAM_TARGET_SINK:
        errs[i] = send_sink(cfg.id, t, payload, payload_len, urgent);
        break;
      case SYS_STREAM_TARGET_PEER:
        errs[i] = sys_session_push(t->t.peer, frame, len, urgent);
        break;
      default:
        t->fn(t->ctx, cfg.id, payload, payload_len);
        break;
    }
  }

  xSemaphoreTake(sys_stream_mutex, portMAX_DELAY);
  s = find(frame[0]);
  for (uint8_t i = 0; i < n; i++) {
    const int at = s != NULL ? find_target(s, &targets[i]) : -1;
    if (at >= 0) {
      account(&s->targets[at], errs[i]);
    } else {
      err_free(errs[i]);
    }
  }
  xSemaphoreGive(sys_stream_mutex);
}

static void stream_task(void* arg) {
  (void)arg;
  while (1) {
    size_t size = 0;
    uint8_t* item = xRingbufferReceive(sys_stream_rb, &size, portMAX_DELAY);
    if (item == NULL) {
      continue;
    }
    // Sent from the queue: its room comes back once every target has it.
    deliver(item, (uint32_t)size);
    vRingbufferReturnItem(sys_stream_rb, item);
  }
}
