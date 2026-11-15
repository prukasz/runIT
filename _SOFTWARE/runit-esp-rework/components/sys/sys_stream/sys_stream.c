#include "cmd.h"
#include "sdkconfig.h"
#include "sys_data_provider.h"
#include "sys_session.h"
#include "sys_stream_internal.h"
#include "sys_stream_router.h"
#include "utils.h"

ERR_MODULE_DEFINE(ERR_MOD_SYS_STREAM, "sys_stream", ERR_SYS_STREAM_MAP)
MODULE_ID_DEFINE(STREAM, 0x29)

#define ID_BYTE 1u  // a queued message and a subscriber's frame are [stream id][payload]

_Static_assert(SYS_STREAM_PAYLOAD_MAX == CMD_DATA_MAX - ID_BYTE, "a stream's frame fits");
_Static_assert(SYS_STREAM_ID_LAST < SYS_SESSION_CHANNEL_FIRST, "a stream id is never a channel");
_Static_assert(SYS_STREAM_FLAG_URGENT == CMD_SEND_URGENT, "SYS_STREAM_FLAG_URGENT must match CMD_SEND_URGENT");
_Static_assert(ROUTER_FLAG_URGENT == CMD_SEND_URGENT, "ROUTER_FLAG_URGENT must match CMD_SEND_URGENT");

/* The streams. Never held over sys_data_provider_open / _close, nor over a send or a callback. */
U_MUTEX_DEFINE(sys_stream_mutex);
U_RINGBUFFER_DEFINE(sys_stream_rb, CONFIG_SYS_STREAM_QUEUE_SIZE, RINGBUF_TYPE_NOSPLIT);
U_TASK_DEFINE(sys_stream_task_h, CONFIG_SYS_STREAM_TASK_STACK);

// A target: LIST's view of it, and the callback it is (CALLBACK).
typedef struct {
  sys_stream_target_t t;
  sys_stream_callback_f fn;
  void* ctx;
} target_t;

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

static void lock(void) {
  xSemaphoreTake(sys_stream_mutex, portMAX_DELAY);
}

static void unlock(void) {
  xSemaphoreGive(sys_stream_mutex);
}

/* ----------------------------------------------------------------------------
 * Registry (under sys_stream_mutex)
 * ------------------------------------------------------------------------- */

static stream_t* find(uint8_t id) {
  for (uint8_t i = 0; i < s_stream.count; i++) {
    if (s_stream.streams[i].head.cfg.id == id) {
      return &s_stream.streams[i];
    }
  }
  return NULL;
}

