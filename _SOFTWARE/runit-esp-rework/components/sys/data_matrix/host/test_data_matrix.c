/* Host tests of the data matrix (data_matrix.c, data_matrix_commands.c, router/) with the real sys_data_provider, the
 * router, the sessions and the matrix's task, on fake providers: two raw, one framed (a session's), one a command port.
 * Run: python test_host/run.py data_matrix   (from runit-esp/) */
#include <pthread.h>
#include <string.h>
#include <time.h>

#include "cmd.h"
#include "cmd_table.h"
#include "data_matrix.h"
#include "data_matrix_internal.h"
#include "data_matrix_router.h"
#include "host_assert.h"
#include "host_mocks.h"
#include "router.h"
#include "router_queue.h"
#include "sys_data_provider.h"
#include "sys_data_provider_driver.h"
#include "sys_event.h"
#include "sys_session.h"
#include "sys_session_internal.h"
#include "unity.h"

#define ASSERT_ERR(call, tag)       \
  do {                              \
    err_h e_ = (call);              \
    TEST_ASSERT_ERR_TAG((tag), e_); \
    err_free(e_);                   \
  } while (0)

#define RAW_A    10u
#define RAW_B    11u
#define SES_ID   12u  // a session's provider
#define CMD_ID   13u  // a command port
#define NOWHERE  99u
#define PEER     0xF0u
#define WAIT_MS  2000
#define QUIET_MS 150

/* ---- the board's identity (board/ on the chip) ---------------------------------------------------------------- */

uint8_t sys_session_board_load_id(void) {
  return 5;
}
err_h sys_session_board_save_id(uint8_t id) {
  (void)id;
  return NULL;
}
uint32_t sys_session_board_boot_id(void) {
  return 0x12345678u;
}
uint32_t sys_session_board_build_id(void) {
  return 0xB01DB01Du;
}

/* ---- fake providers: everything the board sends is kept, with its flags --------------------------------------- */

typedef struct {
  uint8_t data[CMD_FRAME_MAX];
  uint32_t len;
  uint8_t flags;
} sent_t;

#define SENT_MAX 32

typedef struct {
  sys_data_provider_t* p;
  pthread_mutex_t m;
  pthread_cond_t c;
  sent_t sent[SENT_MAX];
  uint32_t head;
  uint32_t tail;
} fake_t;

static fake_t s_a = {.m = PTHREAD_MUTEX_INITIALIZER, .c = PTHREAD_COND_INITIALIZER};
static fake_t s_b = {.m = PTHREAD_MUTEX_INITIALIZER, .c = PTHREAD_COND_INITIALIZER};
static fake_t s_ses = {.m = PTHREAD_MUTEX_INITIALIZER, .c = PTHREAD_COND_INITIALIZER};
static fake_t s_cmd = {.m = PTHREAD_MUTEX_INITIALIZER, .c = PTHREAD_COND_INITIALIZER};

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

static void fake_add(fake_t* f, uint8_t id, uint8_t state) {
  uint8_t args[SYS_DATA_PROVIDER_CREATE_HDR + sizeof(fake_prov_cfg_t)] = {SYS_DATA_PROVIDER_TYPE_UART, 0};
  const fake_prov_cfg_t cfg = {.fake = (uintptr_t)f, .state = state};
  memcpy(args + SYS_DATA_PROVIDER_CREATE_HDR, &cfg, sizeof(cfg));
  TEST_ASSERT_NULL(sys_data_provider_write(id, CONTRACT_PROVIDER_OP_CREATE, args, sizeof(args)));
}

static void fake_clear(fake_t* f) {
  pthread_mutex_lock(&f->m);
  f->head = f->tail;
  pthread_mutex_unlock(&f->m);
}

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

// The next frame on a session's provider that is a PUSH (the hello and the pings are skipped).
static bool next_push(fake_t* f, sent_t* out, int ms) {
  sent_t s;
  while (next_sent(f, &s, ms)) {
    if (SYS_SESSION_FRAME_KIND_OF(s.data[0]) == SYS_SESSION_PUSH) {
      *out = s;
      return true;
    }
  }
  return false;
}

/* ---- a callback end: what it got ------------------------------------------------------------------------------ */

typedef struct {
  data_matrix_end_t from;
  uint8_t data[CMD_DATA_MAX];
  uint32_t len;
} got_t;

#define GOT_MAX 8

