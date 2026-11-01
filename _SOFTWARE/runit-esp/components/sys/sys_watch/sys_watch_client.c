#include <string.h>

#include "esp_timer.h"
#include "router.h"
#include "sdkconfig.h"
#include "sys_data_connector.h"
#include "sys_session.h"
#include "sys_watch.h"
#include "internal/sys_watch_internal.h"
#include "sys_watch_router.h"
#include "utils.h"

/* The client of the watches this board added on other boards: the board's own (sys_watch_remote_add) and the ones a
   cached read makes (sys_watch_read). One table holds both; a cached read's slot also remembers a read that has no
   watch yet. */

#define IDLE_TICK_MS 100u
#define ERROR_MAX    96u
#define ADD_MAX      (3u + sizeof(sys_watch_head_t) + SYS_WATCH_READ_MAX)

typedef struct {
  bool used;
  bool cached;  // made by a read (else by sys_watch_remote_add)
  bool added;   // an ADD went out since the board was last lost, and was not refused
  bool active;  // an entry came: `copy` holds a value
  bool old;     // a write made the copy old: a read after it is on its way
  bool have_seq;
  uint8_t board;
  uint8_t id;  // the watch's id on that board
  uint8_t req_len;
  uint8_t copy_len;
  uint8_t reads;  // saturating: whether the read was made before
  uint16_t gen;   // changes when the slot is taken again: an answer for the slot before it is dropped
  uint16_t seq;
  uint32_t max_age_ms;    // the shortest this read was made with
  uint32_t added_age_ms;  // the max age the watch was added for
  uint32_t last_read_ms;
  uint32_t prev_read_ms;
  uint32_t copy_ms;
  uint32_t retry_ms;  // no ADD before this
  sys_watch_head_t head;
  sys_watch_entry_f cb;
  void* ctx;
  uint8_t req[SYS_WATCH_READ_MAX];
  uint8_t copy[SYS_WATCH_COPY_MAX];
} slot_t;

static struct {
  slot_t slots[CONFIG_SYS_WATCH_CLIENT_MAX];
  uint8_t count;
  esp_timer_handle_t timer;
} s_cl;

U_MUTEX_DEFINE(s_client_lock);
#define LOCK()   U_MUTEX_LOCK(s_client_lock, portMAX_DELAY)
#define UNLOCK() U_MUTEX_UNLOCK(s_client_lock)

#define CTX(slot, gen) ((void*)(uintptr_t)(((uint32_t)(gen) << 8) | (slot)))
#define CTX_SLOT(ctx)  ((uint8_t)((uintptr_t)(ctx) & 0xFFu))
#define CTX_GEN(ctx)   ((uint16_t)((uintptr_t)(ctx) >> 8))

static uint32_t now_ms(void) {
  return (uint32_t)(esp_timer_get_time() / 1000);
}

static uint32_t at_least(uint32_t v, uint32_t floor) {
  return v < floor ? floor : v;
}

static int find_locked(uint8_t board, uint8_t id) {
  for (uint8_t i = 0; i < CONFIG_SYS_WATCH_CLIENT_MAX; i++) {
    if (s_cl.slots[i].used && s_cl.slots[i].board == board && s_cl.slots[i].id == id) {
      return i;
    }
  }
  return -1;
}

static int find_read_locked(uint8_t board, cmd_in_t read) {
  for (uint8_t i = 0; i < CONFIG_SYS_WATCH_CLIENT_MAX; i++) {
    const slot_t* s = &s_cl.slots[i];
    if (s->used && s->cached && s->board == board && s->req_len == read.len && memcmp(s->req, read.data, read.len) == 0) {
      return i;
    }
  }
  return -1;
}

