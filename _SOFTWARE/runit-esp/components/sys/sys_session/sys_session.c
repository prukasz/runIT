#include "sys_session.h"

#include <string.h>

#include "board_id.h"
#include "board_timer.h"
#include "router_queue.h"
#include "sdkconfig.h"
#include "sys_data_provider.h"
#include "sys_data_provider_events.h"
#include "sys_event.h"
#include "sys_session_events.h"
#include "sys_session_internal.h"
#include "utils.h"

ERR_MODULE_DEFINE(MODULE_ID_SESSION, ERR_SYS_SESSION_MAP)

#define HELLO_RETRY_MS 1000u  // a session not open says hello again this often
#define PROVIDER_MAX   0x7Fu  // a provider's id in a session's port name (DATA_MATRIX_PORT_SESSION | provider)

_Static_assert(SYS_SESSION_DEADMAN_MAX_MS + BOARD_TIMER_STEP_MS <= UINT16_MAX, "a session's silence and ping in 16 bits");

typedef struct {
  bool used;
  bool open;                 // its peer's hello came
  bool safety;               // its end is a safety event (sys_system's safe state)
  uint8_t role;              // SYS_SESSION_SLAVE, SYS_SESSION_MASTER or 0: a client
  uint8_t provider;
  uint8_t peer;              // its id, the src of its hello (open)
  uint8_t port_count;        // its sub-ports
  uint16_t deadman_ms;       // the silence that ends it, 0: never; a third of it the keepalive
  uint16_t silent_ms;        // since its last frame
  uint16_t ping_ms;          // since this board's last ping (not open: its last hello)
  uint16_t push_seq[2];      // the next PUSH seq to its peer: [normal, urgent]
  sys_session_hello_t hello; // the peer's
  data_matrix_port_t line;   // linked both ways with its provider
  data_matrix_port_t ports[CONFIG_SYS_SESSION_PORTS];  // its sub-ports, named by their module
} session_t;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static struct {
  session_t sessions[CONFIG_SYS_SESSION_MAX];
  board_timer_t timer;
  const sys_session_sub_t* subs[2];  // the rows of a slave's session, of a master's
  uint8_t sub_count[2];
} s_state;

#define FOR_EACH_SESSION(s) for (session_t* s = s_state.sessions; s < s_state.sessions + CONFIG_SYS_SESSION_MAX; s++)

/* ----------------------------------------------------------------------------
 * The sessions' state (under s_lock)
 * ------------------------------------------------------------------------- */

static session_t* open_with(uint8_t peer) {
  FOR_EACH_SESSION(s) {
    if (s->used && s->open && s->peer == peer) {
      return s;
    }
  }
  return NULL;
}

static session_t* on_provider(uint8_t provider) {
  FOR_EACH_SESSION(s) {
    if (s->used && s->provider == provider) {
      return s;
    }
  }
  return NULL;
}

// Its sub-port of `module`, NULL when it has none.
static data_matrix_port_t* sub_port(session_t* s, uint8_t module) {
  for (uint32_t i = 0; i < s->port_count; i++) {
    if ((uint8_t)s->ports[i].id == module) {
      return &s->ports[i];
    }
  }
  return NULL;
}

// The session a port of it belongs to, by the provider in the port's name. Under s_lock.
static session_t* of_port(data_matrix_port_id_t port) {
  return (port >> 8) & DATA_MATRIX_PORT_SESSION ? on_provider((uint8_t)((port >> 8) & PROVIDER_MAX)) : NULL;
}

/* ----------------------------------------------------------------------------
 * Queries
 * ------------------------------------------------------------------------- */

err_h sys_session_port(uint8_t peer, uint8_t module, data_matrix_port_id_t* port) {
  portENTER_CRITICAL(&s_lock);
  session_t* s = open_with(peer);
  const data_matrix_port_t* p = s != NULL ? sub_port(s, module) : NULL;
  if (p != NULL) {
    *port = p->id;
  }
  portEXIT_CRITICAL(&s_lock);
  ERR_CHECK(p != NULL, ERR_SESSION_NO_ROUTE, .dst = peer);
  return NULL;
}

err_h sys_session_request(uint8_t peer, cmd_in_t req, uint8_t flags, uint32_t timeout_ms, router_done_f cb,
                          void* ctx) {
  data_matrix_port_id_t port = 0;
  ERR_TRY(sys_session_port(peer, MODULE_ID_ROUTER, &port));
  return router_request(port, req, flags, timeout_ms, cb, ctx);
}

