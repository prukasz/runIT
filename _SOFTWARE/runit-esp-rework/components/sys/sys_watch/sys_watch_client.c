#include "sys_watch.h"

#include <string.h>

#include "board_time.h"
#include "board_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "router_queue.h"
#include "sdkconfig.h"
#include "sys_session.h"
#include "sys_watch_internal.h"
#include "sys_watch_router.h"

#define CLIENT_TICK_MS 100u
#define ERR_REC_MAX    128u
#define PERIOD_MS      ((uint32_t)CONFIG_SYS_WATCH_READ_PERIOD_MS)
#define WATCH_TICKS    ((PERIOD_MS + SYS_WATCH_TICK_MS - 1u) / SYS_WATCH_TICK_MS)
#define SILENCE_TICKS  (2u * WATCH_TICKS)
#define MAX_AGE_MS     (3u * PERIOD_MS)

typedef enum {
  SLOT_FREE = 0,
  SLOT_ADDING,  // ADD sent (or awaiting retry on next read); entries ignored here (B3)
  SLOT_ACTIVE,  // ADD answered OK; PUSH entries on 0x80 accepted here
  SLOT_OLD,     // write invalidated copy (sys_watch_stale); refresh read in flight
} slot_state_e;

typedef struct {
  slot_state_e state;
  bool add_in_flight;
  bool have_copy;
  bool has_err;
  uint8_t board;
  uint8_t req_len;
  uint8_t copy_len;
  uint8_t err_len;
  uint16_t gen;
  uint64_t copy_ms;
  uint64_t last_read_ms;
  uint64_t next_try_ms;
  uint8_t req[SYS_WATCH_READ_MAX];
  uint8_t copy[SYS_WATCH_COPY_MAX];
  uint8_t err_rec[ERR_REC_MAX];
} slot_t;

static struct {
  slot_t slots[CONFIG_SYS_WATCH_CLIENT_MAX];
  uint8_t count;
  board_timer_t timer;
} s_cl;

static StaticSemaphore_t s_cl_lock_buf;
static SemaphoreHandle_t s_cl_lock;

__attribute__((constructor)) static void cl_lock_init(void) {
  if (s_cl_lock == NULL) {
    s_cl_lock = xSemaphoreCreateMutexStatic(&s_cl_lock_buf);
  }
}

static void lock(void) {
  cl_lock_init();
  xSemaphoreTake(s_cl_lock, portMAX_DELAY);
}

static void unlock(void) {
  xSemaphoreGive(s_cl_lock);
}

static void* pack_ctx(uint8_t slot, uint16_t gen) {
  return (void*)((uintptr_t)slot | ((uintptr_t)gen << 8));
}

static void unpack_ctx(void* ctx, uint8_t* slot, uint16_t* gen) {
  const uintptr_t v = (uintptr_t)ctx;
  *slot = (uint8_t)(v & 0xFFu);
  *gen = (uint16_t)((v >> 8) & 0xFFFFu);
}

static uint64_t idle_limit_ms(void) {
  const uint64_t scaled = 10ull * (uint64_t)MAX_AGE_MS;
  return scaled > (uint64_t)CONFIG_SYS_WATCH_IDLE_MIN_MS ? scaled : (uint64_t)CONFIG_SYS_WATCH_IDLE_MIN_MS;
}

static err_h cl_on_noop(void* ctx, const cmd_req_t* cmd, uint8_t status, const uint8_t* data, uint32_t len) {
  (void)ctx;
  (void)cmd;
  (void)status;
  (void)data;
  (void)len;
  return NULL;
}

static err_h cl_on_add_done(void* ctx, const cmd_req_t* cmd, uint8_t status, const uint8_t* data, uint32_t len) {
  (void)cmd;
  (void)data;
  (void)len;
  uint8_t slot = 0;
  uint16_t gen = 0;
  unpack_ctx(ctx, &slot, &gen);
  if (slot >= CONFIG_SYS_WATCH_CLIENT_MAX) {
    return NULL;
  }

  lock();
  slot_t* s = &s_cl.slots[slot];
  if (s->state == SLOT_ADDING && s->gen == gen) {
    s->add_in_flight = false;
    if (status == ROUTER_STATUS_OK) {
      s->state = SLOT_ACTIVE;
      s->have_copy = false;
      s->has_err = false;
    } else {
      s->next_try_ms = board_time_uptime_ms() + PERIOD_MS;
    }
  }
  unlock();
  return NULL;
}

