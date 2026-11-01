#include "cmd.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "sys_data_connector_internal.h"
#include "sys_data_connector_router.h"
#include "sys_session.h"
#include "utils.h"

#define CONN_BYTE      1u     // a subscriber's frame is [connector][payload]: the payload is the frame less this byte
#define PAYLOAD_MAX    (CMD_DATA_MAX - CONN_BYTE)

static void conn_task(void* arg);

U_MUTEX_DEFINE(sys_data_connector_mutex);
U_RINGBUFFER_DEFINE(sys_data_connector_rb, CONFIG_SYS_DATA_CONNECTOR_QUEUE_SIZE, RINGBUF_TYPE_NOSPLIT);
U_TASK_DEFINE(sys_data_connector_task_h, CONFIG_SYS_DATA_CONNECTOR_TASK_STACK);

// A connector that mirrors one of another board (sys_data_connector_mirror).
typedef struct {
  bool on;
  bool have_seq;
  uint8_t board;
  uint8_t remote_id;
  uint16_t seq;  // the next frame expected
  sys_data_connector_mirror_f fn;
  void* ctx;
} mirror_t;

// A connector: LIST's view of it, its frame counter and, per target, whether its last send failed (logged once).
typedef struct {
  sys_data_connector_info_t info;
  mirror_t mirror;
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
    if (c->mirror.on && (c->mirror.board == peer || (outside && !sys_session_below(c->mirror.board)))) {
      c->mirror.have_seq = false;  // its stream starts again when the board is reached
    }
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

/* ----------------------------------------------------------------------------
 * Mirrors
 * ------------------------------------------------------------------------- */

// A mirror's input is queued with the connector's own frames, as [0][mirror id][board][seq u16][payload]: 0 is no
// connector, so the task tells it by its first byte.
#define MIRROR_MARK 0u
#define MIRROR_HEAD 5u

// The answer of a SUBSCRIBE or UNSUBSCRIBE to another board: a refusal is logged (the mirror has no frames then).
static err_h on_mirror_answer(void* ctx, const err_cmd_t* cmd, uint8_t status, uint8_t* data, uint32_t len) {
  (void)ctx;
  (void)cmd;
  (void)data;
  (void)len;
  if (status != ROUTER_STATUS_OK) {
    ESP_LOGW("conn", "a mirror was refused by the other board");
  }
  return NULL;
}

static void send_subscribe(uint8_t board, uint8_t remote_id, uint8_t op) {
  const uint8_t req[] = {ROUTER_KEY_CONN, CMD_DIR_WRITE, op, remote_id};
  err_free(sys_session_call(board, (cmd_in_t){req, sizeof(req)}, 0, 0, on_mirror_answer, NULL));
}

// A frame [remote_id][payload] of a board's stream, on the RX task: queued when a mirror takes it.
static void on_stream(uint8_t src, uint16_t seq, const uint8_t* data, uint32_t len) {
  if (len < CONN_BYTE) {
    return;
  }
  uint8_t mirror = 0;
  LOCK();
  for (uint8_t i = 0; i < s_conn.count && mirror == 0; i++) {
    const mirror_t* m = &s_conn.conns[i].mirror;
    if (m->on && m->board == src && m->remote_id == data[0]) {
      mirror = s_conn.conns[i].info.head.id;
    }
  }
  UNLOCK();
  if (mirror == 0) {
    return;
  }
  void* slot = NULL;
  if (xRingbufferSendAcquire(sys_data_connector_rb, &slot, MIRROR_HEAD + len - CONN_BYTE, NO_WAIT) != pdTRUE) {
    return;  // the gap shows in the next frame's seq
  }
  uint8_t* item = slot;
  item[0] = MIRROR_MARK;
  item[1] = mirror;
  item[2] = src;
  item[3] = (uint8_t)seq;
  item[4] = (uint8_t)(seq >> 8);
  memcpy(item + MIRROR_HEAD, data + CONN_BYTE, len - CONN_BYTE);
  xRingbufferSendComplete(sys_data_connector_rb, slot);
}

// A board is reached (again): its mirrors subscribe there.
static void on_reached(uint8_t peer) {
  uint8_t remote[CONFIG_SYS_DATA_CONNECTOR_MAX];
  uint8_t n = 0;
  LOCK();
  for (uint8_t i = 0; i < s_conn.count; i++) {
    const mirror_t* m = &s_conn.conns[i].mirror;
    if (m->on && m->board == peer) {
      remote[n++] = m->remote_id;
    }
  }
  UNLOCK();
  for (uint8_t i = 0; i < n; i++) {
    send_subscribe(peer, remote[i], SYS_DATA_CONNECTOR_OP_SUBSCRIBE);
  }
}

err_h sys_data_connector_mirror(uint8_t id, uint8_t board, uint8_t remote_id, sys_data_connector_mirror_f fn,
                                void* ctx) {
  ERR_CHECK(remote_id != 0 && remote_id != SYS_DATA_CONNECTOR_WATCHES && board <= SYS_SESSION_ID_LAST, ERR_CONN_BAD_ARG);
  conn_t* c;
  ERR_TRY(lock_conn(id, &c));
  const bool bad = (c->info.head.state & SYS_DATA_CONNECTOR_STATE_SYSTEM) || c->mirror.on;
  if (!bad) {
    c->mirror = (mirror_t){.on = true, .board = board, .remote_id = remote_id, .fn = fn, .ctx = ctx};
  }
  UNLOCK();
  ERR_CHECK(!bad, ERR_CONN_BAD_ARG);
  send_subscribe(board, remote_id, SYS_DATA_CONNECTOR_OP_SUBSCRIBE);
  return NULL;
}

err_h sys_data_connector_unmirror(uint8_t id) {
  conn_t* c;
  ERR_TRY(lock_conn(id, &c));
  const mirror_t m = c->mirror;
  c->mirror.on = false;
  UNLOCK();
  if (m.on) {
    send_subscribe(m.board, m.remote_id, SYS_DATA_CONNECTOR_OP_UNSUBSCRIBE);
  }
  return NULL;
}

err_h sys_data_connector_init(void) {
  ERR_TRY(create(SYS_DATA_CONNECTOR_LOGS, 0, true));
  ERR_TRY(create(SYS_DATA_CONNECTOR_ERRORS, SYS_DATA_CONNECTOR_FLAG_URGENT, true));
  ERR_TRY(create(SYS_DATA_CONNECTOR_TELEMETRY, 0, true));
  ERR_TRY(create(SYS_DATA_CONNECTOR_EVENTS, SYS_DATA_CONNECTOR_FLAG_URGENT, true));
  ERR_TRY(create(SYS_DATA_CONNECTOR_EVENTS_ALL, 0, true));
  ERR_TRY(sys_session_on_gone(on_gone));
  ERR_TRY(sys_session_on_reached(on_reached));
  ERR_TRY(sys_session_on_stream(on_stream));
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

// A mirror's input (MIRROR_HEAD): the gap in its seq, the listener, then the frame goes to the mirror's own targets.
static void mirror_in(uint8_t* item, uint32_t len) {
  const uint8_t id = item[1];
  const uint8_t board = item[2];
  const uint16_t seq = (uint16_t)(item[3] | (item[4] << 8));
  sys_data_connector_mirror_f fn = NULL;
  void* ctx = NULL;
  uint16_t missed = 0;
  LOCK();
  conn_t* c = find(id);
  if (c != NULL && c->mirror.on && c->mirror.board == board) {
    missed = c->mirror.have_seq ? (uint16_t)(seq - c->mirror.seq) : 0;
    c->mirror.seq = (uint16_t)(seq + 1u);
    c->mirror.have_seq = true;
    fn = c->mirror.fn;
    ctx = c->mirror.ctx;
  }
  UNLOCK();
  if (missed > 0) {
    err_report_board(ERR_NEW(ERR_CONN_MIRROR_LOST, .connector = id, .board = board, .count = missed), board);
  }
  if (fn != NULL) {
    fn(ctx, board, item + MIRROR_HEAD, len - MIRROR_HEAD, missed);
  }
  // [connector][payload]: the connector's id takes the place of the byte before the payload.
  item[MIRROR_HEAD - 1] = id;
  deliver(item + MIRROR_HEAD - 1, len - MIRROR_HEAD + 1);
}

static void conn_task(void* arg) {
  (void)arg;
  sys_session_no_wait();  // the streams wait behind it: it must not wait for a call's answer
  while (1) {
    size_t size = 0;
    uint8_t* item = xRingbufferReceive(sys_data_connector_rb, &size, WAIT_FOREVER);
    if (item == NULL) {
      continue;
    }
    // Sent from the queue: its room comes back once every target has it.
    if (item[0] == MIRROR_MARK) {
      mirror_in(item, (uint32_t)size);
    } else {
      deliver(item, (uint32_t)size);
    }
    vRingbufferReturnItem(sys_data_connector_rb, item);
  }
}
