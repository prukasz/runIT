#include "sys_session.h"

#include <string.h>

#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "sys_session_frame.h"
#include "sys_session_internal.h"
#include "utils.h"

#define TICK_MS         10u   // the timer: pings, session timeouts, call timeouts
#define REJECT_BYTES    64u   // an error record answered on the RX task (its stack)
#define RECORD_BYTES    160u  // an error record made for a call's callback
#define NVS_NAMESPACE   "session"
#define NVS_KEY_ID      "id"
#define HDR             ((uint32_t)sizeof(sys_session_hdr_t))
#define FRAME(kind)     ((uint8_t)((kind) << 6))
#define SUBTREE_IDS     (SYS_SESSION_ID_LAST + 1u)
#define SUBTREE_BYTES   ((SUBTREE_IDS + 7u) / 8u)
#define NO_WAIT_TASKS   6u    // the RX tasks of the drivers, the timer's task and whoever called sys_session_no_wait
#define CALL_MIN_MS     10u

_Static_assert(CONFIG_SYS_SESSION_TIMEOUT_MS >= SYS_SESSION_TIMEOUT_MIN_MS, "CONFIG_SYS_SESSION_TIMEOUT_MS too short");
_Static_assert((ROUTER_FLAG_URGENT | ROUTER_FLAG_NOREPLY) == SYS_SESSION_FRAME_FLAGS, "the frame byte's flags");
_Static_assert(ROUTER_STATUS_ERROR == 1, "the frame byte's status bit");
_Static_assert(ROUTER_SRC_SELF == SYS_SESSION_ID_SELF, "the board's own commands");

/* A session: a provider and the peer it leads to. Its fields are under s_lock; nothing else runs under it (a send, a
 * listener and the router are called after it is released). */
typedef struct {
  sys_data_provider_id_t provider;
  bool used;
  bool up;                   // the provider carries frames (max_frame > 0)
  bool open;                 // both hellos passed
  uint8_t peer;              // sys_session_peer_e
  uint8_t access;            // sys_session_access_e
  uint8_t peer_id;           // a board's id, or the client id it was given; SYS_SESSION_ID_NONE until its hello
  uint8_t subtree_count;     // a child: the boards reached through it (its hello's list)
  uint16_t peer_timeout_ms;  // the peer's timeout of this session, from its hello: send every third of it
  uint32_t peer_boot_id;
  int64_t last_rx_us;
  int64_t last_tx_us;
  uint8_t subtree[SUBTREE_BYTES];  // a bitmap of those boards
} session_t;

/* A call to another board waiting for its answer. */
typedef struct {
  bool used;
  uint8_t dst;
  uint8_t via;      // the session it went through
  uint8_t req_len;  // bytes of `req` kept
  uint16_t seq;
  int64_t deadline_us;
  router_done_f cb;
  void* ctx;
  uint8_t req[CONFIG_ERRORS_RECORD_REQ_MAX];
} call_t;

static U_SPINLOCK_DEFINE(s_lock);

static struct {
  session_t sessions[CONFIG_SYS_SESSION_MAX];
  call_t calls[CONFIG_SYS_SESSION_CALLS];
  uint16_t call_seq;
  uint8_t board_id;
  uint32_t boot_id;
  uint8_t mac[6];
  uint32_t timeout_ms;  // this board's timeout of its parent's and its clients' sessions (the dead-man's time)
  esp_timer_handle_t timer;
  TaskHandle_t no_wait[NO_WAIT_TASKS];
  sys_session_gone_f gone[CONFIG_SYS_SESSION_GONE_LISTENERS];
  sys_session_reached_f reached[CONFIG_SYS_SESSION_GONE_LISTENERS];
  sys_session_stream_f stream[CONFIG_SYS_SESSION_STREAM_LISTENERS];
} s_session = {.timeout_ms = CONFIG_SYS_SESSION_TIMEOUT_MS};

#define LOCK()   U_CRITICAL_ENTER(s_lock)
#define UNLOCK() U_CRITICAL_EXIT(s_lock)

static int64_t now_us(void) {
  return esp_timer_get_time();
}

static bool in_tree(const uint8_t* bitmap, uint8_t id) {
  return id < SUBTREE_IDS && ((bitmap[id >> 3] >> (id & 7u)) & 1u);
}

static void tree_add(uint8_t* bitmap, uint8_t id) {
  if (id < SUBTREE_IDS) {
    bitmap[id >> 3] |= (uint8_t)(1u << (id & 7u));
  }
}

/* ----------------------------------------------------------------------------
 * Sessions and the way to a peer
 * ------------------------------------------------------------------------- */

static bool same_id(sys_data_provider_id_t a, sys_data_provider_id_t b) {
  return a.link == b.link && a.channel == b.channel;
}

/* The slot of `provider`, -1 when it is not attached. Under the lock. */
static int find_provider(sys_data_provider_id_t provider) {
  for (int i = 0; i < CONFIG_SYS_SESSION_MAX; i++) {
    if (s_session.sessions[i].used && same_id(s_session.sessions[i].provider, provider)) {
      return i;
    }
  }
  return -1;
}

/* The open session whose peer is `peer` itself (a neighbour or a client), -1 when there is none. Under the lock. */
static int find_peer(uint8_t peer) {
  for (int i = 0; i < CONFIG_SYS_SESSION_MAX; i++) {
    const session_t* s = &s_session.sessions[i];
    if (s->used && s->open && s->peer_id == peer) {
      return i;
    }
  }
  return -1;
}

// The open session of a kind, -1 when there is none. Under the lock.
static int find_kind(uint8_t peer) {
  for (int i = 0; i < CONFIG_SYS_SESSION_MAX; i++) {
    const session_t* s = &s_session.sessions[i];
    if (s->used && s->open && s->peer == peer) {
      return i;
    }
  }
  return -1;
}

/* Whether a session of a kind is attached, open or not. Under the lock. */
static bool has_kind(uint8_t peer) {
  for (int i = 0; i < CONFIG_SYS_SESSION_MAX; i++) {
    if (s_session.sessions[i].used && s_session.sessions[i].peer == peer) {
      return true;
    }
  }
  return false;
}

/* The session a frame for `dst` leaves on (SYS_SESSION.MD "Boards, clients, the tree"): down to the child whose subtree
 * holds it, to the client of that id, else up to the parent. `from` is the session it came in on (-1: none), never
 * the way out, so a frame never goes back. -1: no way. Under the lock. */
