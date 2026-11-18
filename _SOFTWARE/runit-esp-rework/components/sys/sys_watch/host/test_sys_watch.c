/* Host tests of sys_watch (sys_watch.c, sys_watch_client.c, router/sys_watch_router.c) with real sys_session,
 * sys_event, router_queue and timer tasks, plus two fake link providers (UP client and DOWN slave).
 * Run: python test_host/run.py sys_watch   (from runit-esp/) */
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>

#include "data_matrix.h"
#include "board_time.h"
#include "cmd.h"
#include "cmd_table.h"
#include "err_record.h"
#include "host_assert.h"
#include "host_mocks.h"
#include "module_id.h"
#include "router.h"
#include "router_queue.h"
#include "router_table.h"
#include "sys_data_provider_driver.h"
#include "sys_event.h"
#include "sys_session.h"
#include "sys_watch.h"
#include "sys_watch_internal.h"
#include "sys_watch_router.h"
#include "unity.h"

#define FRAME SYS_SESSION_FRAME
#define FRAME_KIND SYS_SESSION_FRAME_KIND_OF

#define ASSERT_ERR(call, tag)       \
  do {                              \
    err_h e_ = (call);              \
    TEST_ASSERT_ERR_TAG((tag), e_); \
    err_free(e_);                   \
  } while (0)

#define BOARD   5u
#define CLIENT  0xF0u
#define SLAVE   9u
#define UP_ID   1u
#define DOWN_ID 2u
#define WAIT_MS 2000

/* ---- board identity stubs for sys_session -------------------------------------------------------------------- */

uint8_t sys_session_board_load_id(void) {
  return BOARD;
}
err_h sys_session_board_save_id(uint8_t id) {
  (void)id;
  return NULL;
}
uint32_t sys_session_board_boot_id(void) {
  return 0x11223344u;
}
uint32_t sys_session_board_build_id(void) {
  return 0x55667788u;
}

/* ---- dummy target module (key 0x3F): read returns u32 s_val or an error; write sets s_val ---------------------- */

MODULE_ID_DECLARE(MODULE_ID_TESTW, 0x3F)
#define ERR_TESTW_MAP(X, M) X(M, ERR_TESTW_FAIL, 1, ERR_SEV_MEDIUM, { uint8_t code; })
ERR_TAGS_DECLARE(MODULE_ID_TESTW, ERR_TESTW_MAP)
ERR_MODULE_DEFINE(MODULE_ID_TESTW, ERR_TESTW_MAP)

typedef struct __attribute__((packed)) {
  uint16_t target;
} testw_target_t;

typedef struct __attribute__((packed)) {
  uint16_t target;
  uint32_t val;
} testw_write_t;

#define TESTW_OPERATIONS(X, P)                                       \
  X(P, OP_VAL,   0x01, testw_target_t, uint32_t,      0)             \
  X(P, OP_WRITE, 0x02, cmd_none_t,     testw_write_t, 0)             \
  X(P, OP_BIG,   0x03, cmd_none_t,     cmd_none_t,    0)

CMD_TABLE_DECLARE(TESTW)

static atomic_uint_fast32_t s_val = 42u;
static atomic_bool s_fail_read = false;

ROUTER_READ(TESTW, OP_VAL) {
  (void)args;
  if (atomic_load(&s_fail_read)) {
    return ERR_NEW(ERR_TESTW_FAIL, .code = 7);
  }
  const uint32_t v = (uint32_t)atomic_load(&s_val);
  return cmd_reply(out, &v, sizeof(v));
}

ROUTER_WRITE(TESTW, OP_WRITE) {
  atomic_store(&s_val, args->val);
  return NULL;
}

ROUTER_READ(TESTW, OP_BIG) {
  uint8_t big[SYS_WATCH_COPY_MAX + 8u];
  memset(big, 0xA5, sizeof(big));
  return cmd_reply(out, big, sizeof(big));
}

static const router_op_t k_testw_ops[] = {
    ROUTER_ENTRY_R(TESTW, OP_VAL),
    ROUTER_ENTRY_W(TESTW, OP_WRITE),
    ROUTER_ENTRY_R(TESTW, OP_BIG),
};

ROUTER_MODULE(testw_router, k_testw_ops)
ROUTER_MODULE_REGISTER(MODULE_ID_TESTW, testw_router)

/* ---- fake link providers ------------------------------------------------------------------------------------- */

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
  uint32_t head;
  uint32_t tail;
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

