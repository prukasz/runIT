#include "sys_watch.h"

#include <string.h>

#include "board_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "router.h"
#include "router_queue.h"
#include "sdkconfig.h"
#include "sys_event.h"
#include "sys_session.h"
#include "sys_watch_internal.h"
#include "utils.h"

MODULE_ID_DEFINE(WATCH, 0x2B)
ERR_MODULE_DEFINE(ERR_MOD_SYS_WATCH, "sys_watch", ERR_SYS_WATCH_MAP)

#define ERR_RECORD_MAX 160u
#define ENTRY_HEAD_LEN 3u  // [SYS_SESSION_CHANNEL_WATCHES][id][status]

typedef struct {
  bool used;
  bool busy;        // read injected on router queue, awaiting watch_done
  bool pending;     // ON_EVENT: matching event waits for minimum period gap
  bool have_crc;    // ON_CHANGE: valid CRC-32 kept from last sent OK entry
  bool err_sent;    // last entry sent was an error (deduplicates repeated errors)
  bool subscribed;  // active sys_event subscription
  uint8_t owner;
  uint8_t read_len;
  uint16_t ev_handle;
  uint16_t gen;         // bumped on slot reuse/removal so late callbacks are dropped
  uint16_t since_read;  // ticks since last read injection
  uint16_t since_sent;  // ticks since last entry left for transport
  uint32_t crc;         // CRC-32 of last sent OK data (ON_CHANGE)
  sys_watch_head_t head;
  uint8_t read[SYS_WATCH_READ_MAX];
} watch_t;

static struct {
  watch_t watches[CONFIG_SYS_WATCH_MAX];
  uint8_t count;
  board_timer_t timer;
} s_watch;

static StaticSemaphore_t s_lock_buf;
static SemaphoreHandle_t s_lock;

__attribute__((constructor)) static void watch_lock_init(void) {
  if (s_lock == NULL) {
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf);
  }
}