static err_h cl_on_refresh(void* ctx, const cmd_req_t* cmd, uint8_t status, const uint8_t* data, uint32_t len) {
  (void)cmd;
  uint8_t slot = 0;
  uint16_t gen = 0;
  unpack_ctx(ctx, &slot, &gen);
  if (slot >= CONFIG_SYS_WATCH_CLIENT_MAX) {
    return NULL;
  }

  lock();
  slot_t* s = &s_cl.slots[slot];
  if (s->state == SLOT_OLD && s->gen == gen) {
    if (status == ROUTER_STATUS_OK && len <= SYS_WATCH_COPY_MAX) {
      if (len > 0u) {
        memcpy(s->copy, data, len);
      }
      s->copy_len = (uint8_t)len;
      s->copy_ms = board_time_uptime_ms();
      s->have_copy = true;
      s->has_err = false;
    } else {
      s->have_copy = false;
      s->has_err = false;
    }
    s->state = SLOT_ACTIVE;
  }
  unlock();
  return NULL;
}

static void send_add(uint8_t slot, uint16_t gen, uint8_t board, const uint8_t* read_req, uint8_t read_len) {
  const sys_watch_head_t head = {
      .id = (uint8_t)(slot + 1u),
      .trigger = SYS_WATCH_ON_CHANGE,
      .period = (uint16_t)(WATCH_TICKS > UINT16_MAX ? UINT16_MAX : WATCH_TICKS),
      .max_silence = (uint16_t)(SILENCE_TICKS > UINT16_MAX ? UINT16_MAX : SILENCE_TICKS),
      .filter = {0},
  };
  uint8_t buf[3u + sizeof(head) + SYS_WATCH_READ_MAX];
  buf[0] = MODULE_ID_WATCH;
  buf[1] = CMD_DIR_WRITE;
  buf[2] = WATCH_OP_ADD;
  memcpy(buf + 3u, &head, sizeof(head));
  memcpy(buf + 3u + sizeof(head), read_req, read_len);

  err_h err = sys_session_call(board, (cmd_in_t){buf, (uint32_t)(3u + sizeof(head) + read_len)}, 0, 0, cl_on_add_done,
                               pack_ctx(slot, gen));
  if (err != NULL) {
    err_free(err);
    lock();
    slot_t* s = &s_cl.slots[slot];
    if (s->state == SLOT_ADDING && s->gen == gen) {
      s->add_in_flight = false;
      s->next_try_ms = board_time_uptime_ms() + PERIOD_MS;
    }
    unlock();
  }
}

static void send_remove(uint8_t board, uint8_t id) {
  const uint8_t req[] = {MODULE_ID_WATCH, CMD_DIR_WRITE, WATCH_OP_REMOVE, id};
  err_h err = sys_session_call(board, (cmd_in_t){req, sizeof(req)}, 0, 0, cl_on_noop, NULL);
  if (err != NULL) {
    err_free(err);
  }
}

static void cl_on_push(uint8_t src, uint16_t lost, const uint8_t* data, uint32_t len) {
  (void)lost;
  if (len < 3u || data[0] != SYS_SESSION_CHANNEL_WATCHES) {
    return;
  }
  const uint8_t id = data[1];
  const uint8_t status = data[2];
  if (id == 0u || id > CONFIG_SYS_WATCH_CLIENT_MAX) {
    return;
  }
  const uint8_t* payload = data + 3u;
  const uint32_t plen = len - 3u;
  slot_t* s = &s_cl.slots[id - 1u];

  lock();
  /* B3: accept entries only once ADD succeeded (SLOT_ACTIVE) and never while OLD awaits post-write refresh. */
  if (s->state == SLOT_ACTIVE && s->board == src) {
    if (status == ROUTER_STATUS_OK) {
      if (plen <= SYS_WATCH_COPY_MAX) {
        if (plen > 0u) {
          memcpy(s->copy, payload, plen);
        }
        s->copy_len = (uint8_t)plen;
        s->copy_ms = board_time_uptime_ms();
        s->have_copy = true;
        s->has_err = false;
      } else {
        s->have_copy = false;
        s->has_err = false;
      }
    } else {
      const uint8_t keep = (uint8_t)(plen <= ERR_REC_MAX ? plen : ERR_REC_MAX);
      if (keep > 0u) {
        memcpy(s->err_rec, payload, keep);
      }
      s->err_len = keep;
      s->copy_ms = board_time_uptime_ms();
      s->have_copy = false;
      s->has_err = true;
    }
  }
  unlock();
}

