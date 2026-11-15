#include "sys_watch.h"

#include <string.h>

#include "board_timer.h"
#include "router_queue.h"
#include "sdkconfig.h"
#include "sys_session.h"
#include "sys_watch_internal.h"
#include "utils.h"

MODULE_ID_DEFINE(WATCH, 0x2B)
ERR_MODULE_DEFINE(ERR_MOD_SYS_WATCH, "sys_watch", ERR_SYS_WATCH_MAP)

#define ENTRY_HEAD 3u  // [SYS_WATCH_CHANNEL][id][status]

typedef struct {
  bool used;
  bool busy;        // its read is on the router's queue
  bool pending;     // ON_EVENT: an event came, its read has not run yet
  bool have_crc;    // ON_CHANGE: `crc` is of the last OK entry sent
  bool err_sent;    // the last entry sent was an error: the next ones are not sent until an OK goes
  bool subscribed;  // ON_EVENT: `ev_handle` is its subscription
  uint8_t owner;
  uint8_t read_len;
  uint8_t gen;          // changes when the slot is taken again: a read of the watch before is dropped
  uint16_t ev_handle;
  uint16_t since_read;  // ticks since its last read started
  uint16_t since_sent;  // ticks since its last entry went
  uint32_t crc;
  sys_watch_head_t head;
  uint8_t read[SYS_WATCH_READ_MAX];
} watch_t;

/* The watches. Held over a push and a router_inject (neither blocks nor calls back here), never over sys_event. */
U_MUTEX_DEFINE(sys_watch_mutex);

static struct {
  watch_t watches[CONFIG_SYS_WATCH_MAX];
  board_timer_t timer;
} s_watch;

#define FOR_EACH_WATCH(w) for (watch_t* w = s_watch.watches; w < s_watch.watches + CONFIG_SYS_WATCH_MAX; w++)

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

/* ----------------------------------------------------------------------------
 * A read's answer (the router's task): the entry to the owner, [SYS_WATCH_CHANNEL][id][status][data]
 * ------------------------------------------------------------------------- */

static err_h on_read(void* ctx, const cmd_req_t* cmd, uint8_t status, const uint8_t* data, uint32_t len) {
  (void)cmd;
  if (ENTRY_HEAD + len > CMD_DATA_MAX) {
    return ERR_NEW(ERR_CMD_OUT_FULL, .need = (uint16_t)(ENTRY_HEAD + len), .cap = (uint16_t)CMD_DATA_MAX);
  }
  watch_t* w = &s_watch.watches[(uintptr_t)ctx & 0xFFu];
  xSemaphoreTake(sys_watch_mutex, portMAX_DELAY);
  if (w->used && w->gen == (uint8_t)((uintptr_t)ctx >> 8)) {
    w->busy = false;
    const bool ok = status == ROUTER_STATUS_OK;
    const bool on_change = w->head.trigger == SYS_WATCH_ON_CHANGE;
    const uint32_t crc = (ok && on_change) ? crc32_le(data, len) : 0u;
    const bool silent = w->head.max_silence != 0u && w->since_sent >= w->head.max_silence;
    const bool send = ok ? (!on_change || !w->have_crc || crc != w->crc || silent) : !w->err_sent;
    if (send) {
      uint8_t entry[ENTRY_HEAD + len];
      entry[0] = SYS_WATCH_CHANNEL;
      entry[1] = w->head.id;
      entry[2] = status;
      memcpy(entry + ENTRY_HEAD, data, len);
      err_h err = sys_session_push(w->owner, (cmd_in_t){entry, sizeof(entry)}, 0);
      if (err == NULL) {  // one that did not go is sent by the next read
        w->err_sent = !ok;
        w->have_crc = ok && on_change;
        w->crc = crc;
        w->since_sent = 0;
      }
      err_free(err);
    }
  }
  xSemaphoreGive(sys_watch_mutex);
  return NULL;
}

/* ----------------------------------------------------------------------------
 * The tick (the board timers' task) and the events (sys_event's task)
 * ------------------------------------------------------------------------- */

static void on_tick(void* arg) {
  (void)arg;
  xSemaphoreTake(sys_watch_mutex, portMAX_DELAY);
  FOR_EACH_WATCH(w) {
    if (!w->used) {
      continue;
    }
    w->since_read = w->since_read < UINT16_MAX ? (uint16_t)(w->since_read + 1u) : UINT16_MAX;
    w->since_sent = w->since_sent < UINT16_MAX ? (uint16_t)(w->since_sent + 1u) : UINT16_MAX;
    const bool wanted = w->head.trigger != SYS_WATCH_ON_EVENT || w->pending;
    if (w->busy || !wanted || w->since_read < w->head.period) {
      continue;
    }
    void* ctx = (void*)((uintptr_t)(w - s_watch.watches) | ((uintptr_t)w->gen << 8));
    err_h err = router_inject((cmd_in_t){w->read, w->read_len}, 0, on_read, ctx);
    if (err == NULL) {  // a full queue: the next tick tries again
      w->busy = true;
      w->pending = false;
      w->since_read = 0;
    }
    err_free(err);
  }
  xSemaphoreGive(sys_watch_mutex);
}