static int next_hop(uint8_t dst, int from) {
  for (int i = 0; i < CONFIG_SYS_SESSION_MAX; i++) {
    const session_t* s = &s_session.sessions[i];
    if (i != from && s->used && s->open && s->peer == SYS_SESSION_CHILD &&
        (s->peer_id == dst || in_tree(s->subtree, dst))) {
      return i;
    }
  }
  if (dst >= SYS_SESSION_ID_CLIENT_FIRST && dst <= SYS_SESSION_ID_CLIENT_LAST) {
    for (int i = 0; i < CONFIG_SYS_SESSION_MAX; i++) {
      const session_t* s = &s_session.sessions[i];
      if (i != from && s->used && s->open && s->peer == SYS_SESSION_CLIENT && s->peer_id == dst) {
        return i;
      }
    }
  }
  const int up = find_kind(SYS_SESSION_PARENT);
  return up != from ? up : -1;
}

// This board's timeout of a session of this kind.
static uint32_t timeout_of(uint8_t peer) {
  return peer == SYS_SESSION_CHILD ? CONFIG_SYS_SESSION_TIMEOUT_MS : s_session.timeout_ms;
}

// Whether a frame from `src` may come in on session `s`: a client's carries its id, a child's its own or a board below.
static bool src_ok(const session_t* s, uint8_t src) {
  switch (s->peer) {
    case SYS_SESSION_CLIENT:
      return src == s->peer_id;
    case SYS_SESSION_CHILD:
      return src == s->peer_id || in_tree(s->subtree, src);
    default:
      return true;
  }
}

/* ----------------------------------------------------------------------------
 * Frames out
 * ------------------------------------------------------------------------- */

// A whole frame on session `at`'s provider.
static err_h send_raw(int at, const uint8_t* frame, uint32_t len, uint8_t flags) {
  LOCK();
  const sys_data_provider_id_t provider = s_session.sessions[at].provider;
  s_session.sessions[at].last_tx_us = now_us();
  UNLOCK();
  return sys_data_provider_send(provider, frame, len, flags);
}

/* One frame on a session's provider: the header, then `body`. `buf` has HDR bytes free in front of `body` when `body`
 * is buf + HDR; else the frame is built in `buf`. */
static err_h send_frame(int at, const sys_session_hdr_t* hdr, uint8_t* buf, const uint8_t* body, uint32_t len,
                        uint8_t flags) {
  memcpy(buf, hdr, HDR);
  if (body != NULL && body != buf + HDR && len > 0) {
    memmove(buf + HDR, body, len);
  }
  return send_raw(at, buf, HDR + len, flags);
}

static uint8_t send_flags(uint8_t frame_flags) {
  return SYS_DATA_PROVIDER_SEND_NOWAIT | ((frame_flags & ROUTER_FLAG_URGENT) ? SYS_DATA_PROVIDER_SEND_URGENT : 0);
}

static err_h send_control(int at, uint8_t op, const uint8_t* data, uint32_t len) {
  uint8_t buf[HDR + 1u + 1u + SUBTREE_IDS];  // the largest is GONE: [count][ids]
  ERR_CHECK(len <= 1u + SUBTREE_IDS, ERR_CMD_BAD_LEN, .got = (uint16_t)len, .need = 1u + SUBTREE_IDS);
  LOCK();
  const uint8_t dst = s_session.sessions[at].peer_id;
  UNLOCK();
  const sys_session_hdr_t hdr = {.frame = FRAME(SYS_SESSION_CONTROL), .dst = dst, .src = s_session.board_id};
  buf[HDR] = op;
  if (len > 0) {
    memcpy(buf + HDR + 1, data, len);
  }
  return send_frame(at, &hdr, buf, buf + HDR, 1u + len, SYS_DATA_PROVIDER_SEND_NOWAIT);
}

// The boards behind this board's children (and the children): its subtree. Under the lock.
static uint8_t collect_subtree(uint8_t* ids) {
  uint8_t bits[SUBTREE_BYTES] = {0};
  for (int i = 0; i < CONFIG_SYS_SESSION_MAX; i++) {
    const session_t* s = &s_session.sessions[i];
    if (s->used && s->open && s->peer == SYS_SESSION_CHILD) {
      tree_add(bits, s->peer_id);
      for (uint32_t b = 0; b < SUBTREE_BYTES; b++) {
        bits[b] |= s->subtree[b];
      }
    }
  }
  uint8_t count = 0;
  for (uint32_t id = 0; id < SUBTREE_IDS; id++) {
    if (in_tree(bits, (uint8_t)id)) {
      ids[count++] = (uint8_t)id;
    }
  }
  return count;
}

static err_h send_hello(int at) {
  uint8_t buf[HDR + 1u + sizeof(sys_session_hello_t) + SUBTREE_IDS];
  sys_session_hello_t* hello = (sys_session_hello_t*)(void*)(buf + HDR + 1);
  memset(hello, 0, sizeof(*hello));
  LOCK();
  const session_t* s = &s_session.sessions[at];
  hello->version = SYS_SESSION_VERSION;
  hello->board_id = s_session.board_id;
  hello->your_id = s->peer == SYS_SESSION_CLIENT ? s->peer_id : SYS_SESSION_ID_NONE;
  hello->boot_id = s_session.boot_id;
  hello->timeout_ms = (uint16_t)timeout_of(s->peer);
  hello->child_count = s->peer == SYS_SESSION_PARENT ? collect_subtree(hello->children) : 0;
  const uint8_t dst = s->peer_id;
  UNLOCK();
  memcpy(hello->mac, s_session.mac, sizeof(hello->mac));
  buf[HDR] = SYS_SESSION_HELLO;
  const sys_session_hdr_t hdr = {.frame = FRAME(SYS_SESSION_CONTROL), .dst = dst, .src = s_session.board_id};
  return send_frame(at, &hdr, buf, buf + HDR, 1u + sizeof(*hello) + hello->child_count,
                    SYS_DATA_PROVIDER_SEND_NOWAIT);
}

// This board's subtree changed: its parent hears it in a new hello.
static void hello_up(void) {
  LOCK();
  const int at = find_kind(SYS_SESSION_PARENT);
  UNLOCK();
  if (at >= 0) {
    err_report(send_hello(at));
  }
}

