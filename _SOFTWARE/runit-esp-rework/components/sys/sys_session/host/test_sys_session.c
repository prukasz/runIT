/* Host tests of the sessions (sys_session.c, hello/, call/, router/) on two fake providers: an UP way (a client or
 * this board's master) and a DOWN way (a slave). The router's tasks and the board timers' task run for real; the
 * test is the peers, delivering their frames as a driver's RX task would and reading what the board sends.
 * Run: python test_host/run.py sys_session   (runit-esp/test_host/HOST_TESTS.MD) */
#include <pthread.h>
#include <string.h>
#include <time.h>

#include "cmd.h"
#include "host_assert.h"
#include "host_mocks.h"
#include "router_queue.h"
#include "sys_data_provider_driver.h"
#include "sys_session.h"
#include "sys_session_internal.h"
#include "unity.h"

#define BOARD    5u     // CONFIG_SYS_SESSION_BOARD_ID_DEFAULT: this board
#define CLIENT   0xF0u  // the app
#define SLAVE    9u
#define MASTER   3u
#define UP_ID    1u     // the UP way's provider
#define DOWN_ID  2u     // the DOWN way's provider
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
  sys_data_provider_t p;
  pthread_mutex_t m;
  pthread_cond_t c;
  sent_t sent[SENT_MAX];
  uint32_t head;  // next frame to read
  uint32_t tail;  // next free
} fake_t;

static fake_t s_up = {.m = PTHREAD_MUTEX_INITIALIZER, .c = PTHREAD_COND_INITIALIZER};
static fake_t s_down = {.m = PTHREAD_MUTEX_INITIALIZER, .c = PTHREAD_COND_INITIALIZER};

static err_h fake_send(sys_data_provider_t* self, const uint8_t* head, uint32_t head_len, const uint8_t* body,
                       uint32_t body_len, uint8_t flags) {
  fake_t* f = self->driver;
  pthread_mutex_lock(&f->m);
  sent_t* s = &f->sent[f->tail++ % SENT_MAX];
  memcpy(s->data, head, head_len);
  memcpy(s->data + head_len, body, body_len);
  s->len = head_len + body_len;
  s->flags = flags;
  pthread_cond_broadcast(&f->c);
  pthread_mutex_unlock(&f->m);
  return NULL;
}

static void fake_add(fake_t* f, uint8_t id) {
  f->p = (sys_data_provider_t){.send = fake_send, .driver = f, .id = id, .link = SYS_DATA_PROVIDER_LINK_UART,
                               .state = SYS_DATA_PROVIDER_STATE_FRAMED | SYS_DATA_PROVIDER_STATE_UP |
                                        SYS_DATA_PROVIDER_STATE_WIRED};
  TEST_ASSERT_NULL(sys_data_provider_add(&f->p));
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
  uint8_t buf[CMD_FRAME_MAX];
  const sys_session_hdr_t h = {.frame = frame, .dst = dst, .src = src, .seq = seq};
  memcpy(buf, &h, HDR);
  memcpy(buf + HDR, body, len);
  sys_data_provider_deliver(&f->p, buf, HDR + len);
}

static void deliver_hello_timeout(fake_t* f, uint8_t src, uint32_t boot_id, uint16_t timeout_ms) {
  uint8_t body[1 + sizeof(sys_session_hello_t)] = {SYS_SESSION_HELLO};
  const sys_session_hello_t hello = {.id = src, .boot_id = boot_id, .build_id = 0, .timeout_ms = timeout_ms};
  memcpy(body + 1, &hello, sizeof(hello));
  deliver(f, FRAME(SYS_SESSION_CONTROL), BOARD, src, 0, body, sizeof(body));
}

static void deliver_hello(fake_t* f, uint8_t src, uint32_t boot_id) {
  deliver_hello_timeout(f, src, boot_id, 3000);
}

