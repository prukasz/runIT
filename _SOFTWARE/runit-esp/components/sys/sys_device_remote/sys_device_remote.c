#include "sys_device_remote.h"

#include <string.h>

#include "data_matrix.h"
#include "err_record.h"
#include "freertos/ringbuf.h"
#include "router_queue.h"
#include "sdkconfig.h"
#include "sys_device.h"
#include "sys_device_errors.h"
#include "sys_device_events.h"
#include "sys_device_hooks.h"
#include "sys_device_router.h"
#include "sys_event.h"
#include "sys_device_remote_router.h"
#include "sys_session.h"
#include "sys_session_events.h"
#include "sys_watch.h"
#include "utils.h"

ERR_MODULE_DEFINE(MODULE_ID_REMOTE, ERR_SYS_DEVICE_REMOTE_MAP)

#define REQ_HEAD   (1u + sizeof(sys_device_router_hdr_t))   // a device command: [key][dir][id][op u16], then the args
#define ARGS_MAX   (CMD_DATA_MAX + 1u - REQ_HEAD)           // the args of a call to a remote device: a whole request
#define ANSWER_MAX 16u                                      // a write's answer has no data; a little room anyway
#define DEV_TARGET 1u                                       // the bytes after the dir byte that name the device: its id
#define FRAME_HEAD 2u                                       // a frame: [id][kind], then the data (after its header)
#define OUT_HEAD   (1u + CMD_HEAD)                          // a frame to send, queued: [OUT], its packet's header room
#define IN_HEAD    2u                                       // a frame come in, queued: [IN][board], then the frame

/* The queue's items start with their mark: what came in with IN, what goes out with OUT. */
#define IN  0u
#define OUT 1u

// A peer subscribed to a device of this board (SUBSCRIBE). The first entry of a device holds its event subscription.
typedef struct {
  bool used;
  bool listens;  // holds `handle`, the device's event subscription
  uint8_t peer;
  uint8_t id;
  uint16_t handle;
} sub_t;

static struct {
  sub_t subs[CONFIG_SYS_DEVICE_REMOTE_SUBSCRIPTIONS];
  uint32_t not_wired[(CMD_SRC_BOARD_LAST + 32u) / 32u];  // a board's ERR_REMOTE_NOT_WIRED went out: once until wired
} s_rem;

U_MUTEX_DEFINE(sys_device_remote_lock);  // the table: short copies only, never held while calling out
U_RINGBUFFER_DEFINE(sys_device_remote_rb, CONFIG_SYS_DEVICE_REMOTE_QUEUE_SIZE, RINGBUF_TYPE_NOSPLIT);
U_TASK_DEFINE(sys_device_remote_task_h, CONFIG_SYS_DEVICE_REMOTE_TASK_STACK);

/* ----------------------------------------------------------------------------
 * This board's devices: the peers subscribed to them
 * ------------------------------------------------------------------------- */

static bool has_subscriber(uint8_t id) {
  bool any = false;
  U_MUTEX_GUARD(sys_device_remote_lock);
  for (uint32_t i = 0; i < CONFIG_SYS_DEVICE_REMOTE_SUBSCRIPTIONS && !any; i++) {
    any = s_rem.subs[i].used && s_rem.subs[i].id == id;
  }
  return any;
}

/* Queues a frame [id][kind][data] for the subscribers of device `id`, out of REMOTE, after the room of its packet's
   header (the task writes it there and sends the item as it is). Only queues: a reporter may be a timer's task, and
   the frame goes out from the task. A frame the queue can't take is lost. */
static void queue_out(uint8_t id, uint8_t kind, const void* data, uint32_t len) {
  void* slot = NULL;
  if (xRingbufferSendAcquire(sys_device_remote_rb, &slot, OUT_HEAD + FRAME_HEAD + len, NO_WAIT) != pdTRUE) {
    return;
  }
  uint8_t* item = slot;
  item[0] = OUT;
  item[OUT_HEAD] = id;
  item[OUT_HEAD + 1u] = kind;
  memcpy(item + OUT_HEAD + FRAME_HEAD, data, len);
  xRingbufferSendComplete(sys_device_remote_rb, slot);
}

// An event of a subscribed device (the event task). The registry's CREATED and REMOVED stay on their board: here they
// would reach the policy as the alias's own (the alias exists), and it would forget the alias's settings.
static err_h on_device_event(const sys_event_t* event, void* ctx) {
  (void)ctx;
  if (event->key.tag != SYS_DEVICE_EV_CREATED && event->key.tag != SYS_DEVICE_EV_REMOVED) {
    queue_out((uint8_t)event->key.source, SYS_DEVICE_REMOTE_EVENT, event, sizeof(*event) + event->len);
  }
  return NULL;
}