static struct {
  pthread_mutex_t m;
  pthread_cond_t c;
  got_t got[GOT_MAX];
  uint32_t count;
} s_cb = {.m = PTHREAD_MUTEX_INITIALIZER, .c = PTHREAD_COND_INITIALIZER};

static data_matrix_end_t s_cb_end;

static void on_data(void* ctx, const data_matrix_end_t* from, const uint8_t* data, uint32_t len) {
  (void)ctx;
  pthread_mutex_lock(&s_cb.m);
  if (s_cb.count < GOT_MAX) {
    got_t* g = &s_cb.got[s_cb.count];
    g->from = *from;
    memcpy(g->data, data, len);
    g->len = len;
  }
  s_cb.count++;
  pthread_cond_broadcast(&s_cb.c);
  pthread_mutex_unlock(&s_cb.m);
}

// Waits until the callback has been called `n` times in all (since the test began).
static bool wait_calls(uint32_t n, int ms) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  ts.tv_sec += ms / 1000;
  ts.tv_nsec += (long)(ms % 1000) * 1000000L;
  if (ts.tv_nsec >= 1000000000L) {
    ts.tv_sec++;
    ts.tv_nsec -= 1000000000L;
  }
  bool ok = true;
  pthread_mutex_lock(&s_cb.m);
  while (s_cb.count < n && ok) {
    ok = pthread_cond_timedwait(&s_cb.c, &s_cb.m, &ts) == 0;
  }
  ok = s_cb.count >= n;
  pthread_mutex_unlock(&s_cb.m);
  return ok;
}

static bool is_end(const data_matrix_end_t* e, uint8_t kind, uint8_t a, uint8_t b) {
  return e->kind == kind && e->a == a && e->b == b;
}

/* ---- set up --------------------------------------------------------------------------------------------------- */

static data_matrix_role_args_t raw_role(uint8_t provider) {
  return (data_matrix_role_args_t){.provider = provider, .role = DATA_MATRIX_ROLE_RAW};
}

static void give_raw(uint8_t provider) {
  const data_matrix_role_args_t role = raw_role(provider);
  TEST_ASSERT_NULL(data_matrix_set_role(&role));
}

static void route(data_matrix_end_t from, data_matrix_end_t to) {
  const data_matrix_route_t r = {from, to};
  TEST_ASSERT_NULL(data_matrix_route(&r));
}

#define EP(id)   DATA_MATRIX_END_PROVIDER(id)
#define ET(id, t) DATA_MATRIX_END_TAGGED((id), (t))
#define ES(id)    DATA_MATRIX_END_STREAM(id)

static void rows(uint8_t* roles, uint8_t* routes) {
  data_matrix_role_args_t r[CONFIG_DATA_MATRIX_ROLES];
  data_matrix_route_t t[CONFIG_DATA_MATRIX_ROUTES];
  data_matrix_list(r, roles, t, routes);
}

void setUp(void) {
  static bool started;
  if (!started) {
    host_time_real(true);
    TEST_ASSERT_NULL(router_init());
    TEST_ASSERT_NULL(sys_event_init());
    TEST_ASSERT_NULL(sys_session_init());
    TEST_ASSERT_NULL(data_matrix_init());
    fake_add(&s_a, RAW_A, SYS_DATA_PROVIDER_STATE_UP);
    fake_add(&s_b, RAW_B, SYS_DATA_PROVIDER_STATE_UP);
    fake_add(&s_ses, SES_ID, SYS_DATA_PROVIDER_STATE_UP | SYS_DATA_PROVIDER_STATE_FRAMED);
    fake_add(&s_cmd, CMD_ID, SYS_DATA_PROVIDER_STATE_UP);
    TEST_ASSERT_NULL(data_matrix_listen(on_data, NULL, &s_cb_end));
    static const data_matrix_stream_t k_streams[] = {{6, DATA_MATRIX_URGENT}};
    TEST_ASSERT_NULL(data_matrix_streams(k_streams, 1));
    started = true;
  }
  host_log_clear();
  fake_clear(&s_a);
  fake_clear(&s_b);
  fake_clear(&s_ses);
  fake_clear(&s_cmd);
  pthread_mutex_lock(&s_cb.m);
  s_cb.count = 0;
  pthread_mutex_unlock(&s_cb.m);
}

