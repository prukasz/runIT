#include <string.h>

#include "board_id.h"
#include "board_time.h"
#include "board_timer.h"
#include "data_matrix.h"
#include "err_record.h"
#include "router_queue.h"
#include "sdkconfig.h"
#include "sys_session.h"
#include "sys_session_events.h"
#include "sys_watch.h"
#include "sys_watch_internal.h"
#include "sys_watch_router.h"
#include "utils.h"

#define IDLE_TICK_MS  100u
#define ENTRY_HEAD    2u    // [id][status], then the answer: after the header of a WATCH packet
#define ENTRY_MAX     128u  // an entry kept: an OK answer (at most SYS_WATCH_COPY_MAX) or an error record
#define PERIOD_MS     ((uint32_t)CONFIG_SYS_WATCH_READ_PERIOD_MS)
#define PERIOD_TICKS  ((PERIOD_MS + SYS_WATCH_TICK_MS - 1u) / SYS_WATCH_TICK_MS)
#define MAX_AGE_MS    (3u * PERIOD_MS)

_Static_assert(2u * PERIOD_TICKS <= UINT16_MAX, "CONFIG_SYS_WATCH_READ_PERIOD_MS in a watch's ticks");

typedef enum {
  SLOT_FREE = 0,
  SLOT_ADDING,  // ADD is on its way, or is sent again by a read: entries are not taken (B3)
  SLOT_ACTIVE,  // ADD answered OK: the entries of its watch are taken
} slot_state_e;

/* A cached read: the watch `index + 1` on `board`. */
typedef struct {
  uint8_t state;     // slot_state_e
  bool have;         // an entry came since ADD's OK
  uint8_t status;    // of that entry: router_status_e
  uint8_t len;       // bytes of `data`
  uint8_t board;
  uint8_t req_len;
  uint8_t gen;       // changes when the slot starts again: an ADD's answer from before is dropped
  uint32_t entry_ms;  // when the entry came
  uint32_t read_ms;   // the last sys_watch_read of it
  uint32_t add_ms;    // the last ADD sent
  uint8_t req[SYS_WATCH_READ_MAX];
  uint8_t data[ENTRY_MAX];
} slot_t;

U_MUTEX_DEFINE(sys_watch_client_mutex);  // the slots: copies only, never held over a send

static struct {
  slot_t slots[CONFIG_SYS_WATCH_CLIENT_MAX];
  board_timer_t timer;
} s_cl;

#define FOR_EACH_SLOT(s) for (slot_t* s = s_cl.slots; s < s_cl.slots + CONFIG_SYS_WATCH_CLIENT_MAX; s++)

static uint32_t now_ms(void) {
  return (uint32_t)board_time_uptime_ms();
}

/* ----------------------------------------------------------------------------
 * ADD: its OK opens the slot for the watch's entries
 * ------------------------------------------------------------------------- */

static err_h on_added(void* ctx, const cmd_req_t* cmd, uint8_t status, const uint8_t* data, uint32_t len) {
  (void)cmd;
  (void)data;
  (void)len;
  slot_t* s = &s_cl.slots[(uintptr_t)ctx & 0xFFu];
  xSemaphoreTake(sys_watch_client_mutex, portMAX_DELAY);
  if (s->state == SLOT_ADDING && s->gen == (uint8_t)((uintptr_t)ctx >> 8) && status == ROUTER_STATUS_OK) {
    s->state = SLOT_ACTIVE;
    s->have = false;
  }
  xSemaphoreGive(sys_watch_client_mutex);
  return NULL;
}

// One that does not go, or is refused, is sent again by a read a period later.
static void send_add(uint8_t slot, uint8_t gen, uint8_t board, cmd_in_t read) {
  const sys_watch_head_t head = {
      .id = (uint8_t)(slot + 1u),
      .trigger = SYS_WATCH_ON_CHANGE,
      .period = (uint16_t)PERIOD_TICKS,
      .max_silence = (uint16_t)(2u * PERIOD_TICKS),
  };
  uint8_t req[3u + sizeof(head) + SYS_WATCH_READ_MAX] = {MODULE_ID_WATCH, CMD_DIR_WRITE, WATCH_OP_ADD};
  memcpy(req + 3u, &head, sizeof(head));
  memcpy(req + 3u + sizeof(head), read.data, read.len);
  void* ctx = (void*)((uintptr_t)slot | ((uintptr_t)gen << 8));
  data_matrix_port_id_t port = 0;
  err_h err = sys_session_port(board, MODULE_ID_ROUTER, &port);
  if (err == NULL) {
    err = router_request(port, (cmd_in_t){req, 3u + sizeof(head) + read.len}, 0, 0, on_added, ctx);
  }
  err_free(err);
}