static bool next_sent(fake_t* f, sent_t* out, int ms) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  ts.tv_sec += ms / 1000;
  ts.tv_nsec += (long)(ms % 1000) * 1000000L;
  if (ts.tv_nsec >= 1000000000L) {
    ts.tv_sec++;
    ts.tv_nsec -= 1000000000L;
  }
  pthread_mutex_lock(&f->m);
  while (f->head == f->tail) {
    if (pthread_cond_timedwait(&f->c, &f->m, &ts) != 0) {
      pthread_mutex_unlock(&f->m);
      return false;
    }
  }
  *out = f->sent[f->head++ % SENT_MAX];
  pthread_mutex_unlock(&f->m);
  return true;
}

static bool next_kind(fake_t* f, uint8_t want_kind, sent_t* out, int ms) {
  const uint64_t end = board_time_uptime_ms() + (uint64_t)ms;
  while (true) {
    const uint64_t now = board_time_uptime_ms();
    if (now >= end) {
      return false;
    }
    sent_t m;
    if (!next_sent(f, &m, (int)(end - now))) {
      return false;
    }
    const sys_session_hdr_t* h = (const sys_session_hdr_t*)m.data;
    if (FRAME_KIND(h->frame) == want_kind) {
      *out = m;
      return true;
    }
  }
}

static void deliver(fake_t* f, uint8_t frame, uint8_t dst, uint8_t src, uint16_t seq, const void* body,
                    uint32_t body_len) {
  (void)dst;
  (void)src;
  uint8_t buf[CMD_FRAME_MAX];
  const sys_session_hdr_t hdr = {.frame = frame, .seq = seq};
  memcpy(buf, &hdr, sizeof(hdr));
  if (body_len > 0u) {
    memcpy(buf + sizeof(hdr), body, body_len);
  }
  sys_data_provider_deliver(f->p, buf, sizeof(hdr) + body_len);
}

static void send_hello(fake_t* f, uint8_t from, uint32_t boot, uint16_t timeout) {
  const sys_session_hello_t h = {.id = from, .boot_id = boot, .build_id = 0x11112222u};
  (void)timeout;
  uint8_t body[1 + sizeof(h)];
  body[0] = SYS_SESSION_HELLO;
  memcpy(body + 1, &h, sizeof(h));
  deliver(f, FRAME(SYS_SESSION_CONTROL), BOARD, from, 0, body, sizeof(body));
}

static uint16_t s_seq = 100u;

static sent_t call_over(fake_t* f, uint8_t dst, uint8_t src, const void* req, uint32_t len) {
  const uint16_t seq = ++s_seq;
  deliver(f, FRAME(SYS_SESSION_REQUEST), dst, src, seq, req, len);
  sent_t m;
  while (true) {
    TEST_ASSERT_TRUE(next_kind(f, SYS_SESSION_ANSWER, &m, WAIT_MS));
    const sys_session_hdr_t* h = (const sys_session_hdr_t*)m.data;
    if (h->seq == seq) {
      return m;
    }
  }
}

static bool next_watch_push(fake_t* f, uint8_t* id, uint8_t* status, uint8_t* payload, uint32_t* plen, int ms) {
  const uint64_t end = board_time_uptime_ms() + (uint64_t)ms;
  while (true) {
    const uint64_t now = board_time_uptime_ms();
    if (now >= end) {
      return false;
    }
    sent_t m;
    if (!next_kind(f, SYS_SESSION_PUSH, &m, (int)(end - now))) {
      return false;
    }
    const uint8_t* b = m.data + sizeof(sys_session_hdr_t);
    const uint32_t blen = m.len - sizeof(sys_session_hdr_t);
    if (blen >= 3u && b[0] == SYS_WATCH_CHANNEL) {
      *id = b[1];
      *status = b[2];
      *plen = blen - 3u;
      if (*plen > 0u) {
        memcpy(payload, b + 3u, *plen);
      }
      return true;
    }
  }
}

// A provider a session runs on: its role.
static err_h attach(uint8_t provider) {
  const data_matrix_role_args_t role = {.provider = provider, .role = DATA_MATRIX_ROLE_SESSION};
  return data_matrix_set_role(&role);
}

static void open_sessions(void) {
  TEST_ASSERT_NULL(attach(UP_ID));
  TEST_ASSERT_NULL(attach(DOWN_ID));
  send_hello(&s_up, CLIENT, 0xAAAA0001u, 1500u);
  send_hello(&s_down, SLAVE, 0xBBBB0002u, 1500u);
  sent_t m;
  TEST_ASSERT_TRUE(next_kind(&s_up, SYS_SESSION_CONTROL, &m, WAIT_MS));
  TEST_ASSERT_TRUE(next_kind(&s_down, SYS_SESSION_CONTROL, &m, WAIT_MS));
  fake_clear(&s_up);
  fake_clear(&s_down);
}