// GONE to every child: these boards can no longer be reached (the root's; each board passes it on).
static void send_gone(const uint8_t* ids, uint8_t count) {
  if (count == 0) {
    return;
  }
  uint8_t body[1u + SUBTREE_IDS];
  body[0] = count;
  memcpy(body + 1, ids, count);
  for (int at = 0; at < CONFIG_SYS_SESSION_MAX; at++) {
    LOCK();
    const session_t* s = &s_session.sessions[at];
    const bool child = s->used && s->open && s->peer == SYS_SESSION_CHILD;
    UNLOCK();
    if (child) {
      err_report(send_control(at, SYS_SESSION_GONE, body, 1u + count));
    }
  }
}

// A frame to every open child; the first error comes back, the rest are still sent.
static err_h send_down(const uint8_t* frame, uint32_t len, uint8_t flags) {
  err_h first = NULL;
  for (int at = 0; at < CONFIG_SYS_SESSION_MAX; at++) {
    LOCK();
    const session_t* s = &s_session.sessions[at];
    const bool child = s->used && s->open && s->peer == SYS_SESSION_CHILD;
    UNLOCK();
    if (child) {
      err_h err = send_raw(at, frame, len, send_flags(flags));
      if (first == NULL) {
        first = err;
      } else {
        err_free(err);
      }
    }
  }
  return first;
}

/* ----------------------------------------------------------------------------
 * Listeners
 * ------------------------------------------------------------------------- */

static void tell_gone(uint8_t peer, uint8_t via) {
  for (uint32_t i = 0; i < ARRAY_LEN(s_session.gone) && s_session.gone[i] != NULL; i++) {
    s_session.gone[i](peer, via);
  }
}

static void tell_reached(uint8_t peer) {
  for (uint32_t i = 0; i < ARRAY_LEN(s_session.reached) && s_session.reached[i] != NULL; i++) {
    s_session.reached[i](peer);
  }
}

/* ----------------------------------------------------------------------------
 * The board's calls
 * ------------------------------------------------------------------------- */

static err_cmd_t call_cmd(const call_t* c) {
  return (err_cmd_t){.req = c->req, .req_len = c->req_len, .seq = c->seq, .src = s_session.board_id};
}

static void finish_call(const call_t* c, uint8_t status, uint8_t* data, uint32_t len) {
  if (c->cb != NULL) {
    const err_cmd_t cmd = call_cmd(c);
    err_report(c->cb(c->ctx, &cmd, status, data, len));
  }
}

// A call that ends with this board's own error (no answer, the session ended).
static void fail_call(const call_t* c, err_h err) {
  uint8_t buf[CMD_HEAD + RECORD_BYTES];
  const err_cmd_t cmd = call_cmd(c);
  const uint32_t len = err_record(&cmd, err, buf + CMD_HEAD, RECORD_BYTES);
  err_free(err);
  finish_call(c, ROUTER_STATUS_ERROR, buf + CMD_HEAD, len);
}

// The answer to a call of this board (dst: this board).
static void on_answer(const sys_session_hdr_t* hdr, const uint8_t* body, uint32_t len) {
  call_t call;
  bool found = false;
  LOCK();
  for (int i = 0; i < CONFIG_SYS_SESSION_CALLS && !found; i++) {
    if (s_session.calls[i].used && s_session.calls[i].seq == hdr->seq && s_session.calls[i].dst == hdr->src) {
      call = s_session.calls[i];
      s_session.calls[i].used = false;
      found = true;
    }
  }
  UNLOCK();
  if (!found) {
    return;  // late: the call ended already
  }
  if (!(hdr->frame & SYS_SESSION_FRAME_STATUS)) {
    // The receive buffer has the frame's header in front of the body: the room the callback may write in.
    finish_call(&call, ROUTER_STATUS_OK, (uint8_t*)(uintptr_t)body, len);
    return;
  }
  // That board's chain under ERR_REMOTE, so the error policy's walk stops at it.
  err_h chain = NULL;
  err_free(err_record_parse(body, len, &chain));
  chain = ERR_WRAP(chain, ERR_REMOTE, .board = hdr->src, .seq = hdr->seq);
  fail_call(&call, chain);
}

// Ends the calls that went through session `at`: their peer is down.
static void fail_calls_via(int at) {
  for (;;) {
    call_t call;
    bool found = false;
    LOCK();
    for (int i = 0; i < CONFIG_SYS_SESSION_CALLS && !found; i++) {
      if (s_session.calls[i].used && s_session.calls[i].via == at) {
        call = s_session.calls[i];
        s_session.calls[i].used = false;
        found = true;
      }
    }
    UNLOCK();
    if (!found) {
      return;
    }
    fail_call(&call, ERR_NEW(ERR_SESSION_PEER_DOWN, .board = call.dst));
  }
}

// Ends the calls whose time is up.
static void expire_calls(int64_t now) {
  for (;;) {
    call_t call;
    bool found = false;
    LOCK();
    for (int i = 0; i < CONFIG_SYS_SESSION_CALLS && !found; i++) {
      if (s_session.calls[i].used && s_session.calls[i].deadline_us <= now) {
        call = s_session.calls[i];
        s_session.calls[i].used = false;
        found = true;
      }
    }
    UNLOCK();
    if (!found) {
      return;
    }
    fail_call(&call, ERR_NEW(ERR_SESSION_TIMEOUT, .dst = call.dst, .seq = call.seq));
  }
}

/* ----------------------------------------------------------------------------
 * Ending a session
 * ------------------------------------------------------------------------- */

// Ends a session: its peer, and the boards behind it, are gone. A client's id is free again.
static void end_session(int at) {
  uint8_t ids[SUBTREE_IDS];
  uint8_t count = 0;
  LOCK();
  session_t* s = &s_session.sessions[at];
  const bool was_open = s->open;
  const uint8_t peer = s->peer_id;
  const uint8_t via = s->peer;
  for (uint32_t id = 0; id < SUBTREE_IDS && was_open; id++) {
    if (in_tree(s->subtree, (uint8_t)id)) {
      ids[count++] = (uint8_t)id;
    }
  }
  s->open = false;
  s->peer_id = SYS_SESSION_ID_NONE;
  s->subtree_count = 0;
  memset(s->subtree, 0, sizeof(s->subtree));
  UNLOCK();
  if (!was_open) {
    return;
  }
  fail_calls_via(at);
  if (via == SYS_SESSION_PARENT) {
    tell_gone(SYS_SESSION_ID_BROADCAST, via);  // every peer outside this board's subtree
    return;
  }
  tell_gone(peer, via);
  for (uint8_t i = 0; i < count; i++) {
    tell_gone(ids[i], via);
  }
  // The others learn it: the root tells every board below, a board below tells its parent in its hello.
  LOCK();
  const bool root = !has_kind(SYS_SESSION_PARENT);
  UNLOCK();
  if (root) {
    uint8_t lost[1u + SUBTREE_IDS];
    lost[0] = peer;
    memcpy(lost + 1, ids, count);
    send_gone(lost, (uint8_t)(1u + count));
  } else {
    hello_up();
  }
}