static void deliver_control(fake_t* f, uint8_t src, uint8_t op, const void* data, uint32_t len) {
  uint8_t body[16] = {op};
  memcpy(body + 1, data, len);
  deliver(f, FRAME(SYS_SESSION_CONTROL), BOARD, src, 0, body, 1 + len);
}

static const uint8_t k_peers_req[] = {MODULE_ID_SESSION, CMD_DIR_READ, SESSION_OP_PEERS};

/* ---- what the listeners and the error sink were told ---------------------------------------------------------- */

static struct {
  pthread_mutex_t m;
  int gone;
  uint8_t gone_peer;
  uint8_t gone_role;
  int reached;
  uint8_t reached_peer;
  int none;  // ERR_SESSION_NONE reported
} s_seen = {.m = PTHREAD_MUTEX_INITIALIZER};

static void on_gone(uint8_t peer, uint8_t role) {
  pthread_mutex_lock(&s_seen.m);
  s_seen.gone++;
  s_seen.gone_peer = peer;
  s_seen.gone_role = role;
  pthread_mutex_unlock(&s_seen.m);
}

static void on_reached(uint8_t peer) {
  pthread_mutex_lock(&s_seen.m);
  s_seen.reached++;
  s_seen.reached_peer = peer;
  pthread_mutex_unlock(&s_seen.m);
}

static void on_push(uint8_t src, uint16_t lost, const uint8_t* data, uint32_t len);  // the PUSH tests'

static void sink(err_h chain, const struct cmd_req* cmd) {
  (void)cmd;
  for (err_h n = chain; n != NULL; n = n->next) {
    if (n->tag == ERR_SESSION_NONE) {
      pthread_mutex_lock(&s_seen.m);
      s_seen.none++;
      pthread_mutex_unlock(&s_seen.m);
    }
  }
}

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
    err_set_sink(sink);
    TEST_ASSERT_NULL(router_init());
    TEST_ASSERT_NULL(sys_session_init());
    TEST_ASSERT_NULL(sys_session_on_gone(on_gone));
    TEST_ASSERT_NULL(sys_session_on_reached(on_reached));
    TEST_ASSERT_NULL(sys_session_on_push(on_push));
    fake_add(&s_up, UP_ID);
    fake_add(&s_down, DOWN_ID);
  }
  host_log_clear();
  pthread_mutex_lock(&s_seen.m);
  s_seen.gone = s_seen.reached = s_seen.none = 0;
  pthread_mutex_unlock(&s_seen.m);
  fake_clear(&s_up);
  fake_clear(&s_down);
}

void tearDown(void) {
  err_free(sys_session_detach(UP_ID));
  err_free(sys_session_detach(DOWN_ID));
  sys_data_provider_set_up(&s_up.p, true);
}

// Attaches the UP way and opens it with the client's hello; the board's hellos are read off.
static void open_client(void) {
  TEST_ASSERT_NULL(sys_session_attach(UP_ID, SYS_SESSION_UP));
  sent_t s;
  TEST_ASSERT_TRUE(next_control(&s_up, SYS_SESSION_HELLO, &s, WAIT_MS));  // the board's, on attach (up already)
  deliver_hello(&s_up, CLIENT, 0xC0FFEEu);
  TEST_ASSERT_TRUE(next_control(&s_up, SYS_SESSION_HELLO, &s, WAIT_MS));  // a client is answered every hello
}

static void open_slave(void) {
  TEST_ASSERT_NULL(sys_session_attach(DOWN_ID, SYS_SESSION_DOWN));
  sent_t s;
  TEST_ASSERT_TRUE(next_control(&s_down, SYS_SESSION_HELLO, &s, WAIT_MS));
  deliver_hello(&s_down, SLAVE, 0x5A5Au);
  TEST_ASSERT_TRUE(next_control(&s_down, SYS_SESSION_HELLO, &s, WAIT_MS));  // a board: its first hello answered
}

/* ---- the hello ------------------------------------------------------------------------------------------------ */