static void clear_all_server_watches(void) {
  sys_watch_info_t list[CONFIG_SYS_WATCH_MAX];
  const uint8_t n = sys_watch_list(list);
  for (uint8_t i = 0; i < n; i++) {
    err_h e = sys_watch_remove(list[i].owner, list[i].head.id);
    if (e != NULL) {
      err_free(e);
    }
  }
}

static const uint8_t k_val_read[] = {MODULE_ID_TESTW, CMD_DIR_READ, TESTW_OP_VAL, 0x05, 0x00};
static const uint8_t k_val_write[] = {MODULE_ID_TESTW, CMD_DIR_WRITE, TESTW_OP_VAL, 0x05, 0x00, 99, 0, 0, 0};

void setUp(void) {
  host_time_real(true);
  atomic_store(&s_val, 42u);
  atomic_store(&s_fail_read, false);
  clear_all_server_watches();
  fake_clear(&s_up);
  fake_clear(&s_down);
}

void tearDown(void) {
  clear_all_server_watches();
}

/* ---- tests --------------------------------------------------------------------------------------------------- */

void test_server_add_validation_and_list(void) {
  sys_watch_head_t h = {.id = 1, .trigger = SYS_WATCH_PERIODIC, .period = 2};

  // Invalid id / period / trigger / owner / len
  h.id = 0;
  ASSERT_ERR(sys_watch_add(&h, (cmd_in_t){k_val_read, sizeof(k_val_read)}, CLIENT), ERR_WATCH_BAD_ARG);
  h.id = 1;
  h.period = 0;
  ASSERT_ERR(sys_watch_add(&h, (cmd_in_t){k_val_read, sizeof(k_val_read)}, CLIENT), ERR_WATCH_BAD_ARG);
  h.period = 2;
  h.trigger = 9;
  ASSERT_ERR(sys_watch_add(&h, (cmd_in_t){k_val_read, sizeof(k_val_read)}, CLIENT), ERR_WATCH_BAD_ARG);
  h.trigger = SYS_WATCH_PERIODIC;
  ASSERT_ERR(sys_watch_add(&h, (cmd_in_t){k_val_read, sizeof(k_val_read)}, SYS_SESSION_ID_SELF), ERR_WATCH_BAD_ARG);
  ASSERT_ERR(sys_watch_add(&h, (cmd_in_t){k_val_read, sizeof(k_val_read)}, SYS_SESSION_ID_BROADCAST), ERR_WATCH_BAD_ARG);

  const uint8_t too_long[SYS_WATCH_READ_MAX + 1u] = {MODULE_ID_TESTW, CMD_DIR_READ};
  ASSERT_ERR(sys_watch_add(&h, (cmd_in_t){too_long, sizeof(too_long)}, CLIENT), ERR_WATCH_BAD_ARG);

  // Non-read command -> ERR_WATCH_NOT_READ
  ASSERT_ERR(sys_watch_add(&h, (cmd_in_t){k_val_write, sizeof(k_val_write)}, CLIENT), ERR_WATCH_NOT_READ);

  // Fill all CONFIG_SYS_WATCH_MAX slots
  for (uint8_t i = 1; i <= CONFIG_SYS_WATCH_MAX; i++) {
    h.id = i;
    TEST_ASSERT_NULL(sys_watch_add(&h, (cmd_in_t){k_val_read, sizeof(k_val_read)}, CLIENT));
  }
  h.id = (uint8_t)(CONFIG_SYS_WATCH_MAX + 1u);
  ASSERT_ERR(sys_watch_add(&h, (cmd_in_t){k_val_read, sizeof(k_val_read)}, CLIENT), ERR_WATCH_FULL);

  // Replace an existing watch even when table is full
  h.id = 1;
  h.period = 5;
  TEST_ASSERT_NULL(sys_watch_add(&h, (cmd_in_t){k_val_read, sizeof(k_val_read)}, CLIENT));

  sys_watch_info_t info[CONFIG_SYS_WATCH_MAX];
  TEST_ASSERT_EQUAL_UINT8(CONFIG_SYS_WATCH_MAX, sys_watch_list(info));
  TEST_ASSERT_EQUAL_UINT16(5, info[0].head.period);
  TEST_ASSERT_EQUAL_UINT8(CLIENT, info[0].owner);

  ASSERT_ERR(sys_watch_remove(CLIENT, 99), ERR_WATCH_NOT_FOUND);
  TEST_ASSERT_NULL(sys_watch_remove(CLIENT, 1));
  ASSERT_ERR(sys_watch_remove(CLIENT, 1), ERR_WATCH_NOT_FOUND);
}