/* A free slot with an id free on `board`; -1 when the table is full. */
static int take_locked(uint8_t board, bool cached) {
  int at = -1;
  for (uint8_t i = 0; i < CONFIG_SYS_WATCH_CLIENT_MAX && at < 0; i++) {
    if (!s_cl.slots[i].used) {
      at = i;
    }
  }
  uint8_t id = 1;
  while (id != 0 && find_locked(board, id) >= 0) {
    id++;
  }
  if (at < 0 || id == 0) {
    return -1;
  }
  slot_t* s = &s_cl.slots[at];
  *s = (slot_t){.used = true, .cached = cached, .board = board, .id = id, .gen = (uint16_t)(s->gen + 1u)};
  if (s_cl.count++ == 0) {
    (void)esp_timer_start_periodic(s_cl.timer, (uint64_t)IDLE_TICK_MS * 1000u);
  }
  return at;
}

static void free_locked(uint8_t i) {
  s_cl.slots[i].used = false;
  s_cl.slots[i].gen++;
  if (--s_cl.count == 0) {
    (void)esp_timer_stop(s_cl.timer);
  }
}

/* ----------------------------------------------------------------------------
 * Calls to the other board
 * ------------------------------------------------------------------------- */

// The answer of an ADD, on the RX task: refused, the watch is not there; the owner is told.
static err_h on_add_done(void* ctx, const err_cmd_t* cmd, uint8_t status, uint8_t* data, uint32_t len) {
  (void)cmd;
  sys_watch_entry_f cb = NULL;
  void* cb_ctx = NULL;
  uint8_t board = 0;
  LOCK();
  slot_t* s = &s_cl.slots[CTX_SLOT(ctx)];
  if (s->used && s->gen == CTX_GEN(ctx) && status != ROUTER_STATUS_OK) {
    s->added = false;
    s->active = false;
    s->retry_ms = now_ms() + at_least(s->added_age_ms, SYS_WATCH_TICK_MS);
    cb = s->cb;
    cb_ctx = s->ctx;
    board = s->board;
  }
  UNLOCK();
  if (cb != NULL) {
    cb(cb_ctx, board, ROUTER_STATUS_ERROR, data, len, 0);
  }
  return NULL;
}

static err_h on_ignore(void* ctx, const err_cmd_t* cmd, uint8_t status, uint8_t* data, uint32_t len) {
  (void)ctx;
  (void)cmd;
  (void)status;
  (void)data;
  (void)len;
  return NULL;
}

/* Sends ADD for slot `i` (copied out under the lock by the caller). A send that fails is a way not there yet: the slot
   stays not added, and the next read, or the board being reached, tries again. */
static void send_add(uint8_t i, uint16_t gen, uint8_t board, const sys_watch_head_t* head, const uint8_t* read,
                     uint8_t read_len) {
  uint8_t req[ADD_MAX];
  req[0] = ROUTER_KEY_WATCH;
  req[1] = CMD_DIR_WRITE;
  req[2] = SYS_WATCH_OP_ADD;
  memcpy(req + 3, head, sizeof(*head));
  memcpy(req + 3 + sizeof(*head), read, read_len);
  err_h err = sys_session_call(board, (cmd_in_t){req, 3u + sizeof(*head) + read_len}, 0, 0, on_add_done, CTX(i, gen));
  if (err != NULL) {
    err_free(err);
    LOCK();
    slot_t* s = &s_cl.slots[i];
    if (s->used && s->gen == gen) {
      s->added = false;
      s->retry_ms = now_ms() + at_least(s->added_age_ms, SYS_WATCH_TICK_MS);
    }
    UNLOCK();
  }
}

static void send_remove(uint8_t board, uint8_t id) {
  const uint8_t req[] = {ROUTER_KEY_WATCH, CMD_DIR_WRITE, SYS_WATCH_OP_REMOVE, id};
  err_free(sys_session_call(board, (cmd_in_t){req, sizeof(req)}, 0, 0, on_ignore, NULL));
}

// The ADD of a cached read's watch: ON_CHANGE every half of its max age, sent anyway after the max age.
static sys_watch_head_t cached_head(uint8_t id, uint32_t max_age_ms) {
  const uint32_t period = at_least(max_age_ms / 2 / SYS_WATCH_TICK_MS, 1);
  const uint32_t silence = at_least(max_age_ms / SYS_WATCH_TICK_MS, period);
  return (sys_watch_head_t){
      .id = id,
      .connector = SYS_WATCH_TO_OWNER,
      .trigger = SYS_WATCH_ON_CHANGE,
      .period = (uint16_t)(period > UINT16_MAX ? UINT16_MAX : period),
      .max_silence = (uint16_t)(silence > UINT16_MAX ? UINT16_MAX : silence),
      .filter = {SYS_EVENT_ANY_SOURCE, SYS_EVENT_ANY_EVENT, SYS_EVENT_ANY_CHANNEL},
  };
}

