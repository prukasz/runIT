/* Host tests of the sessions (sys_session.c, call/, router/) on two fake providers: the one a client's session runs
 * on and the one a board's. The router's tasks and the board timers' task run for real; the
 * test is the peers, delivering their frames as a driver's RX task would and reading what the board sends.
 * Run: python test_host/run.py sys_session   (runit-esp-archive/test_host/HOST_TESTS.MD) */
#include <pthread.h>
#include <string.h>
#include <time.h>

#include "cmd.h"
#include "cmd_table.h"
#include "err_record.h"
#include "host_assert.h"
#include "host_mocks.h"
#include "router_queue.h"
#include "sys_data_provider_driver.h"
#include "sys_event.h"
#include "sys_session.h"
#include "sys_session_events.h"
#include "sys_session_internal.h"

#define HDR SYS_SESSION_HDR_BYTES
#define FRAME SYS_SESSION_FRAME
#define FRAME_KIND SYS_SESSION_FRAME_KIND_OF
#include "unity.h"

#define BOARD    5u     // CONFIG_SYS_SESSION_BOARD_ID_DEFAULT: this board
#define CLIENT   0xF0u  // the app
#define SLAVE    9u
#define UP_ID    1u     // the provider of a client's session
#define DOWN_ID  2u     // the provider of a board's session
#define WAIT_MS  2000

/* ---- the board's identity (board/ on the chip) ---------------------------------------------------------------- */

static uint8_t s_saved_id = 0xFF;
uint8_t sys_session_board_load_id(void) {
  return CONFIG_SYS_SESSION_BOARD_ID_DEFAULT;
}
err_h sys_session_board_save_id(uint8_t id) {
  s_saved_id = id;
  return NULL;
}
uint32_t sys_session_board_boot_id(void) {
  return 0x12345678u;
}
uint32_t sys_session_board_build_id(void) {
  return 0xB01DB01Du;
}

/* ---- two fake providers: every frame the board sends is kept, with its send flags ----------------------------- */

typedef struct {
  uint8_t data[CMD_FRAME_MAX];
  uint32_t len;
  uint8_t flags;
} sent_t;

#define SENT_MAX 64

typedef struct {
  sys_data_provider_t* p;
  pthread_mutex_t m;
  pthread_cond_t c;
  sent_t sent[SENT_MAX];
  uint32_t head;  // next frame to read
  uint32_t tail;  // next free
} fake_t;

static fake_t s_up = {.m = PTHREAD_MUTEX_INITIALIZER, .c = PTHREAD_COND_INITIALIZER};
static fake_t s_down = {.m = PTHREAD_MUTEX_INITIALIZER, .c = PTHREAD_COND_INITIALIZER};

static err_h fake_send(sys_data_provider_t* self, cmd_in_t frame, uint8_t flags) {
  fake_t* f = self->driver;
  pthread_mutex_lock(&f->m);
  sent_t* s = &f->sent[f->tail++ % SENT_MAX];
  memcpy(s->data, frame.data, frame.len);
  s->len = frame.len;
  s->flags = flags;
  pthread_cond_broadcast(&f->c);
  pthread_mutex_unlock(&f->m);
  return NULL;
}

typedef struct __attribute__((packed)) {
  uintptr_t fake;
  uint8_t state;
} fake_prov_cfg_t;

static err_h fake_create(sys_data_provider_t* p, void* ctx, const void* cfg_in) {
  (void)ctx;
  fake_prov_cfg_t cfg;
  memcpy(&cfg, cfg_in, sizeof(cfg));
  fake_t* f = (fake_t*)cfg.fake;
  f->p = p;
  p->driver = f;
  p->send = fake_send;
  p->state |= cfg.state;
  return NULL;
}

static err_h fake_remove(sys_data_provider_t* p, void* ctx) {
  (void)ctx;
  fake_t* f = p->driver;
  if (f != NULL && f->p == p) {
    f->p = NULL;
  }
  return NULL;
}

SYS_DATA_PROVIDER_TYPE_DEFINE(SYS_DATA_PROVIDER_TYPE_UART, fake_prov_cfg_t, cmd_none_t, fake_create, fake_remove, NULL, 0)

static void fake_add(fake_t* f, uint8_t id) {
  uint8_t args[SYS_DATA_PROVIDER_CREATE_HDR + sizeof(fake_prov_cfg_t)] = {SYS_DATA_PROVIDER_TYPE_UART, 0};
  const fake_prov_cfg_t cfg = {
      .fake = (uintptr_t)f,
      .state = SYS_DATA_PROVIDER_STATE_FRAMED | SYS_DATA_PROVIDER_STATE_UP | SYS_DATA_PROVIDER_STATE_WIRED,
  };
  memcpy(args + SYS_DATA_PROVIDER_CREATE_HDR, &cfg, sizeof(cfg));
  TEST_ASSERT_NULL(sys_data_provider_write(id, CONTRACT_PROVIDER_OP_CREATE, args, sizeof(args)));
}