err_h sys_session_request_wait(uint8_t peer, cmd_in_t req, uint8_t flags, uint32_t timeout_ms, void* buf,
                               uint32_t cap, uint32_t* len) {
  data_matrix_port_id_t port = 0;
  ERR_TRY(sys_session_port(peer, MODULE_ID_ROUTER, &port));
  return router_request_wait(port, req, flags, timeout_ms, buf, cap, len);
}

bool sys_session_peer(data_matrix_port_id_t port, uint8_t* peer) {
  portENTER_CRITICAL(&s_lock);
  const session_t* s = of_port(port);
  const bool open = s != NULL && s->open;
  if (open) {
    *peer = s->peer;
  }
  portEXIT_CRITICAL(&s_lock);
  return open;
}

bool sys_session_wired(uint8_t board) {
  portENTER_CRITICAL(&s_lock);
  const session_t* s = open_with(board);
  const uint8_t provider = s != NULL ? s->provider : 0u;
  portEXIT_CRITICAL(&s_lock);
  return s != NULL && (sys_data_provider_state(provider) & SYS_DATA_PROVIDER_STATE_WIRED) != 0;
}

uint8_t sys_session_list(sys_session_info_t out[CONFIG_SYS_SESSION_MAX]) {
  uint8_t count = 0;
  portENTER_CRITICAL(&s_lock);
  FOR_EACH_SESSION(s) {
    if (s->used) {
      out[count++] = (sys_session_info_t){
          .provider = s->provider,
          .open = s->open,
          .peer_id = s->open ? s->peer : CMD_SRC_NONE,
          .safety = s->safety,
          .role = s->role,
          .build_id = s->open ? s->hello.build_id : 0,
          .deadman_ms = s->deadman_ms,
      };
    }
  }
  portEXIT_CRITICAL(&s_lock);
  return count;
}

/* ----------------------------------------------------------------------------
 * Frames onto the line: out of the session's port to its provider
 * ------------------------------------------------------------------------- */

// A control frame; one a full queue refuses is lost: the hello is said again, a ping follows.
static void send_control(session_t* s, uint8_t op, const void* data, uint32_t len) {
  uint8_t frame[CMD_HEAD + 1u + sizeof(sys_session_hello_t)];
  const cmd_hdr_t hdr = {.type = CMD_TYPE(CMD_CONTROL), .src = board_id(), .key = MODULE_ID_SESSION};
  memcpy(frame, &hdr, CMD_HEAD);
  frame[CMD_HEAD] = op;
  if (len > 0) {
    memcpy(frame + CMD_HEAD + 1u, data, len);
  }
  data_matrix_send(&s->line, CMD_SEND_NOWAIT, frame, CMD_HEAD + 1u + len);
}

static void send_hello(session_t* s) {
  const sys_session_hello_t me = {.boot_id = board_boot_id(), .build_id = board_build_id()};
  send_control(s, SYS_SESSION_HELLO, &me, sizeof(me));
}

err_h sys_session_broadcast(cmd_in_t req, uint8_t flags) {
  ERR_CHECK(req.data != NULL && req.len >= 2u && req.len <= CMD_DATA_MAX + 1u, ERR_BAD_ARG);
  const cmd_hdr_t hdr = {
      .type = (uint8_t)(CMD_TYPE(CMD_REQUEST) | ROUTER_FLAG_NOREPLY | (flags & ROUTER_FLAG_URGENT)),
      .src = board_id(),
      .key = req.data[0],
  };
  uint8_t frame[CMD_HEAD + req.len - 1u];
  memcpy(frame, &hdr, CMD_HEAD);
  memcpy(frame + CMD_HEAD, req.data + 1, req.len - 1u);
  FOR_EACH_SESSION(s) {
    portENTER_CRITICAL(&s_lock);
    const bool open = s->used && s->open;
    portEXIT_CRITICAL(&s_lock);
    if (open) {
      data_matrix_send(&s->line, CMD_SEND_NOWAIT | (flags & CMD_SEND_URGENT), frame, sizeof(frame));
    }
  }
  return NULL;
}

/* ----------------------------------------------------------------------------
 * A session's end and its hello
 * ------------------------------------------------------------------------- */