void test_server_periodic_and_router_no_owner(void) {
  const sys_watch_head_t head = {.id = 7, .trigger = SYS_WATCH_PERIODIC, .period = 2};
  uint8_t req[3 + sizeof(head) + sizeof(k_val_read)];
  req[0] = MODULE_ID_WATCH;
  req[1] = CMD_DIR_WRITE;
  req[2] = WATCH_OP_ADD;
  memcpy(req + 3, &head, sizeof(head));
  memcpy(req + 3 + sizeof(head), k_val_read, sizeof(k_val_read));

  const sent_t ans = call_over(&s_up, BOARD, CLIENT, req, sizeof(req));
  const sys_session_hdr_t* ah = (const sys_session_hdr_t*)ans.data;
  TEST_ASSERT_EQUAL_UINT8(0u, ah->frame & SYS_SESSION_FRAME_STATUS);

  uint8_t id = 0;
  uint8_t status = 0;
  uint8_t payload[CMD_DATA_MAX];
  uint32_t plen = 0;
  TEST_ASSERT_TRUE(next_watch_push(&s_up, &id, &status, payload, &plen, WAIT_MS));
  TEST_ASSERT_EQUAL_UINT8(7, id);
  TEST_ASSERT_EQUAL_UINT8(ROUTER_STATUS_OK, status);
  TEST_ASSERT_EQUAL_UINT32(sizeof(uint32_t), plen);  // proves NO owner byte (P3)!
  uint32_t got = 0;
  memcpy(&got, payload, sizeof(got));
  TEST_ASSERT_EQUAL_UINT32(42u, got);

  // LIST via router
  const uint8_t list_req[] = {MODULE_ID_WATCH, CMD_DIR_READ, WATCH_OP_LIST};
  const sent_t lans = call_over(&s_up, BOARD, CLIENT, list_req, sizeof(list_req));
  const sys_session_hdr_t* lh = (const sys_session_hdr_t*)lans.data;
  TEST_ASSERT_EQUAL_UINT8(0u, lh->frame & SYS_SESSION_FRAME_STATUS);
  const uint8_t* lbody = lans.data + sizeof(*lh);
  TEST_ASSERT_EQUAL_UINT32(1u + sizeof(sys_watch_info_t), lans.len - sizeof(*lh));
  TEST_ASSERT_EQUAL_UINT8(1, lbody[0]);

  // REMOVE via router
  const uint8_t rem_req[] = {MODULE_ID_WATCH, CMD_DIR_WRITE, WATCH_OP_REMOVE, 7};
  const sent_t rans = call_over(&s_up, BOARD, CLIENT, rem_req, sizeof(rem_req));
  const sys_session_hdr_t* rh = (const sys_session_hdr_t*)rans.data;
  TEST_ASSERT_EQUAL_UINT8(0u, rh->frame & SYS_SESSION_FRAME_STATUS);
}

void test_server_on_change_silence_and_error_recovery(void) {
  const sys_watch_head_t head = {.id = 3, .trigger = SYS_WATCH_ON_CHANGE, .period = 2, .max_silence = 8};
  TEST_ASSERT_NULL(sys_watch_add(&head, (cmd_in_t){k_val_read, sizeof(k_val_read)}, CLIENT));

  uint8_t id = 0, status = 0, payload[CMD_DATA_MAX];
  uint32_t plen = 0;

  // Initial entry
  TEST_ASSERT_TRUE(next_watch_push(&s_up, &id, &status, payload, &plen, WAIT_MS));
  TEST_ASSERT_EQUAL_UINT8(ROUTER_STATUS_OK, status);

  // Quiet period before max_silence: no duplicate entry within 45 ms (period=20ms)
  TEST_ASSERT_FALSE(next_watch_push(&s_up, &id, &status, payload, &plen, 45));

  // Heartbeat after max_silence (80ms)
  TEST_ASSERT_TRUE(next_watch_push(&s_up, &id, &status, payload, &plen, 200));
  TEST_ASSERT_EQUAL_UINT8(ROUTER_STATUS_OK, status);

  // Value changes -> pushed promptly
  atomic_store(&s_val, 77u);
  TEST_ASSERT_TRUE(next_watch_push(&s_up, &id, &status, payload, &plen, 200));
  uint32_t v = 0;
  memcpy(&v, payload, sizeof(v));
  TEST_ASSERT_EQUAL_UINT32(77u, v);

  // Read error -> pushed once, then deduplicated (the last entry sent was an error)
  atomic_store(&s_fail_read, true);
  TEST_ASSERT_TRUE(next_watch_push(&s_up, &id, &status, payload, &plen, 200));
  TEST_ASSERT_EQUAL_UINT8(ROUTER_STATUS_ERROR, status);
  TEST_ASSERT_FALSE(next_watch_push(&s_up, &id, &status, payload, &plen, 60));

  // Recovery even to exact same pre-error value 77 -> emitted immediately
  atomic_store(&s_fail_read, false);
  TEST_ASSERT_TRUE(next_watch_push(&s_up, &id, &status, payload, &plen, 200));
  TEST_ASSERT_EQUAL_UINT8(ROUTER_STATUS_OK, status);
  memcpy(&v, payload, sizeof(v));
  TEST_ASSERT_EQUAL_UINT32(77u, v);
}