static void fake_clear(fake_t* f) {
  pthread_mutex_lock(&f->m);
  f->head = f->tail;
  pthread_mutex_unlock(&f->m);
}

// The next frame the board sent on `f`, waiting at most `ms`; false when none came.
static bool next_sent(fake_t* f, sent_t* out, int ms) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  ts.tv_sec += ms / 1000;
  ts.tv_nsec += (long)(ms % 1000) * 1000000L;
  if (ts.tv_nsec >= 1000000000L) {
    ts.tv_sec++;
    ts.tv_nsec -= 1000000000L;
  }
  bool ok = true;
  pthread_mutex_lock(&f->m);
  while (f->head == f->tail && ok) {
    ok = pthread_cond_timedwait(&f->c, &f->m, &ts) == 0;
  }
  ok = f->head != f->tail;
  if (ok) {
    *out = f->sent[f->head++ % SENT_MAX];
  }
  pthread_mutex_unlock(&f->m);
  return ok;
}

static sys_session_hdr_t hdr_of(const sent_t* s) {
  sys_session_hdr_t h;
  memcpy(&h, s->data, sizeof(h));
  return h;
}

// The next frame on `f` that is a control frame with `op` (others are skipped), within `ms`.
static bool next_control(fake_t* f, uint8_t op, sent_t* out, int ms) {
  sent_t s;
  while (next_sent(f, &s, ms)) {
    if (FRAME_KIND(hdr_of(&s).frame) == SYS_SESSION_CONTROL && s.len > HDR && s.data[HDR] == op) {
      *out = s;
      return true;
    }
  }
  return false;
}

// The next frame on `f` of `kind` (control frames are skipped), within `ms`.
static bool next_kind(fake_t* f, uint8_t kind, sent_t* out, int ms) {
  sent_t s;
  while (next_sent(f, &s, ms)) {
    if (FRAME_KIND(hdr_of(&s).frame) == kind) {
      *out = s;
      return true;
    }
  }
  return false;
}

/* ---- the peers' frames, delivered as the driver's RX task would ----------------------------------------------- */

static void deliver(fake_t* f, uint8_t frame, uint8_t dst, uint8_t src, uint16_t seq, const void* body, uint32_t len) {
  (void)dst;
  (void)src;
  uint8_t buf[CMD_FRAME_MAX];
  const sys_session_hdr_t h = {.frame = frame, .seq = seq};
  memcpy(buf, &h, HDR);
  memcpy(buf + HDR, body, len);
  sys_data_provider_deliver(f->p, buf, HDR + len);
}

static void deliver_hello(fake_t* f, uint8_t src, uint32_t boot_id) {
  uint8_t body[1 + sizeof(sys_session_hello_t)] = {SYS_SESSION_HELLO};
  const sys_session_hello_t hello = {.id = src, .boot_id = boot_id, .build_id = 0};
  memcpy(body + 1, &hello, sizeof(hello));
  deliver(f, FRAME(SYS_SESSION_CONTROL), BOARD, src, 0, body, sizeof(body));
}

static void deliver_control(fake_t* f, uint8_t src, uint8_t op, const void* data, uint32_t len) {
  uint8_t body[16] = {op};
  memcpy(body + 1, data, len);
  deliver(f, FRAME(SYS_SESSION_CONTROL), BOARD, src, 0, body, 1 + len);
}

static const uint8_t k_peers_req[] = {MODULE_ID_SESSION, CMD_DIR_READ, SESSION_OP_PEERS};

/* ---- what the listeners were told ----------------------------------------------------------------------------- */

static struct {
  pthread_mutex_t m;
  int gone;
  uint8_t gone_peer;
  uint8_t gone_safety;
  int reached;
  uint8_t reached_peer;
} s_seen = {.m = PTHREAD_MUTEX_INITIALIZER};

static void on_gone(uint8_t peer, uint8_t safety) {
  pthread_mutex_lock(&s_seen.m);
  s_seen.gone++;
  s_seen.gone_peer = peer;
  s_seen.gone_safety = safety;
  pthread_mutex_unlock(&s_seen.m);
}

static void on_reached(uint8_t peer);
static void on_gone(uint8_t peer, uint8_t safety);

