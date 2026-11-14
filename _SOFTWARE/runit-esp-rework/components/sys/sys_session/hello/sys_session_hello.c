#include <string.h>

#include "sys_session.h"
#include "sys_session_internal.h"

/* The handshake (SYS_SESSION.MD "A session's life"): the hellos both ways, and NO_SESSION when one side has no
 * session. */

static uint32_t s_boot_id;  // random per boot, taken at the first hello: a new one in a hello means the board restarted

// Whether an id is one of a peer's kind: a board 0..LAST but not this board's, a client APP..CLIENT_LAST.
static bool id_of_kind(uint8_t role, uint8_t id) {
  if (role == SYS_SESSION_CLIENT) {
    return id >= SYS_SESSION_ID_APP && id <= SYS_SESSION_ID_CLIENT_LAST;
  }
  return id <= SYS_SESSION_ID_LAST && id != sys_session_get_board_id();
}

err_h sys_session_hello_send(int32_t at) {
  if (s_boot_id == 0) {
    s_boot_id = sys_session_board_boot_id() | 1u;  // never 0: 0 is "not taken yet"
  }
  const sys_session_hello_t hello = {
      .id = sys_session_get_board_id(),
      .boot_id = s_boot_id,
      .build_id = sys_session_board_build_id(),
      .timeout_ms = (uint16_t)sys_session_timeout(at),
  };
  return sys_session_send_control(at, SYS_SESSION_HELLO, &hello, sizeof(hello));
}

/* The peer's hello: the session opens with it. A board's hello on an UP way (the console's UART wired to another
 * board's link) makes that board this board's master: its other sessions end, and none opens while it lasts. */
static void on_hello(int32_t at, const uint8_t* body, uint32_t len) {
  sys_session_hello_t hello;
  if (len < sizeof(hello)) {
    return;
  }
  memcpy(&hello, body, sizeof(hello));
  if (!sys_session_admits(at)) {
    return;  // a board with a master opens no other session: the peer asks again, and opens once the master is gone
  }
  session_slot_t slot = sys_session_slot(at);
  if (session_is_open(&slot) && slot.hello.boot_id != hello.boot_id) {
    sys_session_end(at);  // the peer restarted: what it had here is gone
    slot = sys_session_slot(at);
  }
  slot.hello.id = hello.id;  // the role this hello gives the way
  const uint8_t role = sys_session_role(&slot);
  if (!id_of_kind(role, hello.id)) {
    err_report(ERR_NEW(ERR_SESSION_ID_TAKEN, .board = hello.id));
    return;
  }
  if (role == SYS_SESSION_MASTER) {
    sys_session_end_others(at);  // a board with a master has no other session: its clients and slaves are gone
  }
  uint16_t timeout_ms = hello.timeout_ms;
  if (timeout_ms < SYS_SESSION_TIMEOUT_MIN_MS) {
    timeout_ms = SYS_SESSION_TIMEOUT_MIN_MS;
  } else if (timeout_ms > SYS_SESSION_TIMEOUT_MAX_MS) {
    timeout_ms = SYS_SESSION_TIMEOUT_MAX_MS;
  }
  const sys_session_hello_state_t peer = {
      .id = hello.id, .boot_id = hello.boot_id, .build_id = hello.build_id, .timeout_ms = timeout_ms};
  bool was_open = false;
  if (!sys_session_open(at, &peer, &was_open)) {
    err_report(ERR_NEW(ERR_SESSION_ID_TAKEN, .board = hello.id));
    return;
  }
  // A client is answered every hello (it may have missed ours: a BLE client not yet subscribed); a board only the
  // first one it gets.
  if (role == SYS_SESSION_CLIENT || !was_open) {
    err_report(sys_session_hello_send(at));
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
