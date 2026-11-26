/* Host unit tests of sys_device_remote: what a sweep of this board sends to the subscribers of its devices. Real
 * sys_device, sys_event and data_matrix; the session and the watches are stubs, the subscriber's session is a port.
 * Run: python runit-esp/test_host/run.py sys_device_remote */
#include <string.h>

#include "data_matrix.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host_assert.h"
#include "sys_device.h"
#include "sys_device_driver.h"
#include "sys_device_events.h"
#include "sys_device_remote.h"
#include "sys_session.h"
#include "sys_watch.h"
#include "unity.h"

#define PEER      0xF0u
#define DEV_RESET 1u   // it has a RESET
#define DEV_PLAIN 2u   // as wired it has none
#define DEV_ALONE 3u   // nobody subscribed
#define DEV_NONE  9u   // subscribed, never created
#define SINK_ID   DATA_MATRIX_PORT(DATA_MATRIX_PORT_SESSION, MODULE_ID_REMOTE)

/* ---- The mock type: an instance with or without a RESET ---- */

SYS_DEVICE_TYPE_DECLARE(RMOCK, 0x0073)

typedef struct __attribute__((packed)) {
  uint8_t no_reset;
} mock_cfg_t;

typedef struct {
  uint8_t id;
  bool no_reset;
} mock_t;

#define SYS_DEVICE_CTX mock_t

static bool s_fail_resume[SYS_DEVICE_COUNT];

OP_DECLARE_WRITE(CONTRACT_SYS_DEVICE, OP_RESET) {
  (void)c;
  return NULL;
}

static err_h create(uint8_t id, mock_t* c, const mock_cfg_t* cfg) {
  c->id = id;
  c->no_reset = cfg->no_reset != 0;
  return NULL;
}

static err_h suspend(mock_t* c) {
  (void)c;
  return NULL;
}

static err_h resume(mock_t* c) {
  return s_fail_resume[c->id] ? ERR_NEW(ERR_BAD_ARG) : NULL;
}

static bool has_op(const mock_t* c, uint16_t op) {
  return op != CONTRACT_SYS_DEVICE_OP_RESET || !c->no_reset;
}

static const sys_device_op_t k_ops[] = {OP_ENTRY_W(CONTRACT_SYS_DEVICE, OP_RESET)};
SYS_DEVICE_TYPE_DEFINE_HAS_OP(RMOCK, mock_cfg_t, create, suspend, resume, k_ops, has_op)

/* ---- The session and the watches: stubs ---- */

err_h sys_session_port(uint8_t peer, uint8_t module, data_matrix_port_id_t* port) {
  TEST_ASSERT_EQUAL_UINT8(PEER, peer);
  TEST_ASSERT_EQUAL_UINT8(MODULE_ID_REMOTE, module);
  *port = SINK_ID;
  return NULL;
}

err_h sys_session_request(uint8_t peer, cmd_in_t req, uint8_t flags, uint32_t timeout_ms, router_done_f cb, void* ctx) {
  (void)peer, (void)req, (void)flags, (void)timeout_ms, (void)cb, (void)ctx;
  return NULL;
}

err_h sys_session_request_wait(uint8_t peer, cmd_in_t req, uint8_t flags, uint32_t timeout_ms, void* buf, uint32_t cap,
                               uint32_t* len) {
  (void)peer, (void)req, (void)flags, (void)timeout_ms, (void)buf, (void)cap;
  *len = 0;
  return NULL;
}

bool sys_session_wired(uint8_t board) {
  (void)board;
  return false;
}

err_h router_remote_chain(const uint8_t* record, uint32_t len, uint8_t board, uint16_t seq) {
  (void)record, (void)len, (void)board, (void)seq;
  return NULL;
}

err_h sys_watch_read(uint8_t board, cmd_in_t read, void* buf, uint32_t cap, uint32_t* len) {
  (void)board, (void)read, (void)buf, (void)cap;
  *len = 0;
  return NULL;
}

void sys_watch_stale(uint8_t board, cmd_in_t write, uint32_t target_len) {
  (void)board, (void)write, (void)target_len;
}

/* ---- The subscriber: the events that reached it ---- */

typedef struct {
  uint8_t id;
  uint16_t source;
  uint32_t tag;
} got_t;

static got_t s_got[32];
static volatile uint32_t s_got_n;