static void end_session(session_t* s) {
  portENTER_CRITICAL(&s_lock);
  const bool was_open = s->open;
  const uint8_t peer = s->peer;
  const uint8_t safety = s->safety ? 1u : 0u;
  const uint8_t provider = s->provider;
  s->open = false;
  s->silent_ms = 0;
  portEXIT_CRITICAL(&s_lock);
  if (was_open) {
    router_request_fail_to(SYS_SESSION_PORT(provider, MODULE_ID_ROUTER));
    err_report(SYS_EVENT_PUBLISH(SESSION_EV_GONE, peer, 0, .safety = safety));
  }
}

// A frame on a session not open here: the peer says its hello again, and hears this board's.
static void start_over(session_t* s) {
  send_control(s, SYS_SESSION_NO_SESSION, NULL, 0);
  send_hello(s);
}

static void on_hello(session_t* s, uint8_t src, const uint8_t* body, uint32_t len) {
  sys_session_hello_t hello;
  if (len < sizeof(hello)) {
    return;
  }
  memcpy(&hello, body, sizeof(hello));
  const bool board = src <= CMD_SRC_BOARD_LAST;  // CMD_SRC_NONE too: a board with no id given
  const bool client = src >= CMD_SRC_CLIENT_FIRST && src <= CMD_SRC_CLIENT_LAST;

  portENTER_CRITICAL(&s_lock);
  const session_t* same = src != CMD_SRC_NONE ? open_with(src) : NULL;
  const bool bad = !(board || client) || (src != CMD_SRC_NONE && src == board_id());  // nobody's, or this board's
  const bool twice = same != NULL && same != s;                                      // another open session has it
  const bool restarted = s->open && (s->hello.boot_id != hello.boot_id || s->peer != src);
  portEXIT_CRITICAL(&s_lock);
  if (bad || twice) {
    err_report(ERR_NEW(ERR_SESSION_ID_TAKEN, .board = src));
    return;
  }
  if (restarted) {
    end_session(s);  // what it had here is gone
  }

  portENTER_CRITICAL(&s_lock);
  const bool was_open = s->open;
  s->open = true;
  s->peer = src;
  s->hello = hello;
  s->silent_ms = 0;
  portEXIT_CRITICAL(&s_lock);
  if (!was_open) {
    err_report(SYS_EVENT_PUBLISH(SESSION_EV_REACHED, src, 0));
  }
  // A client is answered every hello (it may have missed this board's); a board its first only, so two boards do not
  // answer each other for ever.
  if (client || !was_open) {
    send_hello(s);
  }
}

/* ----------------------------------------------------------------------------
 * The ports: the line in, the sub-ports in
 * ------------------------------------------------------------------------- */

// A frame from the provider: the session's own, or out of the sub-port of its module.
static void line_in(data_matrix_port_t* self, data_matrix_port_id_t from, uint8_t flags, const uint8_t* data,
                    uint32_t len) {
  (void)from;
  session_t* s = DATA_MATRIX_OWNER(self, session_t, line);
  if (len < CMD_HEAD) {
    return;
  }
  cmd_hdr_t hdr;
  memcpy(&hdr, data, CMD_HEAD);
  const uint8_t kind = CMD_KIND_OF(hdr.type);
  const uint8_t op = kind == CMD_CONTROL && len > CMD_HEAD ? data[CMD_HEAD] : 0u;
  if (op == SYS_SESSION_HELLO) {
    on_hello(s, hdr.src, data + CMD_HEAD + 1u, len - CMD_HEAD - 1u);
    return;
  }
  if (op == SYS_SESSION_NO_SESSION) {
    end_session(s);
    send_hello(s);
    return;
  }
  portENTER_CRITICAL(&s_lock);
  const bool used = s->used;
  const bool open = s->open;
  const uint8_t peer = s->peer;
  s->silent_ms = open ? 0u : s->silent_ms;  // every frame of the peer is its sign of life
  data_matrix_port_t* port = sub_port(s, kind == CMD_PUSH ? hdr.key : MODULE_ID_ROUTER);
  portEXIT_CRITICAL(&s_lock);
  if (!used) {
    return;
  }
  if (!open) {
    start_over(s);
    return;
  }
  // A command or an answer is the peer's own: one in another's name is dropped (a PUSH may be a board's behind it).
  const bool theirs = kind == CMD_PUSH || hdr.src == peer;
  if (kind != CMD_CONTROL && theirs && port != NULL) {
    data_matrix_send(port, flags, data, len);
  }
}

