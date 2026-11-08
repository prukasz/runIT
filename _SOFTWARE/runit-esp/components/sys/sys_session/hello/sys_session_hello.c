#include "sys_session_hello.h"

#include <string.h>

#include "esp_random.h"
#include "sys_session.h"
#include "sys_session_internal.h"

/* The handshake (SYS_SESSION.MD "A session's life"): the hellos both ways, and NO_SESSION when one side has no
 * session. */

static uint32_t s_boot_id;  // random per boot: a new one in a hello means the board restarted

// Whether an id is one of a peer's kind: a board 0..LAST but not this board's, a client APP..CLIENT_LAST.
static bool id_of_kind(uint8_t kind, uint8_t id) {
  if (kind == SYS_SESSION_CLIENT) {
    return id >= SYS_SESSION_ID_APP && id <= SYS_SESSION_ID_CLIENT_LAST;
  }
  return id <= SYS_SESSION_ID_LAST && id != sys_session_get_board_id();
}

err_h sys_session_hello_init(void) {
  s_boot_id = esp_random();
  return NULL;
}

err_h sys_session_hello_send(int32_t at) {
  sys_session_hello_t hello = {
      .version = SYS_SESSION_VERSION,
      .id = sys_session_get_board_id(),
      .boot_id = s_boot_id,
      .timeout_ms = (uint16_t)sys_session_timeout(at),
  };
  return sys_session_send_control(at, SYS_SESSION_HELLO, (const uint8_t*)&hello, sizeof(hello));
}

/* The peer's hello: the session opens with it. A board's hello on a CLIENT way (the console's UART wired to another
 * board's link) makes that board this board's master: its other sessions end, and none opens while it lasts. */
static void on_hello(int32_t at, const uint8_t* body, uint32_t len) {
  sys_session_hello_t hello;
  if (len < sizeof(hello)) {
    return;
  }
  memcpy(&hello, body, sizeof(hello));
  if (hello.version != SYS_SESSION_VERSION) {
    err_report(ERR_NEW(ERR_SESSION_VERSION, .got = hello.version, .mine = SYS_SESSION_VERSION));
    return;
  }
  if (!sys_session_admits(at)) {
    return;  // a board with a master opens no other session: the peer asks again, and opens once the master is gone
  }
  session_slot_t slot = sys_session_slot(at);
  if (session_is_open(&slot) && slot.hello.boot_id != hello.boot_id) {
    sys_session_end(at);  // the peer restarted: what it had here is gone
    slot = sys_session_slot(at);
  }
  const bool master = slot.peer == SYS_SESSION_CLIENT && hello.id <= SYS_SESSION_ID_LAST;
  const uint8_t kind = master ? SYS_SESSION_MASTER : slot.peer;
  if (!id_of_kind(kind, hello.id)) {
    err_report(ERR_NEW(ERR_SESSION_ID_TAKEN, .board = hello.id));
    return;
  }
  if (master && !sys_session_take_master(at)) {
    return;
  }
  const sys_session_hello_state_t peer = {.id = hello.id, .boot_id = hello.boot_id, .timeout_ms = hello.timeout_ms};
  bool was_open = false;
  if (!sys_session_open(at, &peer, &was_open)) {
    sys_session_end(at);  // a master's way not opened is a CLIENT way again
    err_report(ERR_NEW(ERR_SESSION_ID_TAKEN, .board = hello.id));
    return;
  }
  // A client is answered every hello (it may have missed ours: a BLE client not yet subscribed); a board only the first
  // one it gets.
  if (kind == SYS_SESSION_CLIENT || !was_open) {
    err_report(sys_session_hello_send(at));
  }
  if (!was_open) {
    sys_session_reached(hello.id);
  }
}

void sys_session_hello_control(int32_t at, const uint8_t* body, uint32_t len) {
  if (len == 0) {
    return;
  }
  switch (body[0]) {
    case SYS_SESSION_HELLO:
      on_hello(at, body + 1, len - 1);
      break;
    case SYS_SESSION_NO_SESSION:
      sys_session_end(at);
      err_report(sys_session_hello_send(at));
      break;
    default:  // not the handshake's
      break;
  }
}

void sys_session_hello_start_over(int32_t at, uint8_t src) {
  err_report(ERR_NEW(ERR_SESSION_NONE, .src = src));
  err_free(sys_session_send_control(at, SYS_SESSION_NO_SESSION, NULL, 0));
  err_free(sys_session_hello_send(at));
}
