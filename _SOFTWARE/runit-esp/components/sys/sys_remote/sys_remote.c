#include "sys_remote.h"

#include <string.h>

#include "freertos/ringbuf.h"
#include "router.h"
#include "sdkconfig.h"
#include "sys_device.h"
#include "sys_device_errors.h"
#include "sys_event.h"
#include "sys_remote_router.h"
#include "sys_session.h"
#include "sys_watch.h"
#include "utils.h"

#define ANSWER_MAX   16u  // a write's answer has no data; a little room for what a device answers anyway
#define DEV_REQ_ID   2u   // a device command: [ROUTER_KEY_DEVICE][dir][remote_id u16][op u16][args]
#define DEV_TARGET   2u   // the bytes after the dir byte that name the device (its id): sys_watch_stale
#define FRAME_HEAD   4u   // a frame of the channel: [SYS_SESSION_CHANNEL_REMOTE][id u16][kind]
#define IN_HEAD      4u   // a frame come in, queued: [IN][board][seq u16], then the frame
#define ERROR_MAX    160u // an error record of a device, on the reporter's stack
#define CHAIN_MAX    (CONFIG_ERRORS_POOL_BLOCKS + 1u)
#define WATCH_AGE_MS (2u * CONFIG_SYS_REMOTE_WATCH_MS)  // the copy's age: the watch reads every half of it

/* The queue's items: what goes out starts with the channel's byte, what came in with IN (no channel has 0). */
#define IN 0u

_Static_assert(SYS_SESSION_CHANNEL_REMOTE != IN, "the queue's marks");

// A peer subscribed to a device of this board (SUBSCRIBE). The first entry of a device holds its event subscription.
typedef struct {
  bool used;
  bool listens;  // holds `handle`, the device's event subscription
  uint8_t peer;
  uint8_t handle;
  uint16_t id;
  uint16_t seq;  // frames made for the peer, sent or not
} sub_t;

// A remote device of this board whose events and errors come here: its board's subscription to it.
typedef struct {
  bool used;
  bool have_seq;
  uint8_t board;
  uint16_t remote_id;
  uint16_t local;
  uint16_t seq;  // the last frame that came
} alias_t;

static struct {
  sub_t subs[CONFIG_SYS_REMOTE_SUBSCRIPTIONS];
  alias_t aliases[CONFIG_SYS_REMOTE_ALIASES];
  uint32_t not_wired[(SYS_SESSION_ID_LAST + 32u) / 32u];  // a board's ERR_REMOTE_NOT_WIRED went out: once until wired
} s_rem;

U_MUTEX_DEFINE(s_remote_lock);  // the tables: short copies only, never held while calling out
U_RINGBUFFER_DEFINE(s_remote_rb, CONFIG_SYS_REMOTE_QUEUE_SIZE, RINGBUF_TYPE_NOSPLIT);
U_TASK_DEFINE(s_remote_task_h, CONFIG_SYS_REMOTE_TASK_STACK);

static uint16_t rd16(const uint8_t* p) {
  return (uint16_t)(p[0] | (p[1] << 8));
}

/* ----------------------------------------------------------------------------
 * This board's devices: the peers subscribed to them
 * ------------------------------------------------------------------------- */

// Every subscription of device `id` counts a frame it never got (the queue had no room). Under the lock.
static void count_lost_locked(uint16_t id) {
  for (uint32_t i = 0; i < CONFIG_SYS_REMOTE_SUBSCRIPTIONS; i++) {
    if (s_rem.subs[i].used && s_rem.subs[i].id == id) {
      s_rem.subs[i].seq++;
    }
  }
}

static bool has_subscriber(uint16_t id) {
  bool any = false;
  xSemaphoreTake(s_remote_lock, portMAX_DELAY);
  for (uint32_t i = 0; i < CONFIG_SYS_REMOTE_SUBSCRIPTIONS && !any; i++) {
    any = s_rem.subs[i].used && s_rem.subs[i].id == id;
  }
  xSemaphoreGive(s_remote_lock);
  return any;
}

/* Queues a frame [CHANNEL_REMOTE][id][kind][data] for the subscribers of device `id`. Only queues: a reporter may be a
   timer's task, and the frame goes out from the task. A frame the queue can't take still counts in their seq. */
