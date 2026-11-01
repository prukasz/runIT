#include "sys_watch.h"

#include <string.h>

#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "router.h"
#include "sdkconfig.h"
#include "sys_data_connector.h"
#include "sys_session.h"
#include "utils.h"

/* An entry's frame is [WATCHES][owner][id][status][data]: the answer's buffer has CMD_HEAD bytes in front of its data,
   which is where these go, so an entry is made without a copy. */
#define ENTRY_HEAD  3u
#define STREAM_HEAD (ENTRY_HEAD + 1u)
#define ERROR_MAX   128u  // an error record of a read the router refused

typedef struct {
  bool used;
  bool busy;      // its read is queued or running
  bool pending;   // ON_EVENT: a matching event waits for the gap
  bool have_crc;  // ON_CHANGE: an entry went out
  bool err_sent;  // the last entry sent was an error: not sent again until the read worked
  bool subscribed;
  uint8_t owner;
  uint8_t read_len;
  uint8_t handle;       // the event subscription
  uint16_t gen;         // changes when the slot is taken again: an answer of the watch before it is dropped
  uint16_t seq;         // entries made, so the owner sees a gap
  uint16_t since_read;  // ticks since its read was started
  uint16_t since_sent;  // ticks since an entry went out
  uint32_t crc;         // of the last entry sent (ON_CHANGE)
  sys_watch_head_t head;
  uint8_t read[SYS_WATCH_READ_MAX];
} watch_t;

static struct {
  watch_t watches[CONFIG_SYS_WATCH_MAX];
  uint8_t count;
  esp_timer_handle_t timer;
} s_watch;

U_MUTEX_DEFINE(s_watch_lock);
#define LOCK()   U_MUTEX_LOCK(s_watch_lock, portMAX_DELAY)
#define UNLOCK() U_MUTEX_UNLOCK(s_watch_lock)

// What a read in flight carries to its answer: the slot and the generation it was started for.
#define CTX(slot, gen) ((void*)(uintptr_t)(((uint32_t)(gen) << 8) | (slot)))
#define CTX_SLOT(ctx)  ((uint8_t)((uintptr_t)(ctx) & 0xFFu))
#define CTX_GEN(ctx)   ((uint16_t)((uintptr_t)(ctx) >> 8))

static void tick_inc(uint16_t* t) {
  if (*t < UINT16_MAX) {
    (*t)++;
  }
}

static void timer_set(bool on) {
  if (on) {
    (void)esp_timer_start_periodic(s_watch.timer, (uint64_t)SYS_WATCH_TICK_MS * 1000u);
  } else {
    (void)esp_timer_stop(s_watch.timer);
  }
}

/* ----------------------------------------------------------------------------
 * Entries
 * ------------------------------------------------------------------------- */

/* Makes an entry of the read's answer and sends it when the trigger says so. `data` has STREAM_HEAD writable bytes in
   front of it. */
static void emit(uint8_t slot, uint16_t gen, uint8_t status, uint8_t* data, uint32_t len) {
  watch_t* w = &s_watch.watches[slot];
  LOCK();
  if (!w->used || w->gen != gen) {
    UNLOCK();
    return;
  }
  w->busy = false;
  bool send = true;
  uint32_t crc = 0;
  if (status == ROUTER_STATUS_OK) {
    w->err_sent = false;
    if (w->head.trigger == SYS_WATCH_ON_CHANGE) {
      crc = esp_rom_crc32_le(0, data, len);
      const bool silent = w->head.max_silence != 0 && w->since_sent >= w->head.max_silence;
      send = !w->have_crc || crc != w->crc || silent;
    }
  } else {
    send = !w->err_sent;
  }
  const uint8_t owner = w->owner;
  const uint8_t id = w->head.id;
  const uint8_t connector = w->head.connector;
  const uint16_t seq = send ? w->seq++ : 0;
  UNLOCK();
  if (!send) {
    return;
  }
  data[-1] = status;
  data[-2] = id;
  data[-3] = owner;
  err_h err;
  if (connector == SYS_WATCH_TO_OWNER) {
    data[-4] = SYS_DATA_CONNECTOR_WATCHES;
    err = sys_session_stream(owner, seq, data - STREAM_HEAD, STREAM_HEAD + len, 0);
  } else {
    err = sys_data_connector_send(connector, data - ENTRY_HEAD, ENTRY_HEAD + len);
  }
  // Not taken (the way is busy, down or gone): the entry is not kept, the watch sends again at its next read.
  const bool taken = err == NULL;
  err_free(err);
  if (!taken) {
    return;
  }
  LOCK();
  if (w->used && w->gen == gen) {
    w->since_sent = 0;
    if (status != ROUTER_STATUS_OK) {
      w->err_sent = true;
    } else if (w->head.trigger == SYS_WATCH_ON_CHANGE) {
      w->crc = crc;
      w->have_crc = true;
    }
  }
  UNLOCK();
}