void test_server_a_gone_peer_takes_its_watches(void) {
  // One watch of each peer, ON_CHANGE without a heartbeat: one entry each, then quiet while the value stays.
  atomic_store(&s_val, 555u);
  const sys_watch_head_t up_head = {.id = 9, .trigger = SYS_WATCH_ON_CHANGE, .period = 2, .max_silence = 0};
  TEST_ASSERT_NULL(sys_watch_add(&up_head, (cmd_in_t){k_val_read, sizeof(k_val_read)}, CLIENT));
  const sys_watch_head_t sl_head = {.id = 4, .trigger = SYS_WATCH_ON_CHANGE, .period = 2, .max_silence = 0};
  TEST_ASSERT_NULL(sys_watch_add(&sl_head, (cmd_in_t){k_val_read, sizeof(k_val_read)}, SLAVE));

  uint8_t id = 0, status = 0, payload[CMD_DATA_MAX];
  uint32_t plen = 0;
  TEST_ASSERT_TRUE(next_watch_push(&s_up, &id, &status, payload, &plen, WAIT_MS));
  TEST_ASSERT_EQUAL_UINT8(9, id);
  TEST_ASSERT_TRUE(next_watch_push(&s_down, &id, &status, payload, &plen, WAIT_MS));
  TEST_ASSERT_EQUAL_UINT8(4, id);

  // Detach SLAVE -> SLAVE watch removed, CLIENT watch remains
  TEST_ASSERT_NULL(data_matrix_clear_role(DOWN_ID));
  sys_watch_info_t info[CONFIG_SYS_WATCH_MAX];
  TEST_ASSERT_EQUAL_UINT8(1, sys_watch_list(info));
  TEST_ASSERT_EQUAL_UINT8(CLIENT, info[0].owner);

  // Re-attach SLAVE so later tests have both sessions up
  TEST_ASSERT_NULL(attach(DOWN_ID));
  send_hello(&s_down, SLAVE, 0xBBBB0003u, 1500u);
  sent_t m;
  TEST_ASSERT_TRUE(next_kind(&s_down, SYS_SESSION_CONTROL, &m, WAIT_MS));
  fake_clear(&s_down);
}

/* Helper to expect WATCH ADD frame from client on s_down and extract its id + req */
static uint16_t expect_client_add(uint8_t* watch_id) {
  sent_t m;
  while (next_kind(&s_down, SYS_SESSION_REQUEST, &m, WAIT_MS)) {
    const sys_session_hdr_t* h = (const sys_session_hdr_t*)m.data;
    const uint8_t* b = m.data + sizeof(*h);
    const uint32_t blen = m.len - sizeof(*h);
    if (blen >= 3u + sizeof(sys_watch_head_t) && b[0] == MODULE_ID_WATCH && b[1] == CMD_DIR_WRITE &&
        b[2] == WATCH_OP_ADD) {
      const sys_watch_head_t* head = (const sys_watch_head_t*)(b + 3u);
      *watch_id = head->id;
      TEST_ASSERT_EQUAL_UINT8(SYS_WATCH_ON_CHANGE, head->trigger);
      TEST_ASSERT_EQUAL_UINT16(2, head->period);       // 20 ms / 10 ms = 2
      TEST_ASSERT_EQUAL_UINT16(4, head->max_silence);  // 2x period
      return h->seq;
    }
  }
  TEST_FAIL_MESSAGE("expected WATCH ADD request on s_down");
  return 0;
}

