/* Host tests of sys_stream (sys_stream.c, router/sys_stream_router.c) with real sys_session, sys_data_provider,
 * router_queue and timer tasks, and fake drivers for links and raw providers.
 * Run: python test_host/run.py sys_stream   (from runit-esp/) */
#include <pthread.h>
#include <string.h>
#include <time.h>

#include "cmd.h"
#include "cmd_table.h"
#include "host_assert.h"
#include "host_mocks.h"
#include "router_queue.h"
#include "sys_data_provider_driver.h"
#include "sys_session.h"
#include "sys_session_internal.h"
#include "sys_stream.h"
#include "sys_stream_internal.h"
#include "sys_stream_router.h"
#include "unity.h"

#define ASSERT_ERR(call, tag)        \
  do {                               \
    err_h e_ = (call);               \
    TEST_ASSERT_ERR_TAG((tag), e_);  \
    err_free(e_);                    \
  } while (0)

#define BOARD       5u
#define CLIENT      0xF0u
#define SLAVE       9u
#define UP_ID       1u
#define DOWN_ID     2u
#define RAW_A_ID    10u
#define RAW_B_ID    11u
#define REM_PROV_ID 0x22u
#define WAIT_MS     2000

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

/* ---- fake providers: link (framed) and raw ------------------------------------------------------------------- */

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
  err_h next_err;
} fake_t;

static fake_t s_up = {.m = PTHREAD_MUTEX_INITIALIZER, .c = PTHREAD_COND_INITIALIZER};
static fake_t s_down = {.m = PTHREAD_MUTEX_INITIALIZER, .c = PTHREAD_COND_INITIALIZER};
static fake_t s_raw_a = {.m = PTHREAD_MUTEX_INITIALIZER, .c = PTHREAD_COND_INITIALIZER};
static fake_t s_raw_b = {.m = PTHREAD_MUTEX_INITIALIZER, .c = PTHREAD_COND_INITIALIZER};