static void cl_on_gone(uint8_t peer, uint8_t role) {
  (void)role;
  lock();
  for (uint8_t i = 0; i < CONFIG_SYS_WATCH_CLIENT_MAX; i++) {
    slot_t* s = &s_cl.slots[i];
    if (s->state != SLOT_FREE && (peer == SYS_SESSION_ID_BROADCAST || s->board == peer)) {
      s->state = SLOT_ADDING;
      s->add_in_flight = false;
      s->have_copy = false;
      s->has_err = false;
      s->copy_len = 0;
      s->err_len = 0;
      s->next_try_ms = 0;
      s->gen++;
    }
  }
  unlock();
}

static void cl_on_idle_tick(void* ctx) {
  (void)ctx;
  const uint64_t now = board_time_uptime_ms();
  const uint64_t limit = idle_limit_ms();

  for (uint8_t i = 0; i < CONFIG_SYS_WATCH_CLIENT_MAX; i++) {
    bool send_rem = false;
    uint8_t board = 0;

    lock();
    slot_t* s = &s_cl.slots[i];
    if (s->state != SLOT_FREE && now >= s->last_read_ms && (now - s->last_read_ms) > limit) {
      send_rem = s->state == SLOT_ACTIVE || s->state == SLOT_OLD || s->add_in_flight;
      board = s->board;
      s->state = SLOT_FREE;
      s->add_in_flight = false;
      s->have_copy = false;
      s->has_err = false;
      s->gen++;
      s_cl.count--;
    }
    unlock();

    if (send_rem) {
      send_remove(board, (uint8_t)(i + 1u));
    }
  }

  lock();
  const bool empty = s_cl.count == 0u;
  unlock();
  if (empty) {
    board_timer_stop(&s_cl.timer);
  }
}

static bool valid_peer(uint8_t board) {
  return board != SYS_SESSION_ID_NONE && board != SYS_SESSION_ID_SELF && board != SYS_SESSION_ID_BROADCAST &&
         board != sys_session_get_board_id();
}

