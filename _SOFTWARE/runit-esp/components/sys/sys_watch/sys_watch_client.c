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
  uint8_t board;
  uint8_t req_len;
  uint8_t gen;       // changes when the slot starts again: an ADD's answer from before is dropped
  uint16_t len;       // bytes of the entry: only SYS_WATCH_COPY_MAX of them are kept, a longer one is ERR_CMD_OUT_FULL
  uint32_t entry_ms;  // when the entry came, or ADD's OK while none has: what the slot holds is that old
  uint32_t read_ms;   // the last sys_watch_read of it
  uint32_t add_ms;    // the last ADD sent
  uint8_t req[SYS_WATCH_READ_MAX];
  uint8_t data[SYS_WATCH_COPY_MAX];
} slot_t;

U_MUTEX_DEFINE(sys_watch_client_mutex);  // the slots: copies and a read's result, never held over a send

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
  slot_t* s = &s_cl.slots[SYS_WATCH_CTX_SLOT(ctx)];
  xSemaphoreTake(sys_watch_client_mutex, portMAX_DELAY);
  if (s->state == SLOT_ADDING && s->gen == SYS_WATCH_CTX_GEN(ctx) && status == ROUTER_STATUS_OK) {
    s->state = SLOT_ACTIVE;
    s->have = false;
    s->entry_ms = now_ms();  // no entry within MAX_AGE_MS of it: the entries do not reach this board, ADD again
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
  err_free(sys_session_request(board, (cmd_in_t){req, 3u + sizeof(head) + read.len}, 0, 0, on_added,
                               SYS_WATCH_CTX(slot, gen)));
}

/* ----------------------------------------------------------------------------
 * Entries in (the driver's RX task), peers gone, reads not read any more
 * ------------------------------------------------------------------------- */

// An entry from a board, [hdr PUSH, MODULE_ID_WATCH][owner][id][status][data]: this board's (`owner`) only; the
// board is its src.
static void on_entry(data_matrix_port_t* self, data_matrix_port_id_t from, uint8_t flags, const uint8_t* packet,
                     uint32_t size) {
  (void)self;
  (void)from;
  (void)flags;
  cmd_hdr_t hdr;
  if (size < CMD_HEAD + SYS_WATCH_ENTRY_HEAD) {
    return;
  }
  memcpy(&hdr, packet, CMD_HEAD);
  const uint8_t src = hdr.src;
  const uint8_t* data = packet + CMD_HEAD;
  const uint32_t len = size - CMD_HEAD;
  if (data[0] != board_id() || data[1] == 0u || data[1] > CONFIG_SYS_WATCH_CLIENT_MAX) {
    return;  // another's entry, or no watch of this board
  }
  slot_t* s = &s_cl.slots[data[1] - 1u];
  const uint8_t status = data[2];
  const uint32_t n = len - SYS_WATCH_ENTRY_HEAD;
  xSemaphoreTake(sys_watch_client_mutex, portMAX_DELAY);
  if (s->state == SLOT_ACTIVE && s->board == src) {  // B3: only after ADD's OK
    s->have = true;
    s->status = status;
    s->len = (uint16_t)n;
    s->entry_ms = now_ms();
    memcpy(s->data, data + SYS_WATCH_ENTRY_HEAD, n < SYS_WATCH_COPY_MAX ? n : SYS_WATCH_COPY_MAX);
  }
  xSemaphoreGive(sys_watch_client_mutex);
}

data_matrix_port_t sys_watch_port = {.id = DATA_MATRIX_PORT(DATA_MATRIX_PORT_MODULE, MODULE_ID_WATCH), .in = on_entry};

// A session ended (SESSION_EV_GONE, the peer is the event's source): the board's watches are gone with it, the next
// read adds them again.
static err_h on_gone(const sys_event_t* event, void* ctx) {
  (void)ctx;
  const uint8_t peer = (uint8_t)event->key.source;
  xSemaphoreTake(sys_watch_client_mutex, portMAX_DELAY);
  FOR_EACH_SLOT(s) {
    if (s->state != SLOT_FREE && (peer == CMD_SRC_ALL || s->board == peer)) {
      s->state = SLOT_ADDING;
      s->gen++;
      s->add_ms = now_ms() - PERIOD_MS;
    }
  }
  xSemaphoreGive(sys_watch_client_mutex);
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
      err_free(sys_session_request(board, (cmd_in_t){req, sizeof(req)}, 0, 0, NULL, NULL));
    }
  }
}

/* ----------------------------------------------------------------------------
 * Read and stale
 * ------------------------------------------------------------------------- */

err_h sys_watch_read(uint8_t board, cmd_in_t read, void* buf, uint32_t cap, uint32_t* len) {
  ERR_CHECK(board >= CMD_SRC_BOARD_FIRST && board <= CMD_SRC_BOARD_LAST && board != board_id() &&
                read.len >= 2u && read.len <= SYS_WATCH_READ_MAX,
            ERR_BAD_ARG);
  ERR_CHECK(read.data[1] == CMD_DIR_READ, ERR_WATCH_NOT_READ, .id = 0);

  const uint32_t now = now_ms();
  slot_t* at = NULL;
  bool add = false;
  uint8_t gen = 0;
  err_h err = NULL;
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
    // An ACTIVE slot with nothing as new as MAX_AGE_MS (no entry since ADD's OK, or the last too old): the watch is
    // gone on its board, or its entries do not reach this one. It is added again; this read says what it held.
    const uint32_t age = now - at->entry_ms;
    const bool old = at->state == SLOT_ACTIVE && age > MAX_AGE_MS;
    if (old) {
      err = at->have ? ERR_NEW(ERR_WATCH_STALE, .board = board, .age_ms = (uint16_t)(age > UINT16_MAX ? UINT16_MAX : age))
                     : ERR_NEW(ERR_WATCH_NO_VALUE, .board = board);
      at->state = SLOT_ADDING;
      at->gen++;
      at->add_ms = now - PERIOD_MS;
    } else if (at->state != SLOT_ACTIVE || !at->have) {
      err = ERR_NEW(ERR_WATCH_NO_VALUE, .board = board);
    } else if (at->status != ROUTER_STATUS_OK) {  // that board's chain under ERR_REMOTE
      err = router_remote_chain(at->data, at->len < SYS_WATCH_COPY_MAX ? at->len : SYS_WATCH_COPY_MAX, board, 0);
    } else if (at->len > SYS_WATCH_COPY_MAX) {
      err = ERR_NEW(ERR_CMD_OUT_FULL, .need = at->len, .cap = (uint16_t)SYS_WATCH_COPY_MAX);
    } else if (at->len > cap) {
      *len = at->len;
      err = ERR_NEW(ERR_CMD_BAD_LEN, .got = at->len, .need = (uint16_t)cap);
    } else {
      *len = at->len;
      memcpy(buf, at->data, at->len);
    }
    add = at->state == SLOT_ADDING && now - at->add_ms >= PERIOD_MS;
    if (add) {
      at->add_ms = now;
    }
    gen = at->gen;
  }
  xSemaphoreGive(sys_watch_client_mutex);
  ERR_CHECK(at != NULL, ERR_FULL, .max = CONFIG_SYS_WATCH_CLIENT_MAX);
  if (add) {
    send_add((uint8_t)(at - s_cl.slots), gen, board, read);
  }
  return err;
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
  ERR_TRY(sys_event_subscribe(SESSION_GONE_FILTER, on_gone, NULL, SYS_EVENT_RUN_INLINE, &handle));
  return data_matrix_register(&sys_watch_port);
}