// Whether the device, as wired, has a RESET: its OPS answer, rows of [op u16][sides u8].
static bool has_reset(uint8_t id) {
  uint8_t ops[CMD_DATA_MAX];
  cmd_out_t out = {.data = ops, .cap = sizeof(ops)};
  err_h err = sys_device_call(id, CMD_DIR_READ, CONTRACT_SYS_DEVICE_OP_OPS, (cmd_in_t){NULL, 0}, &out);
  bool found = false;
  for (uint32_t at = 0; err == NULL && !found && at + 3u <= out.len; at += 3u) {
    uint16_t op;
    memcpy(&op, ops + at, sizeof(op));
    found = op == CONTRACT_SYS_DEVICE_OP_RESET;
  }
  err_free(err);  // no such device: nothing was reset
  return found;
}

/* Whether a sweep changed device `id`. SUSPENDED: every device that is there (a look at the flag could be late, after
 * the RESUME, and lose it). RESUMED: not one whose resume failed, it is still suspended. RESET: not one without a
 * RESET, the sweep left it out. */
static bool sweep_changed(uint8_t id, uint32_t tag) {
  if (tag == SYS_DEVICE_EV_RESET) {
    return has_reset(id);
  }
  sys_device_info_t info = {0};
  err_h err = SYS_DEVICE_GET(id, CONTRACT_SYS_DEVICE, OP_INFO, &info);
  const bool there = err == NULL;
  err_free(err);
  return there && (tag == SYS_DEVICE_EV_SUSPENDED || info.suspended == 0);
}

/* A sweep of this board (the registry's one event for it, source SYS_DEVICE_ID_NONE, no payload): each followed device
 * it changed gets it as its own, so the board with its alias hears of it. The sweep's own event can't go: there it
 * would read as that board's sweep. */
static err_h on_sweep(const sys_event_t* event, void* ctx) {
  (void)ctx;
  for (uint32_t i = 0; i < CONFIG_SYS_DEVICE_REMOTE_SUBSCRIPTIONS; i++) {
    uint8_t id;
    bool followed;  // one entry per followed device holds its event subscription
    {
      U_MUTEX_GUARD(sys_device_remote_lock);
      followed = s_rem.subs[i].used && s_rem.subs[i].listens;
      id = s_rem.subs[i].id;
    }
    if (followed && sweep_changed(id, event->key.tag)) {
      sys_event_t own = *event;
      own.key.source = id;
      queue_out(id, SYS_DEVICE_REMOTE_EVENT, &own, sizeof(own));
    }
  }
  return NULL;
}

void sys_device_remote_on_error(err_h chain, const cmd_req_t* cmd) {
  if (cmd != NULL) {
    return;  // a command's errors go in its answer, to whoever gave it
  }
  const err_h n = err_find(chain, ERR_DEV_ID);
  if (n == NULL) {
    return;
  }
  const uint8_t id = ERR_DATA(n, ERR_DEV_ID)->id;
  if (!has_subscriber(id)) {
    return;
  }
  uint8_t rec[ERR_RECORD_MAX];
  queue_out(id, SYS_DEVICE_REMOTE_ERROR, rec, err_record(chain, rec, sizeof(rec)));
}

// Frees subscription i; returns the event subscription to drop outside the lock (-1: none). Under the lock.
static int sub_free_locked(uint32_t i) {
  sub_t* s = &s_rem.subs[i];
  s->used = false;
  if (!s->listens) {
    return -1;
  }
  s->listens = false;
  for (uint32_t k = 0; k < CONFIG_SYS_DEVICE_REMOTE_SUBSCRIPTIONS; k++) {
    if (s_rem.subs[k].used && s_rem.subs[k].id == s->id) {
      s_rem.subs[k].listens = true;  // another peer follows the device: it takes the event subscription
      s_rem.subs[k].handle = s->handle;
      return -1;
    }
  }
  return s->handle;
}

static void drop_listener(int handle) {
  if (handle >= 0) {
    err_report(sys_event_unsubscribe((uint16_t)handle));
  }
}

