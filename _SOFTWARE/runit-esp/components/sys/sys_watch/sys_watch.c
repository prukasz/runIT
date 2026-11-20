#include "sys_watch.h"

#include <string.h>

#include "board_timer.h"
#include "data_matrix.h"
#include "router_queue.h"
#include "sdkconfig.h"
#include "sys_session.h"
#include "sys_session_events.h"
#include "sys_watch_internal.h"
#include "utils.h"

ERR_MODULE_DEFINE(MODULE_ID_WATCH, ERR_SYS_WATCH_MAP)

/* What the last entry sent was (watch_t.last). */
#define LAST_NONE  0u  // nothing yet
#define LAST_OK    1u  // an OK answer, `crc` its CRC (ON_CHANGE)
#define LAST_ERROR 2u  // an error: the next ones are not sent until an OK goes

typedef struct {
  bool busy;      // its read is on the router's queue
  uint8_t last;   // LAST_*
  uint8_t owner;
  uint8_t read_len;
  uint8_t gen;    // changes when the slot is taken again: a read of the watch before is dropped
  uint16_t since_read;  // ticks since its last read started
  uint16_t since_sent;  // ticks since its last entry went
  uint32_t crc;
  sys_watch_head_t head;  // id 0: a free slot (ADD never takes 0)
  uint8_t read[SYS_WATCH_READ_MAX];
} watch_t;

#define USED(w) ((w)->head.id != 0u)

/* The watches. Held over a push and a router_inject (neither blocks nor calls back here), never over sys_event. */
U_MUTEX_DEFINE(sys_watch_mutex);