static void queue_out(uint16_t id, uint8_t kind, const void* data, uint32_t len) {
  void* slot = NULL;
  if (xRingbufferSendAcquire(s_remote_rb, &slot, FRAME_HEAD + len, NO_WAIT) != pdTRUE) {
    xSemaphoreTake(s_remote_lock, portMAX_DELAY);
    count_lost_locked(id);
    xSemaphoreGive(s_remote_lock);
    return;
  }
  uint8_t* item = slot;
  item[0] = SYS_SESSION_CHANNEL_REMOTE;
  item[1] = (uint8_t)id;
  item[2] = (uint8_t)(id >> 8);
  item[3] = kind;
  memcpy(item + FRAME_HEAD, data, len);
  xRingbufferSendComplete(s_remote_rb, slot);
}

// An event of a subscribed device (the event task).
static err_h on_device_event(const sys_event_t* event, void* ctx) {
  (void)ctx;
  queue_out(event->source, SYS_REMOTE_EVENT, event, sizeof(*event));
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
  const uint16_t id = ERR_DATA(n, ERR_DEV_ID)->id;
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
    err_report(sys_event_unsubscribe((uint8_t)handle));
  }
}

err_h sys_remote_subscribe(uint16_t id, uint8_t peer) {
  ERR_CHECK(peer != SYS_SESSION_ID_SELF && peer != SYS_SESSION_ID_NONE && peer != SYS_SESSION_ID_BROADCAST,
            ERR_CMD_BAD_ARG);
  int at = -1;
  bool known = false;
  bool listened = false;
  xSemaphoreTake(s_remote_lock, portMAX_DELAY);
  for (uint32_t i = 0; i < CONFIG_SYS_REMOTE_SUBSCRIPTIONS; i++) {
    const sub_t* s = &s_rem.subs[i];
    known |= s->used && s->id == id && s->peer == peer;
    listened |= s->used && s->id == id;
    at = at < 0 && !s->used ? (int)i : at;
  }
  if (!known && at >= 0) {
    s_rem.subs[at] = (sub_t){.used = true, .peer = peer, .id = id};
  }
  xSemaphoreGive(s_remote_lock);
  if (known) {
    return NULL;
  }
  ERR_CHECK(at >= 0, ERR_REMOTE_SUBS_FULL, .id = id, .max = CONFIG_SYS_REMOTE_SUBSCRIPTIONS);
  if (listened) {
    return NULL;
  }
  // The first subscriber of the device: its events, from the event task.
  uint8_t handle = 0;
  err_h err = sys_event_subscribe_fn(id, SYS_EVENT_ANY_EVENT, SYS_EVENT_ANY_CHANNEL, on_device_event, NULL, &handle);
  xSemaphoreTake(s_remote_lock, portMAX_DELAY);
  sub_t* s = &s_rem.subs[at];
  const bool same = s->used && s->id == id && s->peer == peer;
  if (same && err == NULL) {
    s->listens = true;
    s->handle = handle;
  } else if (same) {
    s->used = false;
  }
  xSemaphoreGive(s_remote_lock);
  if (!same && err == NULL) {
    drop_listener(handle);  // unsubscribed meanwhile
  }
  return err;
}

void sys_remote_unsubscribe(uint16_t id, uint8_t peer) {
  int handle = -1;
  xSemaphoreTake(s_remote_lock, portMAX_DELAY);
  for (uint32_t i = 0; i < CONFIG_SYS_REMOTE_SUBSCRIPTIONS; i++) {
    if (s_rem.subs[i].used && s_rem.subs[i].id == id && s_rem.subs[i].peer == peer) {
      handle = sub_free_locked(i);
      break;
    }
  }
  xSemaphoreGive(s_remote_lock);
  drop_listener(handle);
}

// A frame for the subscribers of its device, on the task: each gets it with its own seq, NOWAIT.
static void send_out(const uint8_t* frame, uint32_t len) {
  const uint16_t id = rd16(frame + 1);
  struct {
    uint8_t peer;
    uint16_t seq;
  } to[CONFIG_SYS_REMOTE_SUBSCRIPTIONS];
  uint32_t n = 0;
  xSemaphoreTake(s_remote_lock, portMAX_DELAY);
  for (uint32_t i = 0; i < CONFIG_SYS_REMOTE_SUBSCRIPTIONS; i++) {
    sub_t* s = &s_rem.subs[i];
    if (s->used && s->id == id) {
      to[n].peer = s->peer;
      to[n++].seq = s->seq++;
    }
  }
  xSemaphoreGive(s_remote_lock);
  const uint8_t flags = frame[3] == SYS_REMOTE_ERROR ? ROUTER_FLAG_URGENT : 0;
  for (uint32_t i = 0; i < n; i++) {
    err_free(sys_session_stream(to[i].peer, to[i].seq, frame, len, flags));  // not taken: a gap in its seq
  }
}