err_h sys_device_remote_subscribe(uint8_t id, uint8_t peer) {
  ERR_CHECK(peer != CMD_SRC_SELF && peer != CMD_SRC_NONE && peer != CMD_SRC_ALL, ERR_BAD_ARG);
  data_matrix_port_id_t port = 0;
  ERR_TRY(sys_session_port(peer, MODULE_ID_REMOTE, &port));  // no sub-port REMOTE: the device's frames can't reach it
  int at = -1;
  bool known = false;
  bool listened = false;
  {
    U_MUTEX_GUARD(sys_device_remote_lock);
    for (uint32_t i = 0; i < CONFIG_SYS_DEVICE_REMOTE_SUBSCRIPTIONS; i++) {
      const sub_t* s = &s_rem.subs[i];
      known |= s->used && s->id == id && s->peer == peer;
      listened |= s->used && s->id == id;
      at = at < 0 && !s->used ? (int)i : at;
    }
    if (!known && at >= 0) {
      s_rem.subs[at] = (sub_t){.used = true, .peer = peer, .id = id};
    }
  }
  if (known) {
    return NULL;
  }
  ERR_CHECK(at >= 0, ERR_REMOTE_SUBS_FULL, .id = id, .max = CONFIG_SYS_DEVICE_REMOTE_SUBSCRIPTIONS);
  if (listened) {
    return NULL;
  }
  // The first subscriber of the device: its events, from the event task.
  uint16_t handle = 0;
  err_h err = sys_event_subscribe(
      (sys_event_key_t){.tag = SYS_EVENT_ANY_TAG, .source = id, .channel = SYS_EVENT_ANY_CHANNEL}, on_device_event, NULL,
      SYS_EVENT_RUN_QUEUED_DEVICE, &handle);
  bool same;
  {
    U_MUTEX_GUARD(sys_device_remote_lock);
    sub_t* s = &s_rem.subs[at];
    same = s->used && s->id == id && s->peer == peer;
    if (same && err == NULL) {
      s->listens = true;
      s->handle = handle;
    } else if (same) {
      s->used = false;
    }
  }
  if (!same && err == NULL) {
    drop_listener(handle);  // unsubscribed meanwhile
  }
  return err;
}

void sys_device_remote_unsubscribe(uint8_t id, uint8_t peer) {
  int handle = -1;
  {
    U_MUTEX_GUARD(sys_device_remote_lock);
    for (uint32_t i = 0; i < CONFIG_SYS_DEVICE_REMOTE_SUBSCRIPTIONS; i++) {
      if (s_rem.subs[i].used && s_rem.subs[i].id == id && s_rem.subs[i].peer == peer) {
        handle = sub_free_locked(i);
        break;
      }
    }
  }
  drop_listener(handle);
}

static void frame_in(data_matrix_port_t* self, data_matrix_port_id_t from, uint8_t flags, const uint8_t* packet,
                     uint32_t size);

// REMOTE, DATA_MATRIX_PORT(MODULE, MODULE_ID_REMOTE): frames out to a subscriber's session by name, in from the boards.
static data_matrix_port_t s_port = {.id = DATA_MATRIX_PORT(DATA_MATRIX_PORT_MODULE, MODULE_ID_REMOTE), .in = frame_in};

// A frame for the subscribers of its device, on the task: `packet` is [room of CMD_HEAD][frame]; the header
// [hdr PUSH, MODULE_ID_REMOTE] goes into the room and the packet to each one's session (its sub-port
// MODULE_ID_REMOTE), NOWAIT.
static void send_out(uint8_t* packet, uint32_t len) {
  const uint8_t* frame = packet + CMD_HEAD;
  const uint8_t id = frame[0];
  uint8_t to[CONFIG_SYS_DEVICE_REMOTE_SUBSCRIPTIONS];
  uint32_t n = 0;
  {
    U_MUTEX_GUARD(sys_device_remote_lock);
    for (uint32_t i = 0; i < CONFIG_SYS_DEVICE_REMOTE_SUBSCRIPTIONS; i++) {
      if (s_rem.subs[i].used && s_rem.subs[i].id == id) {
        to[n++] = s_rem.subs[i].peer;
      }
    }
  }
  const uint8_t urgent = frame[1] == SYS_DEVICE_REMOTE_ERROR ? CMD_SEND_URGENT : 0u;
  cmd_push_head(packet, MODULE_ID_REMOTE, urgent);
  for (uint32_t i = 0; i < n; i++) {
    data_matrix_port_id_t port = 0;
    err_h err = sys_session_port(to[i], MODULE_ID_REMOTE, &port);
    if (err == NULL) {
      (void)data_matrix_send_to(&s_port, port, (uint8_t)(CMD_SEND_NOWAIT | urgent), packet, len);
    }
    err_free(err);  // not reached now: it subscribes again when it is
  }
}