static err_h on_gone_event(const sys_event_t* e, void* ctx) {
  (void)ctx;
  on_gone((uint8_t)e->key.source, ((const evt_data_SESSION_EV_GONE_t*)e->data)->safety);
  return NULL;
}

static err_h on_reached_event(const sys_event_t* e, void* ctx) {
  (void)ctx;
  on_reached((uint8_t)e->key.source);
  return NULL;
}

static void on_reached(uint8_t peer) {
  pthread_mutex_lock(&s_seen.m);
  s_seen.reached++;
  s_seen.reached_peer = peer;
  pthread_mutex_unlock(&s_seen.m);
}

static void on_push(uint8_t provider, const uint8_t* data, uint32_t len);  // the PUSH tests'

static int seen(const int* field) {
  pthread_mutex_lock(&s_seen.m);
  const int n = *field;
  pthread_mutex_unlock(&s_seen.m);
  return n;
}

static void sleep_ms(int ms) {
  struct timespec ts = {.tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000L};
  nanosleep(&ts, NULL);
}

// The session of provider `id` in PEERS; false when it has none.
static bool peers_row(uint8_t id, sys_session_info_t* out) {
  sys_session_info_t list[CONFIG_SYS_SESSION_MAX];
  const uint8_t n = sys_session_list(list);
  for (uint8_t i = 0; i < n; i++) {
    if (list[i].provider == id) {
      *out = list[i];
      return true;
    }
  }
  return false;
}

/* ---- set up: the layer once, both ways attached for each test, detached after --------------------------------- */

void setUp(void) {
  static bool started;
  if (!started) {
    started = true;
    host_time_real(true);
    TEST_ASSERT_NULL(router_init());
    TEST_ASSERT_NULL(sys_event_init());
    TEST_ASSERT_NULL(sys_session_init());
    uint16_t handle = 0;
    TEST_ASSERT_NULL(sys_event_subscribe(SESSION_GONE_FILTER, on_gone_event, NULL, SYS_EVENT_RUN_INLINE, &handle));
    TEST_ASSERT_NULL(sys_event_subscribe(SESSION_REACHED_FILTER, on_reached_event, NULL, SYS_EVENT_RUN_INLINE, &handle));
    sys_session_set_push(on_push);
    fake_add(&s_up, UP_ID);
    fake_add(&s_down, DOWN_ID);
  }
  host_log_clear();
  pthread_mutex_lock(&s_seen.m);
  s_seen.gone = s_seen.reached = 0;
  pthread_mutex_unlock(&s_seen.m);
  fake_clear(&s_up);
  fake_clear(&s_down);
}

void tearDown(void) {
  err_free(sys_session_detach(UP_ID));
  err_free(sys_session_detach(DOWN_ID));
  sys_data_provider_set_up(s_up.p, true);
}

// Attaches the client's session and opens it with the client's hello; the board's hellos are read off.
static void open_client(void) {
  TEST_ASSERT_NULL(sys_session_attach(UP_ID, 0, 0));
  sent_t s;
  TEST_ASSERT_TRUE(next_control(&s_up, SYS_SESSION_HELLO, &s, WAIT_MS));  // the board's, on attach (up already)
  deliver_hello(&s_up, CLIENT, 0xC0FFEEu);
  TEST_ASSERT_TRUE(next_control(&s_up, SYS_SESSION_HELLO, &s, WAIT_MS));  // a client is answered every hello
}

static void open_slave(void) {
  TEST_ASSERT_NULL(sys_session_attach(DOWN_ID, 0, 0));
  sent_t s;
  TEST_ASSERT_TRUE(next_control(&s_down, SYS_SESSION_HELLO, &s, WAIT_MS));
  deliver_hello(&s_down, SLAVE, 0x5A5Au);
  TEST_ASSERT_TRUE(next_control(&s_down, SYS_SESSION_HELLO, &s, WAIT_MS));  // a board: its first hello answered
}

/* ---- the hello ------------------------------------------------------------------------------------------------ */

static void test_attach_checks_the_deadman_time(void) {
  err_h low = sys_session_attach(UP_ID, SYS_SESSION_DEADMAN_MIN_MS - 1u, 0);
  TEST_ASSERT_ERR_TAG(ERR_CMD_BAD_ARG, low);
  err_free(low);
  err_h high = sys_session_attach(UP_ID, SYS_SESSION_DEADMAN_MAX_MS + 1u, 0);
  TEST_ASSERT_ERR_TAG(ERR_CMD_BAD_ARG, high);
  err_free(high);
  err_h flag = sys_session_attach(UP_ID, 0, 0x80u);
  TEST_ASSERT_ERR_TAG(ERR_CMD_BAD_ARG, flag);
  err_free(flag);
}