static void test_attach_says_hello_and_the_clients_hello_opens_the_session(void) {
  TEST_ASSERT_NULL(sys_session_attach(UP_ID, SYS_SESSION_UP));
  sent_t s;
  TEST_ASSERT_TRUE(next_control(&s_up, SYS_SESSION_HELLO, &s, WAIT_MS));
  sys_session_hello_t hello;
  memcpy(&hello, s.data + HDR + 1, sizeof(hello));
  TEST_ASSERT_EQUAL_HEX8(BOARD, hello.id);
  TEST_ASSERT_EQUAL_HEX32(0x12345678u | 1u, hello.boot_id);
  TEST_ASSERT_EQUAL_HEX32(0xB01DB01Du, hello.build_id);
  TEST_ASSERT_EQUAL_UINT16(CONFIG_SYS_SESSION_TIMEOUT_MS, hello.timeout_ms);
  sys_session_info_t row;
  TEST_ASSERT_TRUE(peers_row(UP_ID, &row));
  TEST_ASSERT_EQUAL_UINT8(SYS_SESSION_UNKNOWN, row.role);  // an UP way before its hello
  TEST_ASSERT_EQUAL_UINT8(0, row.open);

  deliver_hello(&s_up, CLIENT, 0xC0FFEEu);
  TEST_ASSERT_TRUE(next_control(&s_up, SYS_SESSION_HELLO, &s, WAIT_MS));
  TEST_ASSERT_TRUE(peers_row(UP_ID, &row));
  TEST_ASSERT_EQUAL_UINT8(1, row.open);
  TEST_ASSERT_EQUAL_UINT8(SYS_SESSION_CLIENT, row.role);
  TEST_ASSERT_EQUAL_HEX8(CLIENT, row.peer_id);
  TEST_ASSERT_EQUAL_INT(1, seen(&s_seen.reached));
  TEST_ASSERT_TRUE(sys_session_is_root());
}

static void test_a_request_over_the_session_is_answered_to_its_source(void) {
  open_client();
  deliver(&s_up, FRAME(SYS_SESSION_REQUEST), BOARD, CLIENT, 7, k_peers_req, sizeof(k_peers_req));
  sent_t s;
  TEST_ASSERT_TRUE(next_kind(&s_up, SYS_SESSION_ANSWER, &s, WAIT_MS));
  const sys_session_hdr_t h = hdr_of(&s);
  TEST_ASSERT_EQUAL_HEX8(CLIENT, h.dst);
  TEST_ASSERT_EQUAL_HEX8(BOARD, h.src);
  TEST_ASSERT_EQUAL_UINT16(7, h.seq);
  TEST_ASSERT_EQUAL_HEX8(0, h.frame & SYS_SESSION_FRAME_STATUS);  // OK
  TEST_ASSERT_EQUAL_UINT8(1, s.data[HDR]);                       // one session: the client's
  TEST_ASSERT_EQUAL_UINT32(HDR + 1 + sizeof(sys_session_info_t), s.len);
  TEST_ASSERT_EQUAL_HEX8(0, s.flags & SYS_DATA_PROVIDER_SEND_SPARE);  // an answer never takes only spare room
}

static void test_frames_on_a_closed_session_start_over_and_are_reported_once(void) {
  TEST_ASSERT_NULL(sys_session_attach(UP_ID, SYS_SESSION_UP));
  for (int i = 0; i < 3; i++) {
    deliver(&s_up, FRAME(SYS_SESSION_REQUEST), BOARD, CLIENT, (uint16_t)i, k_peers_req, sizeof(k_peers_req));
    sent_t s;
    TEST_ASSERT_TRUE(next_control(&s_up, SYS_SESSION_NO_SESSION, &s, WAIT_MS));
  }
  TEST_ASSERT_EQUAL_INT(1, seen(&s_seen.none));
}