/* ----------------------------------------------------------------------------
 * This board's remote devices: their boards' frames
 * ------------------------------------------------------------------------- */

// A frame from a board, [hdr PUSH, MODULE_ID_REMOTE][id][kind][data], on the RX task: queued, it is published and
// reported on the task. The board is its src: a frame from anyone else (a client) is dropped.
static void frame_in(data_matrix_port_t* self, data_matrix_port_id_t from, uint8_t flags, const uint8_t* packet,
                     uint32_t size) {
  (void)self;
  (void)from;
  (void)flags;
  cmd_hdr_t hdr;
  if (size < CMD_HEAD + FRAME_HEAD) {
    return;
  }
  memcpy(&hdr, packet, CMD_HEAD);
  const uint8_t src = hdr.src;
  const uint8_t* data = packet + CMD_HEAD;
  const uint32_t len = size - CMD_HEAD;
  if (src < CMD_SRC_BOARD_FIRST || src > CMD_SRC_BOARD_LAST) {
    return;
  }
  void* slot = NULL;
  if (xRingbufferSendAcquire(sys_device_remote_rb, &slot, IN_HEAD + len, NO_WAIT) != pdTRUE) {
    return;
  }
  uint8_t* item = slot;
  item[0] = IN;
  item[1] = src;
  memcpy(item + IN_HEAD, data, len);
  xRingbufferSendComplete(sys_device_remote_rb, slot);
}

static void send_subscription(uint8_t board, uint8_t remote_id, uint8_t op);

// The answer of a SUBSCRIBE: none in the call's time (lost on the way), it is sent again; refused (the board's table
// full, no sub-port REMOTE on its session to this one), it is reported about the board and sent again when the board
// is reached again. A session gone meanwhile: the board's REACHED sends it again.
static err_h on_subscribed(void* ctx, const cmd_req_t* cmd, uint8_t status, const uint8_t* data, uint32_t len) {
  if (status == ROUTER_STATUS_OK) {
    return NULL;
  }
  const uint8_t board = (uint8_t)(uintptr_t)ctx;
  err_h chain = NULL;
  err_free(err_record_parse(data, len, &chain));
  if (err_tag(chain) == ERR_ROUTER_CALL_TIMEOUT) {
    err_free(chain);
    send_subscription(board, cmd->req[3], REMOTE_OP_SUBSCRIBE);  // [key][dir][op][id]
  } else if (err_tag(chain) == ERR_ROUTER_CALL_DOWN) {
    err_free(chain);
  } else {
    err_report(chain);  // the board's answer: its chain under ERR_REMOTE{board} already (router_request)
  }
  return NULL;
}

// SUBSCRIBE or UNSUBSCRIBE of a device on its board. Never blocks: a board not reached gets it when it is.
static void send_subscription(uint8_t board, uint8_t remote_id, uint8_t op) {
  const uint8_t req[] = {MODULE_ID_REMOTE, CMD_DIR_WRITE, op, remote_id};
  const router_done_f done = op == REMOTE_OP_SUBSCRIBE ? on_subscribed : NULL;  // UNSUBSCRIBE: no answer kept
  err_free(sys_session_request(board, (cmd_in_t){req, sizeof(req)}, 0, 0, done, (void*)(uintptr_t)board));
}

/* A frame come in, on the task: the event is published and the error reported as the remote device's local id, so
   ON_EVENT, the listeners and its policy answer them. A frame no remote device names (it was removed) ends the
   board's subscription to it. */
static void take_in(const uint8_t* item, uint32_t len) {
  const uint8_t board = item[1];
  const uint8_t* frame = item + IN_HEAD;
  const uint8_t local = sys_device_find_remote(board, frame[0]);
  if (local == SYS_DEVICE_ID_NONE) {
    send_subscription(board, frame[0], REMOTE_OP_UNSUBSCRIBE);
    return;
  }
  const uint8_t* data = frame + FRAME_HEAD;
  const uint32_t data_len = len - IN_HEAD - FRAME_HEAD;
  if (frame[1] == SYS_DEVICE_REMOTE_EVENT && data_len >= sizeof(sys_event_t)) {
    sys_event_t ev;
    memcpy(&ev, data, sizeof(ev));
    if (data_len == sizeof(ev) + ev.len) {
      ev.key.source = local;
      err_report(sys_event_publish(ev.key, ev.flags | SYS_EVENT_DEFER, data + sizeof(ev), ev.len));
    }
  } else if (frame[1] == SYS_DEVICE_REMOTE_ERROR) {
    err_report(ERR_WRAP(router_remote_chain(data, data_len, board, 0), ERR_DEV_ID, .id = local));
  }
}