/* ----------------------------------------------------------------------------
 * Answers
 * ------------------------------------------------------------------------- */

/* The router's done for a peer's command: the answer frame in the room in front of the data, to the session towards the
 * command's src. It waits for room in the provider's queue at most the driver's bound. */
static err_h session_answer(void* ctx, const err_cmd_t* cmd, uint8_t status, uint8_t* data, uint32_t len) {
  (void)ctx;
  LOCK();
  const int at = next_hop(cmd->src, -1);
  UNLOCK();
  ERR_CHECK(at >= 0, ERR_SESSION_NO_ROUTE, .dst = cmd->src);
  const sys_session_hdr_t hdr = {
      .frame = (uint8_t)(FRAME(SYS_SESSION_ANSWER) | (status == ROUTER_STATUS_ERROR ? SYS_SESSION_FRAME_STATUS : 0)),
      .dst = cmd->src,
      .src = s_session.board_id,
      .seq = cmd->seq,
  };
  const uint8_t flags = (cmd->flags & ROUTER_FLAG_URGENT) ? SYS_DATA_PROVIDER_SEND_URGENT : 0;
  return send_frame(at, &hdr, data - HDR, data, len, flags);
}

// A request this board does not run (refused, no route): its error record to its source, at once.
static void answer_error(const err_cmd_t* cmd, err_h err) {
  uint8_t buf[HDR + REJECT_BYTES];
  LOCK();
  const int at = next_hop(cmd->src, -1);
  UNLOCK();
  const uint32_t n = err_record(cmd, err, buf + HDR, REJECT_BYTES);
  if (at < 0) {
    return;  // the source is out of reach too
  }
  const sys_session_hdr_t hdr = {
      .frame = (uint8_t)(FRAME(SYS_SESSION_ANSWER) | SYS_SESSION_FRAME_STATUS),
      .dst = cmd->src,
      .src = s_session.board_id,
      .seq = cmd->seq,
  };
  err_report(send_frame(at, &hdr, buf, buf + HDR, n, SYS_DATA_PROVIDER_SEND_NOWAIT));
}

/* ----------------------------------------------------------------------------
 * What comes in (the driver's RX task)
 * ------------------------------------------------------------------------- */

// Notes the calling task as one that must not wait for a call's answer (the answer comes on an RX task).
static void note_no_wait(void) {
  const TaskHandle_t me = xTaskGetCurrentTaskHandle();
  LOCK();
  for (uint32_t i = 0; i < NO_WAIT_TASKS; i++) {
    if (s_session.no_wait[i] == me) {
      break;
    }
    if (s_session.no_wait[i] == NULL) {
      s_session.no_wait[i] = me;
      break;
    }
  }
  UNLOCK();
}

static bool no_wait_task(void) {
  const TaskHandle_t me = xTaskGetCurrentTaskHandle();
  bool found = false;
  LOCK();
  for (uint32_t i = 0; i < NO_WAIT_TASKS; i++) {
    found |= s_session.no_wait[i] == me;
  }
  UNLOCK();
  return found;
}

// A frame before the session opened: the peer starts over (NO_SESSION, then this board's hello).
static void no_session(int at, uint8_t src) {
  err_report(ERR_NEW(ERR_SESSION_NONE, .src = src));
  err_free(send_control(at, SYS_SESSION_NO_SESSION, NULL, 0));
  err_free(send_hello(at));
}

static void on_hello(int at, const uint8_t* body, uint32_t len) {
  sys_session_hello_t hello;
  if (len < sizeof(hello)) {
    return;
  }
  memcpy(&hello, body, sizeof(hello));
  const uint8_t* children = body + sizeof(hello);
  if (hello.version != SYS_SESSION_VERSION) {
    err_report(ERR_NEW(ERR_SESSION_VERSION, .got = hello.version, .mine = SYS_SESSION_VERSION));
    return;
  }
  if (len < sizeof(hello) + hello.child_count) {
    return;
  }
  LOCK();
  const bool restarted = s_session.sessions[at].open && s_session.sessions[at].peer_boot_id != hello.boot_id;
  UNLOCK();
  if (restarted) {
    end_session(at);  // the peer restarted: what it had here is gone
  }

  uint8_t old_tree[SUBTREE_BYTES];
  uint8_t new_tree[SUBTREE_BYTES] = {0};
  uint8_t taken_by = SYS_SESSION_ID_NONE;
  bool full = false;
  LOCK();
  session_t* s = &s_session.sessions[at];
  const bool was_open = s->open;
  const bool tree_peer = s->peer != SYS_SESSION_CLIENT;
  memcpy(old_tree, s->subtree, sizeof(old_tree));
  if (!tree_peer) {
    if (s->peer_id == SYS_SESSION_ID_NONE) {
      // A client's id: the first one no other session has.
      full = true;
      for (uint8_t id = SYS_SESSION_ID_CLIENT_FIRST; id <= SYS_SESSION_ID_CLIENT_LAST && full; id++) {
        bool taken = false;
        for (int i = 0; i < CONFIG_SYS_SESSION_MAX; i++) {
          taken |= s_session.sessions[i].used && s_session.sessions[i].peer_id == id;
        }
        if (!taken) {
          s->peer_id = id;
          full = false;
        }
      }
    }
  } else {
    // A board's id: its own place, and the boards it reaches, none of which may be this board or reached another way.
    if (hello.board_id > SYS_SESSION_ID_LAST || hello.board_id == s_session.board_id) {
      taken_by = hello.board_id;
    }
    for (uint8_t c = 0; c < hello.child_count && taken_by == SYS_SESSION_ID_NONE; c++) {
      if (children[c] > SYS_SESSION_ID_LAST || children[c] == s_session.board_id) {
        taken_by = children[c];
      }
      tree_add(new_tree, children[c]);
    }
    for (int i = 0; i < CONFIG_SYS_SESSION_MAX && taken_by == SYS_SESSION_ID_NONE; i++) {
      const session_t* o = &s_session.sessions[i];
      if (i == at || !o->used || !o->open || o->peer == SYS_SESSION_CLIENT) {
        continue;
      }
      const uint8_t ids[1] = {hello.board_id};
      if (o->peer_id == ids[0] || in_tree(o->subtree, ids[0])) {
        taken_by = ids[0];
      }
      for (uint8_t c = 0; c < hello.child_count && taken_by == SYS_SESSION_ID_NONE; c++) {
        if (o->peer_id == children[c] || in_tree(o->subtree, children[c])) {
          taken_by = children[c];
        }
      }
    }
  }
  const bool ok = !full && taken_by == SYS_SESSION_ID_NONE;
  if (ok) {
    if (tree_peer) {
      s->peer_id = hello.board_id;
      if (s->peer == SYS_SESSION_CHILD) {
        memcpy(s->subtree, new_tree, sizeof(new_tree));
        s->subtree_count = hello.child_count;
      }
    }
    s->open = true;
    s->peer_boot_id = hello.boot_id;
    s->peer_timeout_ms = hello.timeout_ms;
    s->last_rx_us = now_us();
  }
  const uint8_t peer = s->peer_id;
  const uint8_t kind = s->peer;
  UNLOCK();
  if (full) {
    err_report(ERR_NEW(ERR_SESSION_FULL));
    return;
  }
  if (taken_by != SYS_SESSION_ID_NONE) {
    err_report(ERR_NEW(ERR_SESSION_ID_TAKEN, .board = taken_by));
    return;
  }
  // A client is answered every hello (it learns its id there); a board only the first one it gets, so two boards that
  // both said hello when their provider came up do not answer each other for ever.
  if (!tree_peer || !was_open) {
    err_report(send_hello(at));
  }
  if (kind != SYS_SESSION_CHILD) {
    if (!was_open) {
      tell_reached(peer);
    }
    return;
  }
  // A child: which boards it reaches changed.
  uint8_t lost[SUBTREE_IDS];
  uint8_t lost_count = 0;
  bool changed = !was_open;
  for (uint32_t id = 0; id < SUBTREE_IDS; id++) {
    const bool before = was_open && in_tree(old_tree, (uint8_t)id);
    const bool after = in_tree(new_tree, (uint8_t)id);
    if (before && !after) {
      lost[lost_count++] = (uint8_t)id;
    }
    changed |= before != after;
  }
  for (uint8_t i = 0; i < lost_count; i++) {
    tell_gone(lost[i], kind);
  }
  if (!was_open) {
    tell_reached(peer);
  }
  for (uint32_t id = 0; id < SUBTREE_IDS; id++) {
    if (in_tree(new_tree, (uint8_t)id) && !(was_open && in_tree(old_tree, (uint8_t)id))) {
      tell_reached((uint8_t)id);
    }
  }
  if (changed) {
    LOCK();
    const bool root = !has_kind(SYS_SESSION_PARENT);
    UNLOCK();
    if (root) {
      send_gone(lost, lost_count);
    } else {
      hello_up();
    }
  }
}