// A frame from a module for the peer: this board's id as src when it has none, a PUSH's seq of its class; onto the
// line.
static void sub_in(data_matrix_port_t* self, data_matrix_port_id_t from, uint8_t flags, const uint8_t* data,
                   uint32_t len) {
  (void)from;
  if (len < CMD_HEAD || len > CMD_FRAME_MAX) {
    return;
  }
  uint8_t frame[len];
  memcpy(frame, data, len);
  cmd_hdr_t hdr;
  memcpy(&hdr, frame, CMD_HEAD);
  const bool push = CMD_KIND_OF(hdr.type) == CMD_PUSH;
  portENTER_CRITICAL(&s_lock);
  session_t* s = of_port(self->id);
  const bool open = s != NULL && s->open;
  if (open && push) {
    hdr.seq = s->push_seq[hdr.type & ROUTER_FLAG_URGENT]++;
  }
  portEXIT_CRITICAL(&s_lock);
  if (!open) {
    return;
  }
  hdr.src = hdr.src != CMD_SRC_NONE ? hdr.src : board_id();
  memcpy(frame, &hdr, CMD_HEAD);
  // A PUSH never waits and never takes the room an answer or a control frame needs.
  data_matrix_send(&s->line, push ? (uint8_t)(flags | CMD_SEND_NOWAIT | CMD_SEND_SPARE) : flags, frame, len);
}

/* ----------------------------------------------------------------------------
 * The providers' events and the timer
 * ------------------------------------------------------------------------- */

static err_h link_provider(const session_t* s);

// A session stays attached whatever its provider does: DOWN and REMOVED end it (a removed provider's links are gone
// with its port), CREATED links its port with the provider made again, UP hears the hello.
static err_h on_provider_event(const sys_event_t* event, void* ctx) {
  (void)ctx;
  portENTER_CRITICAL(&s_lock);
  session_t* s = event->key.source <= PROVIDER_MAX ? on_provider((uint8_t)event->key.source) : NULL;
  portEXIT_CRITICAL(&s_lock);
  if (s == NULL) {
    return NULL;
  }
  if (event->key.tag == SYS_DATA_PROVIDER_EV_UP) {
    send_hello(s);
  } else if (event->key.tag == SYS_DATA_PROVIDER_EV_CREATED) {
    const uint8_t state = ((const evt_data_SYS_DATA_PROVIDER_EV_CREATED_t*)event->data)->state;
    ERR_CHECK(state & SYS_DATA_PROVIDER_STATE_FRAMED, ERR_SESSION_NOT_FRAMED);
    ERR_TRY(link_provider(s));
    if (state & SYS_DATA_PROVIDER_STATE_UP) {
      send_hello(s);
    }
  } else {
    end_session(s);  // DOWN, REMOVED
  }
  return NULL;
}

static void on_tick(void* arg) {
  (void)arg;
  FOR_EACH_SESSION(s) {
    bool end = false;
    bool ping = false;
    bool hello = false;
    portENTER_CRITICAL(&s_lock);
    if (s->used && s->open) {
      s->silent_ms = (uint16_t)(s->silent_ms + BOARD_TIMER_STEP_MS);
      s->ping_ms = (uint16_t)(s->ping_ms + BOARD_TIMER_STEP_MS);
      end = s->deadman_ms > 0 && s->silent_ms > s->deadman_ms;
      ping = s->deadman_ms > 0 && !end && s->ping_ms >= s->deadman_ms / 3u;
    } else if (s->used) {
      s->ping_ms = (uint16_t)(s->ping_ms + BOARD_TIMER_STEP_MS);
      hello = s->ping_ms >= HELLO_RETRY_MS;  // the peer on it may have started after this board
    }
    if (ping || hello) {
      s->ping_ms = 0;
    }
    portEXIT_CRITICAL(&s_lock);
    if (end) {
      end_session(s);
    } else if (ping) {
      send_control(s, SYS_SESSION_PING, NULL, 0);
    } else if (hello) {
      send_hello(s);
    }
  }
}

/* ----------------------------------------------------------------------------
 * Start, attach, detach, dead-man
 * ------------------------------------------------------------------------- */