static void test_attach_says_hello_and_the_clients_hello_opens_the_session(void) {
  TEST_ASSERT_NULL(sys_session_attach(UP_ID, 0, 0));
  sent_t s;
  TEST_ASSERT_TRUE(next_control(&s_up, SYS_SESSION_HELLO, &s, WAIT_MS));
  sys_session_hello_t hello;
  memcpy(&hello, s.data + HDR + 1, sizeof(hello));
  TEST_ASSERT_EQUAL_HEX8(BOARD, hello.id);
  TEST_ASSERT_EQUAL_HEX32(0x12345678u, hello.boot_id);
  TEST_ASSERT_EQUAL_HEX32(0xB01DB01Du, hello.build_id);
  sys_session_info_t row;
  TEST_ASSERT_TRUE(peers_row(UP_ID, &row));
  TEST_ASSERT_EQUAL_UINT8(0, row.open);

  deliver_hello(&s_up, CLIENT, 0xC0FFEEu);
  TEST_ASSERT_TRUE(next_control(&s_up, SYS_SESSION_HELLO, &s, WAIT_MS));
  TEST_ASSERT_TRUE(peers_row(UP_ID, &row));
  TEST_ASSERT_EQUAL_UINT8(1, row.open);
  TEST_ASSERT_EQUAL_HEX8(CLIENT, row.peer_id);
  TEST_ASSERT_EQUAL_INT(1, seen(&s_seen.reached));
}

static void test_a_request_over_the_session_is_answered_to_its_source(void) {
  open_client();
  deliver(&s_up, FRAME(SYS_SESSION_REQUEST), BOARD, CLIENT, 7, k_peers_req, sizeof(k_peers_req));
  sent_t s;
  TEST_ASSERT_TRUE(next_kind(&s_up, SYS_SESSION_ANSWER, &s, WAIT_MS));
  const sys_session_hdr_t h = hdr_of(&s);
  TEST_ASSERT_EQUAL_UINT16(7, h.seq);
  TEST_ASSERT_EQUAL_HEX8(0, h.frame & SYS_SESSION_FRAME_STATUS);  // OK
  TEST_ASSERT_EQUAL_UINT8(1, s.data[HDR]);                       // one session: the client's
  TEST_ASSERT_EQUAL_UINT32(HDR + 1 + sizeof(sys_session_info_t), s.len);
  TEST_ASSERT_EQUAL_HEX8(0, s.flags & CMD_SEND_SPARE);  // an answer never takes only spare room
}

static void test_a_frame_on_a_session_not_open_here_starts_over(void) {
  TEST_ASSERT_NULL(sys_session_attach(UP_ID, 0, 0));
  sent_t s;
  TEST_ASSERT_TRUE(next_control(&s_up, SYS_SESSION_HELLO, &s, WAIT_MS));  // the board's, on attach
  deliver(&s_up, FRAME(SYS_SESSION_REQUEST), BOARD, CLIENT, 1, k_peers_req, sizeof(k_peers_req));
  TEST_ASSERT_TRUE(next_sent(&s_up, &s, WAIT_MS));
  TEST_ASSERT_EQUAL_UINT8(SYS_SESSION_CONTROL, FRAME_KIND(hdr_of(&s).frame));
  TEST_ASSERT_EQUAL_UINT8(SYS_SESSION_NO_SESSION, s.data[HDR]);
  TEST_ASSERT_TRUE(next_control(&s_up, SYS_SESSION_HELLO, &s, WAIT_MS));
}

static void test_a_ping_on_a_session_not_open_here_is_answered_no_session(void) {
  TEST_ASSERT_NULL(sys_session_attach(UP_ID, 0, 0));
  deliver_control(&s_up, CLIENT, SYS_SESSION_PING, NULL, 0);
  sent_t s;
  TEST_ASSERT_TRUE(next_control(&s_up, SYS_SESSION_NO_SESSION, &s, WAIT_MS));
  TEST_ASSERT_TRUE(next_control(&s_up, SYS_SESSION_HELLO, &s, WAIT_MS));
}

static void test_a_new_boot_id_ends_the_session_and_opens_it_again(void) {
  open_client();
  deliver_hello(&s_up, CLIENT, 0xBEEFu);  // the client restarted
  TEST_ASSERT_EQUAL_INT(1, seen(&s_seen.gone));
  TEST_ASSERT_EQUAL_HEX8(CLIENT, s_seen.gone_peer);
  TEST_ASSERT_EQUAL_UINT8(0, s_seen.gone_safety);
  TEST_ASSERT_EQUAL_INT(2, seen(&s_seen.reached));
  sys_session_info_t row;
  TEST_ASSERT_TRUE(peers_row(UP_ID, &row));
  TEST_ASSERT_EQUAL_UINT8(1, row.open);
}