/* ----------------------------------------------------------------------------
 * This board's remote devices: their boards' frames
 * ------------------------------------------------------------------------- */

static int alias_find_locked(uint8_t board, uint16_t remote_id) {
  for (uint32_t i = 0; i < CONFIG_SYS_REMOTE_ALIASES; i++) {
    const alias_t* a = &s_rem.aliases[i];
    if (a->used && a->board == board && a->remote_id == remote_id) {
      return (int)i;
    }
  }
  return -1;
}

// A frame of the remote channel from a board, on the RX task: queued, it is published and reported on the task.
static void on_stream(uint8_t src, uint16_t seq, const uint8_t* data, uint32_t len) {
  if (len < FRAME_HEAD || data[0] != SYS_SESSION_CHANNEL_REMOTE) {
    return;
  }
  void* slot = NULL;
  if (xRingbufferSendAcquire(s_remote_rb, &slot, IN_HEAD + len, NO_WAIT) != pdTRUE) {
    return;  // the gap shows in the next frame's seq
  }
  uint8_t* item = slot;
  item[0] = IN;
  item[1] = src;
  item[2] = (uint8_t)seq;
  item[3] = (uint8_t)(seq >> 8);
  memcpy(item + IN_HEAD, data, len);
  xRingbufferSendComplete(s_remote_rb, slot);
}

/* A frame come in, on the task: the gap in its seq is reported about the board, then the event is published and the
   error reported as the remote device's local id, so ON_EVENT, the listeners and its policy answer them. */
static void take_in(const uint8_t* item, uint32_t len) {
  const uint8_t board = item[1];
  const uint16_t seq = rd16(item + 2);
  const uint8_t* frame = item + IN_HEAD;
  const uint32_t frame_len = len - IN_HEAD;
  const uint16_t remote_id = rd16(frame + 1);
  uint16_t lost = 0;
  uint16_t local = SYS_DEVICE_ID_NONE;
  xSemaphoreTake(s_remote_lock, portMAX_DELAY);
  const int at = alias_find_locked(board, remote_id);
  if (at >= 0) {
    alias_t* a = &s_rem.aliases[at];
    lost = a->have_seq ? (uint16_t)(seq - a->seq - 1u) : 0;
    a->seq = seq;
    a->have_seq = true;
    local = a->local;
  }
  xSemaphoreGive(s_remote_lock);
  if (local == SYS_DEVICE_ID_NONE) {
    return;  // no remote device of this board names it
  }
  if (lost > 0) {
    err_report_board(ERR_NEW(ERR_REMOTE_LOST, .board = board, .count = lost), board);
  }
  const uint8_t* data = frame + FRAME_HEAD;
  const uint32_t data_len = frame_len - FRAME_HEAD;
  if (frame[3] == SYS_REMOTE_EVENT && data_len == sizeof(sys_event_t)) {
    sys_event_t ev;
    memcpy(&ev, data, sizeof(ev));
    ev.source = local;
    err_report(sys_event_publish_deferred(&ev));
  } else if (frame[3] == SYS_REMOTE_ERROR) {
    err_h chain = NULL;
    err_free(err_record_parse(data, data_len, &chain));
    err_report(ERR_WRAP(ERR_WRAP(chain, ERR_REMOTE, .board = board, .seq = seq), ERR_DEV_ID, .id = local));
  }
}

// The answer of a SUBSCRIBE: refused (the board's table full), it is reported about the board; sent again when the
// board is reached again.
static err_h on_subscribed(void* ctx, const cmd_req_t* cmd, uint8_t status, uint8_t* data, uint32_t len) {
  if (status != ROUTER_STATUS_OK) {
    const uint8_t board = (uint8_t)(uintptr_t)ctx;
    err_h chain = NULL;
    err_free(err_record_parse(data, len, &chain));
    err_report_board(ERR_WRAP(chain, ERR_REMOTE, .board = board, .seq = cmd->seq), board);
  }
  return NULL;
}