static void inject_slave_push(uint8_t watch_id, uint8_t status, const void* payload, uint32_t plen) {
  uint8_t body[CMD_DATA_MAX];
  body[0] = SYS_WATCH_CHANNEL;
  body[1] = watch_id;
  body[2] = status;
  if (plen > 0u) {
    memcpy(body + 3u, payload, plen);
  }
  deliver(&s_down, FRAME(SYS_SESSION_PUSH), BOARD, SLAVE, 1u, body, 3u + plen);
}

void test_client_b3_gate_error_and_size_limits(void) {
  uint32_t out = 0, out_len = 0;

  // Arg checks
  ASSERT_ERR(sys_watch_read(SYS_SESSION_ID_SELF, (cmd_in_t){k_val_read, sizeof(k_val_read)}, &out, sizeof(out), &out_len),
             ERR_WATCH_BAD_ARG);
  ASSERT_ERR(sys_watch_read(SLAVE, (cmd_in_t){k_val_write, sizeof(k_val_write)}, &out, sizeof(out), &out_len),
             ERR_WATCH_NOT_READ);

  // First read claims slot -> ADDING, returns ERR_WATCH_NO_VALUE
  ASSERT_ERR(sys_watch_read(SLAVE, (cmd_in_t){k_val_read, sizeof(k_val_read)}, &out, sizeof(out), &out_len),
             ERR_WATCH_NO_VALUE);

  uint8_t wid = 0;
  const uint16_t add_seq = expect_client_add(&wid);

  // B3: Entry arriving BEFORE ADD OK answer MUST be ignored!
  const uint32_t stale_pre_add = 0xDEADBEEFu;
  inject_slave_push(wid, ROUTER_STATUS_OK, &stale_pre_add, sizeof(stale_pre_add));
  ASSERT_ERR(sys_watch_read(SLAVE, (cmd_in_t){k_val_read, sizeof(k_val_read)}, &out, sizeof(out), &out_len),
             ERR_WATCH_NO_VALUE);

  // Now deliver ADD OK answer -> transitions to ACTIVE (still no copy until next entry)
  deliver(&s_down, FRAME(SYS_SESSION_ANSWER), BOARD, SLAVE, add_seq, NULL, 0);
  ASSERT_ERR(sys_watch_read(SLAVE, (cmd_in_t){k_val_read, sizeof(k_val_read)}, &out, sizeof(out), &out_len),
             ERR_WATCH_NO_VALUE);

  // Now deliver genuine PUSH entry -> cached read succeeds!
  const uint32_t fresh = 0x12345678u;
  inject_slave_push(wid, ROUTER_STATUS_OK, &fresh, sizeof(fresh));
  TEST_ASSERT_NULL(sys_watch_read(SLAVE, (cmd_in_t){k_val_read, sizeof(k_val_read)}, &out, sizeof(out), &out_len));
  TEST_ASSERT_EQUAL_UINT32(sizeof(out), out_len);
  TEST_ASSERT_EQUAL_HEX32(0x12345678u, out);

  // Too-small caller buffer -> ERR_CMD_BAD_LEN, sets *len = 4
  uint8_t small_buf[2];
  ASSERT_ERR(sys_watch_read(SLAVE, (cmd_in_t){k_val_read, sizeof(k_val_read)}, small_buf, sizeof(small_buf), &out_len),
             ERR_CMD_BAD_LEN);
  TEST_ASSERT_EQUAL_UINT32(4u, out_len);

  // Too-large entry (> COPY_MAX) drops copy rather than serving old data
  uint8_t huge[SYS_WATCH_COPY_MAX + 4u];
  memset(huge, 0xEE, sizeof(huge));
  inject_slave_push(wid, ROUTER_STATUS_OK, huge, sizeof(huge));
  ASSERT_ERR(sys_watch_read(SLAVE, (cmd_in_t){k_val_read, sizeof(k_val_read)}, &out, sizeof(out), &out_len),
             ERR_WATCH_NO_VALUE);

  // Error entry -> returned wrapped in ERR_REMOTE over serialized cause
  err_h rem_cause = ERR_NEW(ERR_TESTW_FAIL, .code = 9);
  uint8_t rec[128];
  const uint32_t rlen = err_record(rem_cause, rec, sizeof(rec));
  err_free(rem_cause);
  inject_slave_push(wid, ROUTER_STATUS_ERROR, rec, rlen);
  err_h got_err = sys_watch_read(SLAVE, (cmd_in_t){k_val_read, sizeof(k_val_read)}, &out, sizeof(out), &out_len);
  TEST_ASSERT_ERR_TAG(ERR_REMOTE, got_err);
  TEST_ASSERT_NOT_NULL(err_cause(got_err));
  TEST_ASSERT_ERR_TAG(ERR_TESTW_FAIL, err_cause(got_err));
  err_free(got_err);

  // Wait out idle cleanup before next test (board_timer frees client slot after IDLE_MS = 600 ms)
  sent_t m;
  bool got_rem = false;
  while (next_kind(&s_down, SYS_SESSION_REQUEST, &m, 1000)) {
    const sys_session_hdr_t* h = (const sys_session_hdr_t*)m.data;
    const uint8_t* b = m.data + sizeof(*h);
    if (b[0] == MODULE_ID_WATCH && b[1] == CMD_DIR_WRITE && b[2] == WATCH_OP_REMOVE && b[3] == wid) {
      deliver(&s_down, FRAME(SYS_SESSION_ANSWER), BOARD, SLAVE, h->seq, NULL, 0);
      got_rem = true;
      break;
    }
  }
  TEST_ASSERT_TRUE(got_rem);
}