static void test_silence_ends_a_session_with_the_deadman_and_leaves_it_empty(void) {
  TEST_ASSERT_NULL(sys_session_attach(UP_ID, 200, 0));
  sent_t s;
  TEST_ASSERT_TRUE(next_control(&s_up, SYS_SESSION_HELLO, &s, WAIT_MS));
  deliver_hello(&s_up, CLIENT, 0xC0FFEEu);
  sleep_ms(200 * 2 + 50);
  TEST_ASSERT_EQUAL_INT(1, seen(&s_seen.gone));
  TEST_ASSERT_EQUAL_UINT8(0, s_seen.gone_safety);  // no safety flag: the session only ended
  sys_session_info_t row;
  TEST_ASSERT_TRUE(peers_row(UP_ID, &row));  // still on its provider
  TEST_ASSERT_EQUAL_UINT8(0, row.open);
  deliver_hello(&s_up, CLIENT, 0xC0FFEEu);  // and the next hello opens it again
  TEST_ASSERT_TRUE(peers_row(UP_ID, &row));
  TEST_ASSERT_EQUAL_UINT8(1, row.open);
}

static void test_a_session_without_the_deadman_never_ends_by_silence(void) {
  open_client();
  sleep_ms(500);
  TEST_ASSERT_EQUAL_INT(0, seen(&s_seen.gone));
  sys_session_info_t row;
  TEST_ASSERT_TRUE(peers_row(UP_ID, &row));
  TEST_ASSERT_EQUAL_UINT8(1, row.open);
}

static void test_the_provider_down_ends_the_session(void) {
  open_client();
  sys_data_provider_set_up(s_up.p, false);
  TEST_ASSERT_EQUAL_INT(1, seen(&s_seen.gone));
}

/* ---- sessions are independent, no routing --------------------------------------------------------------------- */

static void test_sessions_are_independent_and_an_id_is_in_one_of_them(void) {
  open_client();
  open_slave();
  sys_session_info_t row;
  TEST_ASSERT_TRUE(peers_row(UP_ID, &row));
  TEST_ASSERT_EQUAL_UINT8(1, row.open);
  TEST_ASSERT_TRUE(peers_row(DOWN_ID, &row));
  TEST_ASSERT_EQUAL_UINT8(1, row.open);  // a board's hello did not end the client's session

  // A second provider whose peer says an id an open session has: refused, the session stays closed.
  TEST_ASSERT_NULL(sys_session_detach(DOWN_ID));
  TEST_ASSERT_NULL(sys_session_attach(DOWN_ID, 0, 0));
  deliver_hello(&s_down, CLIENT, 0x7777u);
  TEST_ASSERT_TRUE(peers_row(DOWN_ID, &row));
  TEST_ASSERT_EQUAL_UINT8(0, row.open);
}

static void test_a_call_to_a_peer_with_no_session_has_no_route(void) {
  open_client();
  err_h err = sys_session_call(77, (cmd_in_t){k_peers_req, sizeof(k_peers_req)}, 0, 0, NULL, NULL);
  TEST_ASSERT_ERR_TAG(ERR_SESSION_NO_ROUTE, err);
  err_free(err);
}

static void test_a_broadcast_goes_on_every_open_session(void) {
  open_client();
  open_slave();
  fake_clear(&s_up);
  fake_clear(&s_down);
  TEST_ASSERT_NULL(sys_session_broadcast((cmd_in_t){k_peers_req, sizeof(k_peers_req)}, 0));
  sent_t s;
  TEST_ASSERT_TRUE(next_kind(&s_up, SYS_SESSION_REQUEST, &s, WAIT_MS));
  TEST_ASSERT_TRUE((hdr_of(&s).frame & ROUTER_FLAG_NOREPLY) != 0);
  TEST_ASSERT_TRUE(next_kind(&s_down, SYS_SESSION_REQUEST, &s, WAIT_MS));
  TEST_ASSERT_TRUE((hdr_of(&s).frame & ROUTER_FLAG_NOREPLY) != 0);
}

/* ---- the dead-man is the session's own ------------------------------------------------------------------------ */