void tearDown(void) {
  const uint8_t providers[] = {RAW_A, RAW_B, SES_ID, CMD_ID};
  for (uint32_t i = 0; i < sizeof(providers); i++) {
    err_free(data_matrix_clear_role(providers[i]));
  }
  for (uint8_t stream = 1; stream < 10; stream++) {
    const data_matrix_end_t end = ES(stream);
    err_free(data_matrix_clear(&end));
  }
  const data_matrix_end_t any = ET(DATA_MATRIX_ANY, 0x41);
  err_free(data_matrix_clear(&any));
  const data_matrix_end_t any2 = ET(DATA_MATRIX_ANY, 0x42);
  err_free(data_matrix_clear(&any2));
  uint8_t roles = 0;
  uint8_t routes = 0;
  rows(&roles, &routes);
  TEST_ASSERT_EQUAL_UINT8(0, roles);
  TEST_ASSERT_EQUAL_UINT8(0, routes);
}

/* ---- roles ---------------------------------------------------------------------------------------------------- */

static void rx_unused(void* ctx, uint8_t id, const uint8_t* data, uint32_t len) {
  (void)ctx;
  (void)id;
  (void)data;
  (void)len;
}

static void test_a_role_is_given_once_and_cleared(void) {
  give_raw(RAW_A);
  give_raw(RAW_A);  // the same again: a stored record runs at every start
  TEST_ASSERT_EQUAL_UINT8(DATA_MATRIX_ROLE_RAW, data_matrix_role_of(RAW_A));
  const data_matrix_role_args_t cmds = {.provider = RAW_A, .role = DATA_MATRIX_ROLE_COMMANDS};
  ASSERT_ERR(data_matrix_set_role(&cmds), ERR_DATA_MATRIX_ROLE);
  TEST_ASSERT_EQUAL_UINT8(DATA_MATRIX_ROLE_RAW, data_matrix_role_of(RAW_A));
  TEST_ASSERT_NULL(data_matrix_clear_role(RAW_A));
  TEST_ASSERT_EQUAL_UINT8(DATA_MATRIX_ROLE_NONE, data_matrix_role_of(RAW_A));
  TEST_ASSERT_NULL(data_matrix_clear_role(RAW_A));  // none: nothing to clear
  TEST_ASSERT_NULL(sys_data_provider_open(RAW_A, rx_unused, NULL));  // its rx hook is free again
  TEST_ASSERT_NULL(sys_data_provider_close(RAW_A));
}

static void test_a_role_that_fails_leaves_nothing(void) {
  const data_matrix_role_args_t nowhere = raw_role(NOWHERE);
  ASSERT_ERR(data_matrix_set_role(&nowhere), ERR_PROV_NOT_FOUND);
  TEST_ASSERT_EQUAL_UINT8(DATA_MATRIX_ROLE_NONE, data_matrix_role_of(NOWHERE));
  const data_matrix_role_args_t raw_session = {.provider = RAW_A, .role = DATA_MATRIX_ROLE_SESSION};
  ASSERT_ERR(data_matrix_set_role(&raw_session), ERR_SESSION_NOT_FRAMED);  // a raw provider carries no frame
  TEST_ASSERT_EQUAL_UINT8(DATA_MATRIX_ROLE_NONE, data_matrix_role_of(RAW_A));
  const data_matrix_role_args_t deadman_on_raw = {.provider = RAW_A, .role = DATA_MATRIX_ROLE_RAW, .deadman_ms = 500};
  ASSERT_ERR(data_matrix_set_role(&deadman_on_raw), ERR_DATA_MATRIX_BAD_ARG);
}

/* ---- routes --------------------------------------------------------------------------------------------------- */