// The same target: the same source provider, the same sink (peer, provider), the same peer, or fn + ctx.
static int find_target(const stream_t* s, const target_t* t) {
  for (int i = 0; i < s->head.targets; i++) {
    const target_t* o = &s->targets[i];
    const bool same = o->t.type == t->t.type && o->t.peer == t->t.peer && o->t.provider == t->t.provider &&
                      o->fn == t->fn && o->ctx == t->ctx;
    if (same) {
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

// Removes target i: the last one takes its place.
static void drop_target(stream_t* s, int i) {
  s->targets[i] = s->targets[--s->head.targets];
}

// Adds a target unless it is there. `added`: it was not there.
static err_h add_target(uint8_t id, const target_t* t, bool* added) {
  lock();
  stream_t* s = find(id);
  const bool known = s != NULL && find_target(s, t) >= 0;
  const bool full = s != NULL && !known && s->head.targets >= CONFIG_SYS_STREAM_TARGETS;
  if (s != NULL && !known && !full) {
    s->targets[s->head.targets++] = *t;
  }
  unlock();
  ERR_CHECK(s != NULL, ERR_STREAM_NOT_FOUND, .stream = id);
  ERR_CHECK(!full, ERR_STREAM_TARGETS_FULL, .stream = id);
  if (added != NULL) {
    *added = !known;
  }
  return NULL;
}

static err_h remove_target(uint8_t id, const target_t* t) {
  lock();
  stream_t* s = find(id);
  const int at = s != NULL ? find_target(s, t) : -1;
  if (at >= 0) {
    drop_target(s, at);
  }
  unlock();
  ERR_CHECK(s != NULL, ERR_STREAM_NOT_FOUND, .stream = id);
  return NULL;
}

// A peer gone: its subscriptions end (a SINK is configuration and stays). BROADCAST (the master's end): every peer.
static void on_gone(uint8_t peer, uint8_t role) {
  (void)role;
  lock();
  for (uint8_t i = 0; i < s_stream.count; i++) {
    stream_t* s = &s_stream.streams[i];
    for (int k = 0; k < s->head.targets;) {
      const sys_stream_target_t* t = &s->targets[k].t;
      if (t->type == SYS_STREAM_TARGET_PEER && (peer == SYS_SESSION_ID_BROADCAST || t->peer == peer)) {
        drop_target(s, k);
      } else {
        k++;
      }
    }
  }
  unlock();
}

err_h sys_stream_create(const sys_stream_cfg_t* cfg) {
  ERR_CHECK(cfg->id != 0 && cfg->id <= SYS_STREAM_ID_LAST && (cfg->flags & ~SYS_STREAM_FLAG_URGENT) == 0,
            ERR_STREAM_BAD_ARG);
  lock();
  const bool exists = find(cfg->id) != NULL;
  const bool full = s_stream.count >= CONFIG_SYS_STREAM_MAX;
  if (!exists && !full) {
    s_stream.streams[s_stream.count++] = (stream_t){.head = {.cfg = *cfg}};
  }
  unlock();
  ERR_CHECK(!exists, ERR_STREAM_EXISTS, .stream = cfg->id);
  ERR_CHECK(!full, ERR_STREAM_FULL, .stream = cfg->id);
  return NULL;
}

// Removes the SOURCE and SINK targets of `provider` (SYS_DATA_PROVIDER_ID_NONE: all of them, and with `remove` the
// stream itself); a SOURCE's provider is closed.
static err_h unbind(uint8_t id, uint8_t provider, bool remove) {
  uint8_t sources[CONFIG_SYS_STREAM_TARGETS];
  uint8_t n = 0;
  lock();
  stream_t* s = find(id);
  for (int i = 0; s != NULL && i < s->head.targets;) {
    const sys_stream_target_t* t = &s->targets[i].t;
    const bool bound = t->type == SYS_STREAM_TARGET_SOURCE || t->type == SYS_STREAM_TARGET_SINK;
    if (bound && (provider == SYS_DATA_PROVIDER_ID_NONE || t->provider == provider)) {
      if (t->type == SYS_STREAM_TARGET_SOURCE) {
        sources[n++] = t->provider;
      }
      drop_target(s, i);
    } else {
      i++;
    }
  }
  if (s != NULL && remove) {
    *s = s_stream.streams[--s_stream.count];  // the last one takes its place
  }
  unlock();
  ERR_CHECK(s != NULL, ERR_STREAM_NOT_FOUND, .stream = id);
  for (uint8_t i = 0; i < n; i++) {
    err_report(sys_data_provider_close(sources[i]));
  }
  return NULL;
}

err_h sys_stream_remove(uint8_t id) {
  ERR_CHECK(id >= SYS_STREAM_ID_USER_FIRST, ERR_STREAM_SYSTEM, .stream = id);
  return unbind(id, SYS_DATA_PROVIDER_ID_NONE, true);
}

err_h sys_stream_unbind(uint8_t id, uint8_t provider) {
  return unbind(id, provider, false);
}

/* ----------------------------------------------------------------------------
 * Targets
 * ------------------------------------------------------------------------- */

static bool queue_push(uint8_t id, const void* data, uint32_t len) {
  uint8_t* item = NULL;
  if (xRingbufferSendAcquire(sys_stream_rb, (void**)&item, ID_BYTE + len, NO_WAIT) != pdTRUE) {
    return false;
  }
  item[0] = id;
  if (len > 0) {
    memcpy(item + ID_BYTE, data, len);
  }
  xRingbufferSendComplete(sys_stream_rb, item);
  return true;
}

// What comes in on a SOURCE of stream `ctx` (its id), on the driver's RX task. A full queue drops it: a subscriber
// sees the gap in the PUSH seq.
static void on_provider_rx(void* ctx, uint8_t from, const uint8_t* data, uint32_t len) {
  (void)from;
  if (data != NULL && len <= SYS_STREAM_PAYLOAD_MAX) {
    (void)queue_push((uint8_t)(uintptr_t)ctx, data, len);
  }
}

// The target first, then the provider's rx hook: a stream with a SOURCE refuses sys_stream_send from that moment.
err_h sys_stream_source(uint8_t id, uint8_t provider) {
  ERR_CHECK(id >= SYS_STREAM_ID_USER_FIRST, ERR_STREAM_DIRECTION, .stream = id);
  const target_t t = {.t = {.type = SYS_STREAM_TARGET_SOURCE, .provider = provider}};
  bool added = false;
  ERR_TRY(add_target(id, &t, &added));
  if (!added) {
    return NULL;  // bound already
  }
  err_h err = sys_data_provider_open(provider, on_provider_rx, (void*)(uintptr_t)id);
  if (err != NULL) {
    err_free(remove_target(id, &t));
  }
  return err;
}

err_h sys_stream_sink(uint8_t id, uint8_t board, uint8_t provider) {
  ERR_CHECK(board <= SYS_SESSION_ID_CLIENT_LAST || board == SYS_SESSION_ID_SELF, ERR_STREAM_BAD_ARG);
  const uint8_t peer = board == sys_session_get_board_id() ? SYS_SESSION_ID_SELF : board;
  return add_target(id, &(target_t){.t = {.type = SYS_STREAM_TARGET_SINK, .peer = peer, .provider = provider}}, NULL);
}

err_h sys_stream_subscribe(uint8_t id, uint8_t peer) {
  ERR_CHECK(peer <= SYS_SESSION_ID_CLIENT_LAST, ERR_STREAM_BAD_ARG);
  return add_target(id, &(target_t){.t = {.type = SYS_STREAM_TARGET_PEER, .peer = peer}}, NULL);
}

err_h sys_stream_unsubscribe(uint8_t id, uint8_t peer) {
  return remove_target(id, &(target_t){.t = {.type = SYS_STREAM_TARGET_PEER, .peer = peer}});
}

err_h sys_stream_listen(uint8_t id, sys_stream_callback_f fn, void* ctx) {
  ERR_CHECK(fn != NULL, ERR_STREAM_BAD_ARG);
  return add_target(id, &(target_t){.t = {.type = SYS_STREAM_TARGET_CALLBACK}, .fn = fn, .ctx = ctx}, NULL);
}

err_h sys_stream_unlisten(uint8_t id, sys_stream_callback_f fn, void* ctx) {
  return remove_target(id, &(target_t){.t = {.type = SYS_STREAM_TARGET_CALLBACK}, .fn = fn, .ctx = ctx});
}

uint8_t sys_stream_list(sys_stream_info_t out[CONFIG_SYS_STREAM_MAX]) {
  lock();
  const uint8_t count = s_stream.count;
  for (uint8_t i = 0; i < count; i++) {
    const stream_t* s = &s_stream.streams[i];
    out[i].head = s->head;
    for (uint8_t k = 0; k < s->head.targets; k++) {
      out[i].targets[k] = s->targets[k].t;
    }
  }
  unlock();
  return count;
}

/* ----------------------------------------------------------------------------
 * Sending and delivery (the stream task)
 * ------------------------------------------------------------------------- */

err_h sys_stream_send(uint8_t id, const void* data, uint32_t len) {
  ERR_CHECK(len <= SYS_STREAM_PAYLOAD_MAX, ERR_STREAM_TOO_LONG, .stream = id, .len = (uint16_t)len,
            .max = SYS_STREAM_PAYLOAD_MAX);
  lock();
  const stream_t* s = find(id);
  const bool in = s != NULL && has_source(s);
  const bool idle = s != NULL && s->head.targets == 0;
  unlock();
  ERR_CHECK(s != NULL, ERR_STREAM_NOT_FOUND, .stream = id);
  ERR_CHECK(!in, ERR_STREAM_DIRECTION, .stream = id);
  if (idle) {
    return NULL;  // no target: nothing to queue
  }
  ERR_CHECK(queue_push(id, data, len), ERR_STREAM_QUEUE_FULL, .stream = id, .len = (uint16_t)len);
  return NULL;
}

/* One message to every target of its stream, NOWAIT. What a target does not take is lost: a subscriber sees the gap
 * in the PUSH seq. Never reported: a report goes to the errors stream, i.e. back into this task, and could loop. */
static void deliver(const uint8_t* frame, uint32_t len) {
  target_t targets[CONFIG_SYS_STREAM_TARGETS];
  uint8_t n = 0;  // none: removed since queued
  uint8_t urgent = 0;
  lock();
  const stream_t* s = find(frame[0]);
  if (s != NULL) {
    n = s->head.targets;
    urgent = (uint8_t)(s->head.cfg.flags & CMD_SEND_URGENT);
    memcpy(targets, s->targets, n * sizeof(targets[0]));
  }
  unlock();
  const uint8_t id = frame[0];
  const cmd_in_t payload = {frame + ID_BYTE, len - ID_BYTE};
  for (uint8_t i = 0; i < n; i++) {
    const target_t* t = &targets[i];
    err_h err = NULL;
    if (t->t.type == SYS_STREAM_TARGET_SINK && payload.len == 0) {
      // nothing to write on a provider
    } else if (t->t.type == SYS_STREAM_TARGET_SINK && t->t.peer == SYS_SESSION_ID_SELF) {
      err = sys_data_provider_send(t->t.provider, payload, (uint8_t)(CMD_SEND_NOWAIT | urgent));
    } else if (t->t.type == SYS_STREAM_TARGET_SINK) {
      err = sys_session_data(t->t.peer, t->t.provider, id, payload, urgent);
    } else if (t->t.type == SYS_STREAM_TARGET_PEER) {
      err = sys_session_push(t->t.peer, (cmd_in_t){frame, len}, urgent);
    } else if (t->t.type == SYS_STREAM_TARGET_CALLBACK) {
      t->fn(t->ctx, id, payload.data, payload.len);
    }
    err_free(err);
  }
}

static void stream_task(void* arg) {
  (void)arg;
  while (true) {
    size_t size = 0;
    uint8_t* item = xRingbufferReceive(sys_stream_rb, &size, portMAX_DELAY);
    if (item != NULL) {
      deliver(item, (uint32_t)size);  // sent from the queue: its room comes back once every target has it
      vRingbufferReturnItem(sys_stream_rb, item);
    }
  }
}

err_h sys_stream_init(void) {
  ERR_TRY(sys_session_on_gone(on_gone));
  U_TASK_START(sys_stream_task_h, stream_task, NULL, CONFIG_SYS_STREAM_TASK_PRIO);
  return NULL;
}