static void test_a_ping_on_a_session_not_open_here_is_answered_no_session(void) {
  TEST_ASSERT_NULL(sys_session_attach(UP_ID, SYS_SESSION_UP));
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
  TEST_ASSERT_EQUAL_UINT8(SYS_SESSION_CLIENT, s_seen.gone_role);
  TEST_ASSERT_EQUAL_INT(2, seen(&s_seen.reached));
  sys_session_info_t row;
  TEST_ASSERT_TRUE(peers_row(UP_ID, &row));
  TEST_ASSERT_EQUAL_UINT8(1, row.open);
}

static void test_silence_past_the_timeout_ends_the_session(void) {
  open_client();
  sleep_ms(CONFIG_SYS_SESSION_TIMEOUT_MS * 2 + 50);
  TEST_ASSERT_EQUAL_INT(1, seen(&s_seen.gone));
  TEST_ASSERT_EQUAL_UINT8(SYS_SESSION_CLIENT, s_seen.gone_role);
  sys_session_info_t row;
  TEST_ASSERT_TRUE(peers_row(UP_ID, &row));
  TEST_ASSERT_EQUAL_UINT8(0, row.open);
}

static void test_the_provider_down_ends_the_session(void) {
  open_client();
  sys_data_provider_set_up(&s_up.p, false);
  TEST_ASSERT_EQUAL_INT(1, seen(&s_seen.gone));
}

/* ---- the star: the master, the slaves, PEER ------------------------------------------------------------------- */

static void test_a_boards_hello_on_an_up_way_makes_it_the_master_and_ends_the_rest(void) {
  open_slave();
  TEST_ASSERT_NULL(sys_session_attach(UP_ID, SYS_SESSION_UP));
  deliver_hello(&s_up, MASTER, 0x77u);
  sys_session_info_t row;
  TEST_ASSERT_TRUE(peers_row(UP_ID, &row));
  TEST_ASSERT_EQUAL_UINT8(SYS_SESSION_MASTER, row.role);
  TEST_ASSERT_EQUAL_UINT8(1, row.open);
  TEST_ASSERT_TRUE(peers_row(DOWN_ID, &row));
  TEST_ASSERT_EQUAL_UINT8(0, row.open);  // a board with a master has no other session
  TEST_ASSERT_EQUAL_UINT8(SYS_SESSION_SLAVE, s_seen.gone_role);
  TEST_ASSERT_FALSE(sys_session_is_root());

  // The master's PEER: a client of it went, so what it had here ends.
  const sys_session_peer_t gone = {.id = CLIENT, .up = 0};
  deliver_control(&s_up, MASTER, SYS_SESSION_PEER, &gone, sizeof(gone));
  TEST_ASSERT_EQUAL_HEX8(CLIENT, s_seen.gone_peer);
  TEST_ASSERT_EQUAL_UINT8(SYS_SESSION_VIA_NOTICE, s_seen.gone_role);
}

static void test_the_root_tells_its_other_peers_when_a_session_opens_and_ends(void) {
  open_client();
  fake_clear(&s_up);
  open_slave();
  sent_t s;
  TEST_ASSERT_TRUE(next_control(&s_up, SYS_SESSION_PEER, &s, WAIT_MS));
  sys_session_peer_t peer;
  memcpy(&peer, s.data + HDR + 1, sizeof(peer));
  TEST_ASSERT_EQUAL_HEX8(SLAVE, peer.id);
  TEST_ASSERT_EQUAL_UINT8(1, peer.up);
  TEST_ASSERT_NULL(sys_session_detach(DOWN_ID));
  TEST_ASSERT_TRUE(next_control(&s_up, SYS_SESSION_PEER, &s, WAIT_MS));
  memcpy(&peer, s.data + HDR + 1, sizeof(peer));
  TEST_ASSERT_EQUAL_HEX8(SLAVE, peer.id);
  TEST_ASSERT_EQUAL_UINT8(0, peer.up);
}

