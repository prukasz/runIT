#include "sys_remote.h"

#include <string.h>

#include "freertos/ringbuf.h"
#include "router_queue.h"
#include "sdkconfig.h"
#include "sys_device.h"
#include "sys_device_errors.h"
#include "sys_device_events.h"
#include "sys_device_hooks.h"
#include "sys_device_router.h"
#include "sys_event.h"
#include "sys_remote_router.h"
#include "sys_session.h"
#include "sys_watch.h"
#include "utils.h"

MODULE_ID_DEFINE(REMOTE, 0x2F)
ERR_MODULE_DEFINE(ERR_MOD_SYS_REMOTE, "sys_remote", ERR_SYS_REMOTE_MAP)

#define ARGS_MAX   64u                                      // the args of a call to a remote device
#define ANSWER_MAX 16u                                      // a write's answer has no data; a little room anyway
#define REQ_HEAD   (1u + sizeof(sys_device_router_hdr_t))   // a device command: [key][dir][id][op u16], then the args
#define DEV_TARGET 1u                                       // the bytes after the dir byte that name the device: its id
#define FRAME_HEAD 3u                                       // a frame of the channel: [SYS_REMOTE_CHANNEL][id][kind]
#define IN_HEAD    2u                                       // a frame come in, queued: [IN][board], then the frame
#define ERROR_MAX  160u                                     // an error record of a device, on the reporter's stack
#define CHAIN_MAX  (CONFIG_ERRORS_POOL_BLOCKS + 1u)

/* The queue's items: what goes out starts with the channel's byte, what came in with IN (no channel has 0). */
#define IN 0u

_Static_assert(SYS_REMOTE_CHANNEL != IN, "the queue's marks");

// A peer subscribed to a device of this board (SUBSCRIBE). The first entry of a device holds its event subscription.
typedef struct {
  bool used;
  bool listens;  // holds `handle`, the device's event subscription
  uint8_t peer;
  uint8_t id;
  uint16_t handle;
} sub_t;

static struct {
  sub_t subs[CONFIG_SYS_REMOTE_SUBSCRIPTIONS];
  uint32_t not_wired[(SYS_SESSION_ID_LAST + 32u) / 32u];  // a board's ERR_REMOTE_NOT_WIRED went out: once until wired
} s_rem;

U_MUTEX_DEFINE(sys_remote_lock);  // the table: short copies only, never held while calling out
U_RINGBUFFER_DEFINE(sys_remote_rb, CONFIG_SYS_REMOTE_QUEUE_SIZE, RINGBUF_TYPE_NOSPLIT);
U_TASK_DEFINE(sys_remote_task_h, CONFIG_SYS_REMOTE_TASK_STACK);

/* ----------------------------------------------------------------------------
 * This board's devices: the peers subscribed to them
 * ------------------------------------------------------------------------- */

static bool has_subscriber(uint8_t id) {
  bool any = false;
  xSemaphoreTake(sys_remote_lock, portMAX_DELAY);
  for (uint32_t i = 0; i < CONFIG_SYS_REMOTE_SUBSCRIPTIONS && !any; i++) {
    any = s_rem.subs[i].used && s_rem.subs[i].id == id;
  }
  xSemaphoreGive(sys_remote_lock);
  return any;
}

/* Queues a frame [CHANNEL_REMOTE][id][kind][data] for the subscribers of device `id`. Only queues: a reporter may be a
   timer's task, and the frame goes out from the task. A frame the queue can't take is lost. */
static void queue_out(uint8_t id, uint8_t kind, const void* data, uint32_t len) {
  void* slot = NULL;
  if (xRingbufferSendAcquire(sys_remote_rb, &slot, FRAME_HEAD + len, NO_WAIT) != pdTRUE) {
    return;
  }
  uint8_t* item = slot;
  item[0] = SYS_REMOTE_CHANNEL;
  item[1] = id;
  item[2] = kind;
  memcpy(item + FRAME_HEAD, data, len);
  xRingbufferSendComplete(sys_remote_rb, slot);
}

// An event of a subscribed device (the event task). The registry's CREATED and REMOVED stay on their board: here they
// would reach the policy as the alias's own (the alias exists), and it would forget the alias's settings.
static err_h on_device_event(const sys_event_t* event, void* ctx) {
  (void)ctx;
  if (event->key.tag != SYS_DEVICE_EV_CREATED && event->key.tag != SYS_DEVICE_EV_REMOVED) {
    queue_out((uint8_t)event->key.source, SYS_REMOTE_EVENT, event, sizeof(*event) + event->len);
  }
  return NULL;
}