// A remote device made: its board's subscription to it (the registry published CREATED, on the event task).
static err_h on_created(const sys_event_t* event, void* ctx) {
  (void)ctx;
  sys_device_remote_t where;
  if (sys_device_remote_of((uint8_t)event->key.source, &where)) {
    send_subscription(where.board, where.remote_id, REMOTE_OP_SUBSCRIBE);
  }
  return NULL;
}

/* ----------------------------------------------------------------------------
 * Peers gone and reached
 * ------------------------------------------------------------------------- */

// A session ended (SESSION_EV_GONE, the peer is the event's source): its subscriptions here end.
static err_h on_gone(const sys_event_t* event, void* ctx) {
  (void)ctx;
  const uint8_t peer = (uint8_t)event->key.source;
  const bool all = peer == CMD_SRC_ALL;
  for (uint32_t i = 0; i < CONFIG_SYS_DEVICE_REMOTE_SUBSCRIPTIONS; i++) {
    int handle;
    {
      U_MUTEX_GUARD(sys_device_remote_lock);
      const bool hit = s_rem.subs[i].used && (s_rem.subs[i].peer == peer || all);
      handle = hit ? sub_free_locked(i) : -1;
    }
    drop_listener(handle);
  }
  return NULL;
}

// A session opened (SESSION_EV_REACHED, the peer is the event's source): its board's remote devices subscribe there.
static err_h on_reached(const sys_event_t* event, void* ctx) {
  (void)ctx;
  const uint8_t peer = (uint8_t)event->key.source;
  for (uint8_t id = 0; id < SYS_DEVICE_COUNT; id++) {
    sys_device_remote_t where;
    if (sys_device_remote_of(id, &where) && where.board == peer) {
      send_subscription(peer, where.remote_id, REMOTE_OP_SUBSCRIBE);
    }
  }
  return NULL;
}

/* ----------------------------------------------------------------------------
 * Calls
 * ------------------------------------------------------------------------- */

// Reaching a board failed because it is gone: the device is down. A busy way ("later") and the board's own answer stay.
static err_h down(err_h err, uint8_t board, uint8_t remote_id) {
  if (err == NULL || (err_tag(err) != ERR_SESSION_NO_ROUTE && err_tag(err) != ERR_ROUTER_CALL_DOWN)) {
    return err;
  }
  err_free(err);
  return ERR_NEW(ERR_DEV_REMOTE_DOWN, .id = sys_device_find_remote(board, remote_id), .board = board);
}

/* The answer of a write that was not waited for, on the RX task: an OK makes the copies old, a refusal is the device's
   error (ERR_DEV_ID over it; a board that is gone: ERR_DEV_REMOTE_DOWN). */
static err_h on_write_done(void* ctx, const cmd_req_t* cmd, uint8_t status, const uint8_t* data, uint32_t len) {
  const uint8_t board = (uint8_t)(uintptr_t)ctx;
  const cmd_in_t req = {cmd->req, cmd->req_len};
  if (status == ROUTER_STATUS_OK) {
    sys_watch_stale(board, req, DEV_TARGET);
    return NULL;
  }
  const uint8_t remote_id = req.len >= REQ_HEAD ? req.data[2] : SYS_DEVICE_ID_NONE;
  err_h chain = NULL;
  err_free(err_record_parse(data, len, &chain));
  err_report(ERR_WRAP(down(chain, board, remote_id), ERR_DEV_ID, .id = sys_device_find_remote(board, remote_id)));
  return NULL;
}

/* Whether a WIRED device's board is a wired neighbour now. Not: reported once about the board, until it is again, and
   the call goes the other way (the program's timing has changed). */
static bool wired_now(uint8_t board) {
  const bool wired = sys_session_wired(board);
  const uint32_t bit = 1u << (board % 32u);
  bool tell;
  {
    U_MUTEX_GUARD(sys_device_remote_lock);
    uint32_t* word = &s_rem.not_wired[board / 32u];
    tell = !wired && !(*word & bit);
    *word = wired ? (*word & ~bit) : (*word | bit);
  }
  if (tell) {
    err_report(ERR_NEW(ERR_REMOTE_NOT_WIRED, .board = board));
  }
  return wired;
}