static err_h fake_send(sys_data_provider_t* self, cmd_in_t frame, uint8_t flags) {
  fake_t* f = self->driver;
  pthread_mutex_lock(&f->m);
  err_h fail = f->next_err;
  f->next_err = NULL;
  if (fail != NULL) {
    pthread_mutex_unlock(&f->m);
    return fail;
  }
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

static void fake_add(fake_t* f, uint8_t id, uint8_t type, uint8_t state) {
  uint8_t args[SYS_DATA_PROVIDER_CREATE_HDR + sizeof(fake_prov_cfg_t)] = {type, 0};
  const fake_prov_cfg_t cfg = {.fake = (uintptr_t)f, .state = state};
  memcpy(args + SYS_DATA_PROVIDER_CREATE_HDR, &cfg, sizeof(cfg));
  TEST_ASSERT_NULL(sys_data_provider_write(id, CONTRACT_PROVIDER_OP_CREATE, args, sizeof(args)));
}

static void fake_clear(fake_t* f) {
  pthread_mutex_lock(&f->m);
  f->head = f->tail;
  err_free(f->next_err);
  f->next_err = NULL;
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

static sys_session_hdr_t hdr_of(const sent_t* s) {
  sys_session_hdr_t h;
  memcpy(&h, s->data, sizeof(h));
  return h;
}

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

static void deliver_session(fake_t* f, uint8_t frame, uint8_t dst, uint8_t src, uint16_t seq, const void* body,
                            uint32_t len) {
  uint8_t buf[CMD_FRAME_MAX];
  const sys_session_hdr_t h = {.frame = frame, .dst = dst, .src = src, .seq = seq};
  memcpy(buf, &h, HDR);
  if (len > 0) {
    memcpy(buf + HDR, body, len);
  }
  sys_data_provider_deliver(f->p, buf, HDR + len);
}

static void deliver_hello(fake_t* f, uint8_t src, uint32_t boot_id) {
  uint8_t body[1 + sizeof(sys_session_hello_t)] = {SYS_SESSION_HELLO};
  const sys_session_hello_t h = {.id = src, .boot_id = boot_id, .build_id = 0, .timeout_ms = 3000};
  memcpy(body + 1, &h, sizeof(h));
  deliver_session(f, FRAME(SYS_SESSION_CONTROL), BOARD, src, 0, body, sizeof(body));
}

/* ---- callback capture ---------------------------------------------------------------------------------------- */

static struct {
  pthread_mutex_t m;
  pthread_cond_t c;
  int count;
  uint8_t id;
  uint8_t data[CMD_DATA_MAX];
  uint32_t len;
} s_cb = {.m = PTHREAD_MUTEX_INITIALIZER, .c = PTHREAD_COND_INITIALIZER};

static void on_cb(void* ctx, uint8_t id, const uint8_t* data, uint32_t len) {
  (void)ctx;
  pthread_mutex_lock(&s_cb.m);
  s_cb.id = id;
  s_cb.len = len;
  if (len > 0) {
    memcpy(s_cb.data, data, len);
  }
  s_cb.count++;
  pthread_cond_broadcast(&s_cb.c);
  pthread_mutex_unlock(&s_cb.m);
}

static bool wait_cb(int want, int ms) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  ts.tv_sec += ms / 1000;
  ts.tv_nsec += (long)(ms % 1000) * 1000000L;
  if (ts.tv_nsec >= 1000000000L) {
    ts.tv_sec++;
    ts.tv_nsec -= 1000000000L;
  }
  pthread_mutex_lock(&s_cb.m);
  while (s_cb.count < want) {
    if (pthread_cond_timedwait(&s_cb.c, &s_cb.m, &ts) != 0) {
      break;
    }
  }
  const bool ok = s_cb.count >= want;
  pthread_mutex_unlock(&s_cb.m);
  return ok;
}

/* ---- test setup & teardown ----------------------------------------------------------------------------------- */

void setUp(void) {
  static bool started;
  if (!started) {
    started = true;
    host_time_real(true);
    TEST_ASSERT_NULL(router_init());
    TEST_ASSERT_NULL(sys_session_init());
    TEST_ASSERT_NULL(sys_stream_init());
    const sys_stream_cfg_t sys_err = {.id = SYS_STREAM_ERRORS, .flags = SYS_STREAM_FLAG_URGENT};
    TEST_ASSERT_NULL(sys_stream_create(&sys_err));
    sys_stream_mark_system();

    fake_add(&s_up, UP_ID, SYS_DATA_PROVIDER_TYPE_UART,
             SYS_DATA_PROVIDER_STATE_FRAMED | SYS_DATA_PROVIDER_STATE_UP | SYS_DATA_PROVIDER_STATE_WIRED);
    fake_add(&s_down, DOWN_ID, SYS_DATA_PROVIDER_TYPE_UART,
             SYS_DATA_PROVIDER_STATE_FRAMED | SYS_DATA_PROVIDER_STATE_UP | SYS_DATA_PROVIDER_STATE_WIRED);
    fake_add(&s_raw_a, RAW_A_ID, SYS_DATA_PROVIDER_TYPE_UART, SYS_DATA_PROVIDER_STATE_UP);
    fake_add(&s_raw_b, RAW_B_ID, SYS_DATA_PROVIDER_TYPE_UART, SYS_DATA_PROVIDER_STATE_UP);
  }
  host_log_clear();
  fake_clear(&s_up);
  fake_clear(&s_down);
  fake_clear(&s_raw_a);
  fake_clear(&s_raw_b);
  pthread_mutex_lock(&s_cb.m);
  s_cb.count = 0;
  pthread_mutex_unlock(&s_cb.m);
}

void tearDown(void) {
  sys_stream_info_t list[CONFIG_SYS_STREAM_MAX];
  const uint8_t n = sys_stream_list(list);
  for (uint8_t i = 0; i < n; i++) {
    const uint8_t id = list[i].head.cfg.id;
    if (list[i].head.state & SYS_STREAM_STATE_SYSTEM) {
      err_free(sys_stream_unbind_all(id));
      err_free(sys_stream_unlisten(id, on_cb, NULL));
      err_free(sys_stream_unsubscribe(id, CLIENT));
      err_free(sys_stream_unsubscribe(id, SLAVE));
    } else {
      err_free(sys_stream_remove(id));
    }
  }
  err_free(sys_session_detach(UP_ID));
  err_free(sys_session_detach(DOWN_ID));
}

static void open_client(void) {
  TEST_ASSERT_NULL(sys_session_attach(UP_ID, SYS_SESSION_UP));
  sent_t s;
  TEST_ASSERT_TRUE(next_control(&s_up, SYS_SESSION_HELLO, &s, WAIT_MS));
  deliver_hello(&s_up, CLIENT, 0x100u);
  TEST_ASSERT_TRUE(next_control(&s_up, SYS_SESSION_HELLO, &s, WAIT_MS));
}

static void open_slave(void) {
  TEST_ASSERT_NULL(sys_session_attach(DOWN_ID, SYS_SESSION_DOWN));
  sent_t s;
  TEST_ASSERT_TRUE(next_control(&s_down, SYS_SESSION_HELLO, &s, WAIT_MS));
  deliver_hello(&s_down, SLAVE, 0x200u);
  TEST_ASSERT_TRUE(next_control(&s_down, SYS_SESSION_HELLO, &s, WAIT_MS));
}

static void send_cmd(fake_t* f, uint8_t src, uint16_t seq, const void* req, uint32_t len, sent_t* ans) {
  deliver_session(f, FRAME(SYS_SESSION_REQUEST), BOARD, src, seq, req, len);
  TEST_ASSERT_TRUE(next_kind(f, SYS_SESSION_ANSWER, ans, WAIT_MS));
  TEST_ASSERT_EQUAL_UINT16(seq, hdr_of(ans).seq);
}

/* ---- tests --------------------------------------------------------------------------------------------------- */

static void test_system_stream_cannot_be_removed_or_take_a_source(void) {
  ASSERT_ERR(sys_stream_remove(SYS_STREAM_ERRORS), ERR_STREAM_SYSTEM);
  ASSERT_ERR(sys_stream_source(SYS_STREAM_ERRORS, RAW_A_ID), ERR_STREAM_DIRECTION);

  // User streams reject reserved/invalid IDs and unknown flags.
  const sys_stream_cfg_t bad_zero = {.id = 0, .flags = 0};
  const sys_stream_cfg_t bad_chan = {.id = SYS_SESSION_CHANNEL_FIRST, .flags = 0};
  const sys_stream_cfg_t bad_flag = {.id = 0x10u, .flags = 0x80u};
  ASSERT_ERR(sys_stream_create(&bad_zero), ERR_STREAM_BAD_ARG);
  ASSERT_ERR(sys_stream_create(&bad_chan), ERR_STREAM_BAD_ARG);
  ASSERT_ERR(sys_stream_create(&bad_flag), ERR_STREAM_BAD_ARG);
}

static void test_send_delivers_to_local_sink_remote_sink_peer_and_callback(void) {
  open_client();
  open_slave();
  fake_clear(&s_up);
  fake_clear(&s_down);

  const sys_stream_cfg_t cfg = {.id = 0x10u, .flags = SYS_STREAM_FLAG_URGENT};
  TEST_ASSERT_NULL(sys_stream_create(&cfg));
  // Local SINK accepted both via SYS_SESSION_ID_SELF and BOARD (deduplicated).
  TEST_ASSERT_NULL(sys_stream_sink(0x10u, BOARD, RAW_A_ID));
  TEST_ASSERT_NULL(sys_stream_sink(0x10u, SYS_SESSION_ID_SELF, RAW_A_ID));
  TEST_ASSERT_NULL(sys_stream_sink(0x10u, SLAVE, REM_PROV_ID));
  TEST_ASSERT_NULL(sys_stream_subscribe(0x10u, CLIENT));
  TEST_ASSERT_NULL(sys_stream_listen(0x10u, on_cb, NULL));

  // Canonical local SINK peer is SYS_SESSION_ID_SELF and deduplication keeps 4 targets total.
  sys_stream_info_t info[CONFIG_SYS_STREAM_MAX];
  const uint8_t n = sys_stream_list(info);
  const sys_stream_info_t* found = NULL;
  for (uint8_t i = 0; i < n; i++) {
    if (info[i].head.cfg.id == 0x10u) {
      found = &info[i];
    }
  }
  TEST_ASSERT_NOT_NULL(found);
  TEST_ASSERT_EQUAL_UINT8(4, found->head.targets);

  const uint8_t msg[] = {'h', 'i', '!'};
  TEST_ASSERT_NULL(sys_stream_send(0x10u, msg, sizeof(msg)));

  // 1) Local SINK gets raw payload (no header), with NOWAIT | URGENT (not SPARE).
  sent_t raw_out;
  TEST_ASSERT_TRUE(next_sent(&s_raw_a, &raw_out, WAIT_MS));
  TEST_ASSERT_EQUAL_UINT32(sizeof(msg), raw_out.len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(msg, raw_out.data, sizeof(msg));
  TEST_ASSERT_EQUAL_HEX8(CMD_SEND_NOWAIT | CMD_SEND_URGENT, raw_out.flags);

  // 2) Remote SINK gets SYS_SESSION_CHANNEL_DATA [0x82][to][from][payload] through slave session.
  sent_t rem_out;
  TEST_ASSERT_TRUE(next_kind(&s_down, SYS_SESSION_PUSH, &rem_out, WAIT_MS));
  TEST_ASSERT_TRUE((hdr_of(&rem_out).frame & ROUTER_FLAG_URGENT) != 0);
  TEST_ASSERT_EQUAL_UINT8(SYS_SESSION_CHANNEL_DATA, rem_out.data[HDR]);
  TEST_ASSERT_EQUAL_UINT8(REM_PROV_ID, rem_out.data[HDR + 1]);
  TEST_ASSERT_EQUAL_UINT8(0x10u, rem_out.data[HDR + 2]);
  TEST_ASSERT_EQUAL_UINT32(sizeof(msg), rem_out.len - HDR - 3u);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(msg, rem_out.data + HDR + 3u, sizeof(msg));

  // 3) Subscribed PEER gets PUSH [stream_id][payload] with session seq.
  sent_t peer_out;
  TEST_ASSERT_TRUE(next_kind(&s_up, SYS_SESSION_PUSH, &peer_out, WAIT_MS));
  TEST_ASSERT_TRUE((hdr_of(&peer_out).frame & ROUTER_FLAG_URGENT) != 0);
  TEST_ASSERT_EQUAL_UINT8(0x10u, peer_out.data[HDR]);
  TEST_ASSERT_EQUAL_UINT32(sizeof(msg), peer_out.len - HDR - 1u);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(msg, peer_out.data + HDR + 1u, sizeof(msg));

  // 4) Callback ran on stream task.
  TEST_ASSERT_TRUE(wait_cb(1, WAIT_MS));
  TEST_ASSERT_EQUAL_UINT8(0x10u, s_cb.id);
  TEST_ASSERT_EQUAL_UINT32(sizeof(msg), s_cb.len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(msg, s_cb.data, sizeof(msg));
}

static void test_source_takes_rx_refuses_direct_send_and_forwards_to_local_and_remote_sink(void) {
  open_client();
  open_slave();
  fake_clear(&s_up);
  fake_clear(&s_down);

  const sys_stream_cfg_t cfg = {.id = 0x20u, .flags = 0};
  TEST_ASSERT_NULL(sys_stream_create(&cfg));
  TEST_ASSERT_NULL(sys_stream_source(0x20u, RAW_A_ID));
  TEST_ASSERT_NULL(sys_stream_sink(0x20u, SYS_SESSION_ID_SELF, RAW_B_ID));
  TEST_ASSERT_NULL(sys_stream_sink(0x20u, SLAVE, REM_PROV_ID));
  TEST_ASSERT_NULL(sys_stream_subscribe(0x20u, CLIENT));

  // While a SOURCE exists, sys_stream_send fails with ERR_STREAM_DIRECTION.
  const uint8_t direct[] = {1, 2};
  ASSERT_ERR(sys_stream_send(0x20u, direct, sizeof(direct)), ERR_STREAM_DIRECTION);

  // Another user cannot open the SOURCE provider's RX hook.
  ASSERT_ERR(sys_stream_source(SYS_STREAM_ERRORS, RAW_A_ID), ERR_STREAM_DIRECTION);
  const sys_stream_cfg_t other = {.id = 0x21u, .flags = 0};
  TEST_ASSERT_NULL(sys_stream_create(&other));
  ASSERT_ERR(sys_stream_source(0x21u, RAW_A_ID), ERR_PROV_IN_USE);

  // Incoming data on RAW_A_ID goes to local SINK (RAW_B_ID), remote SINK (SLAVE/REM_PROV_ID), and PEER (CLIENT).
  const uint8_t pkt[] = {0xAA, 0xBB, 0xCC, 0xDD};
  sys_data_provider_deliver(s_raw_a.p, pkt, sizeof(pkt));

  sent_t out;
  TEST_ASSERT_TRUE(next_sent(&s_raw_b, &out, WAIT_MS));
  TEST_ASSERT_EQUAL_UINT32(sizeof(pkt), out.len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(pkt, out.data, sizeof(pkt));

  TEST_ASSERT_TRUE(next_kind(&s_down, SYS_SESSION_PUSH, &out, WAIT_MS));
  TEST_ASSERT_EQUAL_UINT8(SYS_SESSION_CHANNEL_DATA, out.data[HDR]);
  TEST_ASSERT_EQUAL_UINT8(REM_PROV_ID, out.data[HDR + 1]);
  TEST_ASSERT_EQUAL_UINT8(0x20u, out.data[HDR + 2]);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(pkt, out.data + HDR + 3u, sizeof(pkt));

  TEST_ASSERT_TRUE(next_kind(&s_up, SYS_SESSION_PUSH, &out, WAIT_MS));
  TEST_ASSERT_EQUAL_UINT8(0x20u, out.data[HDR]);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(pkt, out.data + HDR + 1u, sizeof(pkt));

  // Unbinding the SOURCE returns RAW_A_ID's RX hook and lets the stream take sys_stream_send again.
  TEST_ASSERT_NULL(sys_stream_unbind(0x20u, RAW_A_ID));
  TEST_ASSERT_NULL(sys_stream_source(0x21u, RAW_A_ID));
  TEST_ASSERT_NULL(sys_stream_send(0x20u, direct, sizeof(direct)));
  TEST_ASSERT_TRUE(next_kind(&s_up, SYS_SESSION_PUSH, &out, WAIT_MS));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(direct, out.data + HDR + 1u, sizeof(direct));
}

static void test_gone_peer_drops_subscription_but_keeps_sink_and_counts_dropped(void) {
  open_client();
  open_slave();
  const sys_stream_cfg_t cfg = {.id = 0x11u, .flags = 0};
  TEST_ASSERT_NULL(sys_stream_create(&cfg));
  TEST_ASSERT_NULL(sys_stream_subscribe(0x11u, CLIENT));
  TEST_ASSERT_NULL(sys_stream_sink(0x11u, SLAVE, REM_PROV_ID));

  // End both sessions: PEER subscription is removed, SINK configuration remains.
  TEST_ASSERT_NULL(sys_session_detach(UP_ID));
  TEST_ASSERT_NULL(sys_session_detach(DOWN_ID));

  // Sending while SLAVE has no route fails for SINK and increments its `dropped` counter without error return.
  const uint8_t payload[] = {9, 8, 7};
  TEST_ASSERT_NULL(sys_stream_send(0x11u, payload, sizeof(payload)));
  TEST_ASSERT_NULL(sys_stream_listen(0x11u, on_cb, NULL));
  TEST_ASSERT_NULL(sys_stream_send(0x11u, payload, sizeof(payload)));
  TEST_ASSERT_TRUE(wait_cb(1, WAIT_MS));  // wait until stream task finished delivering

  sys_stream_info_t list[CONFIG_SYS_STREAM_MAX];
  const sys_stream_info_t* found = NULL;
  uint16_t sink_dropped = 0;
  // account() updates target `dropped` after all target callbacks return in deliver(); wait briefly for it to finish.
  for (int attempt = 0; attempt < WAIT_MS; attempt++) {
    const uint8_t n = sys_stream_list(list);
    found = NULL;
    for (uint8_t i = 0; i < n; i++) {
      if (list[i].head.cfg.id == 0x11u) {
        found = &list[i];
      }
    }
    if (found != NULL) {
      for (uint8_t k = 0; k < found->head.targets; k++) {
        if (found->targets[k].type == SYS_STREAM_TARGET_SINK) {
          sink_dropped = found->targets[k].dropped;
        }
      }
    }
    if (sink_dropped >= 2u) {
      break;
    }
    vTaskDelay(1);
  }
  TEST_ASSERT_NOT_NULL(found);
  TEST_ASSERT_EQUAL_UINT8(2, found->head.targets);  // SINK + CALLBACK, no PEER
  bool saw_sink = false;
  for (uint8_t k = 0; k < found->head.targets; k++) {
    if (found->targets[k].type == SYS_STREAM_TARGET_SINK) {
      saw_sink = true;
      TEST_ASSERT_EQUAL_UINT8(SLAVE, found->targets[k].peer);
      TEST_ASSERT_EQUAL_UINT8(REM_PROV_ID, found->targets[k].provider);
      TEST_ASSERT_EQUAL_UINT16(2, found->targets[k].dropped);
    }
  }
  TEST_ASSERT_TRUE(saw_sink);
}

static void test_router_commands_create_source_sink_subscribe_list_unbind_remove(void) {
  open_client();
  fake_clear(&s_up);
  sent_t ans;
  uint16_t seq = 1;

  // CREATE stream 0x30
  const uint8_t create_cmd[] = {MODULE_ID_STREAM, CMD_DIR_WRITE, STREAM_OP_CREATE, 0x30u, 0x00u};
  send_cmd(&s_up, CLIENT, seq++, create_cmd, sizeof(create_cmd), &ans);
  TEST_ASSERT_EQUAL_UINT8(0, hdr_of(&ans).frame & SYS_SESSION_FRAME_STATUS);

  // SOURCE 0x30 <- RAW_A_ID
  const uint8_t src_cmd[] = {MODULE_ID_STREAM, CMD_DIR_WRITE, STREAM_OP_SOURCE, 0x30u, RAW_A_ID};
  send_cmd(&s_up, CLIENT, seq++, src_cmd, sizeof(src_cmd), &ans);
  TEST_ASSERT_EQUAL_UINT8(0, hdr_of(&ans).frame & SYS_SESSION_FRAME_STATUS);

  // SINK 0x30 -> (SELF, RAW_B_ID)
  const uint8_t sink_cmd[] = {MODULE_ID_STREAM, CMD_DIR_WRITE, STREAM_OP_SINK, 0x30u, SYS_SESSION_ID_SELF, RAW_B_ID};
  send_cmd(&s_up, CLIENT, seq++, sink_cmd, sizeof(sink_cmd), &ans);
  TEST_ASSERT_EQUAL_UINT8(0, hdr_of(&ans).frame & SYS_SESSION_FRAME_STATUS);

  // SUBSCRIBE 0x30 (subscriber is CLIENT from origin->src)
  const uint8_t sub_cmd[] = {MODULE_ID_STREAM, CMD_DIR_WRITE, STREAM_OP_SUBSCRIBE, 0x30u};
  send_cmd(&s_up, CLIENT, seq++, sub_cmd, sizeof(sub_cmd), &ans);
  TEST_ASSERT_EQUAL_UINT8(0, hdr_of(&ans).frame & SYS_SESSION_FRAME_STATUS);

  // LIST includes 0x30 with 3 targets (SOURCE, SINK, PEER).
  const uint8_t list_cmd[] = {MODULE_ID_STREAM, CMD_DIR_READ, STREAM_OP_LIST};
  send_cmd(&s_up, CLIENT, seq++, list_cmd, sizeof(list_cmd), &ans);
  TEST_ASSERT_EQUAL_UINT8(0, hdr_of(&ans).frame & SYS_SESSION_FRAME_STATUS);
  const uint8_t* p = ans.data + HDR;
  const uint8_t count = *p++;
  TEST_ASSERT_GREATER_OR_EQUAL_UINT8(2, count);
  bool checked_30 = false;
  for (uint8_t i = 0; i < count; i++) {
    sys_stream_head_t h;
    memcpy(&h, p, sizeof(h));
    p += sizeof(h);
    if (h.cfg.id == 0x30u) {
      checked_30 = true;
      TEST_ASSERT_EQUAL_UINT8(3, h.targets);
    }
    p += h.targets * sizeof(sys_stream_target_t);
  }
  TEST_ASSERT_TRUE(checked_30);

  // Deliver via RAW_A_ID -> CLIENT receives PUSH
  const uint8_t msg[] = {'r', 'o', 'u', 't', 'e'};
  sys_data_provider_deliver(s_raw_a.p, msg, sizeof(msg));
  sent_t out;
  TEST_ASSERT_TRUE(next_sent(&s_raw_b, &out, WAIT_MS));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(msg, out.data, sizeof(msg));
  TEST_ASSERT_TRUE(next_kind(&s_up, SYS_SESSION_PUSH, &out, WAIT_MS));
  TEST_ASSERT_EQUAL_UINT8(0x30u, out.data[HDR]);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(msg, out.data + HDR + 1u, sizeof(msg));

  // REMOVE 0x30 frees RAW_A_ID RX hook cleanly
  const uint8_t rm_cmd[] = {MODULE_ID_STREAM, CMD_DIR_WRITE, STREAM_OP_REMOVE, 0x30u};
  send_cmd(&s_up, CLIENT, seq++, rm_cmd, sizeof(rm_cmd), &ans);
  TEST_ASSERT_EQUAL_UINT8(0, hdr_of(&ans).frame & SYS_SESSION_FRAME_STATUS);
  TEST_ASSERT_NULL(sys_data_provider_open(RAW_A_ID, NULL, NULL));
  TEST_ASSERT_NULL(sys_data_provider_close(RAW_A_ID));
}

static void test_tunnel_round_trip_source_remote_sink_and_incoming_data_channel(void) {
  open_client();
  open_slave();
  fake_clear(&s_up);
  fake_clear(&s_down);

  // Incoming DATA frame from SLAVE targeted to RAW_B_ID on this board goes straight to RAW_B_ID.
  const uint8_t rx_data[] = {SYS_SESSION_CHANNEL_DATA, RAW_B_ID, 0x20u, 't', 'u', 'n'};
  deliver_session(&s_down, FRAME(SYS_SESSION_PUSH), BOARD, SLAVE, 1, rx_data, sizeof(rx_data));
  sent_t raw_out;
  TEST_ASSERT_TRUE(next_sent(&s_raw_b, &raw_out, WAIT_MS));
  TEST_ASSERT_EQUAL_UINT32(3u, raw_out.len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(rx_data + 3, raw_out.data, 3u);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_system_stream_cannot_be_removed_or_take_a_source);
  RUN_TEST(test_send_delivers_to_local_sink_remote_sink_peer_and_callback);
  RUN_TEST(test_source_takes_rx_refuses_direct_send_and_forwards_to_local_and_remote_sink);
  RUN_TEST(test_gone_peer_drops_subscription_but_keeps_sink_and_counts_dropped);
  RUN_TEST(test_router_commands_create_source_sink_subscribe_list_unbind_remove);
  RUN_TEST(test_tunnel_round_trip_source_remote_sink_and_incoming_data_channel);
  return UNITY_END();
}