// GONE from the parent: these peers can no longer be reached. Every board below hears it too.
static void on_gone_frame(int at, const uint8_t* body, uint32_t len) {
  LOCK();
  const bool from_parent = s_session.sessions[at].peer == SYS_SESSION_PARENT;
  UNLOCK();
  if (!from_parent || len < 1u || len < 1u + body[0]) {
    return;
  }
  for (uint8_t i = 0; i < body[0]; i++) {
    tell_gone(body[1 + i], SYS_SESSION_VIA_NOTICE);
  }
  send_gone(body + 1, body[0]);
}

static void on_control(int at, const uint8_t* body, uint32_t len) {
  if (len == 0) {
    return;
  }
  switch (body[0]) {
    case SYS_SESSION_HELLO:
      on_hello(at, body + 1, len - 1);
      break;
    case SYS_SESSION_NO_SESSION:
      end_session(at);
      err_report(send_hello(at));
      break;
    case SYS_SESSION_GONE:
      on_gone_frame(at, body + 1, len - 1);
      break;
    default:  // PING: its arrival is all it says
      break;
  }
}

// A request for this board (or a broadcast): to the router, with the session's framer as the callback of its answer.
static void run_request(uint8_t access, const sys_session_hdr_t* hdr, const uint8_t* body, uint32_t len) {
  err_cmd_t cmd = {.req = body, .req_len = len, .seq = hdr->seq, .src = hdr->src,
                   .flags = (uint8_t)(hdr->frame & SYS_SESSION_FRAME_FLAGS)};
  if (hdr->dst == SYS_SESSION_ID_BROADCAST) {
    cmd.flags |= ROUTER_FLAG_NOREPLY;  // a broadcast is never answered
  }
  err_h err = NULL;
  if (access == SYS_SESSION_READ && len > 1 && body[1] == CMD_DIR_WRITE) {
    err = ERR_NEW(ERR_SESSION_DENIED, .src = hdr->src);
  } else {
    err = router_submit(&cmd, session_answer, NULL);  // copied; never blocks
  }
  if (err == NULL) {
    return;
  }
  if (!(cmd.flags & ROUTER_FLAG_NOREPLY)) {
    answer_error(&cmd, err);
  }
  router_report_cmd(&cmd, err);
}

static void on_stream(const sys_session_hdr_t* hdr, const uint8_t* body, uint32_t len) {
  for (uint32_t i = 0; i < ARRAY_LEN(s_session.stream) && s_session.stream[i] != NULL; i++) {
    s_session.stream[i](hdr->src, hdr->seq, body, len);
  }
}

/* A frame for another board: on, to the next hop's provider with NOWAIT, without the router and without keeping
 * anything. A request that can't go on is answered to its source; an answer or a stream frame is dropped. */
static void forward(int from, uint8_t access, const sys_session_hdr_t* hdr, const uint8_t* data, uint32_t len) {
  const uint8_t kind = (uint8_t)((hdr->frame & SYS_SESSION_FRAME_KIND) >> 6);
  err_h err = NULL;
  LOCK();
  const int to = next_hop(hdr->dst, from);
  UNLOCK();
  if (kind == SYS_SESSION_REQUEST && access == SYS_SESSION_READ && len > HDR + 1u && data[HDR + 1u] == CMD_DIR_WRITE) {
    err = ERR_NEW(ERR_SESSION_DENIED, .src = hdr->src);
  } else if (to < 0) {
    err = ERR_NEW(ERR_SESSION_NO_ROUTE, .dst = hdr->dst);
  } else {
    err = send_raw(to, data, len, send_flags(hdr->frame));
  }
  if (err == NULL) {
    return;
  }
  if (kind == SYS_SESSION_REQUEST && !(hdr->frame & ROUTER_FLAG_NOREPLY)) {
    const err_cmd_t cmd = {.req = data + HDR, .req_len = len - HDR, .seq = hdr->seq, .src = hdr->src,
                           .flags = (uint8_t)(hdr->frame & SYS_SESSION_FRAME_FLAGS)};
    answer_error(&cmd, err);
  } else {
    err_free(err);
  }
}