/* ----------------------------------------------------------------------------
 * Entries
 * ------------------------------------------------------------------------- */

// A STREAM frame [WATCHES][owner][id][status][data] from a board.
static void on_stream(uint8_t src, uint16_t seq, const uint8_t* data, uint32_t len) {
  if (len < 4 || data[0] != SYS_DATA_CONNECTOR_WATCHES) {
    return;
  }
  const uint8_t id = data[2];
  const uint8_t status = data[3];
  const uint8_t* payload = data + 4;
  const uint32_t plen = len - 4;
  sys_watch_entry_f cb = NULL;
  void* cb_ctx = NULL;
  uint16_t lost = 0;
  LOCK();
  const int at = find_locked(src, id);
  if (at >= 0) {
    slot_t* s = &s_cl.slots[at];
    lost = s->have_seq ? (uint16_t)(seq - s->seq - 1u) : 0;
    s->seq = seq;
    s->have_seq = true;
    if (s->cached) {
      if (!s->old) {  // before the read that follows a write, an entry may be older than the write
        s->active = status == ROUTER_STATUS_OK && plen <= SYS_WATCH_COPY_MAX;
        if (s->active) {
          memcpy(s->copy, payload, plen);
          s->copy_len = (uint8_t)plen;
          s->copy_ms = now_ms();
        }
      }
    } else {
      cb = s->cb;
      cb_ctx = s->ctx;
    }
  }
  UNLOCK();
  if (cb != NULL) {
    cb(cb_ctx, src, status, payload, plen, lost);
  }
}

// The answer of the read a write asks for: the copy is new again.
static err_h on_refresh(void* ctx, const err_cmd_t* cmd, uint8_t status, uint8_t* data, uint32_t len) {
  (void)cmd;
  LOCK();
  slot_t* s = &s_cl.slots[CTX_SLOT(ctx)];
  if (s->used && s->gen == CTX_GEN(ctx)) {
    s->old = false;
    s->active = status == ROUTER_STATUS_OK && len <= SYS_WATCH_COPY_MAX;
    if (s->active) {
      memcpy(s->copy, data, len);
      s->copy_len = (uint8_t)len;
      s->copy_ms = now_ms();
    }
  }
  UNLOCK();
  return NULL;
}

/* A board's watches are gone with its session: a cached read's slot asks again at its next read, the owner's watch is
   sent again when the board is reached. The owners are told. */
static void on_gone(uint8_t peer, uint8_t via) {
  const bool outside = peer == SYS_SESSION_ID_BROADCAST && via == SYS_SESSION_PARENT;
  for (uint8_t i = 0; i < CONFIG_SYS_WATCH_CLIENT_MAX; i++) {
    sys_watch_entry_f cb = NULL;
    void* cb_ctx = NULL;
    uint8_t board = 0;
    LOCK();
    slot_t* s = &s_cl.slots[i];
    if (s->used && (s->board == peer || (outside && !sys_session_below(s->board)))) {
      s->added = false;
      s->active = false;
      s->old = false;
      s->have_seq = false;
      s->retry_ms = 0;
      cb = s->cb;
      cb_ctx = s->ctx;
      board = s->board;
    }
    UNLOCK();
    if (cb != NULL) {
      uint8_t buf[ERROR_MAX];
      err_h err = ERR_NEW(ERR_SESSION_PEER_DOWN, .board = board);
      const uint32_t len = err_record(NULL, err, buf, sizeof(buf));
      err_free(err);
      cb(cb_ctx, board, ROUTER_STATUS_ERROR, buf, len, 0);
    }
  }
}