// Before app_main: the provider events it follows, and its timer (pings, hellos, the dead-man).
__attribute__((constructor)) static void session_start(void) {
  static const uint32_t k_tags[] = {SYS_DATA_PROVIDER_EV_CREATED, SYS_DATA_PROVIDER_EV_REMOVED, SYS_DATA_PROVIDER_EV_UP,
                                    SYS_DATA_PROVIDER_EV_DOWN};
  for (uint32_t i = 0; i < ARRAY_LEN(k_tags); i++) {
    uint16_t handle = 0;
    const sys_event_key_t filter = {
        .tag = k_tags[i], .source = SYS_EVENT_ANY_SOURCE, .channel = SYS_EVENT_ANY_CHANNEL};
    err_boot(sys_event_subscribe(filter, on_provider_event, NULL, SYS_EVENT_RUN_INLINE, &handle));
  }
  board_timer_init(&s_state.timer, on_tick, NULL);
  board_timer_start(&s_state.timer, BOARD_TIMER_STEP_MS);
}

static bool deadman_ok(uint32_t ms) {
  return ms == 0 || (ms >= SYS_SESSION_DEADMAN_MIN_MS && ms <= SYS_SESSION_DEADMAN_MAX_MS);
}

// Takes a session's ports out of the data matrix (their links go with them).
static void unregister_ports(session_t* s) {
  err_free(data_matrix_unregister(&s->line));
  for (uint32_t i = 0; i < s->port_count; i++) {
    err_free(data_matrix_unregister(&s->ports[i]));
  }
}

// Links a session's port with its provider both ways.
static err_h link_provider(const session_t* s) {
  const data_matrix_port_id_t provider = DATA_MATRIX_PORT(DATA_MATRIX_PORT_PROVIDER, s->provider);
  ERR_TRY(data_matrix_connect(&(data_matrix_link_t){.from = provider, .to = s->line.id}));
  return data_matrix_connect(&(data_matrix_link_t){.from = s->line.id, .to = provider});
}

// Registers a session's ports and links its port with its provider.
static err_h register_ports(session_t* s) {
  ERR_TRY(data_matrix_register(&s->line));
  for (uint32_t i = 0; i < s->port_count; i++) {
    ERR_TRY(data_matrix_register(&s->ports[i]));
  }
  return link_provider(s);
}

err_h sys_session_set_boards(const sys_session_sub_t* slave, uint32_t slave_n, const sys_session_sub_t* master,
                             uint32_t master_n) {
  ERR_CHECK(slave_n <= CONFIG_SYS_SESSION_PORTS && master_n <= CONFIG_SYS_SESSION_PORTS, ERR_BAD_ARG);
  s_state.subs[0] = slave;
  s_state.sub_count[0] = (uint8_t)slave_n;
  s_state.subs[1] = master;
  s_state.sub_count[1] = (uint8_t)master_n;
  return NULL;
}

// The rows a board's session gets: its role's.
static const sys_session_sub_t* board_rows(uint8_t role, uint32_t* n) {
  const uint32_t at = role == SYS_SESSION_MASTER ? 1u : 0u;
  *n = s_state.sub_count[at];
  return s_state.subs[at];
}

// Links a board's session's sub-ports with its modules, as its rows say.
static err_h link_board(const session_t* s) {
  uint32_t n;
  const sys_session_sub_t* rows = board_rows(s->role, &n);
  for (uint32_t i = 0; i < n; i++) {
    const data_matrix_port_id_t sub = SYS_SESSION_PORT(s->provider, rows[i].module);
    const data_matrix_port_id_t module = DATA_MATRIX_PORT(DATA_MATRIX_PORT_MODULE, rows[i].module);
    if (rows[i].ways & SYS_SESSION_IN) {
      ERR_TRY(data_matrix_connect(&(data_matrix_link_t){.from = sub, .to = module}));
    }
    if (rows[i].ways & SYS_SESSION_OUT) {
      ERR_TRY(data_matrix_connect(&(data_matrix_link_t){.from = module, .to = sub}));
    }
  }
  return NULL;
}