/* A broadcast request (dst 0xFF) goes up to the root, which runs it and sends it down; each board runs it on the way
 * down and passes it to its children. It is never answered. */
static void on_broadcast(int at, uint8_t peer, uint8_t access, const sys_session_hdr_t* hdr, const uint8_t* data,
                         uint32_t len) {
  if (((hdr->frame & SYS_SESSION_FRAME_KIND) >> 6) != SYS_SESSION_REQUEST) {
    return;
  }
  if (peer != SYS_SESSION_PARENT) {
    LOCK();
    const int up = find_kind(SYS_SESSION_PARENT);
    UNLOCK();
    if (up >= 0) {
      err_free(send_raw(up, data, len, send_flags(hdr->frame)));
      return;
    }
  }
  (void)at;
  run_request(access, hdr, data + HDR, len - HDR);
  err_free(send_down(data, len, hdr->frame));
}

// The provider's rx (ctx: the slot): a frame, or the provider up or down.
static void on_rx(void* ctx, sys_data_provider_id_t id, const uint8_t* data, uint32_t len) {
  (void)id;
  const int at = (int)(uintptr_t)ctx;
  note_no_wait();
  if (data == NULL) {
    LOCK();
    s_session.sessions[at].up = len > 0;
    UNLOCK();
    if (len > 0) {
      err_free(send_hello(at));  // nobody may hear it yet (a BLE client not subscribed): the peer's hello asks again
    } else {
      end_session(at);
    }
    return;
  }
  sys_session_hdr_t hdr;
  if (len < HDR) {
    return;
  }
  memcpy(&hdr, data, HDR);
  const uint8_t kind = (uint8_t)((hdr.frame & SYS_SESSION_FRAME_KIND) >> 6);
  const uint8_t* body = data + HDR;
  const uint32_t body_len = len - HDR;
  if (kind == SYS_SESSION_CONTROL) {
    on_control(at, body, body_len);
    LOCK();
    s_session.sessions[at].last_rx_us = now_us();
    UNLOCK();
    return;
  }

  // Anything else on an open session only, from the peer it was opened with (or a board or client behind it).
  LOCK();
  session_t* s = &s_session.sessions[at];
  const bool ok = s->open && src_ok(s, hdr.src);
  const uint8_t access = s->access;
  const uint8_t peer = s->peer;
  if (ok) {
    s->last_rx_us = now_us();
  }
  UNLOCK();
  if (!ok) {
    no_session(at, hdr.src);
    return;
  }
  if (hdr.dst == SYS_SESSION_ID_BROADCAST) {
    on_broadcast(at, peer, access, &hdr, data, len);
  } else if (hdr.dst != s_session.board_id) {
    forward(at, access, &hdr, data, len);
  } else if (kind == SYS_SESSION_REQUEST) {
    run_request(access, &hdr, body, body_len);
  } else if (kind == SYS_SESSION_ANSWER) {
    on_answer(&hdr, body, body_len);
  } else {
    on_stream(&hdr, body, body_len);
  }
}

/* ----------------------------------------------------------------------------
 * The timer: session timeouts, pings, call timeouts
 * ------------------------------------------------------------------------- */

static void on_tick(void* arg) {
  (void)arg;
  note_no_wait();
  const int64_t now = now_us();
  for (int at = 0; at < CONFIG_SYS_SESSION_MAX; at++) {
    LOCK();
    const session_t* s = &s_session.sessions[at];
    const bool live = s->used && s->open;
    const bool silent = live && now - s->last_rx_us > (int64_t)timeout_of(s->peer) * 1000;
    const bool ping = live && s->up && s->peer_timeout_ms > 0 &&
                      now - s->last_tx_us >= (int64_t)s->peer_timeout_ms * 1000 / 3;
    UNLOCK();
    if (silent) {
      end_session(at);
    } else if (ping) {
      err_free(send_control(at, SYS_SESSION_PING, NULL, 0));  // busy: the next tick tries again
    }
  }
  expire_calls(now);
}

/* ----------------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------------- */

err_h sys_session_init(void) {
  // The board id: NVS, the default when there is none (the default partition: sys_ble initialises it again).
  uint8_t id = SYS_SESSION_ID_DEFAULT;
  esp_err_t rc = nvs_flash_init();
  if (rc == ESP_OK) {
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
      if (nvs_get_u8(nvs, NVS_KEY_ID, &id) != ESP_OK || id > SYS_SESSION_ID_LAST) {
        id = SYS_SESSION_ID_DEFAULT;
      }
      nvs_close(nvs);
    }
  }
  s_session.board_id = id;
  s_session.boot_id = esp_random();
  ERR_TRY_ESP(esp_efuse_mac_get_default(s_session.mac));
  if (s_session.timer == NULL) {
    const esp_timer_create_args_t args = {.callback = on_tick, .name = "session"};
    ERR_TRY_ESP(esp_timer_create(&args, &s_session.timer));
    ERR_TRY_ESP(esp_timer_start_periodic(s_session.timer, TICK_MS * 1000u));
  }
  return NULL;
}

uint8_t sys_session_board_id(void) {
  return s_session.board_id;
}

bool sys_session_is_root(void) {
  LOCK();
  const bool root = !has_kind(SYS_SESSION_PARENT);
  UNLOCK();
  return root;
}

err_h sys_session_set_board_id(uint8_t board_id) {
  ERR_CHECK(board_id <= SYS_SESSION_ID_LAST, ERR_CMD_BAD_ARG);
  nvs_handle_t nvs;
  ERR_TRY_ESP(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs));
  esp_err_t rc = nvs_set_u8(nvs, NVS_KEY_ID, board_id);
  if (rc == ESP_OK) {
    rc = nvs_commit(nvs);
  }
  nvs_close(nvs);
  return ERR_ESP(rc);
}