static void lock(void) {
  watch_lock_init();
  xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlock(void) {
  xSemaphoreGive(s_lock);
}

static void* pack_ctx(uint8_t slot, uint16_t gen) {
  return (void*)((uintptr_t)slot | ((uintptr_t)gen << 8));
}

static void unpack_ctx(void* ctx, uint8_t* slot, uint16_t* gen) {
  const uintptr_t v = (uintptr_t)ctx;
  *slot = (uint8_t)(v & 0xFFu);
  *gen = (uint16_t)((v >> 8) & 0xFFFFu);
}

static uint32_t crc32_le(const uint8_t* data, uint32_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (uint32_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8u; b++) {
      const uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return ~crc;
}

static void tick_inc(uint16_t* v) {
  if (*v < UINT16_MAX) {
    (*v)++;
  }
}

static err_h push_entry(uint8_t owner, uint8_t id, uint8_t status, const uint8_t* data, uint32_t len) {
  ERR_CHECK(ENTRY_HEAD_LEN + len <= CMD_DATA_MAX, ERR_CMD_OUT_FULL,
            .need = (uint16_t)(ENTRY_HEAD_LEN + len), .cap = (uint16_t)CMD_DATA_MAX);
  uint8_t frame[CMD_DATA_MAX];
  frame[0] = SYS_SESSION_CHANNEL_WATCHES;
  frame[1] = id;
  frame[2] = status;
  if (len > 0u) {
    memcpy(frame + ENTRY_HEAD_LEN, data, len);
  }
  return sys_session_push(owner, frame, ENTRY_HEAD_LEN + len, 0);
}

static void mark_err_sent(uint8_t slot, uint16_t gen) {
  lock();
  watch_t* w = &s_watch.watches[slot];
  if (w->used && w->gen == gen) {
    w->err_sent = true;
    w->have_crc = false;
    w->since_sent = 0;
  }
  unlock();
}

static void send_err_record(uint8_t slot, uint16_t gen, uint8_t owner, uint8_t id, const cmd_req_t* cmd, err_h err) {
  uint8_t frame[ENTRY_HEAD_LEN + ERR_RECORD_MAX];
  frame[0] = SYS_SESSION_CHANNEL_WATCHES;
  frame[1] = id;
  frame[2] = ROUTER_STATUS_ERROR;
  const uint32_t rlen = cmd_err_record(cmd, err, frame + ENTRY_HEAD_LEN, ERR_RECORD_MAX);
  err_h perr = sys_session_push(owner, frame, ENTRY_HEAD_LEN + rlen, 0);
  if (perr == NULL) {
    mark_err_sent(slot, gen);
  } else {
    err_free(perr);
  }
}

static err_h watch_done(void* ctx, const cmd_req_t* cmd, uint8_t status, const uint8_t* data, uint32_t len) {
  uint8_t slot = 0;
  uint16_t gen = 0;
  unpack_ctx(ctx, &slot, &gen);
  if (slot >= CONFIG_SYS_WATCH_MAX) {
    return NULL;
  }

  lock();
  watch_t* w = &s_watch.watches[slot];
  if (!w->used || w->gen != gen) {
    unlock();
    return NULL;
  }
  w->busy = false;
  const uint8_t owner = w->owner;
  const uint8_t id = w->head.id;
  const uint8_t trigger = w->head.trigger;
  const uint16_t max_silence = w->head.max_silence;
  const uint16_t since_sent = w->since_sent;
  const uint32_t prev_crc = w->crc;
  const bool have_crc = w->have_crc;
  const bool err_sent = w->err_sent;
  unlock();

  if (status == ROUTER_STATUS_OK) {
    if (ENTRY_HEAD_LEN + len > CMD_DATA_MAX) {
      if (!err_sent) {
        err_h oob = ERR_NEW(ERR_CMD_OUT_FULL, .need = (uint16_t)(ENTRY_HEAD_LEN + len), .cap = (uint16_t)CMD_DATA_MAX);
        send_err_record(slot, gen, owner, id, cmd, oob);
        err_free(oob);
      }
      return NULL;
    }

    const uint32_t crc = (trigger == SYS_WATCH_ON_CHANGE) ? crc32_le(data, len) : 0u;
    const bool silent = max_silence != 0u && since_sent >= max_silence;
    const bool send = (trigger != SYS_WATCH_ON_CHANGE) || !have_crc || err_sent || crc != prev_crc || silent;
    if (!send) {
      return NULL;
    }

    err_h perr = push_entry(owner, id, ROUTER_STATUS_OK, data, len);
    if (perr == NULL) {
      lock();
      if (w->used && w->gen == gen) {
        w->err_sent = false;
        w->since_sent = 0;
        if (trigger == SYS_WATCH_ON_CHANGE) {
          w->crc = crc;
          w->have_crc = true;
        }
      }
      unlock();
    } else {
      err_free(perr);
    }
    return NULL;
  }

  if (!err_sent) {
    err_h perr = push_entry(owner, id, status, data, len);
    if (perr == NULL) {
      mark_err_sent(slot, gen);
    } else {
      err_free(perr);
    }
  }
  return NULL;
}

static void start_if_due(uint8_t slot) {
  uint8_t req[SYS_WATCH_READ_MAX];
  uint8_t req_len = 0;
  uint8_t owner = 0;
  uint8_t id = 0;
  uint16_t gen = 0;
  bool err_sent = false;

  lock();
  watch_t* w = &s_watch.watches[slot];
  const bool event = w->head.trigger == SYS_WATCH_ON_EVENT;
  const bool due = w->used && !w->busy && w->since_read >= w->head.period && (!event || w->pending);
  if (due) {
    w->busy = true;
    w->pending = false;
    w->since_read = 0;
    req_len = w->read_len;
    gen = w->gen;
    owner = w->owner;
    id = w->head.id;
    err_sent = w->err_sent;
    memcpy(req, w->read, req_len);
  }
  unlock();

  if (!due) {
    return;
  }

  err_h err = router_inject((cmd_in_t){req, req_len}, 0, watch_done, pack_ctx(slot, gen));
  if (err != NULL) {
    lock();
    if (w->used && w->gen == gen) {
      w->busy = false;
    }
    unlock();
    if (!err_sent) {
      const cmd_req_t cmd = {.req = req, .req_len = req_len, .src = owner};
      send_err_record(slot, gen, owner, id, &cmd, err);
    }
    err_free(err);
  }
}

static void on_tick(void* ctx) {
  (void)ctx;
  for (uint8_t i = 0; i < CONFIG_SYS_WATCH_MAX; i++) {
    lock();
    watch_t* w = &s_watch.watches[i];
    if (w->used) {
      tick_inc(&w->since_read);
      tick_inc(&w->since_sent);
    }
    unlock();
    start_if_due(i);
  }
}

static err_h on_event(const sys_event_t* ev, void* ctx) {
  (void)ev;
  uint8_t slot = 0;
  uint16_t gen = 0;
  unpack_ctx(ctx, &slot, &gen);
  if (slot >= CONFIG_SYS_WATCH_MAX) {
    return NULL;
  }

  bool trigger = false;
  lock();
  watch_t* w = &s_watch.watches[slot];
  if (w->used && w->gen == gen && w->head.trigger == SYS_WATCH_ON_EVENT) {
    w->pending = true;
    trigger = true;
  }
  unlock();

  if (trigger) {
    start_if_due(slot);
  }
  return NULL;
}

static bool valid_owner(uint8_t owner) {
  return owner != SYS_SESSION_ID_SELF && owner != SYS_SESSION_ID_NONE && owner != SYS_SESSION_ID_BROADCAST;
}

err_h sys_watch_add(const sys_watch_head_t* head, cmd_in_t read, uint8_t owner) {
  ERR_CHECK(head != NULL, ERR_WATCH_BAD_ARG, .id = 0);
  ERR_CHECK(head->id != 0 && head->period != 0 && head->trigger <= SYS_WATCH_ON_EVENT &&
                read.data != NULL && read.len >= 2u && read.len <= SYS_WATCH_READ_MAX && valid_owner(owner),
            ERR_WATCH_BAD_ARG, .id = head->id);
  ERR_CHECK(read.data[1] == CMD_DIR_READ, ERR_WATCH_NOT_READ, .id = head->id);

  lock();
  int32_t at = -1;
  int32_t free_at = -1;
  for (int32_t i = 0; i < CONFIG_SYS_WATCH_MAX; i++) {
    const watch_t* w = &s_watch.watches[i];
    if (!w->used) {
      if (free_at < 0) {
        free_at = i;
      }
    } else if (w->owner == owner && w->head.id == head->id) {
      at = i;
      break;
    }
  }
  const bool replacing = at >= 0;
  if (!replacing) {
    at = free_at;
  }
  if (at < 0) {
    unlock();
    return ERR_NEW(ERR_WATCH_FULL);
  }

  watch_t* w = &s_watch.watches[at];
  const bool had_sub = w->used && w->subscribed;
  const uint16_t old_handle = w->ev_handle;
  const uint16_t gen = (uint16_t)(w->gen + 1u);
  const bool start_timer = s_watch.count == 0u;

  memset(w, 0, sizeof(*w));
  w->used = true;
  w->owner = owner;
  w->read_len = (uint8_t)read.len;
  w->gen = gen;
  w->since_read = head->period;  // first PERIODIC/ON_CHANGE read runs on the next tick
  w->head = *head;
  memcpy(w->read, read.data, read.len);
  if (!replacing) {
    s_watch.count++;
  }
  unlock();

  if (had_sub) {
    err_report(sys_event_unsubscribe(old_handle));
  }

  if (head->trigger == SYS_WATCH_ON_EVENT) {
    const sys_event_key_t filter = head->filter;
    uint16_t ev_handle = 0;
    err_h err = sys_event_subscribe(filter, on_event, pack_ctx((uint8_t)at, gen), SYS_EVENT_RUN_QUEUED, &ev_handle);
    if (err != NULL) {
      bool stop_timer = false;
      lock();
      if (w->used && w->gen == gen) {
        w->used = false;
        w->gen++;
        s_watch.count--;
        stop_timer = s_watch.count == 0u;
      }
      unlock();
      if (stop_timer) {
        board_timer_stop(&s_watch.timer);
      }
      return err;
    }

    bool unsub_orphan = false;
    lock();
    if (w->used && w->gen == gen) {
      w->ev_handle = ev_handle;
      w->subscribed = true;
    } else {
      unsub_orphan = true;
    }
    unlock();
    if (unsub_orphan) {
      err_report(sys_event_unsubscribe(ev_handle));
      return NULL;
    }
  }

  if (start_timer) {
    board_timer_start(&s_watch.timer, SYS_WATCH_TICK_MS);
  }
  return NULL;
}

err_h sys_watch_remove(uint8_t owner, uint8_t id) {
  ERR_CHECK(id != 0 && valid_owner(owner), ERR_WATCH_NOT_FOUND, .id = id);

  bool found = false;
  bool had_sub = false;
  uint16_t old_handle = 0;
  bool stop_timer = false;

  lock();
  for (int32_t i = 0; i < CONFIG_SYS_WATCH_MAX; i++) {
    watch_t* w = &s_watch.watches[i];
    if (w->used && w->owner == owner && w->head.id == id) {
      found = true;
      had_sub = w->subscribed;
      old_handle = w->ev_handle;
      w->used = false;
      w->subscribed = false;
      w->gen++;
      s_watch.count--;
      stop_timer = s_watch.count == 0u;
      break;
    }
  }
  unlock();

  ERR_CHECK(found, ERR_WATCH_NOT_FOUND, .id = id);

  if (had_sub) {
    err_report(sys_event_unsubscribe(old_handle));
  }
  if (stop_timer) {
    board_timer_stop(&s_watch.timer);
  }
  return NULL;
}

uint8_t sys_watch_list(sys_watch_info_t* out, uint8_t max) {
  if (out == NULL || max == 0u) {
    return 0u;
  }
  uint8_t n = 0;
  lock();
  for (uint8_t i = 0; i < CONFIG_SYS_WATCH_MAX && n < max; i++) {
    const watch_t* w = &s_watch.watches[i];
    if (w->used) {
      out[n++] = (sys_watch_info_t){
          .head = w->head,
          .owner = w->owner,
          .read_len = w->read_len,
      };
    }
  }
  unlock();
  return n;
}

static void on_gone(uint8_t peer, uint8_t role) {
  (void)role;
  uint16_t handles[CONFIG_SYS_WATCH_MAX];
  uint8_t handle_n = 0;
  bool stop_timer = false;

  lock();
  for (uint8_t i = 0; i < CONFIG_SYS_WATCH_MAX; i++) {
    watch_t* w = &s_watch.watches[i];
    if (w->used && (peer == SYS_SESSION_ID_BROADCAST || w->owner == peer)) {
      if (w->subscribed) {
        handles[handle_n++] = w->ev_handle;
      }
      w->used = false;
      w->subscribed = false;
      w->gen++;
      s_watch.count--;
      if (s_watch.count == 0u) {
        stop_timer = true;
      }
    }
  }
  unlock();

  for (uint8_t i = 0; i < handle_n; i++) {
    err_report(sys_event_unsubscribe(handles[i]));
  }
  if (stop_timer) {
    board_timer_stop(&s_watch.timer);
  }
}

err_h sys_watch_init(void) {
  watch_lock_init();
  board_timer_init(&s_watch.timer, on_tick, NULL);
  ERR_TRY(sys_session_on_gone(on_gone));
  return sys_watch_client_init();
}