/* ----------------------------------------------------------------------------
 * Entries in (the driver's RX task), peers gone, reads not read any more
 * ------------------------------------------------------------------------- */

// An entry from a board, [hdr PUSH, MODULE_ID_WATCH][id][status][data]: the board is its src.
static void on_entry(data_matrix_port_t* self, data_matrix_port_id_t from, uint8_t flags, const uint8_t* packet,
                     uint32_t size) {
  (void)self;
  (void)from;
  (void)flags;
  cmd_hdr_t hdr;
  if (size < CMD_HEAD + ENTRY_HEAD) {
    return;
  }
  memcpy(&hdr, packet, CMD_HEAD);
  const uint8_t src = hdr.src;
  const uint8_t* data = packet + CMD_HEAD;
  const uint32_t len = size - CMD_HEAD;
  if (data[0] == 0u || data[0] > CONFIG_SYS_WATCH_CLIENT_MAX) {
    return;
  }
  slot_t* s = &s_cl.slots[data[0] - 1u];
  const uint8_t status = data[1];
  const uint32_t n = len - ENTRY_HEAD;
  xSemaphoreTake(sys_watch_client_mutex, portMAX_DELAY);
  if (s->state == SLOT_ACTIVE && s->board == src) {  // B3: only after ADD's OK
    s->have = status != ROUTER_STATUS_OK || n <= SYS_WATCH_COPY_MAX;  // a longer answer is not kept
    s->status = status;
    s->len = (uint8_t)(n < ENTRY_MAX ? n : ENTRY_MAX);
    s->entry_ms = now_ms();
    memcpy(s->data, data + ENTRY_HEAD, s->len);
  }
  xSemaphoreGive(sys_watch_client_mutex);
}

data_matrix_port_t sys_watch_port = {.id = DATA_MATRIX_PORT(DATA_MATRIX_PORT_MODULE, MODULE_ID_WATCH), .in = on_entry};

// A board is gone, and its watch with it: the next read adds it again.
static void on_gone(uint8_t peer, uint8_t safety) {
  (void)safety;
  xSemaphoreTake(sys_watch_client_mutex, portMAX_DELAY);
  FOR_EACH_SLOT(s) {
    if (s->state != SLOT_FREE && (peer == CMD_SRC_ALL || s->board == peer)) {
      s->state = SLOT_ADDING;
      s->gen++;
      s->add_ms = now_ms() - PERIOD_MS;
    }
  }
  xSemaphoreGive(sys_watch_client_mutex);
}

// A session ended (SESSION_EV_GONE): the peer is the event's source.
static err_h on_gone_event(const sys_event_t* event, void* ctx) {
  (void)ctx;
  on_gone((uint8_t)event->key.source, ((const evt_data_SESSION_EV_GONE_t*)event->data)->safety);
  return NULL;
}

// A read nobody read for CONFIG_SYS_WATCH_IDLE_MS: its slot is free and its watch removed from its board.
static void on_idle_tick(void* arg) {
  (void)arg;
  FOR_EACH_SLOT(s) {
    xSemaphoreTake(sys_watch_client_mutex, portMAX_DELAY);
    const bool idle = s->state != SLOT_FREE && now_ms() - s->read_ms > CONFIG_SYS_WATCH_IDLE_MS;
    const uint8_t board = s->board;
    if (idle) {
      s->state = SLOT_FREE;
      s->gen++;
    }
    xSemaphoreGive(sys_watch_client_mutex);
    if (idle) {
      const uint8_t req[] = {MODULE_ID_WATCH, CMD_DIR_WRITE, WATCH_OP_REMOVE, (uint8_t)(s - s_cl.slots + 1)};
      data_matrix_port_id_t port = 0;
      err_h err = sys_session_port(board, MODULE_ID_ROUTER, &port);
      err_free(err != NULL ? err : router_request(port, (cmd_in_t){req, sizeof(req)}, 0, 0, NULL, NULL));
    }
  }
}

/* ----------------------------------------------------------------------------
 * Read and stale
 * ------------------------------------------------------------------------- */