static void test_a_route_needs_the_roles_and_an_allowed_pair(void) {
  const data_matrix_route_t to_a = {ES(5), EP(RAW_A)};
  ASSERT_ERR(data_matrix_route(&to_a), ERR_DATA_MATRIX_PAIR);  // no role: not attached
  give_raw(RAW_A);
  TEST_ASSERT_NULL(data_matrix_route(&to_a));
  TEST_ASSERT_NULL(data_matrix_route(&to_a));  // there already
  uint8_t roles = 0;
  uint8_t routes = 0;
  rows(&roles, &routes);
  TEST_ASSERT_EQUAL_UINT8(1, routes);

  const data_matrix_route_t stream_to_stream = {ES(5), ES(6)};
  ASSERT_ERR(data_matrix_route(&stream_to_stream), ERR_DATA_MATRIX_PAIR);
  const data_matrix_route_t to_unknown_callback = {EP(RAW_A), {DATA_MATRIX_END_C, 9, 0}};
  ASSERT_ERR(data_matrix_route(&to_unknown_callback), ERR_DATA_MATRIX_PAIR);
  const data_matrix_route_t from_callback = {s_cb_end, EP(RAW_A)};
  ASSERT_ERR(data_matrix_route(&from_callback), ERR_DATA_MATRIX_PAIR);
  const data_matrix_route_t tagged_from_raw = {ET(RAW_A, 3), s_cb_end};
  ASSERT_ERR(data_matrix_route(&tagged_from_raw), ERR_DATA_MATRIX_PAIR);  // a tag comes from a session
  const data_matrix_route_t tagged_to_raw = {ES(5), ET(RAW_A, 3)};
  TEST_ASSERT_NULL(data_matrix_route(&tagged_to_raw));  // a raw provider takes the data without the tag
  const data_matrix_route_t from_any = {ET(DATA_MATRIX_ANY, 0x41), s_cb_end};
  TEST_ASSERT_NULL(data_matrix_route(&from_any));  // that tag from every session: no provider's role
}

static void test_a_full_table_refuses_a_route(void) {
  give_raw(RAW_A);
  for (uint8_t i = 0; i < CONFIG_DATA_MATRIX_ROUTES; i++) {
    const data_matrix_route_t r = {ES(1), ET(RAW_A, i)};
    TEST_ASSERT_NULL(data_matrix_route(&r));
  }
  const data_matrix_route_t one_more = {ES(2), EP(RAW_A)};
  ASSERT_ERR(data_matrix_route(&one_more), ERR_DATA_MATRIX_FULL);
}

static void test_unroute_and_clear_remove_routes(void) {
  give_raw(RAW_A);
  give_raw(RAW_B);
  const data_matrix_route_t one = {ES(5), EP(RAW_A)};
  const data_matrix_route_t two = {ES(5), EP(RAW_B)};
  const data_matrix_route_t three = {EP(RAW_A), EP(RAW_B)};
  TEST_ASSERT_NULL(data_matrix_route(&one));
  TEST_ASSERT_NULL(data_matrix_route(&two));
  TEST_ASSERT_NULL(data_matrix_route(&three));
  TEST_ASSERT_NULL(data_matrix_unroute(&one));
  TEST_ASSERT_NULL(data_matrix_unroute(&one));  // not there: nothing
  uint8_t roles = 0;
  uint8_t routes = 0;
  rows(&roles, &routes);
  TEST_ASSERT_EQUAL_UINT8(2, routes);
  const data_matrix_end_t a = EP(RAW_A);
  TEST_ASSERT_NULL(data_matrix_clear(&a));  // on either side
  rows(&roles, &routes);
  TEST_ASSERT_EQUAL_UINT8(1, routes);  // ES(5) to B stays
}

static void test_clearing_a_role_takes_its_routes_with_it(void) {
  give_raw(RAW_A);
  give_raw(RAW_B);
  route(ES(5), EP(RAW_A));
  route(EP(RAW_A), EP(RAW_B));
  route(ES(5), EP(RAW_B));
  TEST_ASSERT_NULL(data_matrix_clear_role(RAW_A));
  uint8_t roles = 0;
  uint8_t routes = 0;
  rows(&roles, &routes);
  TEST_ASSERT_EQUAL_UINT8(1, roles);
  TEST_ASSERT_EQUAL_UINT8(1, routes);  // only ES(5) to B is left
}

/* ---- delivery ------------------------------------------------------------------------------------------------- */

static void test_a_stream_reaches_every_route(void) {
  give_raw(RAW_A);
  give_raw(RAW_B);
  route(ES(5), EP(RAW_A));
  route(ES(5), EP(RAW_B));
  TEST_ASSERT_NULL(data_matrix_send(5, "hello", 5));
  sent_t a;
  sent_t b;
  TEST_ASSERT_TRUE(next_sent(&s_a, &a, WAIT_MS));
  TEST_ASSERT_TRUE(next_sent(&s_b, &b, WAIT_MS));
  TEST_ASSERT_EQUAL_UINT32(5, a.len);
  TEST_ASSERT_EQUAL_MEMORY("hello", a.data, 5);
  TEST_ASSERT_EQUAL_MEMORY("hello", b.data, 5);
  TEST_ASSERT_EQUAL_HEX8(CMD_SEND_NOWAIT, a.flags);  // never blocked on
}