// The read's answer, on the router's work task.
static err_h watch_done(void* ctx, const err_cmd_t* cmd, uint8_t status, uint8_t* data, uint32_t len) {
  (void)cmd;
  emit(CTX_SLOT(ctx), CTX_GEN(ctx), status, data, len);
  return NULL;
}

// A read the router refused is an entry of its error.
static void refused(uint8_t slot, uint16_t gen, err_h err) {
  uint8_t buf[STREAM_HEAD + ERROR_MAX];
  const uint32_t len = err_record(NULL, err, buf + STREAM_HEAD, ERROR_MAX);
  err_free(err);
  emit(slot, gen, ROUTER_STATUS_ERROR, buf + STREAM_HEAD, len);
}

/* ----------------------------------------------------------------------------
 * Running
 * ------------------------------------------------------------------------- */

/* Starts the read of watch `slot` when it is due: its period has passed (and, for ON_EVENT, an event waits) and its
   last read is done. */
static void start_if_due(uint8_t slot) {
  watch_t* w = &s_watch.watches[slot];
  uint8_t read[SYS_WATCH_READ_MAX];
  uint8_t read_len = 0;
  uint16_t gen = 0;
  LOCK();
  const bool event = w->head.trigger == SYS_WATCH_ON_EVENT;
  const bool due = w->used && !w->busy && w->since_read >= w->head.period && (!event || w->pending);
  if (due) {
    w->busy = true;
    w->pending = false;
    w->since_read = 0;
    read_len = w->read_len;
    gen = w->gen;
    memcpy(read, w->read, read_len);
  }
  UNLOCK();
  if (!due) {
    return;
  }
  err_h err = router_inject((cmd_in_t){read, read_len}, 0, watch_done, CTX(slot, gen));
  if (err != NULL) {
    refused(slot, gen, err);
  }
}

static void on_tick(void* arg) {
  (void)arg;
  for (uint8_t i = 0; i < CONFIG_SYS_WATCH_MAX; i++) {
    LOCK();
    watch_t* w = &s_watch.watches[i];
    if (w->used) {
      tick_inc(&w->since_read);
      tick_inc(&w->since_sent);
    }
    UNLOCK();
    start_if_due(i);
  }
}

// An event passing the filter of the ON_EVENT watch in slot `ctx` (the event task).
static err_h on_event(const sys_event_t* event, void* ctx) {
  const uint8_t slot = (uint8_t)(uintptr_t)ctx;
  watch_t* w = &s_watch.watches[slot];
  LOCK();
  const bool hit = w->used && w->head.trigger == SYS_WATCH_ON_EVENT && sys_event_filter_match(&w->head.filter, event);
  if (hit) {
    w->pending = true;
  }
  UNLOCK();
  if (hit) {
    start_if_due(slot);
  }
  return NULL;
}

/* ----------------------------------------------------------------------------
 * The table
 * ------------------------------------------------------------------------- */

#define NO_SUB (-1)

// Frees slot i; the caller holds the lock. Returns the event subscription to drop outside it (NO_SUB: none).
static int free_locked(uint8_t i) {
  watch_t* w = &s_watch.watches[i];
  const int handle = w->subscribed ? w->handle : NO_SUB;
  w->used = false;
  w->subscribed = false;
  w->gen++;
  if (--s_watch.count == 0) {
    timer_set(false);
  }
  return handle;
}

static int find_locked(uint8_t owner, uint8_t id) {
  for (uint8_t i = 0; i < CONFIG_SYS_WATCH_MAX; i++) {
    const watch_t* w = &s_watch.watches[i];
    if (w->used && w->owner == owner && w->head.id == id) {
      return i;
    }
  }
  return -1;
}

static int free_slot_locked(void) {
  for (uint8_t i = 0; i < CONFIG_SYS_WATCH_MAX; i++) {
    if (!s_watch.watches[i].used) {
      return i;
    }
  }
  return -1;
}