err_h sys_session_attach(uint8_t provider, uint32_t deadman_ms, uint8_t flags, const uint8_t* modules, uint32_t n) {
  const uint8_t role = flags & (SYS_SESSION_SLAVE | SYS_SESSION_MASTER);
  ERR_CHECK(provider <= PROVIDER_MAX && deadman_ok(deadman_ms) &&
                (flags & ~(SYS_SESSION_SAFETY | SYS_SESSION_SLAVE | SYS_SESSION_MASTER)) == 0u &&
                role != (SYS_SESSION_SLAVE | SYS_SESSION_MASTER) && n <= CONFIG_SYS_SESSION_PORTS &&
                (n == 0 || modules != NULL) && (role == 0u || n == 0),
            ERR_BAD_ARG);
  const bool safety = (flags & SYS_SESSION_SAFETY) != 0u;
  // A board's session: its sub-ports are its role's rows.
  uint8_t board_modules[CONFIG_SYS_SESSION_PORTS];
  if (role != 0u) {
    const sys_session_sub_t* rows = board_rows(role, &n);
    ERR_CHECK(n > 0, ERR_BAD_ARG);
    for (uint32_t i = 0; i < n; i++) {
      board_modules[i] = rows[i].module;
    }
    modules = board_modules;
  }
  session_t* at = NULL;
  bool same = false;
  bool attached = false;
  portENTER_CRITICAL(&s_lock);
  const session_t* was = on_provider(provider);
  if (was != NULL) {
    attached = true;
    same = was->safety == safety && was->role == role && was->deadman_ms == deadman_ms && was->port_count == n;
    for (uint32_t i = 0; same && i < n; i++) {
      same = (uint8_t)was->ports[i].id == modules[i];
    }
  }
  FOR_EACH_SESSION(s) {
    if (!attached && at == NULL && !s->used) {
      at = s;
      at->used = true;  // taken; its ports are not registered yet, so nothing reaches it
      at->open = false;
      at->provider = provider;
    }
  }
  portEXIT_CRITICAL(&s_lock);
  if (same) {
    return NULL;  // a stored record that attaches runs at every start
  }
  ERR_CHECK(!attached, ERR_BAD_ARG);
  ERR_CHECK(at != NULL, ERR_FULL, .max = CONFIG_SYS_SESSION_MAX);
  const uint8_t state = sys_data_provider_state(provider);
  err_h err = (state & SYS_DATA_PROVIDER_STATE_FRAMED) != 0 ? NULL : ERR_NEW(ERR_SESSION_NOT_FRAMED);
  if (err == NULL) {
    at->safety = safety;
    at->role = role;
    at->deadman_ms = (uint16_t)deadman_ms;
    at->silent_ms = 0;
    at->ping_ms = 0;
    at->push_seq[0] = at->push_seq[1] = 0;
    at->port_count = (uint8_t)n;
    at->line = (data_matrix_port_t){.id = SYS_SESSION_PORT(provider, 0), .in = line_in};
    for (uint32_t i = 0; i < n; i++) {
      at->ports[i] = (data_matrix_port_t){.id = SYS_SESSION_PORT(provider, modules[i]), .in = sub_in};
    }
    err = register_ports(at);
    if (err == NULL && role != 0u) {
      err = link_board(at);
    }
  }
  if (err != NULL) {
    unregister_ports(at);
    portENTER_CRITICAL(&s_lock);
    at->used = false;
    portEXIT_CRITICAL(&s_lock);
    return err;
  }
  if (state & SYS_DATA_PROVIDER_STATE_UP) {
    send_hello(at);
  }
  return NULL;
}

err_h sys_session_detach(uint8_t provider) {
  portENTER_CRITICAL(&s_lock);
  session_t* s = on_provider(provider);
  portEXIT_CRITICAL(&s_lock);
  if (s != NULL) {
    end_session(s);
    unregister_ports(s);
    portENTER_CRITICAL(&s_lock);
    s->used = false;
    portEXIT_CRITICAL(&s_lock);
  }
  return NULL;
}

err_h sys_session_deadman_set(uint8_t peer, uint32_t ms, bool safety) {
  ERR_CHECK(deadman_ok(ms), ERR_BAD_ARG);
  portENTER_CRITICAL(&s_lock);
  session_t* s = open_with(peer);
  if (s != NULL) {
    s->safety = safety && ms > 0;
    s->deadman_ms = (uint16_t)ms;
    s->silent_ms = 0;
  }
  portEXIT_CRITICAL(&s_lock);
  ERR_CHECK(s != NULL, ERR_SESSION_NO_ROUTE, .dst = peer);
  return NULL;
}

uint32_t sys_session_deadman_get(uint8_t peer) {
  portENTER_CRITICAL(&s_lock);
  const session_t* s = open_with(peer);
  const uint32_t ms = s != NULL ? s->deadman_ms : 0u;
  portEXIT_CRITICAL(&s_lock);
  return ms;
}