static void test_the_deadman_is_the_sessions_own(void) {
  TEST_ASSERT_NULL(sys_session_attach(UP_ID, 300, SYS_SESSION_SAFETY));
  sent_t s;
  TEST_ASSERT_TRUE(next_control(&s_up, SYS_SESSION_HELLO, &s, WAIT_MS));
  sys_session_info_t row;
  TEST_ASSERT_TRUE(peers_row(UP_ID, &row));
  TEST_ASSERT_EQUAL_UINT8(1, row.safety);
  TEST_ASSERT_EQUAL_UINT16(300, row.deadman_ms);
  deliver_hello(&s_up, CLIENT, 0xC0FFEEu);
  TEST_ASSERT_EQUAL_UINT32(300, sys_session_deadman_get(CLIENT));

  // Another time, and off.
  TEST_ASSERT_NULL(sys_session_deadman_set(CLIENT, 500, true));
  TEST_ASSERT_EQUAL_UINT32(500, sys_session_deadman_get(CLIENT));
  TEST_ASSERT_NULL(sys_session_deadman_set(CLIENT, 0, false));
  TEST_ASSERT_EQUAL_UINT32(0, sys_session_deadman_get(CLIENT));
  err_h bad = sys_session_deadman_set(CLIENT, 20, true);
  TEST_ASSERT_ERR_TAG(ERR_CMD_BAD_ARG, bad);
  err_free(bad);
  err_h none = sys_session_deadman_set(0x42, 300, true);
  TEST_ASSERT_ERR_TAG(ERR_SESSION_NO_ROUTE, none);
  err_free(none);

  // The keepalive is a third of it: pings at 100 ms.
  TEST_ASSERT_NULL(sys_session_deadman_set(CLIENT, 300, true));
  fake_clear(&s_up);
  TEST_ASSERT_TRUE(next_control(&s_up, SYS_SESSION_PING, &s, 250));

  // With the safety flag the end of the session says so to the listeners.
  sleep_ms(300 * 2 + 50);
  TEST_ASSERT_EQUAL_INT(1, seen(&s_seen.gone));
  TEST_ASSERT_EQUAL_UINT8(1, s_seen.gone_safety);
}

/* ---- PUSH: spare room only, the seq per peer and class, DATA -------------------------------------------------- */

static const uint8_t k_msg[] = {0x10, 0xAB};

static void test_a_push_needs_an_open_session(void) {
  TEST_ASSERT_EQUAL_HEX8(SYS_SESSION_ID_NONE, sys_session_peer(UP_ID));
  err_h e = sys_session_push(UP_ID, (cmd_in_t){k_msg, sizeof(k_msg)}, 0);
  TEST_ASSERT_ERR_TAG(ERR_SESSION_NOT_OPEN, e);
  err_free(e);
  open_client();
  TEST_ASSERT_EQUAL_HEX8(CLIENT, sys_session_peer(UP_ID));
  uint8_t provider = 0;
  TEST_ASSERT_TRUE(sys_session_route(CLIENT, &provider));
  TEST_ASSERT_EQUAL_UINT8(UP_ID, provider);
}

static void test_a_push_takes_only_spare_room(void) {
  open_client();
  TEST_ASSERT_NULL(sys_session_push(UP_ID, (cmd_in_t){k_msg, sizeof(k_msg)}, 0));
  sent_t s;
  TEST_ASSERT_TRUE(next_kind(&s_up, SYS_SESSION_PUSH, &s, WAIT_MS));
  TEST_ASSERT_EQUAL_HEX8(CMD_SEND_SPARE | CMD_SEND_NOWAIT, s.flags);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(k_msg, s.data + HDR, sizeof(k_msg));
}

// The header of the next PUSH the board sent on `f`.
static sys_session_hdr_t next_push(fake_t* f) {
  sent_t s;
  TEST_ASSERT_TRUE(next_kind(f, SYS_SESSION_PUSH, &s, WAIT_MS));
  return hdr_of(&s);
}

static void test_push_seq_counts_per_session_and_class(void) {
  open_client();
  open_slave();
  fake_clear(&s_up);
  fake_clear(&s_down);
  TEST_ASSERT_NULL(sys_session_push(UP_ID, (cmd_in_t){k_msg, sizeof(k_msg)}, 0));
  TEST_ASSERT_NULL(sys_session_push(UP_ID, (cmd_in_t){k_msg, sizeof(k_msg)}, 0));
  TEST_ASSERT_NULL(sys_session_push(UP_ID, (cmd_in_t){k_msg, sizeof(k_msg)}, ROUTER_FLAG_URGENT));
  TEST_ASSERT_NULL(sys_session_push(DOWN_ID, (cmd_in_t){k_msg, sizeof(k_msg)}, 0));
  const uint16_t want[] = {0, 1, 0};  // normal, normal, urgent (its own counter)
  uint16_t first[2] = {0, 0};  // the counters run on from the tests before: the first seen of each class
  for (uint32_t i = 0; i < ARRAY_LEN(want); i++) {
    const sys_session_hdr_t h = next_push(&s_up);
    const uint32_t urgent = h.frame & ROUTER_FLAG_URGENT;
    if (want[i] == 0) {
      first[urgent] = h.seq;
    }
    TEST_ASSERT_EQUAL_UINT16((uint16_t)(first[urgent] + want[i]), h.seq);
  }
  const sys_session_hdr_t h = next_push(&s_down);
  TEST_ASSERT_EQUAL_UINT16(0, h.seq);  // another session: a counter of its own, its first frame
}