static void on_reached(uint8_t peer) {
  for (uint8_t i = 0; i < CONFIG_SYS_WATCH_CLIENT_MAX; i++) {
    sys_watch_head_t head;
    uint8_t read[SYS_WATCH_READ_MAX];
    uint8_t read_len = 0;
    uint16_t gen = 0;
    bool send = false;
    LOCK();
    slot_t* s = &s_cl.slots[i];
    if (s->used && !s->cached && s->board == peer && !s->added) {
      s->added = true;
      head = s->head;
      read_len = s->req_len;
      gen = s->gen;
      memcpy(read, s->req, read_len);
      send = true;
    }
    UNLOCK();
    if (send) {
      send_add(i, gen, peer, &head, read, read_len);
    }
  }
}

// Every IDLE_TICK_MS: a cached read nobody reads any more ends, its watch with it.
static void on_idle_tick(void* arg) {
  (void)arg;
  const uint32_t now = now_ms();
  for (uint8_t i = 0; i < CONFIG_SYS_WATCH_CLIENT_MAX; i++) {
    bool remove = false;
    uint8_t board = 0;
    uint8_t id = 0;
    LOCK();
    slot_t* s = &s_cl.slots[i];
    if (s->used && s->cached &&
        now - s->last_read_ms > at_least(10u * s->max_age_ms, CONFIG_SYS_WATCH_IDLE_MIN_MS)) {
      remove = s->added;
      board = s->board;
      id = s->id;
      free_locked(i);
    }
    UNLOCK();
    if (remove) {
      send_remove(board, id);
    }
  }
}

/* ----------------------------------------------------------------------------
 * Use
 * ------------------------------------------------------------------------- */

err_h sys_watch_remote_add(uint8_t board, const sys_watch_head_t* head, cmd_in_t read, sys_watch_entry_f cb, void* ctx,
                           uint8_t* id) {
  ERR_CHECK(cb != NULL && head->period != 0 && head->trigger <= SYS_WATCH_ON_EVENT &&
                head->connector == SYS_WATCH_TO_OWNER && read.len <= SYS_WATCH_READ_MAX,
            ERR_WATCH_BAD_ARG, .id = head->id);
  ERR_CHECK(read.len >= 2 && read.data[1] == CMD_DIR_READ, ERR_WATCH_NOT_READ, .id = head->id);
  LOCK();
  const int at = take_locked(board, false);
  if (at < 0) {
    UNLOCK();
    return ERR_NEW(ERR_WATCH_FULL);
  }
  slot_t* s = &s_cl.slots[at];
  s->head = *head;
  s->head.id = s->id;
  s->req_len = (uint8_t)read.len;
  memcpy(s->req, read.data, read.len);
  s->cb = cb;
  s->ctx = ctx;
  s->added = true;
  s->added_age_ms = SYS_WATCH_TICK_MS;
  const sys_watch_head_t sent = s->head;
  const uint16_t gen = s->gen;
  *id = s->id;
  UNLOCK();
  send_add((uint8_t)at, gen, board, &sent, read.data, (uint8_t)read.len);
  return NULL;
}

err_h sys_watch_remote_remove(uint8_t board, uint8_t id) {
  bool added = false;
  LOCK();
  const int at = find_locked(board, id);
  if (at >= 0 && !s_cl.slots[at].cached) {
    added = s_cl.slots[at].added;
    free_locked((uint8_t)at);
  } else {
    UNLOCK();
    return ERR_NEW(ERR_WATCH_NOT_FOUND, .id = id);
  }
  UNLOCK();
  if (added) {
    send_remove(board, id);
  }
  return NULL;
}