// ctx: owner | id << 8. Its read starts at the next tick, at most once per period.
static err_h on_event(const sys_event_t* ev, void* ctx) {
  (void)ev;
  xSemaphoreTake(sys_watch_mutex, portMAX_DELAY);
  FOR_EACH_WATCH(w) {
    if (w->used && w->owner == ((uintptr_t)ctx & 0xFFu) && w->head.id == ((uintptr_t)ctx >> 8)) {
      w->pending = true;
    }
  }
  xSemaphoreGive(sys_watch_mutex);
  return NULL;
}

/* ----------------------------------------------------------------------------
 * Add, remove, list
 * ------------------------------------------------------------------------- */

err_h sys_watch_add(const sys_watch_head_t* head, cmd_in_t read, uint8_t owner) {
  ERR_CHECK(head->id != 0 && head->period != 0 && head->trigger <= SYS_WATCH_ON_EVENT && read.len >= 2u &&
                read.len <= SYS_WATCH_READ_MAX && owner <= SYS_SESSION_ID_CLIENT_LAST,
            ERR_WATCH_BAD_ARG, .id = head->id);
  ERR_CHECK(read.data[1] == CMD_DIR_READ, ERR_WATCH_NOT_READ, .id = head->id);

  // The subscription first, so the watch is whole when it gets its slot.
  const bool on_event_trigger = head->trigger == SYS_WATCH_ON_EVENT;
  uint16_t handle = 0;
  if (on_event_trigger) {
    void* ctx = (void*)((uintptr_t)owner | ((uintptr_t)head->id << 8));
    ERR_TRY(sys_event_subscribe(head->filter, on_event, ctx, SYS_EVENT_RUN_QUEUED, &handle));
  }

  watch_t* at = NULL;
  bool unsubscribe = on_event_trigger;  // the new one's when no slot takes it, the replaced watch's else
  xSemaphoreTake(sys_watch_mutex, portMAX_DELAY);
  FOR_EACH_WATCH(w) {
    if (w->used && w->owner == owner && w->head.id == head->id) {
      at = w;  // ADD with an id it has replaces that watch
      break;
    }
    if (!w->used && at == NULL) {
      at = w;
    }
  }
  if (at != NULL) {
    const uint16_t old_handle = at->ev_handle;
    unsubscribe = at->used && at->subscribed;
    *at = (watch_t){
        .used = true,
        .subscribed = on_event_trigger,
        .owner = owner,
        .read_len = (uint8_t)read.len,
        .gen = (uint8_t)(at->gen + 1u),
        .ev_handle = handle,
        .since_read = head->period,  // the first read at the next tick
        .head = *head,
    };
    memcpy(at->read, read.data, read.len);
    handle = old_handle;
  }
  xSemaphoreGive(sys_watch_mutex);
  if (unsubscribe) {
    err_report(sys_event_unsubscribe(handle));
  }
  ERR_CHECK(at != NULL, ERR_WATCH_FULL);
  return NULL;
}

// Removes the watches of `owner` (SYS_SESSION_ID_BROADCAST: of everyone) with `id` (0: every id); how many.
static uint32_t remove_watches(uint8_t owner, uint8_t id) {
  uint32_t removed = 0;
  FOR_EACH_WATCH(w) {
    xSemaphoreTake(sys_watch_mutex, portMAX_DELAY);
    const bool theirs = owner == SYS_SESSION_ID_BROADCAST || w->owner == owner;
    const bool hit = w->used && theirs && (id == 0 || w->head.id == id);
    const bool subscribed = hit && w->subscribed;
    const uint16_t handle = w->ev_handle;
    if (hit) {
      w->used = false;
      w->gen++;
      removed++;
    }
    xSemaphoreGive(sys_watch_mutex);
    if (subscribed) {
      err_report(sys_event_unsubscribe(handle));
    }
  }
  return removed;
}

err_h sys_watch_remove(uint8_t owner, uint8_t id) {
  ERR_CHECK(id != 0 && owner != SYS_SESSION_ID_BROADCAST && remove_watches(owner, id) > 0, ERR_WATCH_NOT_FOUND,
            .id = id);
  return NULL;
}

uint8_t sys_watch_list(sys_watch_info_t out[CONFIG_SYS_WATCH_MAX]) {
  uint8_t n = 0;
  xSemaphoreTake(sys_watch_mutex, portMAX_DELAY);
  FOR_EACH_WATCH(w) {
    if (w->used) {
      out[n++] = (sys_watch_info_t){.head = w->head, .owner = w->owner, .read_len = w->read_len};
    }
  }
  xSemaphoreGive(sys_watch_mutex);
  return n;
}

// A peer is gone: its watches go with it.
static void on_gone(uint8_t peer, uint8_t role) {
  (void)role;
  (void)remove_watches(peer, 0);
}

err_h sys_watch_init(void) {
  board_timer_init(&s_watch.timer, on_tick, NULL);
  board_timer_start(&s_watch.timer, SYS_WATCH_TICK_MS);
  ERR_TRY(sys_session_on_gone(on_gone));
  return sys_watch_client_init();
}