static void test_the_root_forwards_between_a_client_and_a_slave_as_it_is(void) {
  open_client();
  open_slave();
  fake_clear(&s_down);
  deliver(&s_up, FRAME(SYS_SESSION_REQUEST), SLAVE, CLIENT, 42, k_peers_req, sizeof(k_peers_req));
  sent_t s;
  TEST_ASSERT_TRUE(next_kind(&s_down, SYS_SESSION_REQUEST, &s, WAIT_MS));
  const sys_session_hdr_t h = hdr_of(&s);
  TEST_ASSERT_EQUAL_HEX8(SLAVE, h.dst);
  TEST_ASSERT_EQUAL_HEX8(CLIENT, h.src);  // the source stays: the slave answers the client
  TEST_ASSERT_EQUAL_UINT16(42, h.seq);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(k_peers_req, s.data + HDR, sizeof(k_peers_req));
}

/* ---- PUSH: spare room only, the session's counters ------------------------------------------------------------ */

static const uint8_t k_msg[] = {0x10, 0xAB};

static void test_a_push_takes_only_spare_room(void) {
  open_client();
  TEST_ASSERT_NULL(sys_session_push(CLIENT, k_msg, sizeof(k_msg), 0));
  sent_t s;
  TEST_ASSERT_TRUE(next_kind(&s_up, SYS_SESSION_PUSH, &s, WAIT_MS));
  TEST_ASSERT_EQUAL_HEX8(SYS_DATA_PROVIDER_SEND_SPARE | SYS_DATA_PROVIDER_SEND_NOWAIT, s.flags);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(k_msg, s.data + HDR, sizeof(k_msg));
}

// The header of the next PUSH the board sent on `f`.
static sys_session_hdr_t next_push(fake_t* f) {
  sent_t s;
  TEST_ASSERT_TRUE(next_kind(f, SYS_SESSION_PUSH, &s, WAIT_MS));
  return hdr_of(&s);
}

static void test_push_seq_counts_per_peer_and_class(void) {
  open_client();
  open_slave();
  fake_clear(&s_up);
  fake_clear(&s_down);
  TEST_ASSERT_NULL(sys_session_push(CLIENT, k_msg, sizeof(k_msg), 0));
  TEST_ASSERT_NULL(sys_session_push(CLIENT, k_msg, sizeof(k_msg), 0));
  TEST_ASSERT_NULL(sys_session_push(CLIENT, k_msg, sizeof(k_msg), ROUTER_FLAG_URGENT));
  TEST_ASSERT_NULL(sys_session_data(CLIENT, 0x01, 0x02, k_msg, sizeof(k_msg), 0));
  TEST_ASSERT_NULL(sys_session_push(SLAVE, k_msg, sizeof(k_msg), 0));
  const uint16_t want[] = {0, 1, 0, 2};  // normal, normal, urgent (its own counter), DATA (the normal one's)
  for (uint32_t i = 0; i < ARRAY_LEN(want); i++) {
    const sys_session_hdr_t h = next_push(&s_up);
    TEST_ASSERT_EQUAL_UINT16(want[i], h.seq);
    TEST_ASSERT_EQUAL_HEX8(0, h.frame & SYS_SESSION_FRAME_UNCOUNTED);
  }
  const sys_session_hdr_t h = next_push(&s_down);
  TEST_ASSERT_EQUAL_HEX8(SLAVE, h.dst);
  TEST_ASSERT_EQUAL_UINT16(0, h.seq);  // another peer: a counter of its own
}

static struct {
  int calls;
  uint8_t src;
  uint16_t lost;
} s_push;

static void on_push(uint8_t src, uint16_t lost, const uint8_t* data, uint32_t len) {
  (void)data;
  (void)len;
  s_push.calls++;
  s_push.src = src;
  s_push.lost = lost;
}

// A PUSH from `src` delivered on `f`; what the listener was told as lost (it runs on this thread, as on an RX task).
static uint16_t push_in(fake_t* f, uint8_t src, uint8_t frame_flags, uint16_t seq) {
  const int before = s_push.calls;
  deliver(f, (uint8_t)(FRAME(SYS_SESSION_PUSH) | frame_flags), BOARD, src, seq, k_msg, sizeof(k_msg));
  TEST_ASSERT_EQUAL_INT(before + 1, s_push.calls);
  TEST_ASSERT_EQUAL_HEX8(src, s_push.src);
  return s_push.lost;
}