void test_client_stale_adds_the_watch_again(void) {
  uint32_t out = 0, out_len = 0;

  // Claim & activate slot for k_val_read
  ASSERT_ERR(sys_watch_read(SLAVE, (cmd_in_t){k_val_read, sizeof(k_val_read)}, &out, sizeof(out), &out_len),
             ERR_WATCH_NO_VALUE);
  uint8_t wid = 0;
  const uint16_t add_seq = expect_client_add(&wid);
  deliver(&s_down, FRAME(SYS_SESSION_ANSWER), BOARD, SLAVE, add_seq, NULL, 0);
  const uint32_t v1 = 0x11111111u;
  inject_slave_push(wid, ROUTER_STATUS_OK, &v1, sizeof(v1));
  TEST_ASSERT_NULL(sys_watch_read(SLAVE, (cmd_in_t){k_val_read, sizeof(k_val_read)}, &out, sizeof(out), &out_len));
  TEST_ASSERT_EQUAL_HEX32(v1, out);

  fake_clear(&s_down);
  // A write's answer came -> sys_watch_stale drops the copy and sends ADD for the same watch again
  sys_watch_stale(SLAVE, (cmd_in_t){k_val_write, sizeof(k_val_write)}, 2u);
  uint8_t wid2 = 0;
  const uint16_t add_seq2 = expect_client_add(&wid2);
  TEST_ASSERT_EQUAL_UINT8(wid, wid2);

  // An entry from before the write, arriving before that ADD's OK, is not taken
  const uint32_t pre_write = 0x22222222u;
  inject_slave_push(wid, ROUTER_STATUS_OK, &pre_write, sizeof(pre_write));
  ASSERT_ERR(sys_watch_read(SLAVE, (cmd_in_t){k_val_read, sizeof(k_val_read)}, &out, sizeof(out), &out_len),
             ERR_WATCH_NO_VALUE);

  // ADD's OK, then the watch's first entry: the value after the write
  deliver(&s_down, FRAME(SYS_SESSION_ANSWER), BOARD, SLAVE, add_seq2, NULL, 0);
  const uint32_t post_write = 0x33333333u;
  inject_slave_push(wid, ROUTER_STATUS_OK, &post_write, sizeof(post_write));
  TEST_ASSERT_NULL(sys_watch_read(SLAVE, (cmd_in_t){k_val_read, sizeof(k_val_read)}, &out, sizeof(out), &out_len));
  TEST_ASSERT_EQUAL_HEX32(post_write, out);

  // A write to another target leaves it as it is
  const uint8_t other_write[] = {MODULE_ID_TESTW, CMD_DIR_WRITE, TESTW_OP_VAL, 0x09, 0x00, 1, 0, 0, 0};
  sys_watch_stale(SLAVE, (cmd_in_t){other_write, sizeof(other_write)}, 2u);
  TEST_ASSERT_NULL(sys_watch_read(SLAVE, (cmd_in_t){k_val_read, sizeof(k_val_read)}, &out, sizeof(out), &out_len));
}