static err_h on_ignore(void* ctx, const cmd_req_t* cmd, uint8_t status, uint8_t* data, uint32_t len) {
  (void)ctx;
  (void)cmd;
  (void)status;
  (void)data;
  (void)len;
  return NULL;
}

// SUBSCRIBE or UNSUBSCRIBE of a device on its board. Never blocks: a board not reached gets it when it is.
static void send_subscription(uint8_t board, uint16_t remote_id, uint8_t op) {
  const uint8_t req[] = {ROUTER_KEY_REMOTE, CMD_DIR_WRITE, op, (uint8_t)remote_id, (uint8_t)(remote_id >> 8)};
  const router_done_f done = op == SYS_REMOTE_OP_SUBSCRIBE ? on_subscribed : on_ignore;
  err_free(sys_session_call(board, (cmd_in_t){req, sizeof(req)}, 0, 0, done, (void*)(uintptr_t)board));
}

// A remote device made: its board's subscription to it. Under the registry lock: never blocks.
static void on_added(uint16_t id, const sys_device_remote_t* where) {
  int at = -1;
  xSemaphoreTake(s_remote_lock, portMAX_DELAY);
  for (uint32_t i = 0; i < CONFIG_SYS_REMOTE_ALIASES && at < 0; i++) {
    at = s_rem.aliases[i].used ? -1 : (int)i;
  }
  if (at >= 0) {
    s_rem.aliases[at] = (alias_t){.used = true, .board = where->board, .remote_id = where->remote_id, .local = id};
  }
  xSemaphoreGive(s_remote_lock);
  if (at < 0) {
    // No slot: the device's calls work, its events and errors do not come.
    err_report_board(ERR_NEW(ERR_REMOTE_ALIASES_FULL, .board = where->board, .max = CONFIG_SYS_REMOTE_ALIASES),
                     where->board);
    return;
  }
  send_subscription(where->board, where->remote_id, SYS_REMOTE_OP_SUBSCRIBE);
}

static void on_removed(uint16_t id, const sys_device_remote_t* where) {
  bool had = false;
  xSemaphoreTake(s_remote_lock, portMAX_DELAY);
  const int at = alias_find_locked(where->board, where->remote_id);
  if (at >= 0 && s_rem.aliases[at].local == id) {
    s_rem.aliases[at].used = false;
    had = true;
  }
  xSemaphoreGive(s_remote_lock);
  if (had) {
    send_subscription(where->board, where->remote_id, SYS_REMOTE_OP_UNSUBSCRIBE);
  }
}

/* ----------------------------------------------------------------------------
 * Peers gone and reached
 * ------------------------------------------------------------------------- */

/* A peer gone: its subscriptions here end; its devices' frames start a new count. The master's session ending
   (BROADCAST) names every peer. */
static void on_gone(uint8_t peer, uint8_t via) {
  (void)via;
  const bool all = peer == SYS_SESSION_ID_BROADCAST;
  for (uint32_t i = 0; i < CONFIG_SYS_REMOTE_SUBSCRIPTIONS; i++) {
    xSemaphoreTake(s_remote_lock, portMAX_DELAY);
    const bool hit = s_rem.subs[i].used && (s_rem.subs[i].peer == peer || all);
    const int handle = hit ? sub_free_locked(i) : -1;
    xSemaphoreGive(s_remote_lock);
    drop_listener(handle);
  }
  xSemaphoreTake(s_remote_lock, portMAX_DELAY);
  for (uint32_t i = 0; i < CONFIG_SYS_REMOTE_ALIASES; i++) {
    alias_t* a = &s_rem.aliases[i];
    if (a->used && (a->board == peer || all)) {
      a->have_seq = false;
    }
  }
  xSemaphoreGive(s_remote_lock);
}

// A board reached (again): its remote devices subscribe there.
static void on_reached(uint8_t peer) {
  uint16_t remote[CONFIG_SYS_REMOTE_ALIASES];
  uint32_t n = 0;
  xSemaphoreTake(s_remote_lock, portMAX_DELAY);
  for (uint32_t i = 0; i < CONFIG_SYS_REMOTE_ALIASES; i++) {
    const alias_t* a = &s_rem.aliases[i];
    if (a->used && a->board == peer) {
      remote[n++] = a->remote_id;
    }
  }
  xSemaphoreGive(s_remote_lock);
  for (uint32_t i = 0; i < n; i++) {
    send_subscription(peer, remote[i], SYS_REMOTE_OP_SUBSCRIBE);
  }
}