static void test_an_urgent_stream_goes_to_the_front(void) {
  give_raw(RAW_A);
  route(ES(6), EP(RAW_A));
  TEST_ASSERT_NULL(data_matrix_send(6, "!", 1));
  sent_t a;
  TEST_ASSERT_TRUE(next_sent(&s_a, &a, WAIT_MS));
  TEST_ASSERT_EQUAL_HEX8(CMD_SEND_NOWAIT | CMD_SEND_URGENT, a.flags);
}

static void test_a_stream_without_a_route_is_dropped(void) {
  give_raw(RAW_A);
  TEST_ASSERT_NULL(data_matrix_send(5, "nobody", 6));
  sent_t a;
  TEST_ASSERT_FALSE(next_sent(&s_a, &a, QUIET_MS));
  ASSERT_ERR(data_matrix_send(0, "x", 1), ERR_DATA_MATRIX_BAD_ARG);
  ASSERT_ERR(data_matrix_send(0x80, "x", 1), ERR_DATA_MATRIX_BAD_ARG);
  uint8_t big[CMD_DATA_MAX];
  memset(big, 0, sizeof(big));
  ASSERT_ERR(data_matrix_send(5, big, sizeof(big)), ERR_DATA_MATRIX_TOO_LONG);
}

static void test_a_raw_provider_bridges_to_another(void) {
  give_raw(RAW_A);
  give_raw(RAW_B);
  route(EP(RAW_A), EP(RAW_B));
  sys_data_provider_deliver(s_a.p, (const uint8_t*)"tunnel", 6);
  sent_t b;
  TEST_ASSERT_TRUE(next_sent(&s_b, &b, WAIT_MS));
  TEST_ASSERT_EQUAL_UINT32(6, b.len);
  TEST_ASSERT_EQUAL_MEMORY("tunnel", b.data, 6);
  sent_t a;
  TEST_ASSERT_FALSE(next_sent(&s_a, &a, QUIET_MS));  // a route goes one way
}

static void test_a_provider_with_no_role_does_nothing_with_its_bytes(void) {
  give_raw(RAW_B);
  sys_data_provider_deliver(s_a.p, (const uint8_t*)"dropped", 7);
  sent_t b;
  TEST_ASSERT_FALSE(next_sent(&s_b, &b, QUIET_MS));
}

static void test_a_provider_feeds_a_stream_and_a_callback(void) {
  give_raw(RAW_A);
  route(EP(RAW_A), ES(7));
  route(ES(7), s_cb_end);
  route(EP(RAW_A), s_cb_end);
  sys_data_provider_deliver(s_a.p, (const uint8_t*)"abc", 3);
  TEST_ASSERT_TRUE(wait_calls(2, WAIT_MS));  // once from the provider, once from the stream it entered
  bool from_provider = false;
  bool from_stream = false;
  pthread_mutex_lock(&s_cb.m);
  for (uint32_t i = 0; i < 2; i++) {
    TEST_ASSERT_EQUAL_UINT32(3, s_cb.got[i].len);
    TEST_ASSERT_EQUAL_MEMORY("abc", s_cb.got[i].data, 3);
    from_provider = from_provider || is_end(&s_cb.got[i].from, DATA_MATRIX_END_P, RAW_A, 0);
    from_stream = from_stream || is_end(&s_cb.got[i].from, DATA_MATRIX_END_S, 7, 0);
  }
  pthread_mutex_unlock(&s_cb.m);
  TEST_ASSERT_TRUE(from_provider);
  TEST_ASSERT_TRUE(from_stream);
}

/* ---- the session as a layer ----------------------------------------------------------------------------------- */

static void deliver_frame(uint8_t frame, const void* body, uint32_t len) {
  uint8_t buf[CMD_FRAME_MAX];
  const sys_session_hdr_t h = {.frame = frame, .seq = 0};
  memcpy(buf, &h, SYS_SESSION_HDR_BYTES);
  memcpy(buf + SYS_SESSION_HDR_BYTES, body, len);
  sys_data_provider_deliver(s_ses.p, buf, SYS_SESSION_HDR_BYTES + len);
}