static void test_a_gap_in_a_peers_counter_of_a_class_is_told_as_lost(void) {
  open_client();
  open_slave();
  TEST_ASSERT_EQUAL_UINT16(0, push_in(&s_up, CLIENT, 0, 10));  // the first: no gap known
  TEST_ASSERT_EQUAL_UINT16(0, push_in(&s_up, CLIENT, 0, 11));
  TEST_ASSERT_EQUAL_UINT16(2, push_in(&s_up, CLIENT, 0, 14));
  TEST_ASSERT_EQUAL_UINT16(0, push_in(&s_up, CLIENT, ROUTER_FLAG_URGENT, 3));  // urgent: a counter of its own
  TEST_ASSERT_EQUAL_UINT16(0, push_in(&s_up, CLIENT, ROUTER_FLAG_URGENT, 4));
  TEST_ASSERT_EQUAL_UINT16(0, push_in(&s_up, CLIENT, 0, 15));
  TEST_ASSERT_EQUAL_UINT16(0, push_in(&s_down, SLAVE, 0, 100));  // another peer
  TEST_ASSERT_EQUAL_UINT16(0, push_in(&s_down, SLAVE, 0, 101));

  // DATA is a PUSH of the same counter: after it, 17 missing is one frame lost, not two.
  const uint8_t data[] = {SYS_SESSION_CHANNEL_DATA, 0x7F, 0x01, 0xAA};
  const int calls = s_push.calls;
  deliver(&s_up, FRAME(SYS_SESSION_PUSH), BOARD, CLIENT, 16, data, sizeof(data));
  TEST_ASSERT_EQUAL_INT(calls, s_push.calls);  // DATA goes to the provider, not to the listeners
  TEST_ASSERT_EQUAL_UINT16(1, push_in(&s_up, CLIENT, 0, 18));

  // The client restarts: its counters start again with its session, no false gap.
  deliver_hello(&s_up, CLIENT, 0xBEEFu);
  TEST_ASSERT_EQUAL_UINT16(0, push_in(&s_up, CLIENT, 0, 5));
  // An uncounted one (the sender's table full) tells nothing and leaves the counter as it is.
  TEST_ASSERT_EQUAL_UINT16(0, push_in(&s_up, CLIENT, SYS_SESSION_FRAME_UNCOUNTED, 0));
  TEST_ASSERT_EQUAL_UINT16(0, push_in(&s_up, CLIENT, 0, 6));
}

static void test_a_full_table_sends_the_push_uncounted(void) {
  TEST_ASSERT_NULL(sys_session_attach(UP_ID, SYS_SESSION_UP));
  deliver_hello(&s_up, MASTER, 0x77u);  // a slave: every peer through its master
  fake_clear(&s_up);
  for (uint8_t peer = 0x20; peer < 0x20 + CONFIG_SYS_SESSION_PUSH_PEERS; peer++) {
    TEST_ASSERT_NULL(sys_session_push(peer, k_msg, sizeof(k_msg), 0));
    TEST_ASSERT_EQUAL_HEX8(0, next_push(&s_up).frame & SYS_SESSION_FRAME_UNCOUNTED);
  }
  const uint8_t extra = 0x20 + CONFIG_SYS_SESSION_PUSH_PEERS;
  for (int i = 0; i < 2; i++) {
    TEST_ASSERT_NULL(sys_session_push(extra, k_msg, sizeof(k_msg), 0));  // never refused
    const sys_session_hdr_t h = next_push(&s_up);
    TEST_ASSERT_EQUAL_HEX8(SYS_SESSION_FRAME_UNCOUNTED, h.frame & SYS_SESSION_FRAME_UNCOUNTED);
    TEST_ASSERT_EQUAL_UINT16(0, h.seq);
  }

  // The master's PEER: 0x20 went, so its entry is free and the next peer is counted.
  const sys_session_peer_t gone = {.id = 0x20, .up = 0};
  deliver_control(&s_up, MASTER, SYS_SESSION_PEER, &gone, sizeof(gone));
  TEST_ASSERT_NULL(sys_session_push(extra, k_msg, sizeof(k_msg), 0));
  const sys_session_hdr_t h = next_push(&s_up);
  TEST_ASSERT_EQUAL_HEX8(0, h.frame & SYS_SESSION_FRAME_UNCOUNTED);
  TEST_ASSERT_EQUAL_UINT16(0, h.seq);
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
  TEST_ASSERT_EQUAL_HEX32(ERR_SESSION_TIMEOUT, err->tag);
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
  TEST_ASSERT_EQUAL_HEX32(ERR_REMOTE, w.err->tag);
  TEST_ASSERT_EQUAL_UINT8(SLAVE, ERR_DATA(w.err, ERR_REMOTE)->board);
  TEST_ASSERT_NOT_NULL(w.err->next);
  TEST_ASSERT_EQUAL_HEX32(ERR_CMD_BAD_ARG, w.err->next->tag);
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
  TEST_ASSERT_EQUAL_HEX32(ERR_SESSION_PEER_DOWN, w.err->tag);
  err_free(w.err);
}