static struct {
  watch_t watches[CONFIG_SYS_WATCH_MAX];
  board_timer_t timer;
} s_watch;

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
 * A read's answer (the router's task): the entry, [hdr PUSH, MODULE_ID_WATCH][owner][id][status][data], out of WATCH
 * to every port linked from it (a session's sub-port, the raw telemetry), NOWAIT: whoever takes it keeps its own by
 * `owner`. A line that does not take it loses it; max_silence sends the value again.
 * ------------------------------------------------------------------------- */

static err_h on_read(void* ctx, const cmd_req_t* cmd, uint8_t status, const uint8_t* data, uint32_t len) {
  (void)cmd;
  if (SYS_WATCH_ENTRY_HEAD + len > CMD_DATA_MAX) {
    return ERR_NEW(ERR_CMD_OUT_FULL, .need = (uint16_t)(SYS_WATCH_ENTRY_HEAD + len), .cap = (uint16_t)CMD_DATA_MAX);
  }
  watch_t* w = &s_watch.watches[(uintptr_t)ctx & 0xFFu];
  xSemaphoreTake(sys_watch_mutex, portMAX_DELAY);
  if (USED(w) && w->gen == (uint8_t)((uintptr_t)ctx >> 8)) {
    w->busy = false;
    const bool ok = status == ROUTER_STATUS_OK;
    const bool on_change = w->head.trigger == SYS_WATCH_ON_CHANGE;
    const uint32_t crc = (ok && on_change) ? crc32_le(data, len) : 0u;
    const bool silent = w->head.max_silence != 0u && w->since_sent >= w->head.max_silence;
    const bool changed = w->last != LAST_OK || crc != w->crc;
    const bool send = ok ? (!on_change || changed || silent) : w->last != LAST_ERROR;
    if (send) {
      uint8_t packet[CMD_HEAD + SYS_WATCH_ENTRY_HEAD + len];  // one frame: the header, the entry's head, the answer
      const cmd_hdr_t hdr = {.type = CMD_TYPE(CMD_PUSH), .key = MODULE_ID_WATCH};
      memcpy(packet, &hdr, CMD_HEAD);
      packet[CMD_HEAD] = w->owner;
      packet[CMD_HEAD + 1u] = w->head.id;
      packet[CMD_HEAD + 2u] = status;
      memcpy(packet + CMD_HEAD + SYS_WATCH_ENTRY_HEAD, data, len);
      data_matrix_send(&sys_watch_port, CMD_SEND_NOWAIT, packet, sizeof(packet));
      w->last = ok ? LAST_OK : LAST_ERROR;
      w->crc = crc;
      w->since_sent = 0;
    }
  }
  xSemaphoreGive(sys_watch_mutex);
  return NULL;
}

/* ----------------------------------------------------------------------------
 * The tick (the board timers' task)
 * ------------------------------------------------------------------------- */

static void on_tick(void* arg) {
  (void)arg;
  xSemaphoreTake(sys_watch_mutex, portMAX_DELAY);
  for (watch_t* w = s_watch.watches; w < s_watch.watches + CONFIG_SYS_WATCH_MAX; w++) {
    if (!USED(w)) {
      continue;
    }
    w->since_read = w->since_read < UINT16_MAX ? (uint16_t)(w->since_read + 1u) : UINT16_MAX;
    w->since_sent = w->since_sent < UINT16_MAX ? (uint16_t)(w->since_sent + 1u) : UINT16_MAX;
    if (w->busy || w->since_read < w->head.period) {
      continue;
    }
    void* ctx = (void*)((uintptr_t)(w - s_watch.watches) | ((uintptr_t)w->gen << 8));
    err_h err = router_inject((cmd_in_t){w->read, w->read_len}, 0, on_read, ctx);
    if (err == NULL) {  // a full queue: the next tick tries again
      w->busy = true;
      w->since_read = 0;
    }
    err_free(err);
  }
  xSemaphoreGive(sys_watch_mutex);
}

/* ----------------------------------------------------------------------------
 * Add, remove, list
 * ------------------------------------------------------------------------- */

err_h sys_watch_add(const sys_watch_head_t* head, cmd_in_t read, uint8_t owner) {
  ERR_CHECK(head->id != 0 && head->period != 0 && head->trigger <= SYS_WATCH_ON_CHANGE && read.len >= 2u &&
                read.len <= SYS_WATCH_READ_MAX && owner != CMD_SRC_NONE && owner <= CMD_SRC_CLIENT_LAST,
            ERR_WATCH_BAD_ARG, .id = head->id);
  ERR_CHECK(read.data[1] == CMD_DIR_READ, ERR_WATCH_NOT_READ, .id = head->id);

  watch_t* at = NULL;
  xSemaphoreTake(sys_watch_mutex, portMAX_DELAY);
  for (watch_t* w = s_watch.watches; w < s_watch.watches + CONFIG_SYS_WATCH_MAX; w++) {
    if (USED(w) && w->owner == owner && w->head.id == head->id) {
      at = w;  // ADD with an id it has replaces that watch
      break;
    }
    if (!USED(w) && at == NULL) {
      at = w;
    }
  }
  if (at != NULL) {
    *at = (watch_t){
        .owner = owner,
        .read_len = (uint8_t)read.len,
        .gen = (uint8_t)(at->gen + 1u),
        .since_read = head->period,  // the first read at the next tick
        .head = *head,
    };
    memcpy(at->read, read.data, read.len);
  }
  xSemaphoreGive(sys_watch_mutex);
  ERR_CHECK(at != NULL, ERR_WATCH_FULL);
  return NULL;
}

// Removes the watches of `owner` (CMD_SRC_ALL: of everyone) with `id` (0: every id); how many.
static uint32_t remove_watches(uint8_t owner, uint8_t id) {
  uint32_t removed = 0;
  xSemaphoreTake(sys_watch_mutex, portMAX_DELAY);
  for (watch_t* w = s_watch.watches; w < s_watch.watches + CONFIG_SYS_WATCH_MAX; w++) {
    const bool theirs = owner == CMD_SRC_ALL || w->owner == owner;
    if (USED(w) && theirs && (id == 0 || w->head.id == id)) {
      w->head.id = 0;  // free
      w->gen++;
      removed++;
    }
  }
  xSemaphoreGive(sys_watch_mutex);
  return removed;
}

err_h sys_watch_remove(uint8_t owner, uint8_t id) {
  ERR_CHECK(id != 0 && owner != CMD_SRC_ALL && remove_watches(owner, id) > 0, ERR_WATCH_NOT_FOUND,
            .id = id);
  return NULL;
}

uint8_t sys_watch_list(sys_watch_info_t out[CONFIG_SYS_WATCH_MAX]) {
  uint8_t n = 0;
  xSemaphoreTake(sys_watch_mutex, portMAX_DELAY);
  for (watch_t* w = s_watch.watches; w < s_watch.watches + CONFIG_SYS_WATCH_MAX; w++) {
    if (USED(w)) {
      out[n++] = (sys_watch_info_t){.head = w->head, .owner = w->owner, .read_len = w->read_len};
    }
  }
  xSemaphoreGive(sys_watch_mutex);
  return n;
}

// A session ended (SESSION_EV_GONE, the peer is the event's source): its watches go with it.
static err_h on_gone(const sys_event_t* event, void* ctx) {
  (void)ctx;
  (void)remove_watches((uint8_t)event->key.source, 0);
  return NULL;
}

err_h sys_watch_init(void) {
  board_timer_init(&s_watch.timer, on_tick, NULL);
  board_timer_start(&s_watch.timer, SYS_WATCH_TICK_MS);
  uint16_t handle = 0;
  ERR_TRY(sys_event_subscribe(SESSION_GONE_FILTER, on_gone, NULL, SYS_EVENT_RUN_INLINE, &handle));
  return sys_watch_client_init();
}