// The peer's hello on the session's provider: the session opens.
static void open_session(void) {
  const data_matrix_role_args_t role = {.provider = SES_ID, .role = DATA_MATRIX_ROLE_SESSION};
  TEST_ASSERT_NULL(data_matrix_set_role(&role));
  uint8_t body[1 + sizeof(sys_session_hello_t)] = {SYS_SESSION_HELLO};
  const sys_session_hello_t hello = {.id = PEER, .boot_id = 1, .build_id = 0};
  memcpy(body + 1, &hello, sizeof(hello));
  deliver_frame(SYS_SESSION_FRAME(SYS_SESSION_CONTROL), body, sizeof(body));
  TEST_ASSERT_EQUAL_HEX8(PEER, sys_session_peer(SES_ID));
}

static void push_from_peer(uint8_t tag, const char* payload) {
  uint8_t body[CMD_DATA_MAX];
  body[0] = tag;
  const uint32_t n = (uint32_t)strlen(payload);
  memcpy(body + 1, payload, n);
  deliver_frame(SYS_SESSION_FRAME(SYS_SESSION_PUSH), body, 1u + n);
}

static void test_a_push_with_a_tag_reaches_its_routes_only(void) {
  open_session();
  route(ET(SES_ID, 0x41), s_cb_end);
  push_from_peer(0x41, "watch");
  TEST_ASSERT_TRUE(wait_calls(1, WAIT_MS));
  pthread_mutex_lock(&s_cb.m);
  TEST_ASSERT_TRUE(is_end(&s_cb.got[0].from, DATA_MATRIX_END_T, SES_ID, 0x41));
  TEST_ASSERT_EQUAL_UINT32(5, s_cb.got[0].len);  // the payload: the tag is not in it
  TEST_ASSERT_EQUAL_MEMORY("watch", s_cb.got[0].data, 5);
  pthread_mutex_unlock(&s_cb.m);
  push_from_peer(0x77, "other");  // no route for this tag
  TEST_ASSERT_FALSE(wait_calls(2, QUIET_MS));
}

static void test_a_tag_from_any_session_reaches_a_callback(void) {
  open_session();
  route(ET(DATA_MATRIX_ANY, 0x42), s_cb_end);
  push_from_peer(0x42, "any");
  TEST_ASSERT_TRUE(wait_calls(1, WAIT_MS));
  pthread_mutex_lock(&s_cb.m);
  TEST_ASSERT_TRUE(is_end(&s_cb.got[0].from, DATA_MATRIX_END_T, SES_ID, 0x42));  // from is the provider it came on
  pthread_mutex_unlock(&s_cb.m);
}

static void test_a_tagged_target_is_a_push_on_a_session_and_bare_data_on_a_raw_provider(void) {
  open_session();
  give_raw(RAW_A);
  route(ES(5), ET(SES_ID, 0x03));
  route(ES(5), ET(RAW_A, 0x03));
  TEST_ASSERT_NULL(data_matrix_send(5, "log", 3));
  sent_t push;
  TEST_ASSERT_TRUE(next_push(&s_ses, &push, WAIT_MS));
  TEST_ASSERT_EQUAL_UINT32(SYS_SESSION_HDR_BYTES + 1u + 3u, push.len);
  TEST_ASSERT_EQUAL_HEX8(0x03, push.data[SYS_SESSION_HDR_BYTES]);  // the tag, then the data
  TEST_ASSERT_EQUAL_MEMORY("log", push.data + SYS_SESSION_HDR_BYTES + 1u, 3);
  sent_t raw;
  TEST_ASSERT_TRUE(next_sent(&s_a, &raw, WAIT_MS));
  TEST_ASSERT_EQUAL_UINT32(3, raw.len);  // no tag on a raw provider
  TEST_ASSERT_EQUAL_MEMORY("log", raw.data, 3);
}