err_h sys_session_attach(sys_data_provider_id_t provider, uint8_t peer, uint8_t access) {
  ERR_CHECK(peer <= SYS_SESSION_CLIENT && access <= SYS_SESSION_FULL, ERR_CMD_BAD_ARG);
  ERR_CHECK(sys_data_provider_state(provider) & SYS_DATA_PROVIDER_STATE_FRAMED, ERR_SESSION_NOT_FRAMED,
            .link = provider.link, .channel = provider.channel);
  LOCK();
  const bool attached = find_provider(provider) >= 0;
  const bool parent_taken = peer == SYS_SESSION_PARENT && has_kind(SYS_SESSION_PARENT);
  // Clients only on a board without a parent, and no parent on a board with clients.
  const bool tree_clash = (peer == SYS_SESSION_CLIENT && has_kind(SYS_SESSION_PARENT)) ||
                          (peer == SYS_SESSION_PARENT && has_kind(SYS_SESSION_CLIENT));
  int at = -1;
  for (int i = 0; !attached && !parent_taken && !tree_clash && i < CONFIG_SYS_SESSION_MAX && at < 0; i++) {
    if (!s_session.sessions[i].used) {
      at = i;
      s_session.sessions[i] = (session_t){
          .provider = provider,
          .used = true,
          .peer = peer,
          .access = access,
          .peer_id = SYS_SESSION_ID_NONE,
      };
    }
  }
  UNLOCK();
  ERR_CHECK(!attached, ERR_CMD_BAD_ARG);
  ERR_CHECK(!parent_taken, ERR_SESSION_PARENT_TAKEN);
  ERR_CHECK(!tree_clash, ERR_SESSION_CLIENT);
  ERR_CHECK(at >= 0, ERR_SESSION_FULL);
  err_h err = sys_data_provider_open(provider, on_rx, (void*)(uintptr_t)at);
  if (err != NULL) {
    LOCK();
    s_session.sessions[at].used = false;
    UNLOCK();
    return err;
  }
  // Up already (UART): the hello goes now; else when its rx says up.
  if (sys_data_provider_max_frame(provider) > 0) {
    on_rx((void*)(uintptr_t)at, provider, NULL, sys_data_provider_max_frame(provider));
  }
  return NULL;
}

err_h sys_session_detach(sys_data_provider_id_t provider) {
  LOCK();
  const int at = find_provider(provider);
  UNLOCK();
  if (at < 0) {
    return NULL;
  }
  ERR_TRY(sys_data_provider_close(provider));
  end_session(at);
  LOCK();
  s_session.sessions[at].used = false;
  UNLOCK();
  return NULL;
}

err_h sys_session_set_timeout(uint32_t ms) {
  ERR_CHECK(ms >= SYS_SESSION_TIMEOUT_MIN_MS && ms <= SYS_SESSION_TIMEOUT_MAX_MS, ERR_CMD_BAD_ARG);
  s_session.timeout_ms = ms;
  for (int at = 0; at < CONFIG_SYS_SESSION_MAX; at++) {
    LOCK();
    const session_t* s = &s_session.sessions[at];
    const bool tell = s->used && s->open && s->peer != SYS_SESSION_CHILD;
    UNLOCK();
    if (tell) {
      err_report(send_hello(at));
    }
  }
  return NULL;
}

/* The request of a call as a frame in `buf` (CMD_FRAME_MAX): the header, then the command. */
static uint32_t call_frame(uint8_t* buf, uint8_t dst, uint16_t seq, uint8_t flags, cmd_in_t req) {
  const sys_session_hdr_t hdr = {
      .frame = (uint8_t)(FRAME(SYS_SESSION_REQUEST) | (flags & SYS_SESSION_FRAME_FLAGS)),
      .dst = dst,
      .src = s_session.board_id,
      .seq = seq,
  };
  memcpy(buf, &hdr, HDR);
  memcpy(buf + HDR, req.data, req.len);
  return HDR + req.len;
}

// A broadcast: up to the root, which runs it and sends it down; at the root, run here and sent down.
static err_h call_broadcast(cmd_in_t req, uint8_t flags) {
  uint8_t buf[CMD_FRAME_MAX];
  LOCK();
  const int up = find_kind(SYS_SESSION_PARENT);
  const uint16_t seq = ++s_session.call_seq;
  UNLOCK();
  const uint32_t len = call_frame(buf, SYS_SESSION_ID_BROADCAST, seq, flags, req);
  if (up >= 0) {
    return send_raw(up, buf, len, send_flags(flags));
  }
  err_h first = router_inject(req, (uint8_t)(flags | ROUTER_FLAG_NOREPLY), NULL, NULL);
  err_h down = send_down(buf, len, flags);
  if (first == NULL) {
    return down;
  }
  err_free(down);
  return first;
}

err_h sys_session_call(uint8_t dst, cmd_in_t req, uint8_t flags, uint32_t timeout_ms, router_done_f cb, void* ctx) {
  ERR_CHECK(req.len >= 2u && req.len <= CMD_DATA_MAX, ERR_CMD_BAD_ARG);
  if (dst == SYS_SESSION_ID_BROADCAST) {
    ERR_CHECK((flags & ROUTER_FLAG_NOREPLY) && cb == NULL, ERR_CMD_BAD_ARG);
    return call_broadcast(req, flags);
  }
  if (dst == s_session.board_id || dst == SYS_SESSION_ID_SELF) {
    return router_inject(req, flags, cb, ctx);
  }
  ERR_CHECK(!(flags & ROUTER_FLAG_NOREPLY), ERR_CMD_BAD_ARG);  // between boards every call is answered
  uint8_t buf[CMD_FRAME_MAX];
  LOCK();
  const int to = next_hop(dst, -1);
  int slot = -1;
  uint32_t in_flight = 0;
  for (int i = 0; i < CONFIG_SYS_SESSION_CALLS; i++) {
    if (!s_session.calls[i].used) {
      slot = slot < 0 ? i : slot;
    } else if (s_session.calls[i].dst == dst) {
      in_flight++;
    }
  }
  const bool busy = in_flight >= CONFIG_SYS_SESSION_PEER_IN_FLIGHT;
  uint16_t seq = 0;
  if (to >= 0 && slot >= 0 && !busy) {
    const session_t* s = &s_session.sessions[to];
    const bool wired = s->peer_id == dst && (sys_data_provider_state(s->provider) & SYS_DATA_PROVIDER_STATE_WIRED);
    uint32_t ms = timeout_ms != 0 ? timeout_ms : (wired ? CONFIG_SYS_SESSION_CALL_TIMEOUT_WIRED_MS
                                                         : CONFIG_SYS_SESSION_CALL_TIMEOUT_MS);
    ms = ms < CALL_MIN_MS ? CALL_MIN_MS : ms;
    seq = ++s_session.call_seq;
    call_t* c = &s_session.calls[slot];
    *c = (call_t){.used = true, .dst = dst, .via = (uint8_t)to, .seq = seq, .cb = cb, .ctx = ctx,
                  .deadline_us = now_us() + (int64_t)ms * 1000};
    c->req_len = (uint8_t)(req.len < sizeof(c->req) ? req.len : sizeof(c->req));
    memcpy(c->req, req.data, c->req_len);
  }
  UNLOCK();
  ERR_CHECK(to >= 0, ERR_SESSION_NO_ROUTE, .dst = dst);
  ERR_CHECK(!busy, ERR_SESSION_BUSY, .dst = dst);
  ERR_CHECK(slot >= 0, ERR_SESSION_CALLS_FULL);
  const uint32_t len = call_frame(buf, dst, seq, flags, req);
  err_h err = send_raw(to, buf, len, send_flags(flags));
  if (err != NULL) {
    LOCK();
    if (s_session.calls[slot].used && s_session.calls[slot].seq == seq) {
      s_session.calls[slot].used = false;
    }
    UNLOCK();
  }
  return err;
}

