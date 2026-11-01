#include "sys_remote.h"

#include <string.h>

#include "router.h"
#include "sdkconfig.h"
#include "sys_data_connector.h"
#include "sys_device.h"
#include "sys_event.h"
#include "sys_session.h"
#include "sys_watch.h"
#include "utils.h"

#define MIRROR_FIRST 0xE0u  // the connector ids of the events mirrors: one per board, from here
#define ANSWER_MAX   16u    // a write's answer has no data; a little room for what a device answers anyway

typedef struct {
  uint8_t board;
  uint8_t devices;  // remote devices on it: the mirror lives while there is one
} board_t;

static board_t s_boards[CONFIG_SYS_REMOTE_BOARDS];  // under the registry lock: added and removed run under it

/* ----------------------------------------------------------------------------
 * Events: a remote device's events are published here as its own
 * ------------------------------------------------------------------------- */

// A frame of the events connector of a board: [ROUTER_KEY_EVENT][NOTIFY][sys_event_t], on the connector task.
static void on_events(void* ctx, uint8_t board, const uint8_t* payload, uint32_t len, uint16_t missed) {
  (void)ctx;
  (void)missed;
  sys_event_t ev;
  if (len != 2 + sizeof(ev) || payload[0] != ROUTER_KEY_EVENT || payload[1] != SYS_EVENT_OP_NOTIFY) {
    return;
  }
  memcpy(&ev, payload + 2, sizeof(ev));
  const uint16_t local = sys_device_find_remote(board, ev.source);
  if (local == SYS_DEVICE_ID_NONE) {
    return;  // not a device this board has
  }
  ev.source = local;
  err_report(sys_event_publish_deferred(&ev));
}

static board_t* find_board(uint8_t board) {
  for (uint8_t i = 0; i < CONFIG_SYS_REMOTE_BOARDS; i++) {
    if (s_boards[i].devices > 0 && s_boards[i].board == board) {
      return &s_boards[i];
    }
  }
  return NULL;
}

static uint8_t mirror_id(const board_t* b) {
  return (uint8_t)(MIRROR_FIRST + (b - s_boards));
}

// The first remote device of a board makes the mirror of its events; the last one ends it.
static void on_added(uint8_t board) {
  board_t* b = find_board(board);
  if (b != NULL) {
    b->devices++;
    return;
  }
  for (uint8_t i = 0; i < CONFIG_SYS_REMOTE_BOARDS; i++) {
    if (s_boards[i].devices == 0) {
      s_boards[i] = (board_t){.board = board, .devices = 1};
      const uint8_t id = mirror_id(&s_boards[i]);
      err_report(sys_data_connector_create(id, 0));
      err_report(sys_data_connector_mirror(id, board, SYS_DATA_CONNECTOR_EVENTS, on_events, NULL));
      return;
    }
  }
  // No slot: the board's remote devices work, their events are not mirrored.
  err_report(ERR_NEW(ERR_CONN_FULL, .connector = 0));
}

static void on_removed(uint8_t board) {
  board_t* b = find_board(board);
  if (b == NULL || --b->devices > 0) {
    return;
  }
  const uint8_t id = mirror_id(b);
  err_report(sys_data_connector_unmirror(id));
  err_report(sys_data_connector_remove(id));
}

/* ----------------------------------------------------------------------------
 * Calls
 * ------------------------------------------------------------------------- */

// Reaching a board failed because it is gone: the device is down. A busy way ("later") and the board's own answer stay.
static err_h down(err_h err, const sys_device_remote_t* where) {
  if (err == NULL || (err->tag != ERR_SESSION_NO_ROUTE && err->tag != ERR_SESSION_PEER_DOWN)) {
    return err;
  }
  err_free(err);
  return ERR_NEW(ERR_DEV_REMOTE_DOWN, .id = sys_device_find_remote(where->board, where->remote_id),
                 .board = where->board);
}

// The answer of an async write, on the RX task: a stale copy is refreshed, a refusal is the device's error.
static err_h on_write_done(void* ctx, const err_cmd_t* cmd, uint8_t status, uint8_t* data, uint32_t len) {
  const uint8_t board = (uint8_t)(uintptr_t)ctx;
  if (status == ROUTER_STATUS_OK) {
    sys_watch_stale(board, (cmd_in_t){cmd->req, cmd->req_len}, 2);
    return NULL;
  }
  uint16_t remote_id = 0;
  if (cmd->req_len >= 4) {
    memcpy(&remote_id, cmd->req + 2, sizeof(remote_id));
  }
  err_h chain = NULL;
  err_free(err_record_parse(data, len, &chain));
  err_report(ERR_WRAP(chain, ERR_DEV_ID, .id = sys_device_find_remote(board, remote_id)));
  return NULL;
}

static err_h call(const sys_device_remote_t* where, cmd_in_t req, uint8_t* out, uint32_t cap, uint32_t* out_len) {
  const bool blocking = where->mode == SYS_DEVICE_REMOTE_BLOCKING;
  const uint16_t local = sys_device_find_remote(where->board, where->remote_id);
  ERR_CHECK(!blocking || sys_session_wired(where->board), ERR_DEV_REMOTE_NOT_WIRED, .id = local, .board = where->board);
  *out_len = 0;
  if (req.data[1] == CMD_DIR_READ) {
    return down(sys_watch_read(where->board, req, where->max_age_ms, blocking, out, cap, out_len), where);
  }
  if (!blocking) {
    return down(sys_session_call(where->board, req, 0, 0, on_write_done, (void*)(uintptr_t)where->board), where);
  }
  uint8_t answer[ANSWER_MAX];
  uint32_t answer_len = 0;
  err_h err = sys_session_call_wait(where->board, req, 0, 0, answer, sizeof(answer), &answer_len);
  if (err == NULL) {
    sys_watch_stale(where->board, req, 2);
  }
  return down(err, where);
}

static const sys_device_remote_ops_t k_ops = {.call = call, .added = on_added, .removed = on_removed};

err_h sys_remote_init(void) {
  sys_device_set_remote(&k_ops);
  return NULL;
}