/* ----------------------------------------------------------------------------
 * Calls
 * ------------------------------------------------------------------------- */

// Reaching a board failed because it is gone: the device is down. A busy way ("later") and the board's own answer stay.
static err_h down(err_h err, uint8_t board, uint16_t remote_id) {
  if (err == NULL || (err->tag != ERR_SESSION_NO_ROUTE && err->tag != ERR_SESSION_PEER_DOWN)) {
    return err;
  }
  err_free(err);
  return ERR_NEW(ERR_DEV_REMOTE_DOWN, .id = sys_device_find_remote(board, remote_id), .board = board);
}

/* The answer of a write that was not waited for, on the RX task: an OK makes the copies old, a refusal is the device's
   error (ERR_DEV_ID over it; a board that is gone: ERR_DEV_REMOTE_DOWN). */
static err_h on_write_done(void* ctx, const cmd_req_t* cmd, uint8_t status, uint8_t* data, uint32_t len) {
  const uint8_t board = (uint8_t)(uintptr_t)ctx;
  const cmd_in_t req = {cmd->req, cmd->req_len};
  if (status == ROUTER_STATUS_OK) {
    sys_watch_stale(board, req, DEV_TARGET);
    return NULL;
  }
  const uint16_t remote_id = req.len >= DEV_REQ_ID + 2u ? rd16(req.data + DEV_REQ_ID) : SYS_DEVICE_ID_NONE;
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
  xSemaphoreTake(s_remote_lock, portMAX_DELAY);
  uint32_t* word = &s_rem.not_wired[board / 32u];
  const bool tell = !wired && !(*word & bit);
  *word = wired ? (*word & ~bit) : (*word | bit);
  xSemaphoreGive(s_remote_lock);
  if (tell) {
    err_report_board(ERR_NEW(ERR_REMOTE_NOT_WIRED, .board = board), board);
  }
  return wired;
}

static err_h call(const sys_device_remote_t* where, cmd_in_t req, uint8_t* out, uint32_t cap, uint32_t* out_len) {
  const uint8_t board = where->board;
  const bool read = req.data[1] == CMD_DIR_READ;
  *out_len = 0;
  if ((where->flags & SYS_DEVICE_REMOTE_WIRED) && wired_now(board)) {
    // Every call waits for its answer: a read's value is of this pass, a write's error comes at once.
    uint8_t answer[ANSWER_MAX];
    uint32_t answer_len = 0;
    err_h err = read ? sys_session_call_wait(board, req, 0, 0, out, cap, out_len)
                     : sys_session_call_wait(board, req, 0, 0, answer, sizeof(answer), &answer_len);
    if (err == NULL && !read) {
      sys_watch_stale(board, req, DEV_TARGET);
    }
    return down(err, board, where->remote_id);
  }
  if (read) {
    return down(sys_watch_read(board, req, WATCH_AGE_MS, false, out, cap, out_len), board, where->remote_id);
  }
  return down(sys_session_call(board, req, 0, 0, on_write_done, (void*)(uintptr_t)board), board, where->remote_id);
}

/* ----------------------------------------------------------------------------
 * The task and init
 * ------------------------------------------------------------------------- */

static void remote_task(void* arg) {
  (void)arg;
  sys_session_no_wait();  // the frames wait behind it: it never waits for a call's answer
  while (1) {
    size_t size = 0;
    uint8_t* item = xRingbufferReceive(s_remote_rb, &size, portMAX_DELAY);
    if (item == NULL) {
      continue;
    }
    if (item[0] == IN) {
      take_in(item, (uint32_t)size);
    } else {
      send_out(item, (uint32_t)size);
    }
    vRingbufferReturnItem(s_remote_rb, item);
  }
}

static const sys_device_remote_ops_t k_ops = {.call = call, .added = on_added, .removed = on_removed};

err_h sys_remote_init(void) {
  ERR_TRY(sys_session_on_gone(on_gone));
  ERR_TRY(sys_session_on_reached(on_reached));
  ERR_TRY(sys_session_on_stream(on_stream));
  if (s_remote_task_h == NULL) {
    U_TASK_START(s_remote_task_h, remote_task, NULL, CONFIG_SYS_REMOTE_TASK_PRIO);
  }
  sys_device_set_remote(&k_ops);
  return NULL;
}