static struct {
  int calls;
  uint8_t provider;
  uint8_t first;
} s_push;

static void on_push(uint8_t provider, const uint8_t* data, uint32_t len) {
  s_push.calls++;
  s_push.provider = provider;
  s_push.first = len > 0 ? data[0] : 0;
}

static void test_a_push_goes_to_the_taker_with_its_provider(void) {
  open_client();
  open_slave();
  const int before = s_push.calls;  // the listener runs on this thread, as on an RX task
  deliver(&s_up, FRAME(SYS_SESSION_PUSH), BOARD, CLIENT, 0, k_msg, sizeof(k_msg));
  TEST_ASSERT_EQUAL_INT(before + 1, s_push.calls);
  TEST_ASSERT_EQUAL_HEX8(UP_ID, s_push.provider);
  TEST_ASSERT_EQUAL_HEX8(k_msg[0], s_push.first);
}

/* ---- calls: B1, the waiter's own limit ------------------------------------------------------------------------ */

typedef struct {
  TaskHandle_t task;
  volatile bool started;
  volatile bool done;
  err_h err;
  uint8_t buf[32];
  uint32_t len;
} waiting_t;

static void* waiter(void* arg) {
  waiting_t* w = arg;
  w->task = xTaskGetCurrentTaskHandle();
  w->started = true;
  w->err = sys_session_call_wait(SLAVE, (cmd_in_t){k_peers_req, sizeof(k_peers_req)}, 0, 1000, w->buf,
                                 sizeof(w->buf), &w->len);
  w->done = true;
  return NULL;
}

static void test_a_waited_call_ends_with_its_answer_not_with_another_wake(void) {
  open_slave();
  fake_clear(&s_down);
  waiting_t w = {0};
  pthread_t t;
  pthread_create(&t, NULL, waiter, &w);
  sent_t s;
  TEST_ASSERT_TRUE(next_kind(&s_down, SYS_SESSION_REQUEST, &s, WAIT_MS));
  const uint16_t seq = hdr_of(&s).seq;
  for (int i = 0; i < 5; i++) {
    xTaskNotifyGive(w.task);  // an urgent command's, a timer's: not the call's end
    sleep_ms(5);
  }
  TEST_ASSERT_FALSE(w.done);
  const uint8_t answer[] = {0xAA, 0xBB, 0xCC};
  deliver(&s_down, FRAME(SYS_SESSION_ANSWER), BOARD, SLAVE, seq, answer, sizeof(answer));
  pthread_join(t, NULL);
  TEST_ASSERT_NULL(w.err);
  TEST_ASSERT_EQUAL_UINT32(sizeof(answer), w.len);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(answer, w.buf, sizeof(answer));
}

static void test_a_waited_call_without_an_answer_ends_in_its_time(void) {
  open_slave();
  const TickType_t start = xTaskGetTickCount();
  uint8_t buf[8];
  uint32_t len = 0;
  err_h err = sys_session_call_wait(SLAVE, (cmd_in_t){k_peers_req, sizeof(k_peers_req)}, 0, 100, buf, sizeof(buf), &len);
  const uint32_t took = (uint32_t)(xTaskGetTickCount() - start);
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_EQUAL_HEX32(ERR_SESSION_TIMEOUT, err_tag(err));
  err_free(err);
  TEST_ASSERT_TRUE(took >= 100 && took < 400);
}

static void test_an_error_answer_comes_back_under_err_remote(void) {
  open_slave();
  fake_clear(&s_down);
  waiting_t w = {0};
  pthread_t t;
  pthread_create(&t, NULL, waiter, &w);
  sent_t s;
  TEST_ASSERT_TRUE(next_kind(&s_down, SYS_SESSION_REQUEST, &s, WAIT_MS));
  uint8_t record[64];
  err_h cause = ERR_NEW(ERR_CMD_BAD_ARG);
  const uint32_t n = err_record(cause, record, sizeof(record));
  err_free(cause);
  deliver(&s_down, (uint8_t)(FRAME(SYS_SESSION_ANSWER) | SYS_SESSION_FRAME_STATUS), BOARD, SLAVE, hdr_of(&s).seq,
          record, n);
  pthread_join(t, NULL);
  TEST_ASSERT_NOT_NULL(w.err);
  TEST_ASSERT_EQUAL_HEX32(ERR_REMOTE, err_tag(w.err));
  TEST_ASSERT_EQUAL_UINT8(SLAVE, ERR_DATA(w.err, ERR_REMOTE)->board);
  TEST_ASSERT_NOT_NULL(err_cause(w.err));
  TEST_ASSERT_EQUAL_HEX32(ERR_CMD_BAD_ARG, err_tag(err_cause(w.err)));
  err_free(w.err);
}