static void test_a_peer_timeout_below_min_is_clamped(void) {
  TEST_ASSERT_NULL(sys_session_attach(UP_ID, SYS_SESSION_UP));
  sent_t s;
  TEST_ASSERT_TRUE(next_control(&s_up, SYS_SESSION_HELLO, &s, WAIT_MS));
  deliver_hello_timeout(&s_up, CLIENT, 1, 0);
  TEST_ASSERT_TRUE(next_control(&s_up, SYS_SESSION_HELLO, &s, WAIT_MS));
  fake_clear(&s_up);
  sleep_ms(120);
  int pings = 0;
  while (next_control(&s_up, SYS_SESSION_PING, &s, 0)) {
    pings++;
  }
  // Clamped to SYS_SESSION_TIMEOUT_MIN_MS (150 ms -> ping every 50 ms): 1..3 pings in 120 ms, not a 10 ms flood.
  TEST_ASSERT_GREATER_OR_EQUAL_INT(1, pings);
  TEST_ASSERT_LESS_OR_EQUAL_INT(3, pings);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_attach_says_hello_and_the_clients_hello_opens_the_session);
  RUN_TEST(test_a_request_over_the_session_is_answered_to_its_source);
  RUN_TEST(test_frames_on_a_closed_session_start_over_and_are_reported_once);
  RUN_TEST(test_a_ping_on_a_session_not_open_here_is_answered_no_session);
  RUN_TEST(test_a_new_boot_id_ends_the_session_and_opens_it_again);
  RUN_TEST(test_silence_past_the_timeout_ends_the_session);
  RUN_TEST(test_the_provider_down_ends_the_session);
  RUN_TEST(test_a_boards_hello_on_an_up_way_makes_it_the_master_and_ends_the_rest);
  RUN_TEST(test_the_root_tells_its_other_peers_when_a_session_opens_and_ends);
  RUN_TEST(test_the_root_forwards_between_a_client_and_a_slave_as_it_is);
  RUN_TEST(test_a_push_takes_only_spare_room);
  RUN_TEST(test_push_seq_counts_per_peer_and_class);
  RUN_TEST(test_a_gap_in_a_peers_counter_of_a_class_is_told_as_lost);
  RUN_TEST(test_a_full_table_sends_the_push_uncounted);
  RUN_TEST(test_a_waited_call_ends_with_its_answer_not_with_another_wake);
  RUN_TEST(test_a_waited_call_without_an_answer_ends_in_its_time);
  RUN_TEST(test_an_error_answer_comes_back_under_err_remote);
  RUN_TEST(test_a_call_through_a_session_that_ends_fails_peer_down);
  RUN_TEST(test_a_peer_timeout_below_min_is_clamped);
  return UNITY_END();
}