static void unsubscribe(int handle) {
  if (handle != NO_SUB) {
    err_report(sys_event_unsubscribe((uint8_t)handle));
  }
}

err_h sys_watch_add(const sys_watch_head_t* head, cmd_in_t read, uint8_t owner) {
  ERR_CHECK(head->id != 0 && head->period != 0 && head->trigger <= SYS_WATCH_ON_EVENT &&
                read.len <= SYS_WATCH_READ_MAX && !(owner == SYS_SESSION_ID_SELF && head->connector == SYS_WATCH_TO_OWNER),
            ERR_WATCH_BAD_ARG, .id = head->id);
  ERR_CHECK(read.len >= 2 && read.data[1] == CMD_DIR_READ, ERR_WATCH_NOT_READ, .id = head->id);
  LOCK();
  int at = find_locked(owner, head->id);
  int old = NO_SUB;
  if (at >= 0) {
    old = s_watch.watches[at].subscribed ? s_watch.watches[at].handle : NO_SUB;
  } else {
    at = free_slot_locked();
    if (at < 0) {
      UNLOCK();
      return ERR_NEW(ERR_WATCH_FULL);
    }
    if (s_watch.count++ == 0) {
      timer_set(true);
    }
  }
  watch_t* w = &s_watch.watches[at];
  *w = (watch_t){
      .used = true,
      .owner = owner,
      .read_len = (uint8_t)read.len,
      .gen = (uint16_t)(w->gen + 1u),
      .since_read = head->period,  // the first read is due at the next tick
      .head = *head,
  };
  memcpy(w->read, read.data, read.len);
  UNLOCK();
  unsubscribe(old);
  if (head->trigger != SYS_WATCH_ON_EVENT) {
    return NULL;
  }
  uint8_t handle = 0;
  err_h err = sys_event_subscribe_fn(head->filter.source, head->filter.event, head->filter.channel, on_event,
                                     (void*)(uintptr_t)at, &handle);
  LOCK();
  if (err == NULL) {
    w->handle = handle;
    w->subscribed = true;
  } else {
    (void)free_locked((uint8_t)at);
  }
  UNLOCK();
  return err;
}

err_h sys_watch_remove(uint8_t owner, uint8_t id) {
  LOCK();
  const int at = find_locked(owner, id);
  const int handle = at >= 0 ? free_locked((uint8_t)at) : NO_SUB;
  UNLOCK();
  ERR_CHECK(at >= 0, ERR_WATCH_NOT_FOUND, .id = id);
  unsubscribe(handle);
  return NULL;
}

uint8_t sys_watch_list(sys_watch_info_t* info, uint8_t max) {
  uint8_t n = 0;
  LOCK();
  for (uint8_t i = 0; i < CONFIG_SYS_WATCH_MAX && n < max; i++) {
    const watch_t* w = &s_watch.watches[i];
    if (w->used) {
      info[n++] = (sys_watch_info_t){.head = w->head, .owner = w->owner, .read_len = w->read_len};
    }
  }
  UNLOCK();
  return n;
}

/* A peer is gone: its watches end (the board's own never do). The parent's session ending names every peer outside
   this board's subtree: the watches of the owners the session layer no longer reaches end. */
static void on_gone(uint8_t peer, uint8_t via) {
  const bool outside = peer == SYS_SESSION_ID_BROADCAST && via == SYS_SESSION_PARENT;
  for (uint8_t i = 0; i < CONFIG_SYS_WATCH_MAX; i++) {
    LOCK();
    const bool used = s_watch.watches[i].used;
    const uint8_t owner = s_watch.watches[i].owner;
    UNLOCK();
    if (!used || owner == SYS_SESSION_ID_SELF || !(owner == peer || (outside && !sys_session_below(owner)))) {
      continue;
    }
    LOCK();
    const bool same = s_watch.watches[i].used && s_watch.watches[i].owner == owner;
    const int handle = same ? free_locked(i) : NO_SUB;
    UNLOCK();
    unsubscribe(handle);
  }
}

err_h sys_watch_init(void) {
  const esp_timer_create_args_t args = {.callback = on_tick, .name = "sys_watch"};
  ERR_TRY(ERR_ESP(esp_timer_create(&args, &s_watch.timer)));
  return sys_session_on_gone(on_gone);
}