static void test_a_call_through_a_session_that_ends_fails_peer_down(void) {
  open_slave();
  fake_clear(&s_down);
  waiting_t w = {0};
  pthread_t t;
  pthread_create(&t, NULL, waiter, &w);
  sent_t s;
  TEST_ASSERT_TRUE(next_kind(&s_down, SYS_SESSION_REQUEST, &s, WAIT_MS));
  TEST_ASSERT_NULL(sys_session_detach(DOWN_ID));
  pthread_join(t, NULL);
  TEST_ASSERT_NOT_NULL(w.err);
  TEST_ASSERT_EQUAL_HEX32(ERR_SESSION_PEER_DOWN, err_tag(w.err));
  err_free(w.err);
}

static void test_a_refused_hello_does_not_keep_the_session_alive(void) {
  TEST_ASSERT_NULL(sys_session_attach(UP_ID, 300, 0));
  sent_t s;
  TEST_ASSERT_TRUE(next_control(&s_up, SYS_SESSION_HELLO, &s, WAIT_MS));

  // Dead-man 300 -> PING every 10 ticks (100 ms of ticks); board expires at tick 31 (silent_ms > 300).
  deliver_hello(&s_up, CLIENT, 0xC0FFEEu);
  TEST_ASSERT_TRUE(next_control(&s_up, SYS_SESSION_HELLO, &s, WAIT_MS));
  TEST_ASSERT_TRUE(next_control(&s_up, SYS_SESSION_PING, &s, WAIT_MS));  // ~tick 10
  TEST_ASSERT_TRUE(next_control(&s_up, SYS_SESSION_PING, &s, WAIT_MS));  // ~tick 20
  TEST_ASSERT_EQUAL_INT(0, seen(&s_seen.gone));

  // A hello with an id of the wrong kind is refused: it is not its peer's sign of life.
  deliver_hello(&s_up, SYS_SESSION_ID_SELF, 0xBADu);
  TEST_ASSERT_TRUE(next_control(&s_up, SYS_SESSION_PING, &s, WAIT_MS));  // ~tick 30
  sleep_ms(80);                                                          // past tick 31, well before reset tick 51
  TEST_ASSERT_EQUAL_INT(1, seen(&s_seen.gone));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_attach_checks_the_deadman_time);
  RUN_TEST(test_attach_says_hello_and_the_clients_hello_opens_the_session);
  RUN_TEST(test_a_request_over_the_session_is_answered_to_its_source);
  RUN_TEST(test_a_frame_on_a_session_not_open_here_starts_over);
  RUN_TEST(test_a_ping_on_a_session_not_open_here_is_answered_no_session);
  RUN_TEST(test_a_new_boot_id_ends_the_session_and_opens_it_again);
  RUN_TEST(test_silence_ends_a_session_with_the_deadman_and_leaves_it_empty);
  RUN_TEST(test_a_session_without_the_deadman_never_ends_by_silence);
  RUN_TEST(test_the_provider_down_ends_the_session);
  RUN_TEST(test_sessions_are_independent_and_an_id_is_in_one_of_them);
  RUN_TEST(test_a_call_to_a_peer_with_no_session_has_no_route);
  RUN_TEST(test_a_broadcast_goes_on_every_open_session);
  RUN_TEST(test_the_deadman_is_the_sessions_own);
  RUN_TEST(test_a_push_needs_an_open_session);
  RUN_TEST(test_a_push_takes_only_spare_room);
  RUN_TEST(test_push_seq_counts_per_session_and_class);
  RUN_TEST(test_a_push_goes_to_the_taker_with_its_provider);
  RUN_TEST(test_a_waited_call_ends_with_its_answer_not_with_another_wake);
  RUN_TEST(test_a_waited_call_without_an_answer_ends_in_its_time);
  RUN_TEST(test_an_error_answer_comes_back_under_err_remote);
  RUN_TEST(test_a_call_through_a_session_that_ends_fails_peer_down);
  RUN_TEST(test_a_refused_hello_does_not_keep_the_session_alive);
  return UNITY_END();
}