err_h sys_watch_read(uint8_t board, cmd_in_t read, uint32_t max_age_ms, bool wait, void* buf, uint32_t cap,
                     uint32_t* len) {
  ERR_CHECK(read.len >= 2 && read.len <= SYS_WATCH_READ_MAX && read.data[1] == CMD_DIR_READ, ERR_WATCH_NOT_READ, .id = 0);
  *len = 0;
  const uint32_t now = now_ms();
  bool fresh = false;
  bool have_copy = false;
  uint32_t age = 0;
  bool add = false;
  uint8_t slot = 0;
  uint16_t gen = 0;
  sys_watch_head_t head = {0};
  err_h err = NULL;
  if (max_age_ms != 0) {
    LOCK();
    int at = find_read_locked(board, read);
    if (at < 0) {
      at = take_locked(board, true);
      if (at >= 0) {
        s_cl.slots[at].req_len = (uint8_t)read.len;
        memcpy(s_cl.slots[at].req, read.data, read.len);
      }
    }
    if (at >= 0) {
      slot_t* s = &s_cl.slots[at];
      const bool before = s->reads > 0 && now - s->last_read_ms <= 2u * max_age_ms;
      if (s->reads < UINT8_MAX) {
        s->reads++;
      }
      s->last_read_ms = now;
      if (s->max_age_ms == 0 || max_age_ms < s->max_age_ms) {
        s->max_age_ms = max_age_ms;
      }
      if (s->active && !s->old) {
        have_copy = true;
        age = now - s->copy_ms;
        fresh = age <= max_age_ms;
        if (fresh) {
          if (s->copy_len > cap) {
            err = ERR_NEW(ERR_CMD_BAD_LEN, .got = s->copy_len, .need = (uint16_t)cap);
          } else {
            memcpy(buf, s->copy, s->copy_len);
            *len = s->copy_len;
          }
        }
      }
      // A watch when the reads are about as often as its entries would be; again when a shorter age asks for more.
      const bool wanted = s->added ? max_age_ms < s->added_age_ms : (wait ? before : true);
      if (wanted && now >= s->retry_ms) {
        s->added = true;
        s->added_age_ms = max_age_ms;
        s->have_seq = false;
        head = cached_head(s->id, max_age_ms);
        slot = (uint8_t)at;
        gen = s->gen;
        add = true;
      }
    }
    UNLOCK();
  }
  if (add) {
    send_add(slot, gen, board, &head, read.data, (uint8_t)read.len);
  }
  if (fresh) {
    return err;
  }
  if (wait) {
    return sys_session_call_wait(board, read, 0, 0, buf, cap, len);
  }
  return have_copy ? ERR_NEW(ERR_WATCH_STALE, .board = board, .age_ms = (uint16_t)(age > UINT16_MAX ? UINT16_MAX : age))
                   : ERR_NEW(ERR_WATCH_NO_VALUE, .board = board);
}

void sys_watch_stale(uint8_t board, cmd_in_t write, uint32_t target_len) {
  if (write.len < 2 + target_len) {
    return;
  }
  for (uint8_t i = 0; i < CONFIG_SYS_WATCH_CLIENT_MAX; i++) {
    uint8_t req[SYS_WATCH_READ_MAX];
    uint8_t req_len = 0;
    uint16_t gen = 0;
    LOCK();
    slot_t* s = &s_cl.slots[i];
    const bool same = s->used && s->cached && s->active && s->board == board && s->req[0] == write.data[0] &&
                      s->req_len >= 2 + target_len && memcmp(s->req + 2, write.data + 2, target_len) == 0;
    if (same) {
      s->old = true;
      req_len = s->req_len;
      gen = s->gen;
      memcpy(req, s->req, req_len);
    }
    UNLOCK();
    if (!same) {
      continue;
    }
    err_h err = sys_session_call(board, (cmd_in_t){req, req_len}, 0, 0, on_refresh, CTX(i, gen));
    if (err != NULL) {  // no read follows: the copy is dropped, the next entry makes it again
      err_free(err);
      LOCK();
      if (s_cl.slots[i].used && s_cl.slots[i].gen == gen) {
        s_cl.slots[i].old = false;
        s_cl.slots[i].active = false;
      }
      UNLOCK();
    }
  }
}

err_h sys_watch_client_init(void) {
  const esp_timer_create_args_t args = {.callback = on_idle_tick, .name = "sys_watch_cl"};
  ERR_TRY(ERR_ESP(esp_timer_create(&args, &s_cl.timer)));
  ERR_TRY(sys_session_on_gone(on_gone));
  ERR_TRY(sys_session_on_reached(on_reached));
  return sys_session_on_stream(on_stream);
}