// [hdr PUSH][id][kind][sys_event_t]; errors are left out.
static void sink_in(data_matrix_port_t* self, data_matrix_port_id_t from, uint8_t flags, const uint8_t* packet,
                    uint32_t len) {
  (void)self, (void)from, (void)flags;
  const uint8_t* frame = packet + CMD_HEAD;
  if (frame[1] != SYS_DEVICE_REMOTE_EVENT || s_got_n >= 32) {
    return;
  }
  sys_event_t ev;
  TEST_ASSERT_TRUE(len >= CMD_HEAD + 2u + sizeof(ev));
  memcpy(&ev, frame + 2, sizeof(ev));
  s_got[s_got_n] = (got_t){.id = frame[0], .source = ev.key.source, .tag = ev.key.tag};
  s_got_n++;
}

static data_matrix_port_t s_sink = {.id = SINK_ID, .in = sink_in};

static uint32_t count_of(uint8_t id, uint32_t tag) {
  uint32_t n = 0;
  for (uint32_t i = 0; i < s_got_n; i++) {
    n += s_got[i].id == id && s_got[i].source == id && s_got[i].tag == tag ? 1u : 0u;
  }
  return n;
}

static void create_device(uint8_t id, bool no_reset) {
  uint8_t frame[SYS_DEVICE_CREATE_HDR + sizeof(mock_cfg_t)] = {0};
  const uint16_t type = RMOCK_TYPE;
  memcpy(frame, &type, sizeof(type));
  frame[SYS_DEVICE_CREATE_HDR] = no_reset ? 1u : 0u;
  TEST_ASSERT_NULL(sys_device_write(id, CONTRACT_SYS_DEVICE_OP_CREATE, frame, sizeof(frame)));
}

// The sweep, then the way to the subscriber: the DEVICE lane's task and the component's own. A short wait, since
// "nothing more comes" is part of what is tested.
static void sweep(uint16_t op) {
  s_got_n = 0;
  err_free(sys_device_sweep(op));
  vTaskDelay(pdMS_TO_TICKS(100));
}

void setUp(void) {
  memset(s_fail_resume, 0, sizeof(s_fail_resume));
}

void tearDown(void) {}

/* ---- Tests ---- */

/* A sweep's one event has no device: each followed device gets it as its own, when the sweep changed that device. */
static void test_a_sweep_reaches_the_subscribers_of_each_device_it_changed(void) {
  sweep(CONTRACT_SYS_DEVICE_OP_SUSPEND);
  TEST_ASSERT_EQUAL_UINT32(1, count_of(DEV_RESET, SYS_DEVICE_EV_SUSPENDED));
  TEST_ASSERT_EQUAL_UINT32(1, count_of(DEV_PLAIN, SYS_DEVICE_EV_SUSPENDED));
  TEST_ASSERT_EQUAL_UINT32(2, s_got_n);  // none for the device nobody follows, none for the one that is not there

  s_fail_resume[DEV_PLAIN] = true;
  sweep(CONTRACT_SYS_DEVICE_OP_RESUME);
  TEST_ASSERT_EQUAL_UINT32(1, count_of(DEV_RESET, SYS_DEVICE_EV_RESUMED));
  TEST_ASSERT_EQUAL_UINT32(1, s_got_n);  // the one whose resume failed is still suspended

  s_fail_resume[DEV_PLAIN] = false;
  sweep(CONTRACT_SYS_DEVICE_OP_RESUME);
  sweep(CONTRACT_SYS_DEVICE_OP_RESET);
  TEST_ASSERT_EQUAL_UINT32(1, count_of(DEV_RESET, SYS_DEVICE_EV_RESET));
  TEST_ASSERT_EQUAL_UINT32(1, s_got_n);  // a device with no RESET was not reset
}

int main(void) {
  TEST_ASSERT_NULL(data_matrix_register(&s_sink));
  create_device(DEV_RESET, false);
  create_device(DEV_PLAIN, true);
  create_device(DEV_ALONE, false);
  TEST_ASSERT_NULL(sys_device_remote_subscribe(DEV_RESET, PEER));
  TEST_ASSERT_NULL(sys_device_remote_subscribe(DEV_PLAIN, PEER));
  TEST_ASSERT_NULL(sys_device_remote_subscribe(DEV_NONE, PEER));
  UNITY_BEGIN();
  RUN_TEST(test_a_sweep_reaches_the_subscribers_of_each_device_it_changed);
  return UNITY_END();
}