err_h sys_watch_read(uint8_t board, cmd_in_t read, void* buf, uint32_t cap, uint32_t* len) {
  ERR_CHECK(valid_peer(board) && read.data != NULL && read.len >= 2u && read.len <= SYS_WATCH_READ_MAX &&
                len != NULL && (cap == 0u || buf != NULL),
            ERR_WATCH_BAD_ARG, .id = 0);
  ERR_CHECK(read.data[1] == CMD_DIR_READ, ERR_WATCH_NOT_READ, .id = 0);

  const uint64_t now = board_time_uptime_ms();
  bool send_now = false;
  bool start_timer = false;
  uint8_t slot_idx = 0;
  uint16_t gen = 0;

  lock();
  int32_t at = -1;
  int32_t free_at = -1;
  for (int32_t i = 0; i < CONFIG_SYS_WATCH_CLIENT_MAX; i++) {
    const slot_t* s = &s_cl.slots[i];
    if (s->state == SLOT_FREE) {
      if (free_at < 0) {
        free_at = i;
      }
    } else if (s->board == board && s->req_len == read.len && memcmp(s->req, read.data, read.len) == 0) {
      at = i;
      break;
    }
  }

  if (at < 0) {
    if (free_at < 0) {
      unlock();
      return ERR_NEW(ERR_WATCH_FULL);
    }
    slot_t* s = &s_cl.slots[free_at];
    start_timer = s_cl.count == 0u;
    gen = (uint16_t)(s->gen + 1u);
    memset(s, 0, sizeof(*s));
    s->state = SLOT_ADDING;
    s->add_in_flight = true;
    s->board = board;
    s->req_len = (uint8_t)read.len;
    s->gen = gen;
    s->last_read_ms = now;
    memcpy(s->req, read.data, read.len);
    s_cl.count++;
    slot_idx = (uint8_t)free_at;
    send_now = true;
    unlock();

    if (start_timer) {
      board_timer_start(&s_cl.timer, CLIENT_TICK_MS);
    }
    send_add(slot_idx, gen, board, read.data, (uint8_t)read.len);
    return ERR_NEW(ERR_WATCH_NO_VALUE, .board = board);
  }

  slot_t* s = &s_cl.slots[at];
  s->last_read_ms = now;

  if (s->state == SLOT_ADDING) {
    if (!s->add_in_flight && now >= s->next_try_ms) {
      s->add_in_flight = true;
      gen = s->gen;
      slot_idx = (uint8_t)at;
      send_now = true;
    }
    unlock();
    if (send_now) {
      send_add(slot_idx, gen, board, read.data, (uint8_t)read.len);
    }
    return ERR_NEW(ERR_WATCH_NO_VALUE, .board = board);
  }

  if (s->state == SLOT_OLD || (!s->have_copy && !s->has_err)) {
    unlock();
    return ERR_NEW(ERR_WATCH_NO_VALUE, .board = board);
  }

  const uint64_t age = (now >= s->copy_ms) ? (now - s->copy_ms) : 0u;
  if (age > MAX_AGE_MS) {
    unlock();
    return ERR_NEW(ERR_WATCH_STALE, .board = board, .age_ms = (uint16_t)(age > UINT16_MAX ? UINT16_MAX : age));
  }

  if (s->has_err) {
    uint8_t err_rec[ERR_REC_MAX];
    const uint8_t err_len = s->err_len;
    if (err_len > 0u) {
      memcpy(err_rec, s->err_rec, err_len);
    }
    unlock();
    err_h chain = NULL;
    err_free(err_record_parse(err_rec, err_len, &chain));
    return ERR_WRAP(chain, ERR_REMOTE, .board = board, .seq = 0);
  }

  uint8_t copy[SYS_WATCH_COPY_MAX];
  const uint8_t copy_len = s->copy_len;
  if (copy_len > 0u) {
    memcpy(copy, s->copy, copy_len);
  }
  unlock();

  *len = copy_len;
  ERR_CHECK(copy_len <= cap, ERR_CMD_BAD_LEN, .got = (uint16_t)copy_len, .need = (uint16_t)cap);
  if (copy_len > 0u) {
    memcpy(buf, copy, copy_len);
  }
  return NULL;
}

void sys_watch_stale(uint8_t board, cmd_in_t write, uint32_t target_len) {
  if (write.data == NULL || write.len < 2u + target_len) {
    return;
  }

  for (uint8_t i = 0; i < CONFIG_SYS_WATCH_CLIENT_MAX; i++) {
    uint8_t req[SYS_WATCH_READ_MAX];
    uint8_t req_len = 0;
    uint16_t gen = 0;
    bool matched = false;

    lock();
    slot_t* s = &s_cl.slots[i];
    if ((s->state == SLOT_ACTIVE || s->state == SLOT_OLD) && s->board == board && s->req[0] == write.data[0] &&
        s->req_len >= 2u + target_len && memcmp(s->req + 2u, write.data + 2u, target_len) == 0) {
      s->state = SLOT_OLD;
      s->have_copy = false;
      s->has_err = false;
      s->gen++;
      gen = s->gen;
      req_len = s->req_len;
      memcpy(req, s->req, req_len);
      matched = true;
    }
    unlock();

    if (!matched) {
      continue;
    }

    err_h err = sys_session_call(board, (cmd_in_t){req, req_len}, 0, 0, cl_on_refresh, pack_ctx(i, gen));
    if (err != NULL) {
      err_free(err);
      lock();
      if (s->state == SLOT_OLD && s->gen == gen) {
        s->state = SLOT_ACTIVE;
      }
      unlock();
    }
  }
}

err_h sys_watch_client_init(void) {
  cl_lock_init();
  board_timer_init(&s_cl.timer, cl_on_idle_tick, NULL);
  ERR_TRY(sys_session_on_gone(cl_on_gone));
  return sys_session_on_push(cl_on_push);
}