typedef struct {
  TaskHandle_t task;
  uint8_t* buf;
  uint32_t cap;
  uint32_t* len;
  err_h err;
} waiter_t;

// The answer of a waited call: into the caller's buffer, then the caller wakes.
static err_h wait_done(void* ctx, const err_cmd_t* cmd, uint8_t status, uint8_t* data, uint32_t len) {
  (void)cmd;
  waiter_t* w = ctx;
  if (status == ROUTER_STATUS_OK) {
    if (len > w->cap) {
      w->err = ERR_NEW(ERR_CMD_BAD_LEN, .got = (uint16_t)len, .need = (uint16_t)w->cap);
    } else {
      memcpy(w->buf, data, len);
      *w->len = len;
    }
  } else {
    err_h chain = NULL;
    err_free(err_record_parse(data, len, &chain));
    w->err = chain != NULL ? chain : ERR_NEW(ERR_CMD_BAD_ARG);
  }
  const TaskHandle_t task = w->task;
  xTaskNotifyGive(task);  // `w` is the caller's again from here
  return NULL;
}

err_h sys_session_call_wait(uint8_t dst, cmd_in_t req, uint8_t flags, uint32_t timeout_ms, void* buf, uint32_t cap,
                            uint32_t* len) {
  const bool here = dst == s_session.board_id || dst == SYS_SESSION_ID_SELF;
  ERR_CHECK(dst != SYS_SESSION_ID_BROADCAST && buf != NULL && len != NULL, ERR_CMD_BAD_ARG);
  ERR_CHECK(!no_wait_task() && !(here && router_is_task()), ERR_SESSION_WOULD_BLOCK);
  waiter_t w = {.task = xTaskGetCurrentTaskHandle(), .buf = buf, .cap = cap, .len = len};
  (void)ulTaskNotifyTake(pdTRUE, 0);  // a stale notification must not end the wait at once
  ERR_TRY(sys_session_call(dst, req, flags, timeout_ms, wait_done, &w));
  (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // the call always ends: an answer, its timeout, its session
  return w.err;
}

void sys_session_no_wait(void) {
  note_no_wait();
}

bool sys_session_wired(uint8_t peer) {
  LOCK();
  const int at = find_peer(peer);
  const sys_data_provider_id_t provider = at >= 0 ? s_session.sessions[at].provider : (sys_data_provider_id_t){0};
  UNLOCK();
  return at >= 0 && (sys_data_provider_state(provider) & SYS_DATA_PROVIDER_STATE_WIRED);
}

err_h sys_session_stream(uint8_t peer, uint16_t seq, const uint8_t* data, uint32_t len, uint8_t flags) {
  ERR_CHECK(data != NULL && len > 0 && len <= CMD_DATA_MAX, ERR_CMD_BAD_ARG);
  LOCK();
  const int at = next_hop(peer, -1);
  UNLOCK();
  ERR_CHECK(at >= 0, ERR_SESSION_NO_ROUTE, .dst = peer);
  uint8_t buf[CMD_FRAME_MAX];
  const sys_session_hdr_t hdr = {
      .frame = (uint8_t)(FRAME(SYS_SESSION_STREAM) | (flags & SYS_SESSION_FRAME_FLAGS)),
      .dst = peer,
      .src = s_session.board_id,
      .seq = seq,
  };
  return send_frame(at, &hdr, buf, data, len, send_flags(flags));
}

bool sys_session_below(uint8_t peer) {
  bool below = false;
  LOCK();
  for (int i = 0; i < CONFIG_SYS_SESSION_MAX; i++) {
    const session_t* s = &s_session.sessions[i];
    below |= s->used && s->open && s->peer != SYS_SESSION_PARENT && (s->peer_id == peer || in_tree(s->subtree, peer));
  }
  UNLOCK();
  return below;
}

// The body of an on_* call: `fn` into the first free slot of a listener table (again: one slot).
#define ADD_LISTENER(table, fn)                     \
  ERR_CHECK((fn) != NULL, ERR_CMD_BAD_ARG);         \
  for (uint32_t i = 0; i < ARRAY_LEN(table); i++) { \
    if ((table)[i] == NULL || (table)[i] == (fn)) { \
      (table)[i] = (fn);                            \
      return NULL;                                  \
    }                                               \
  }                                                 \
  return ERR_NEW(ERR_SESSION_FULL)

err_h sys_session_on_gone(sys_session_gone_f fn) {
  ADD_LISTENER(s_session.gone, fn);
}

err_h sys_session_on_reached(sys_session_reached_f fn) {
  ADD_LISTENER(s_session.reached, fn);
}

err_h sys_session_on_stream(sys_session_stream_f fn) {
  ADD_LISTENER(s_session.stream, fn);
}

/* ----------------------------------------------------------------------------
 * The router's
 * ------------------------------------------------------------------------- */

uint8_t sys_session_list(sys_session_info_t out[CONFIG_SYS_SESSION_MAX]) {
  uint8_t count = 0;
  LOCK();
  for (int i = 0; i < CONFIG_SYS_SESSION_MAX; i++) {
    const session_t* s = &s_session.sessions[i];
    if (s->used) {
      out[count++] = (sys_session_info_t){
          .provider = s->provider,
          .peer = s->peer,
          .access = s->access,
          .open = s->open,
          .peer_id = s->peer_id,
          .timeout_ms = (uint16_t)timeout_of(s->peer),
          .subtree = s->subtree_count,
      };
    }
  }
  UNLOCK();
  return count;
}