void test_client_staleness_gone_and_idle_remove(void) {
  uint32_t out = 0, out_len = 0;

  const uint8_t req2[] = {MODULE_ID_TESTW, CMD_DIR_READ, TESTW_OP_VAL, 0x06, 0x00};
  ASSERT_ERR(sys_watch_read(SLAVE, (cmd_in_t){req2, sizeof(req2)}, &out, sizeof(out), &out_len), ERR_WATCH_NO_VALUE);
  uint8_t wid = 0;
  const uint16_t seq1 = expect_client_add(&wid);
  deliver(&s_down, FRAME(SYS_SESSION_ANSWER), BOARD, SLAVE, seq1, NULL, 0);

  const uint32_t v = 0xCAFEBABEu;
  inject_slave_push(wid, ROUTER_STATUS_OK, &v, sizeof(v));
  TEST_ASSERT_NULL(sys_watch_read(SLAVE, (cmd_in_t){req2, sizeof(req2)}, &out, sizeof(out), &out_len));
  TEST_ASSERT_EQUAL_HEX32(v, out);

  // Past MAX_AGE_MS (3 * 20 = 60 ms) without new entry -> ERR_WATCH_STALE
  vTaskDelay(MSEC(85));
  ASSERT_ERR(sys_watch_read(SLAVE, (cmd_in_t){req2, sizeof(req2)}, &out, sizeof(out), &out_len), ERR_WATCH_STALE);

  // Refresh with new PUSH -> back to valid
  inject_slave_push(wid, ROUTER_STATUS_OK, &v, sizeof(v));
  TEST_ASSERT_NULL(sys_watch_read(SLAVE, (cmd_in_t){req2, sizeof(req2)}, &out, sizeof(out), &out_len));

  // SLAVE goes down -> on_gone moves slots to ADDING; read returns NO_VALUE and does not serve old value
  TEST_ASSERT_NULL(data_matrix_clear_role(DOWN_ID));
  ASSERT_ERR(sys_watch_read(SLAVE, (cmd_in_t){req2, sizeof(req2)}, &out, sizeof(out), &out_len), ERR_WATCH_NO_VALUE);

  // Re-attach SLAVE -> next read past retry backoff (20 ms) sends fresh WATCH ADD
  TEST_ASSERT_NULL(attach(DOWN_ID));
  send_hello(&s_down, SLAVE, 0xBBBB0009u, 1500u);
  sent_t m;
  TEST_ASSERT_TRUE(next_kind(&s_down, SYS_SESSION_CONTROL, &m, WAIT_MS));
  fake_clear(&s_down);

  vTaskDelay(MSEC(30));
  ASSERT_ERR(sys_watch_read(SLAVE, (cmd_in_t){req2, sizeof(req2)}, &out, sizeof(out), &out_len), ERR_WATCH_NO_VALUE);
  uint8_t wid2 = 0;
  const uint16_t seq2 = expect_client_add(&wid2);
  deliver(&s_down, FRAME(SYS_SESSION_ANSWER), BOARD, SLAVE, seq2, NULL, 0);
  inject_slave_push(wid2, ROUTER_STATUS_OK, &v, sizeof(v));
  TEST_ASSERT_NULL(sys_watch_read(SLAVE, (cmd_in_t){req2, sizeof(req2)}, &out, sizeof(out), &out_len));

  // Idle removal: wait past IDLE_MS (600 ms) for board_timer to send WATCH OP_REMOVE
  fake_clear(&s_down);
  bool got_remove = false;
  while (next_kind(&s_down, SYS_SESSION_REQUEST, &m, 1000)) {
    const sys_session_hdr_t* h = (const sys_session_hdr_t*)m.data;
    const uint8_t* b = m.data + sizeof(*h);
    if (b[0] == MODULE_ID_WATCH && b[1] == CMD_DIR_WRITE && b[2] == WATCH_OP_REMOVE && b[3] == wid2) {
      deliver(&s_down, FRAME(SYS_SESSION_ANSWER), BOARD, SLAVE, h->seq, NULL, 0);
      got_remove = true;
      break;
    }
  }
  TEST_ASSERT_TRUE(got_remove);
}

int main(void) {
  host_time_real(true);
  TEST_ASSERT_NULL(router_init());
  TEST_ASSERT_NULL(sys_event_init());
  TEST_ASSERT_NULL(sys_session_init());
  TEST_ASSERT_NULL(data_matrix_init());
  TEST_ASSERT_NULL(sys_watch_init());
  fake_add(&s_up, UP_ID);
  fake_add(&s_down, DOWN_ID);
  open_sessions();

  UNITY_BEGIN();
  RUN_TEST(test_server_add_validation_and_list);
  RUN_TEST(test_server_periodic_and_router_no_owner);
  RUN_TEST(test_server_on_change_silence_and_error_recovery);
  RUN_TEST(test_server_a_gone_peer_takes_its_watches);
  RUN_TEST(test_client_b3_gate_error_and_size_limits);
  RUN_TEST(test_client_stale_adds_the_watch_again);
  RUN_TEST(test_client_staleness_gone_and_idle_remove);
  return UNITY_END();
}