err_h sys_watch_read(uint8_t board, cmd_in_t read, void* buf, uint32_t cap, uint32_t* len) {
  ERR_CHECK(board >= CMD_SRC_BOARD_FIRST && board <= CMD_SRC_BOARD_LAST && board != board_id() &&
                read.len >= 2u && read.len <= SYS_WATCH_READ_MAX,
            ERR_WATCH_BAD_ARG, .id = 0);
  ERR_CHECK(read.data[1] == CMD_DIR_READ, ERR_WATCH_NOT_READ, .id = 0);

  const uint32_t now = now_ms();
  slot_t* at = NULL;
  slot_t got = {0};
  bool add = false;
  xSemaphoreTake(sys_watch_client_mutex, portMAX_DELAY);
  FOR_EACH_SLOT(s) {
    const bool same = s->board == board && s->req_len == read.len && memcmp(s->req, read.data, read.len) == 0;
    if (s->state != SLOT_FREE && same) {
      at = s;
      break;
    }
    if (s->state == SLOT_FREE && at == NULL) {
      at = s;
    }
  }
  if (at != NULL) {
    if (at->state == SLOT_FREE) {
      *at = (slot_t){.state = SLOT_ADDING, .board = board, .req_len = (uint8_t)read.len, .gen = (uint8_t)(at->gen + 1u),
                     .add_ms = now - PERIOD_MS};
      memcpy(at->req, read.data, read.len);
    }
    at->read_ms = now;
    add = at->state == SLOT_ADDING && now - at->add_ms >= PERIOD_MS;
    if (add) {
      at->add_ms = now;
    }
    got = *at;
  }
  xSemaphoreGive(sys_watch_client_mutex);
  ERR_CHECK(at != NULL, ERR_WATCH_FULL);
  if (add) {
    send_add((uint8_t)(at - s_cl.slots), got.gen, board, read);
  }

  ERR_CHECK(got.state == SLOT_ACTIVE && got.have, ERR_WATCH_NO_VALUE, .board = board);
  const uint32_t age = now - got.entry_ms;
  ERR_CHECK(age <= MAX_AGE_MS, ERR_WATCH_STALE, .board = board,
            .age_ms = (uint16_t)(age > UINT16_MAX ? UINT16_MAX : age));
  if (got.status != ROUTER_STATUS_OK) {  // that board's chain under ERR_REMOTE
    err_h chain = NULL;
    err_free(err_record_parse(got.data, got.len, &chain));
    return ERR_WRAP(chain, ERR_REMOTE, .board = board, .seq = 0);
  }
  *len = got.len;
  ERR_CHECK(got.len <= cap, ERR_CMD_BAD_LEN, .got = got.len, .need = (uint16_t)cap);
  memcpy(buf, got.data, got.len);
  return NULL;
}

/* A write went to `board`: what its cached reads hold is from before it. Their watches are added again: ADD's OK
 * comes after the write's answer, so every entry taken from then on was read after the write, and the watch sends its
 * first read at once. */
void sys_watch_stale(uint8_t board, cmd_in_t write, uint32_t target_len) {
  if (write.len < 2u + target_len) {
    return;
  }
  FOR_EACH_SLOT(s) {
    uint8_t req[SYS_WATCH_READ_MAX];
    xSemaphoreTake(sys_watch_client_mutex, portMAX_DELAY);
    const bool hit = s->state == SLOT_ACTIVE && s->board == board && s->req[0] == write.data[0] &&
                     s->req_len >= 2u + target_len && memcmp(s->req + 2u, write.data + 2u, target_len) == 0;
    if (hit) {
      s->state = SLOT_ADDING;
      s->gen++;
      s->add_ms = now_ms();
      memcpy(req, s->req, s->req_len);
    }
    const uint8_t gen = s->gen;
    const uint8_t req_len = s->req_len;
    xSemaphoreGive(sys_watch_client_mutex);
    if (hit) {
      send_add((uint8_t)(s - s_cl.slots), gen, board, (cmd_in_t){req, req_len});
    }
  }
}

err_h sys_watch_client_init(void) {
  board_timer_init(&s_cl.timer, on_idle_tick, NULL);
  board_timer_start(&s_cl.timer, IDLE_TICK_MS);
  uint16_t handle = 0;
  ERR_TRY(sys_event_subscribe(SESSION_GONE_FILTER, on_gone_event, NULL, SYS_EVENT_RUN_INLINE, &handle));
  return data_matrix_register(&sys_watch_port);
}