void sys_remote_on_error(err_h chain, const cmd_req_t* cmd) {
  if (cmd != NULL) {
    return;  // a command's errors go in its answer, to whoever gave it
  }
  uint32_t left = CHAIN_MAX;
  err_h n = chain;
  while (n != NULL && left-- > 0 && n->tag != ERR_DEV_ID) {
    n = n->next;
  }
  if (n == NULL || n->tag != ERR_DEV_ID) {
    return;
  }
  const uint8_t id = ERR_DATA(n, ERR_DEV_ID)->id;
  if (!has_subscriber(id)) {
    return;
  }
  uint8_t rec[ERROR_MAX];
  queue_out(id, SYS_REMOTE_ERROR, rec, err_record(chain, rec, sizeof(rec)));
}

// Frees subscription i; returns the event subscription to drop outside the lock (-1: none). Under the lock.
static int sub_free_locked(uint32_t i) {
  sub_t* s = &s_rem.subs[i];
  s->used = false;
  if (!s->listens) {
    return -1;
  }
  s->listens = false;
  for (uint32_t k = 0; k < CONFIG_SYS_REMOTE_SUBSCRIPTIONS; k++) {
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

err_h sys_remote_subscribe(uint8_t id, uint8_t peer) {
  ERR_CHECK(peer != SYS_SESSION_ID_SELF && peer != SYS_SESSION_ID_NONE && peer != SYS_SESSION_ID_BROADCAST,
            ERR_CMD_BAD_ARG);
  int at = -1;
  bool known = false;
  bool listened = false;
  xSemaphoreTake(sys_remote_lock, portMAX_DELAY);
  for (uint32_t i = 0; i < CONFIG_SYS_REMOTE_SUBSCRIPTIONS; i++) {
    const sub_t* s = &s_rem.subs[i];
    known |= s->used && s->id == id && s->peer == peer;
    listened |= s->used && s->id == id;
    at = at < 0 && !s->used ? (int)i : at;
  }
  if (!known && at >= 0) {
    s_rem.subs[at] = (sub_t){.used = true, .peer = peer, .id = id};
  }
  xSemaphoreGive(sys_remote_lock);
  if (known) {
    return NULL;
  }
  ERR_CHECK(at >= 0, ERR_REMOTE_SUBS_FULL, .id = id, .max = CONFIG_SYS_REMOTE_SUBSCRIPTIONS);
  if (listened) {
    return NULL;
  }
  // The first subscriber of the device: its events, from the event task.
  uint16_t handle = 0;
  err_h err = sys_event_subscribe(
      (sys_event_key_t){.tag = SYS_EVENT_ANY_TAG, .source = id, .channel = SYS_EVENT_ANY_CHANNEL}, on_device_event, NULL,
      SYS_EVENT_RUN_QUEUED, &handle);
  xSemaphoreTake(sys_remote_lock, portMAX_DELAY);
  sub_t* s = &s_rem.subs[at];
  const bool same = s->used && s->id == id && s->peer == peer;
  if (same && err == NULL) {
    s->listens = true;
    s->handle = handle;
  } else if (same) {
    s->used = false;
  }
  xSemaphoreGive(sys_remote_lock);
  if (!same && err == NULL) {
    drop_listener(handle);  // unsubscribed meanwhile
  }
  return err;
}

void sys_remote_unsubscribe(uint8_t id, uint8_t peer) {
  int handle = -1;
  xSemaphoreTake(sys_remote_lock, portMAX_DELAY);
  for (uint32_t i = 0; i < CONFIG_SYS_REMOTE_SUBSCRIPTIONS; i++) {
    if (s_rem.subs[i].used && s_rem.subs[i].id == id && s_rem.subs[i].peer == peer) {
      handle = sub_free_locked(i);
      break;
    }
  }
  xSemaphoreGive(sys_remote_lock);
  drop_listener(handle);
}

// A frame for the subscribers of its device, on the task: each gets it, NOWAIT.
static void send_out(const uint8_t* frame, uint32_t len) {
  const uint8_t id = frame[1];
  uint8_t to[CONFIG_SYS_REMOTE_SUBSCRIPTIONS];
  uint32_t n = 0;
  xSemaphoreTake(sys_remote_lock, portMAX_DELAY);
  for (uint32_t i = 0; i < CONFIG_SYS_REMOTE_SUBSCRIPTIONS; i++) {
    if (s_rem.subs[i].used && s_rem.subs[i].id == id) {
      to[n++] = s_rem.subs[i].peer;
    }
  }
  xSemaphoreGive(sys_remote_lock);
  const uint8_t flags = frame[2] == SYS_REMOTE_ERROR ? ROUTER_FLAG_URGENT : 0;
  for (uint32_t i = 0; i < n; i++) {
    err_free(sys_session_push(to[i], (cmd_in_t){frame, len}, flags));  // not taken: a gap in the session's count
  }
}

/* ----------------------------------------------------------------------------
 * This board's remote devices: their boards' frames
 * ------------------------------------------------------------------------- */

// A frame of the remote channel from a board, on the RX task: queued, it is published and reported on the task.
static void on_push(uint8_t src, const uint8_t* data, uint32_t len) {
  if (len < FRAME_HEAD || data[0] != SYS_REMOTE_CHANNEL) {
    return;
  }
  void* slot = NULL;
  if (xRingbufferSendAcquire(sys_remote_rb, &slot, IN_HEAD + len, NO_WAIT) != pdTRUE) {
    return;
  }
  uint8_t* item = slot;
  item[0] = IN;
  item[1] = src;
  memcpy(item + IN_HEAD, data, len);
  xRingbufferSendComplete(sys_remote_rb, slot);
}

// The answer of a SUBSCRIBE: refused (the board's table full), it is reported about the board; sent again when the
// board is reached again.
static err_h on_subscribed(void* ctx, const cmd_req_t* cmd, uint8_t status, const uint8_t* data, uint32_t len) {
  (void)cmd;
  if (status != ROUTER_STATUS_OK) {
    const uint8_t board = (uint8_t)(uintptr_t)ctx;
    err_h chain = NULL;
    err_free(err_record_parse(data, len, &chain));
    err_report_board(ERR_WRAP(chain, ERR_REMOTE, .board = board, .seq = 0), board);
  }
  return NULL;
}

static err_h on_ignore(void* ctx, const cmd_req_t* cmd, uint8_t status, const uint8_t* data, uint32_t len) {
  (void)ctx;
  (void)cmd;
  (void)status;
  (void)data;
  (void)len;
  return NULL;
}

// SUBSCRIBE or UNSUBSCRIBE of a device on its board. Never blocks: a board not reached gets it when it is.
static void send_subscription(uint8_t board, uint8_t remote_id, uint8_t op) {
  const uint8_t req[] = {MODULE_ID_REMOTE, CMD_DIR_WRITE, op, remote_id};
  const router_done_f done = op == REMOTE_OP_SUBSCRIBE ? on_subscribed : on_ignore;
  err_free(sys_session_call(board, (cmd_in_t){req, sizeof(req)}, 0, 0, done, (void*)(uintptr_t)board));
}

/* A frame come in, on the task: the event is published and the error reported as the remote device's local id, so
   ON_EVENT, the listeners and its policy answer them. A frame no remote device names (it was removed) ends the
   board's subscription to it. */
static void take_in(const uint8_t* item, uint32_t len) {
  const uint8_t board = item[1];
  const uint8_t* frame = item + IN_HEAD;
  const uint8_t local = sys_device_find_remote(board, frame[1]);
  if (local == SYS_DEVICE_ID_NONE) {
    send_subscription(board, frame[1], REMOTE_OP_UNSUBSCRIBE);
    return;
  }
  const uint8_t* data = frame + FRAME_HEAD;
  const uint32_t data_len = len - IN_HEAD - FRAME_HEAD;
  if (frame[2] == SYS_REMOTE_EVENT && data_len >= sizeof(sys_event_t)) {
    sys_event_t ev;
    memcpy(&ev, data, sizeof(ev));
    if (data_len == sizeof(ev) + ev.len) {
      ev.key.source = local;
      err_report(sys_event_publish(ev.key, ev.flags | SYS_EVENT_DEFER, data + sizeof(ev), ev.len));
    }
  } else if (frame[2] == SYS_REMOTE_ERROR) {
    err_h chain = NULL;
    err_free(err_record_parse(data, data_len, &chain));
    err_report(ERR_WRAP(ERR_WRAP(chain, ERR_REMOTE, .board = board, .seq = 0), ERR_DEV_ID, .id = local));
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

// A peer gone: its subscriptions here end. The master's session ending (BROADCAST) names every peer.
static void on_gone(uint8_t peer, uint8_t role) {
  (void)role;
  const bool all = peer == SYS_SESSION_ID_BROADCAST;
  for (uint32_t i = 0; i < CONFIG_SYS_REMOTE_SUBSCRIPTIONS; i++) {
    xSemaphoreTake(sys_remote_lock, portMAX_DELAY);
    const bool hit = s_rem.subs[i].used && (s_rem.subs[i].peer == peer || all);
    const int handle = hit ? sub_free_locked(i) : -1;
    xSemaphoreGive(sys_remote_lock);
    drop_listener(handle);
  }
}

// A board reached (again): its remote devices subscribe there.
static void on_reached(uint8_t peer) {
  for (uint8_t id = 0; id < SYS_DEVICE_COUNT; id++) {
    sys_device_remote_t where;
    if (sys_device_remote_of(id, &where) && where.board == peer) {
      send_subscription(peer, where.remote_id, REMOTE_OP_SUBSCRIBE);
    }
  }
}

/* ----------------------------------------------------------------------------
 * Calls
 * ------------------------------------------------------------------------- */

// Reaching a board failed because it is gone: the device is down. A busy way ("later") and the board's own answer stay.
static err_h down(err_h err, uint8_t board, uint8_t remote_id) {
  if (err == NULL || (err->tag != ERR_SESSION_NO_ROUTE && err->tag != ERR_SESSION_PEER_DOWN)) {
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
  xSemaphoreTake(sys_remote_lock, portMAX_DELAY);
  uint32_t* word = &s_rem.not_wired[board / 32u];
  const bool tell = !wired && !(*word & bit);
  *word = wired ? (*word & ~bit) : (*word | bit);
  xSemaphoreGive(sys_remote_lock);
  if (tell) {
    err_report_board(ERR_NEW(ERR_REMOTE_NOT_WIRED, .board = board), board);
  }
  return wired;
}

// The remote hook: the call as a device command of its board, with the device's ID there.
static err_h call(const sys_device_remote_t* where, uint8_t dir, uint16_t op, cmd_in_t in, cmd_out_t* out) {
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
  if ((where->flags & SYS_DEVICE_REMOTE_WIRED) && wired_now(board)) {
    // Every call waits for its answer: a read's value is of this pass, a write's error comes at once.
    uint8_t answer[ANSWER_MAX];
    uint32_t answer_len = 0;
    err_h err = read ? sys_session_call_wait(board, req, 0, 0, out->data, out->cap, &out->len)
                     : sys_session_call_wait(board, req, 0, 0, answer, sizeof(answer), &answer_len);
    if (err == NULL && !read) {
      sys_watch_stale(board, req, DEV_TARGET);
    }
    return down(err, board, where->remote_id);
  }
  if (read) {
    return down(sys_watch_read(board, req, out->data, out->cap, &out->len), board, where->remote_id);
  }
  return down(sys_session_call(board, req, 0, 0, on_write_done, (void*)(uintptr_t)board), board, where->remote_id);
}

/* ----------------------------------------------------------------------------
 * The task and init
 * ------------------------------------------------------------------------- */

static void remote_task(void* arg) {
  (void)arg;
  while (1) {
    size_t size = 0;
    uint8_t* item = xRingbufferReceive(sys_remote_rb, &size, portMAX_DELAY);
    if (item == NULL) {
      continue;
    }
    if (item[0] == IN) {
      take_in(item, (uint32_t)size);
    } else {
      send_out(item, (uint32_t)size);
    }
    vRingbufferReturnItem(sys_remote_rb, item);
  }
}

err_h sys_remote_init(void) {
  ERR_TRY(sys_session_on_gone(on_gone));
  ERR_TRY(sys_session_on_reached(on_reached));
  ERR_TRY(sys_session_on_push(on_push));
  uint16_t handle = 0;
  ERR_TRY(sys_event_subscribe((sys_event_key_t){.tag = SYS_DEVICE_EV_CREATED, .source = SYS_EVENT_ANY_SOURCE,
                                                .channel = SYS_EVENT_ANY_CHANNEL},
                              on_created, NULL, SYS_EVENT_RUN_QUEUED, &handle));
  if (sys_remote_task_h == NULL) {
    U_TASK_START(sys_remote_task_h, remote_task, NULL, CONFIG_SYS_REMOTE_TASK_PRIO);
  }
  sys_device_set_remote(call);
  return NULL;
}