static void test_send_to_answers_one_end(void) {
  open_session();
  const data_matrix_end_t to = ET(SES_ID, 0x50);
  TEST_ASSERT_NULL(data_matrix_send_to(&to, "entry", 5, DATA_MATRIX_URGENT));
  sent_t push;
  TEST_ASSERT_TRUE(next_push(&s_ses, &push, WAIT_MS));
  TEST_ASSERT_TRUE((push.data[0] & ROUTER_FLAG_URGENT) != 0);
  TEST_ASSERT_EQUAL_HEX8(0x50, push.data[SYS_SESSION_HDR_BYTES]);
  const data_matrix_end_t raw = EP(RAW_B);
  ASSERT_ERR(data_matrix_send_to(&raw, "x", 1, 0), ERR_DATA_MATRIX_ROLE);  // no role
  const data_matrix_end_t callback = s_cb_end;
  ASSERT_ERR(data_matrix_send_to(&callback, "x", 1, 0), ERR_DATA_MATRIX_PAIR);
  TEST_ASSERT_NULL(data_matrix_clear_role(SES_ID));
  ASSERT_ERR(data_matrix_send_to(&to, "x", 1, 0), ERR_DATA_MATRIX_ROLE);
}

static void test_a_push_on_a_session_not_open_is_refused(void) {
  const data_matrix_role_args_t role = {.provider = SES_ID, .role = DATA_MATRIX_ROLE_SESSION};
  TEST_ASSERT_NULL(data_matrix_set_role(&role));  // attached, the peer has not said hello
  const data_matrix_end_t to = ET(SES_ID, 0x50);
  ASSERT_ERR(data_matrix_send_to(&to, "x", 1, 0), ERR_SESSION_NOT_OPEN);
}

/* ---- the command port and the remote commands ----------------------------------------------------------------- */

static void test_a_command_port_runs_a_command_and_answers_on_the_provider(void) {
  const data_matrix_role_args_t role = {.provider = CMD_ID, .role = DATA_MATRIX_ROLE_COMMANDS};
  TEST_ASSERT_NULL(data_matrix_set_role(&role));
  const uint8_t list[] = {MODULE_ID_DATA_MATRIX, CMD_DIR_READ, DATA_MATRIX_OP_LIST};
  sys_data_provider_deliver(s_cmd.p, list, sizeof(list));
  sent_t answer;
  TEST_ASSERT_TRUE(next_sent(&s_cmd, &answer, WAIT_MS));
  TEST_ASSERT_EQUAL_HEX8(ROUTER_STATUS_OK, answer.data[0]);
  TEST_ASSERT_EQUAL_UINT8(1, answer.data[1]);  // one role: this port; and no route
  TEST_ASSERT_EQUAL_UINT32(1u + 1u + sizeof(data_matrix_role_args_t) + 1u, answer.len);
}

static void test_the_commands_set_roles_and_routes(void) {
  const data_matrix_role_args_t port = {.provider = CMD_ID, .role = DATA_MATRIX_ROLE_COMMANDS};
  TEST_ASSERT_NULL(data_matrix_set_role(&port));

  uint8_t role[3 + sizeof(data_matrix_role_args_t)] = {MODULE_ID_DATA_MATRIX, CMD_DIR_WRITE, DATA_MATRIX_OP_ROLE};
  const data_matrix_role_args_t raw = raw_role(RAW_A);
  memcpy(role + 3, &raw, sizeof(raw));
  sys_data_provider_deliver(s_cmd.p, role, sizeof(role));
  sent_t answer;
  TEST_ASSERT_TRUE(next_sent(&s_cmd, &answer, WAIT_MS));
  TEST_ASSERT_EQUAL_HEX8(ROUTER_STATUS_OK, answer.data[0]);
  TEST_ASSERT_EQUAL_UINT8(DATA_MATRIX_ROLE_RAW, data_matrix_role_of(RAW_A));

  uint8_t add[3 + sizeof(data_matrix_route_t)] = {MODULE_ID_DATA_MATRIX, CMD_DIR_WRITE, DATA_MATRIX_OP_ROUTE};
  const data_matrix_route_t r = {ES(5), EP(RAW_A)};
  memcpy(add + 3, &r, sizeof(r));
  sys_data_provider_deliver(s_cmd.p, add, sizeof(add));
  TEST_ASSERT_TRUE(next_sent(&s_cmd, &answer, WAIT_MS));
  TEST_ASSERT_EQUAL_HEX8(ROUTER_STATUS_OK, answer.data[0]);
  TEST_ASSERT_NULL(data_matrix_send(5, "ok", 2));
  sent_t a;
  TEST_ASSERT_TRUE(next_sent(&s_a, &a, WAIT_MS));

  // a route from a provider with no role: refused, answered ERROR
  const data_matrix_route_t bad = {EP(RAW_B), EP(RAW_A)};
  memcpy(add + 3, &bad, sizeof(bad));
  sys_data_provider_deliver(s_cmd.p, add, sizeof(add));
  TEST_ASSERT_TRUE(next_sent(&s_cmd, &answer, WAIT_MS));
  TEST_ASSERT_EQUAL_HEX8(ROUTER_STATUS_ERROR, answer.data[0]);
}