/* The remote hook: the call as a device command of its board, with the device's ID there. A WIRELESS read comes from
   the watch's copy, but for a DIRECT one (it takes something or costs: a watch would run it every period), sent and
   waited for as a WIRED one. */
static err_h call(const sys_device_remote_t* where, uint8_t dir, uint16_t op, uint8_t op_flags, cmd_in_t in,
                  cmd_out_t* out) {
  ERR_CHECK(in.len <= ARGS_MAX, ERR_CMD_BAD_LEN, .got = (uint16_t)in.len, .need = ARGS_MAX);
  uint8_t buf[REQ_HEAD + ARGS_MAX];
  const sys_device_router_hdr_t hdr = {.dir = dir, .id = where->remote_id, .op = op};
  buf[0] = MODULE_ID_SYS_DEVICE;
  memcpy(buf + 1, &hdr, sizeof(hdr));
  if (in.len > 0) {
    memcpy(buf + REQ_HEAD, in.data, in.len);
  }
  const cmd_in_t req = {buf, REQ_HEAD + in.len};
  const uint8_t board = where->board;
  const bool read = dir == CMD_DIR_READ;
  out->len = 0;
  const bool direct = read && (op_flags & SYS_DEVICE_OP_DIRECT);
  if (((where->flags & SYS_DEVICE_REMOTE_WIRED) && wired_now(board)) || direct) {
    // Every call waits for its answer: a read's value is of this pass, a write's error comes at once.
    uint8_t answer[ANSWER_MAX];
    uint32_t answer_len = 0;
    const err_h err = read ? sys_session_request_wait(board, req, 0, 0, out->data, out->cap, &out->len)
                           : sys_session_request_wait(board, req, 0, 0, answer, sizeof(answer), &answer_len);
    if (err == NULL && !read) {
      sys_watch_stale(board, req, DEV_TARGET);
    }
    return down(err, board, where->remote_id);
  }
  if (read) {
    return down(sys_watch_read(board, req, out->data, out->cap, &out->len), board, where->remote_id);
  }
  return down(sys_session_request(board, req, 0, 0, on_write_done, (void*)(uintptr_t)board), board, where->remote_id);
}

/* ----------------------------------------------------------------------------
 * The task and init
 * ------------------------------------------------------------------------- */

static void remote_task(void* arg) {
  (void)arg;
  while (1) {
    size_t size = 0;
    uint8_t* item = xRingbufferReceive(sys_device_remote_rb, &size, portMAX_DELAY);
    if (item == NULL) {
      continue;
    }
    if (item[0] == IN) {
      take_in(item, (uint32_t)size);
    } else {
      send_out(item + 1u, (uint32_t)size - 1u);
    }
    vRingbufferReturnItem(sys_device_remote_rb, item);
  }
}

// Before app_main: the task (static: stack and TCB in U_TASK_DEFINE), the port, the session and device events it
// follows, and the registry's remote hook.
__attribute__((constructor)) static void remote_start(void) {
  U_TASK_START(sys_device_remote_task_h, remote_task, NULL, CONFIG_SYS_DEVICE_REMOTE_TASK_PRIO);
  uint16_t handle = 0;
  err_boot(sys_event_subscribe(SESSION_GONE_FILTER, on_gone, NULL, SYS_EVENT_RUN_INLINE, &handle));
  err_boot(sys_event_subscribe(SESSION_REACHED_FILTER, on_reached, NULL, SYS_EVENT_RUN_INLINE, &handle));
  err_boot(data_matrix_register(&s_port));
  err_boot(sys_event_subscribe((sys_event_key_t){.tag = SYS_DEVICE_EV_CREATED, .source = SYS_EVENT_ANY_SOURCE,
                                                 .channel = SYS_EVENT_ANY_CHANNEL},
                               on_created, NULL, SYS_EVENT_RUN_QUEUED_DEVICE, &handle));
  err_boot(sys_event_subscribe((sys_event_key_t){.tag = EVT_TAG(MODULE_ID_SYS_DEVICE, SYS_EVENT_ANY_CODE),
                                                 .source = SYS_DEVICE_ID_NONE, .channel = SYS_EVENT_ANY_CHANNEL},
                               on_sweep, NULL, SYS_EVENT_RUN_QUEUED_DEVICE, &handle));
  sys_device_set_remote(call);
}