static void test_a_cleared_command_port_answers_nobody(void) {
  const data_matrix_role_args_t role = {.provider = CMD_ID, .role = DATA_MATRIX_ROLE_COMMANDS};
  TEST_ASSERT_NULL(data_matrix_set_role(&role));
  TEST_ASSERT_NULL(data_matrix_clear_role(CMD_ID));
  const uint8_t list[] = {MODULE_ID_DATA_MATRIX, CMD_DIR_READ, DATA_MATRIX_OP_LIST};
  sys_data_provider_deliver(s_cmd.p, list, sizeof(list));  // its rx hook is free again: nobody takes it
  sent_t answer;
  TEST_ASSERT_FALSE(next_sent(&s_cmd, &answer, QUIET_MS));
}

/* ---- the streams of a module ---------------------------------------------------------------------------------- */

#define TEST_STREAMS(X, P)                                  \
  X(P, FIRST,  0x20, 0,                  "the first stream") \
  X(P, SECOND, 0x21, DATA_MATRIX_URGENT, "the second stream")
DATA_MATRIX_STREAMS_DECLARE(TEST)
DATA_MATRIX_STREAMS_DEFINE(TEST)

static void test_a_modules_streams_are_declared_in_a_table(void) {
  TEST_ASSERT_EQUAL_INT(0x20, TEST_STREAM_FIRST);
  TEST_ASSERT_EQUAL_INT(0x21, TEST_STREAM_SECOND);
  TEST_ASSERT_EQUAL_INT(2, TEST_STREAMS_COUNT);
  TEST_ASSERT_NULL(data_matrix_streams(TEST_streams, TEST_STREAMS_COUNT));
  give_raw(RAW_A);
  route(ES(TEST_STREAM_SECOND), EP(RAW_A));
  TEST_ASSERT_NULL(data_matrix_send(TEST_STREAM_SECOND, "u", 1));
  sent_t a;
  TEST_ASSERT_TRUE(next_sent(&s_a, &a, WAIT_MS));
  TEST_ASSERT_EQUAL_HEX8(CMD_SEND_NOWAIT | CMD_SEND_URGENT, a.flags);
  const data_matrix_end_t second = ES(TEST_STREAM_SECOND);
  TEST_ASSERT_NULL(data_matrix_clear(&second));
  static const data_matrix_stream_t bad[] = {{0, 0}};
  ASSERT_ERR(data_matrix_streams(bad, 1), ERR_DATA_MATRIX_BAD_ARG);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_role_is_given_once_and_cleared);
  RUN_TEST(test_a_role_that_fails_leaves_nothing);
  RUN_TEST(test_a_route_needs_the_roles_and_an_allowed_pair);
  RUN_TEST(test_a_full_table_refuses_a_route);
  RUN_TEST(test_unroute_and_clear_remove_routes);
  RUN_TEST(test_clearing_a_role_takes_its_routes_with_it);
  RUN_TEST(test_a_stream_reaches_every_route);
  RUN_TEST(test_an_urgent_stream_goes_to_the_front);
  RUN_TEST(test_a_stream_without_a_route_is_dropped);
  RUN_TEST(test_a_raw_provider_bridges_to_another);
  RUN_TEST(test_a_provider_with_no_role_does_nothing_with_its_bytes);
  RUN_TEST(test_a_provider_feeds_a_stream_and_a_callback);
  RUN_TEST(test_a_push_with_a_tag_reaches_its_routes_only);
  RUN_TEST(test_a_tag_from_any_session_reaches_a_callback);
  RUN_TEST(test_a_tagged_target_is_a_push_on_a_session_and_bare_data_on_a_raw_provider);
  RUN_TEST(test_send_to_answers_one_end);
  RUN_TEST(test_a_push_on_a_session_not_open_is_refused);
  RUN_TEST(test_a_command_port_runs_a_command_and_answers_on_the_provider);
  RUN_TEST(test_the_commands_set_roles_and_routes);
  RUN_TEST(test_a_cleared_command_port_answers_nobody);
  RUN_TEST(test_a_modules_streams_are_declared_in_a_table);
  return UNITY_END();
}
