/* Host tests of the device registry: the real slots, registry, call path and remote devices, with the real router,
 * sys_event and errors; only the platform is replaced (tasks are threads). The devices are of a type of the tests'
 * own, PROBE, written the way a driver is. Every test removes its devices (tearDown sweeps REMOVE); the one with a
 * board device runs last, since a board device can't be removed. The lock timeout is short here (host config).
 * Run: python test_host/run.py sys_device   (test_host/HOST_TESTS.MD) */
#include <pthread.h>
#include <string.h>
#include "module_id.h"
#include "unity.h"
#include "host_assert.h"
#include "host_mocks.h"
#include "utils.h"
#include "sys_device.h"
#include "sys_device_driver.h"
#include "sys_device_events.h"
#include "sys_device_hooks.h"

#define WAIT_MS 2000  // the longest a test waits for another task
#define NONE    SYS_DEVICE_ID_NONE

/* ---- a contract of the tests' own (an interceptor needs one) -------------------------------------------------- */

MODULE_ID_DECLARE(MODULE_ID_TESTC, 0x1E)
#define CONTRACT_TESTC_OPERATIONS(X, P) X(P, OP_LEVEL, 0x00, cmd_none_t, uint8_t, 0)
SYS_DEVICE_CONTRACT_DECLARE(CONTRACT_TESTC, MODULE_ID_TESTC)

/* ---- the PROBE driver: what each op and hook did is kept for the tests ---------------------------------------- */

SYS_DEVICE_TYPE_DECLARE(PROBE, 0x0070)

typedef struct __attribute__((packed)) {
  uint8_t dep;   // a device it uses, NONE for none: create() declares the use and pings it, suspend() pings it
  uint8_t fail;  // 1: create() fails after it took its two resources
} probe_cfg_t;

/*   PING  - counts, keeps who called               CALL  - writes op `value` to its dep, no args
 *   SELF  - pings itself (REENTRY)                 EMIT  - publishes POKE inline, then fails while VALUE is 1
 *   BLOCK - holds its lock until the test lets go  TICK  - a step: counts (LOCAL); fails while VALUE is 0xEE
 *   VALUE - a byte, both sides                     USE   - declares a use of device `value` (from an op)
 *   RES2  - adds one node as a resource twice      SWEEP - sweeps SUSPEND from inside its op */
#define OWN_PROBE_OPERATIONS(X, P)                                       \
  X(P, OP_PING,  0x00, cmd_none_t, cmd_none_t, 0)                    \
  X(P, OP_CALL,  0x01, cmd_none_t, uint16_t,   0)                    \
  X(P, OP_SELF,  0x02, cmd_none_t, cmd_none_t, 0)                    \
  X(P, OP_EMIT,  0x03, cmd_none_t, cmd_none_t, 0)                    \
  X(P, OP_BLOCK, 0x04, cmd_none_t, cmd_none_t, 0)                    \
  X(P, OP_TICK,  0x05, cmd_none_t, cmd_none_t, SYS_DEVICE_OP_LOCAL)  \
  X(P, OP_VALUE, 0x06, cmd_none_t, uint8_t,    0)                    \
  X(P, OP_USE,   0x07, cmd_none_t, uint8_t,    0)                    \
  X(P, OP_RES2,  0x08, cmd_none_t, cmd_none_t, 0)                    \
  X(P, OP_SWEEP, 0x09, cmd_none_t, cmd_none_t, 0)                    \
  X(P, OP_EVT,   0x0A, cmd_none_t, uint8_t,    SYS_DEVICE_OP_LOCAL | SYS_DEVICE_OP_ALWAYS)
SYS_DEVICE_OWN_DECLARE(OWN_PROBE)

#define PROBE_EVENTS(X, P) X(P, POKE, 0x01, 0, { uint8_t n; })
SYS_DEVICE_OWN_EVENTS(PROBE)

typedef struct {
  uint8_t id;
  uint8_t i;
} rel_t;

typedef struct {
  uint8_t id;
  uint8_t dep;
  uint8_t gen;  // sys_device_gen() in create()
  uint8_t value;
  uint8_t level;
  int pings;
  uint8_t last_caller;
  uint8_t create_self;
  bool create_suspended;
  uint8_t last_self;
  bool last_suspended;
  uint8_t self_after_call;
  bool suspend_saw_suspended;
  bool resume_saw_suspended;
  int ticks;
  int resets;
  int deliver_count;
  uint8_t deliver_val;
  bool deliver_suspended;
  uint32_t hold_ms;
  uint32_t call_done;  // the sequence number when CALL's call returned
  sys_device_res_t res[2];
  rel_t rel[2];
  sys_device_res_t extra;
} probe_t;

#define SYS_DEVICE_CTX probe_t

static probe_t* s_dev[SYS_DEVICE_COUNT];  // the struct of the device under each ID while it exists
static uint32_t s_seq;                    // the order of things across ops and listeners

/* What the hooks and releases did, in order: 'S' suspend, 'R' resume, 'X' a release (i: the resource). */
typedef struct {
  char what;
  uint8_t id;
  uint8_t i;
} step_t;

static pthread_mutex_t s_log_lock = PTHREAD_MUTEX_INITIALIZER;
static step_t s_log[64];
static int s_log_n;

static void log_step(char what, uint8_t id, uint8_t i) {
  pthread_mutex_lock(&s_log_lock);
  if (s_log_n < (int)ARRAY_LEN(s_log)) {
    s_log[s_log_n++] = (step_t){what, id, i};
  }
  pthread_mutex_unlock(&s_log_lock);
}

static uint8_t s_release_fail = NONE;  // this device's resource 1 fails to release
static uint8_t s_suspend_block = NONE; // this device's suspend() waits for the test

static SemaphoreHandle_t s_blocked;  // given by an op or a hook that waits (BLOCK, a blocking suspend)
static SemaphoreHandle_t s_release;  // given by the test to let it go
static StaticSemaphore_t s_blocked_buf, s_release_buf;

static void wait_released(void) {
  xSemaphoreGive(s_blocked);
  xSemaphoreTake(s_release, portMAX_DELAY);
}

static err_h release_res(void* arg) {
  const rel_t* r = arg;
  log_step('X', r->id, r->i);
  return r->id == s_release_fail && r->i == 1 ? ERR_NEW(ERR_BAD_ARG) : NULL;
}

static err_h release_extra(void* arg) {
  (void)arg;
  return NULL;
}

OP_DECLARE_WRITE(OWN_PROBE, OP_PING) {
  c->pings++;
  c->last_caller = sys_device_caller();
  c->last_self = sys_device_self();
  c->last_suspended = sys_device_self_suspended();
  return NULL;
}

OP_DECLARE_WRITE(OWN_PROBE, OP_CALL) {
  const err_h err = sys_device_write(c->dep, *args, NULL, 0);
  c->self_after_call = sys_device_self();
  c->call_done = ++s_seq;
  return err;
}

OP_DECLARE_WRITE(OWN_PROBE, OP_SELF) {
  return SYS_DEVICE_SET(c->id, OWN_PROBE, OP_PING);
}

OP_DECLARE_WRITE(OWN_PROBE, OP_EMIT) {
  ERR_TRY(SYS_EVENT_PUBLISH(PROBE_EV_POKE, c->id, 0, .n = 1));
  return c->value == 1 ? ERR_NEW(ERR_BAD_ARG) : NULL;
}

OP_DECLARE_WRITE(OWN_PROBE, OP_BLOCK) {
  if (c->hold_ms > 0) {
    xSemaphoreGive(s_blocked);
    vTaskDelay(pdMS_TO_TICKS(c->hold_ms));
    return NULL;
  }
  wait_released();
  return NULL;
}

OP_DECLARE_WRITE(OWN_PROBE, OP_EVT) {
  c->deliver_count++;
  c->deliver_val = *args;
  c->deliver_suspended = sys_device_self_suspended();
  return NULL;
}

OP_DECLARE_WRITE(OWN_PROBE, OP_TICK) {
  if (c->value == 0xEE) {
    return ERR_NEW(ERR_BAD_ARG);
  }
  c->ticks++;
  return NULL;
}

OP_DECLARE_READ(OWN_PROBE, OP_VALUE) {
  c->last_self = sys_device_self();
  c->last_suspended = sys_device_self_suspended();
  return CMD_REPLY(OWN_PROBE, OP_VALUE, c->value);
}

OP_DECLARE_WRITE(OWN_PROBE, OP_VALUE) {
  c->value = *args;
  return NULL;
}

OP_DECLARE_WRITE(OWN_PROBE, OP_USE) {
  return sys_device_use(c->id, *args);
}

OP_DECLARE_WRITE(OWN_PROBE, OP_RES2) {
  ERR_TRY(sys_device_res_add(c->id, &c->extra, release_extra, c));
  return sys_device_res_add(c->id, &c->extra, release_extra, c);
}

OP_DECLARE_WRITE(OWN_PROBE, OP_SWEEP) {
  return sys_device_sweep(CONTRACT_SYS_DEVICE_OP_SUSPEND);
}

OP_DECLARE_READ(CONTRACT_TESTC, OP_LEVEL) {
  return CMD_REPLY(CONTRACT_TESTC, OP_LEVEL, c->level);
}

OP_DECLARE_WRITE(CONTRACT_TESTC, OP_LEVEL) {
  c->level = *args;
  return NULL;
}

OP_DECLARE_WRITE(CONTRACT_SYS_DEVICE, OP_RESET) {
  c->resets++;
  return NULL;
}

static err_h suspend(probe_t* c) {
  c->suspend_saw_suspended = sys_device_self_suspended();
  log_step('S', c->id, 0);
  if (c->id == s_suspend_block) {
    wait_released();
  }
  return c->dep != NONE ? SYS_DEVICE_SET(c->dep, OWN_PROBE, OP_PING) : NULL;  // a hook reaches the device it uses
}

static err_h resume(probe_t* c) {
  c->resume_saw_suspended = sys_device_self_suspended();
  log_step('R', c->id, 0);
  return NULL;
}

static err_h create(uint8_t id, probe_t* c, const probe_cfg_t* cfg) {
  c->id = id;
  c->create_self = sys_device_self();
  c->create_suspended = sys_device_self_suspended();
  c->dep = cfg->dep;
  c->gen = sys_device_gen(id);
  c->last_caller = 0xAA;  // nobody called yet
  for (uint8_t i = 0; i < 2; i++) {
    c->rel[i] = (rel_t){id, i};
    ERR_TRY(sys_device_res_add(id, &c->res[i], release_res, &c->rel[i]));
  }
  if (cfg->dep != NONE) {
    ERR_TRY(sys_device_use(id, cfg->dep));
    ERR_TRY(SYS_DEVICE_SET(cfg->dep, OWN_PROBE, OP_PING));  // the dep sees this device as its caller
  }
  ERR_CHECK(cfg->fail == 0, ERR_BAD_ARG);
  s_dev[id] = c;
  return NULL;
}

static const sys_device_op_t k_ops[] = {
    OP_ENTRY_W(CONTRACT_SYS_DEVICE, OP_RESET), OP_ENTRY_W(OWN_PROBE, OP_PING),  OP_ENTRY_W(OWN_PROBE, OP_CALL), OP_ENTRY_W(OWN_PROBE, OP_SELF),
    OP_ENTRY_W(OWN_PROBE, OP_EMIT),       OP_ENTRY_W(OWN_PROBE, OP_BLOCK), OP_ENTRY_W(OWN_PROBE, OP_TICK), OP_ENTRY(OWN_PROBE, OP_VALUE),
    OP_ENTRY_W(OWN_PROBE, OP_USE),        OP_ENTRY_W(OWN_PROBE, OP_RES2),  OP_ENTRY_W(OWN_PROBE, OP_SWEEP), OP_ENTRY_W(OWN_PROBE, OP_EVT),
    OP_ENTRY(CONTRACT_TESTC, OP_LEVEL),
};

SYS_DEVICE_TYPE_DEFINE(PROBE, probe_cfg_t, create, suspend, resume, k_ops)

/* ---- helpers -------------------------------------------------------------------------------------------------- */

static err_h create_at(uint8_t* frame, uint8_t id, uint8_t dep, uint8_t fail, uint8_t flags) {
  const uint16_t type = PROBE_TYPE;
  const probe_cfg_t cfg = {.dep = dep, .fail = fail};
  memcpy(frame, &type, sizeof(type));
  frame[2] = flags;
  memcpy(frame + SYS_DEVICE_CREATE_HDR, &cfg, sizeof(cfg));
  return sys_device_call(id, CMD_DIR_WRITE, CONTRACT_SYS_DEVICE_OP_CREATE,
                         (cmd_in_t){frame, SYS_DEVICE_CREATE_HDR + sizeof(cfg)}, NULL);
}

static err_h create_probe(uint8_t id, uint8_t dep) {
  uint8_t frame[SYS_DEVICE_CREATE_HDR + sizeof(probe_cfg_t)];
  return create_at(frame, id, dep, 0, 0);
}

static void make(uint8_t id, uint8_t dep) {
  TEST_ASSERT_NULL(create_probe(id, dep));
}

static bool exists(uint8_t id) {
  sys_device_info_t info;
  err_h e = SYS_DEVICE_GET(id, CONTRACT_SYS_DEVICE, OP_INFO, &info);
  err_free(e);
  return e == NULL;
}

static bool suspended(uint8_t id) {
  sys_device_info_t info = {0};
  TEST_ASSERT_NULL(SYS_DEVICE_GET(id, CONTRACT_SYS_DEVICE, OP_INFO, &info));
  return info.suspended != 0;
}

static err_h write(uint8_t id, uint16_t op) {
  return sys_device_write(id, op, NULL, 0);
}

static err_h root_of(err_h e) {
  while (e != NULL && err_cause(e) != NULL) {
    e = err_cause(e);
  }
  return e;
}

// `e` is a chain whose root cause is `tag`; it is freed.
#define TEST_ASSERT_ROOT(tag, e)                                   \
  do {                                                             \
    err_h e_ = (e);                                                \
    TEST_ASSERT_NOT_NULL_MESSAGE(e_, "expected an error chain");   \
    TEST_ASSERT_EQUAL_HEX32((tag), err_root_tag(e_));              \
    err_free(e_);                                                  \
  } while (0)

/* A call made in a task of its own, while the test does something else. */
static err_h (*s_job)(void);
static err_h s_job_err;
static SemaphoreHandle_t s_job_done;
static StaticSemaphore_t s_job_done_buf;
static uint8_t s_job_id;

static void job_task(void* arg) {
  (void)arg;
  s_job_err = s_job();
  xSemaphoreGive(s_job_done);
}

static void start_job(err_h (*fn)(void)) {
  s_job = fn;
  s_job_err = NULL;
  xTaskCreateStatic(job_task, "job", 0, NULL, 1, NULL, NULL);
}

static err_h finish_job(void) {
  TEST_ASSERT_TRUE(xSemaphoreTake(s_job_done, WAIT_MS) == pdTRUE);
  return s_job_err;
}

static err_h job_block(void) {
  return write(s_job_id, OWN_PROBE_OP_BLOCK);
}

static err_h job_sweep_suspend(void) {
  return sys_device_sweep(CONTRACT_SYS_DEVICE_OP_SUSPEND);
}

/* ---- what the registry published ------------------------------------------------------------------------------ */

typedef struct {
  uint32_t tag;
  uint16_t source;
  uint16_t flags;
  uint8_t gen;
} seen_t;

static seen_t s_seen[32];
static int s_seen_n;
static uint16_t s_subs[4];
static int s_sub_n;

static err_h on_registry_event(const sys_event_t* e, void* ctx) {
  (void)ctx;
  if (s_seen_n < (int)ARRAY_LEN(s_seen)) {
    s_seen[s_seen_n++] = (seen_t){e->key.tag, e->key.source, e->flags, e->len > 0 ? e->data[0] : 0};
  }
  return NULL;
}

static void follow_registry(void) {
  const sys_event_key_t all = {.tag = EVT_TAG(SYS_DEVICE_EV_MODULE, SYS_EVENT_ANY_CODE),
                               .source = SYS_EVENT_ANY_SOURCE, .channel = SYS_EVENT_ANY_CHANNEL};
  TEST_ASSERT_NULL(sys_event_subscribe(all, on_registry_event, NULL, SYS_EVENT_RUN_INLINE, &s_subs[s_sub_n++]));
}

static int seen(uint32_t tag) {
  int n = 0;
  for (int i = 0; i < s_seen_n; i++) {
    n += s_seen[i].tag == tag ? 1 : 0;
  }
  return n;
}

void setUp(void) {
  host_log_clear();
  if (s_blocked == NULL) {
    s_blocked = xSemaphoreCreateBinaryStatic(&s_blocked_buf);
    s_release = xSemaphoreCreateBinaryStatic(&s_release_buf);
    s_job_done = xSemaphoreCreateBinaryStatic(&s_job_done_buf);
  }
  s_log_n = 0;
  s_seen_n = 0;
  s_release_fail = NONE;
  s_suspend_block = NONE;
}

void tearDown(void) {
  s_suspend_block = NONE;  // the REMOVE sweep below runs suspend() on this task: it must not wait for the test
  for (int i = 0; i < s_sub_n; i++) {
    err_free(sys_event_unsubscribe(s_subs[i]));
  }
  s_sub_n = 0;
  sys_device_set_remote(NULL);
  err_free(sys_device_sweep(CONTRACT_SYS_DEVICE_OP_REMOVE));  // a board device stays
  memset(s_dev, 0, sizeof(s_dev));
}

/* ---- CREATE, INFO, OPS ---------------------------------------------------------------------------------------- */

static void test_create_installs_a_device_that_answers_its_ops(void) {
  make(1, NONE);
  sys_device_info_t info = {0};
  TEST_ASSERT_NULL(SYS_DEVICE_GET(1, CONTRACT_SYS_DEVICE, OP_INFO, &info));
  TEST_ASSERT_EQUAL_HEX16(PROBE_TYPE, info.type_id);
  TEST_ASSERT_EQUAL_UINT8(0, info.suspended);
  TEST_ASSERT_NULL(SYS_DEVICE_SET(1, OWN_PROBE, OP_VALUE, 42));
  uint8_t v = 0;
  TEST_ASSERT_NULL(SYS_DEVICE_GET(1, OWN_PROBE, OP_VALUE, &v));
  TEST_ASSERT_EQUAL_UINT8(42, v);
}

static void test_create_refuses_a_taken_id_an_unknown_type_and_a_wrong_size(void) {
  make(1, NONE);
  TEST_ASSERT_ROOT(ERR_DEV_ID_TAKEN, create_probe(1, NONE));
  uint8_t frame[8] = {0x7F, 0x7F, 0};  // type 0x7F7F: no driver
  TEST_ASSERT_ROOT(ERR_DEV_TYPE_UNKNOWN,
                   sys_device_call(2, CMD_DIR_WRITE, CONTRACT_SYS_DEVICE_OP_CREATE, (cmd_in_t){frame, 5}, NULL));
  const uint16_t type = PROBE_TYPE;
  memcpy(frame, &type, sizeof(type));
  TEST_ASSERT_ROOT(ERR_CMD_BAD_LEN,  // one byte short of the create struct
                   sys_device_call(2, CMD_DIR_WRITE, CONTRACT_SYS_DEVICE_OP_CREATE, (cmd_in_t){frame, 4}, NULL));
  TEST_ASSERT_FALSE(exists(2));
}

static void test_create_reads_its_struct_where_the_frame_is_also_at_an_odd_address(void) {
  uint8_t buf[1 + SYS_DEVICE_CREATE_HDR + sizeof(probe_cfg_t)];
  make(1, NONE);
  TEST_ASSERT_NULL(create_at(buf + 1, 2, 1, 0, 0));  // dep 1 read from an odd address
  TEST_ASSERT_EQUAL_UINT8(1, s_dev[2]->dep);
  TEST_ASSERT_EQUAL_INT(1, s_dev[1]->pings);
}

static bool ops_has(const uint8_t* rows, uint32_t len, uint16_t op, uint8_t sides) {
  for (uint32_t at = 0; at + 3 <= len; at += 3) {
    uint16_t o;
    memcpy(&o, rows + at, sizeof(o));
    if (o == op) {
      return rows[at + 2] == sides;
    }
  }
  return false;
}

static void test_ops_lists_the_registrys_and_the_types_ops_but_no_local_one(void) {
  make(1, NONE);
  uint8_t rows[96];
  cmd_out_t out = {.data = rows, .cap = sizeof(rows)};
  TEST_ASSERT_NULL(sys_device_call(1, CMD_DIR_READ, CONTRACT_SYS_DEVICE_OP_OPS, (cmd_in_t){NULL, 0}, &out));
  TEST_ASSERT_EQUAL_UINT32(0, out.len % 3);
  TEST_ASSERT_TRUE(ops_has(rows, out.len, CONTRACT_SYS_DEVICE_OP_SUSPEND, 2));
  TEST_ASSERT_TRUE(ops_has(rows, out.len, CONTRACT_SYS_DEVICE_OP_INFO, 1));
  TEST_ASSERT_TRUE(ops_has(rows, out.len, CONTRACT_SYS_DEVICE_OP_RESET, 2));
  TEST_ASSERT_TRUE(ops_has(rows, out.len, OWN_PROBE_OP_PING, 2));
  TEST_ASSERT_TRUE(ops_has(rows, out.len, OWN_PROBE_OP_VALUE, 3));
  TEST_ASSERT_TRUE(ops_has(rows, out.len, CONTRACT_TESTC_OP_LEVEL, 3));
  TEST_ASSERT_FALSE(ops_has(rows, out.len, OWN_PROBE_OP_TICK, 2));  // LOCAL: not a command
  TEST_ASSERT_EQUAL_UINT32(3 * (5 + 12 - 1), out.len);          // 5 of the registry, the type's 12 but TICK
}

static void test_a_call_names_the_device_and_the_op_and_a_local_op_is_c_only(void) {
  make(1, NONE);
  err_h e = sys_device_call(1, CMD_DIR_READ, OWN_PROBE_OP_PING, (cmd_in_t){NULL, 0}, NULL);  // PING has no read side
  TEST_ASSERT_ERR_TAG(ERR_DEV_ID, e);
  TEST_ASSERT_EQUAL_UINT8(1, ERR_DATA(e, ERR_DEV_ID)->id);
  TEST_ASSERT_EQUAL_HEX32(ERR_DEV_OP, err_tag(err_cause(e)));
  TEST_ASSERT_EQUAL_HEX16(OWN_PROBE_OP_PING, ERR_DATA(err_cause(e), ERR_DEV_OP)->op);
  TEST_ASSERT_EQUAL_HEX32(ERR_DEV_UNKNOWN_OP, err_root_tag(e));
  err_free(e);
  TEST_ASSERT_ROOT(ERR_DEV_UNKNOWN_OP, write(1, SYS_DEVICE_OP(0xFF, 0x7F)));
  TEST_ASSERT_ROOT(ERR_DEV_NOT_FOUND, write(9, OWN_PROBE_OP_PING));
  TEST_ASSERT_ROOT(ERR_DEV_BAD_ID, write(SYS_DEVICE_COUNT, OWN_PROBE_OP_PING));
  TEST_ASSERT_ROOT(ERR_DEV_LOCAL_OP,
                   sys_device_call_routed(1, CMD_DIR_WRITE, OWN_PROBE_OP_TICK, (cmd_in_t){NULL, 0}, NULL));
  TEST_ASSERT_NULL(write(1, OWN_PROBE_OP_TICK));  // from C it runs
  TEST_ASSERT_EQUAL_INT(1, s_dev[1]->ticks);
  TEST_ASSERT_ROOT(ERR_CMD_BAD_LEN, sys_device_write(1, OWN_PROBE_OP_VALUE, "ab", 2));
}

/* ---- the caller, nesting, events of an op --------------------------------------------------------------------- */

static void test_the_caller_is_the_device_whose_op_made_the_call(void) {
  make(3, NONE);
  make(4, 3);  // create() pings 3
  TEST_ASSERT_EQUAL_UINT8(4, s_dev[3]->last_caller);
  TEST_ASSERT_NULL(write(3, OWN_PROBE_OP_PING));
  TEST_ASSERT_EQUAL_UINT8(NONE, s_dev[3]->last_caller);  // a call from no device
  TEST_ASSERT_NULL(SYS_DEVICE_SET(4, OWN_PROBE, OP_CALL, OWN_PROBE_OP_PING));
  TEST_ASSERT_EQUAL_UINT8(4, s_dev[3]->last_caller);
  TEST_ASSERT_EQUAL_UINT8(NONE, sys_device_caller());  // outside an op
}

static void test_self_and_self_suspended_reflect_the_running_device(void) {
  TEST_ASSERT_EQUAL_UINT8(NONE, sys_device_self());
  TEST_ASSERT_FALSE(sys_device_self_suspended());

  make(3, NONE);
  make(4, 3);
  TEST_ASSERT_EQUAL_UINT8(3, s_dev[3]->create_self);
  TEST_ASSERT_FALSE(s_dev[3]->create_suspended);
  TEST_ASSERT_EQUAL_UINT8(4, s_dev[4]->create_self);
  TEST_ASSERT_FALSE(s_dev[4]->create_suspended);

  TEST_ASSERT_NULL(SYS_DEVICE_SET(4, OWN_PROBE, OP_CALL, OWN_PROBE_OP_PING));
  TEST_ASSERT_EQUAL_UINT8(3, s_dev[3]->last_self);
  TEST_ASSERT_FALSE(s_dev[3]->last_suspended);
  TEST_ASSERT_EQUAL_UINT8(4, s_dev[4]->self_after_call);

  TEST_ASSERT_NULL(write(3, CONTRACT_SYS_DEVICE_OP_SUSPEND));
  TEST_ASSERT_FALSE(s_dev[3]->suspend_saw_suspended);

  uint8_t val = 0;
  TEST_ASSERT_NULL(SYS_DEVICE_GET(3, OWN_PROBE, OP_VALUE, &val));
  TEST_ASSERT_EQUAL_UINT8(3, s_dev[3]->last_self);
  TEST_ASSERT_TRUE(s_dev[3]->last_suspended);

  TEST_ASSERT_NULL(write(3, CONTRACT_SYS_DEVICE_OP_RESUME));
  TEST_ASSERT_TRUE(s_dev[3]->resume_saw_suspended);
  TEST_ASSERT_NULL(SYS_DEVICE_GET(3, OWN_PROBE, OP_VALUE, &val));
  TEST_ASSERT_FALSE(s_dev[3]->last_suspended);

  TEST_ASSERT_EQUAL_UINT8(NONE, sys_device_self());
  TEST_ASSERT_FALSE(sys_device_self_suspended());
}

static void test_an_op_calling_its_own_device_is_reentry(void) {
  make(1, NONE);
  TEST_ASSERT_ROOT(ERR_DEV_REENTRY, write(1, OWN_PROBE_OP_SELF));
  TEST_ASSERT_NULL(write(1, OWN_PROBE_OP_PING));  // the lock was let go
}

static uint8_t s_poke_target;
static err_h s_poke_err;
static uint32_t s_poke_at;
static int s_pokes;

// Inline listener of POKE: calls the device whose op caused it, which a listener inside that op could not.
static err_h on_poke(const sys_event_t* e, void* ctx) {
  (void)e, (void)ctx;
  s_pokes++;
  s_poke_at = ++s_seq;
  s_poke_err = SYS_DEVICE_SET(s_poke_target, OWN_PROBE, OP_PING);
  return NULL;
}

static void test_an_event_of_an_op_reaches_an_inline_listener_after_the_outermost_call(void) {
  make(3, NONE);
  make(4, 3);
  const sys_event_key_t poke = {.tag = PROBE_EV_POKE, .source = SYS_EVENT_ANY_SOURCE, .channel = 0};
  TEST_ASSERT_NULL(sys_event_subscribe(poke, on_poke, NULL, SYS_EVENT_RUN_INLINE, &s_subs[s_sub_n++]));
  s_poke_target = 4;
  s_pokes = 0;
  TEST_ASSERT_NULL(SYS_DEVICE_SET(4, OWN_PROBE, OP_CALL, OWN_PROBE_OP_EMIT));  // 4 holds its lock and calls 3, which publishes
  TEST_ASSERT_EQUAL_INT(1, s_pokes);
  TEST_ASSERT_NULL(s_poke_err);                         // no REENTRY, no BUSY: 4's lock was let go
  TEST_ASSERT_TRUE(s_poke_at > s_dev[4]->call_done);    // after the op that published, not inside it
  TEST_ASSERT_EQUAL_HEX16(PROBE_EV_MODULE, 0x8000u | PROBE_TYPE);
  // An op that failed after it published: the event is delivered all the same.
  TEST_ASSERT_NULL(SYS_DEVICE_SET(3, OWN_PROBE, OP_VALUE, 1));
  TEST_ASSERT_ROOT(ERR_BAD_ARG, write(3, OWN_PROBE_OP_EMIT));
  TEST_ASSERT_EQUAL_INT(2, s_pokes);
}

static void test_created_and_removed_carry_the_generation_and_a_new_create_takes_a_new_one(void) {
  follow_registry();
  make(5, NONE);
  const uint8_t gen = s_dev[5]->gen;
  TEST_ASSERT_EQUAL_UINT8(gen, sys_device_gen(5));  // create() saw its own generation
  TEST_ASSERT_EQUAL_INT(1, seen(SYS_DEVICE_EV_CREATED));
  TEST_ASSERT_EQUAL_UINT8(gen, s_seen[0].gen);
  TEST_ASSERT_EQUAL_UINT16(5, s_seen[0].source);
  TEST_ASSERT_EQUAL_HEX16(MODULE_ID_SYS_DEVICE, EVT_MODULE(SYS_DEVICE_EV_CREATED));
  TEST_ASSERT_NULL(write(5, CONTRACT_SYS_DEVICE_OP_SUSPEND));
  TEST_ASSERT_EQUAL_INT(1, seen(SYS_DEVICE_EV_SUSPENDED));
  TEST_ASSERT_EQUAL_HEX16(SYS_EVENT_NOTIFY | SYS_EVENT_FLAG_LANE(SYS_EVENT_LANE_DEVICE), s_seen[1].flags);
  TEST_ASSERT_NULL(write(5, CONTRACT_SYS_DEVICE_OP_REMOVE));
  TEST_ASSERT_EQUAL_INT(1, seen(SYS_DEVICE_EV_REMOVED));
  TEST_ASSERT_EQUAL_UINT8(gen, s_seen[2].gen);
  make(5, NONE);
  TEST_ASSERT_NOT_EQUAL(gen, s_dev[5]->gen);
  TEST_ASSERT_EQUAL_UINT8(s_dev[5]->gen, s_seen[3].gen);
}

/* ---- resources, REMOVE ---------------------------------------------------------------------------------------- */

static void test_a_failed_create_gives_back_what_it_took_last_first_and_runs_no_suspend(void) {
  make(3, NONE);
  uint8_t frame[SYS_DEVICE_CREATE_HDR + sizeof(probe_cfg_t)];
  err_h e = create_at(frame, 6, 3, 1, 0);
  TEST_ASSERT_NOT_NULL(e);
  TEST_ASSERT_EQUAL_HEX32(ERR_DEV_INSTALL, err_tag(err_cause(err_cause(e))));  // ID, OP, then the CREATE's context
  TEST_ASSERT_EQUAL_HEX32(ERR_BAD_ARG, err_root_tag(e));
  err_free(e);
  TEST_ASSERT_EQUAL_INT(2, s_log_n);
  TEST_ASSERT_EQUAL_CHAR('X', s_log[0].what);
  TEST_ASSERT_EQUAL_UINT8(1, s_log[0].i);  // the last added first
  TEST_ASSERT_EQUAL_UINT8(0, s_log[1].i);
  TEST_ASSERT_FALSE(exists(6));
  TEST_ASSERT_NULL(write(3, CONTRACT_SYS_DEVICE_OP_REMOVE));  // the use create() declared went with it
  make(6, NONE);
}

static void test_remove_suspends_a_working_device_then_releases_last_first(void) {
  make(7, NONE);
  TEST_ASSERT_NULL(write(7, CONTRACT_SYS_DEVICE_OP_REMOVE));
  TEST_ASSERT_EQUAL_INT(3, s_log_n);
  TEST_ASSERT_EQUAL_CHAR('S', s_log[0].what);
  TEST_ASSERT_EQUAL_CHAR('X', s_log[1].what);
  TEST_ASSERT_EQUAL_UINT8(1, s_log[1].i);
  TEST_ASSERT_EQUAL_UINT8(0, s_log[2].i);
  TEST_ASSERT_FALSE(exists(7));
  make(8, NONE);  // a suspended one: no second suspend()
  TEST_ASSERT_NULL(write(8, CONTRACT_SYS_DEVICE_OP_SUSPEND));
  s_log_n = 0;
  TEST_ASSERT_NULL(write(8, CONTRACT_SYS_DEVICE_OP_REMOVE));
  TEST_ASSERT_EQUAL_INT(2, s_log_n);
  TEST_ASSERT_EQUAL_CHAR('X', s_log[0].what);
}

static void test_a_failing_release_still_releases_the_rest_and_frees_the_slot(void) {
  make(9, NONE);
  s_release_fail = 9;
  TEST_ASSERT_ROOT(ERR_BAD_ARG, write(9, CONTRACT_SYS_DEVICE_OP_REMOVE));
  TEST_ASSERT_EQUAL_INT(3, s_log_n);  // suspend, both releases
  TEST_ASSERT_FALSE(exists(9));
  s_release_fail = NONE;
  make(9, NONE);
}

static void test_a_resource_is_added_once_and_only_with_the_lock_of_its_owner(void) {
  make(1, NONE);
  TEST_ASSERT_ROOT(ERR_BAD_ARG, write(1, OWN_PROBE_OP_RES2));  // the second add of the same node
  sys_device_res_t node = {0};
  rel_t rel = {1, 0};
  err_h e = sys_device_res_add(1, &node, release_res, &rel);  // from no op of device 1
  TEST_ASSERT_ERR_TAG(ERR_DEV_NOT_LOCKED, e);
  err_free(e);
  TEST_ASSERT_NULL(node.release);  // still not added
  TEST_ASSERT_NULL(write(1, CONTRACT_SYS_DEVICE_OP_REMOVE));
}

/* ---- uses ----------------------------------------------------------------------------------------------------- */

static void test_remove_of_a_device_in_use_is_refused_and_names_the_users(void) {
  make(3, NONE);
  make(4, 3);
  err_h e = write(3, CONTRACT_SYS_DEVICE_OP_REMOVE);
  const err_h r = root_of(e);
  TEST_ASSERT_NOT_NULL(r);
  TEST_ASSERT_EQUAL_HEX32(ERR_DEV_IN_USE, err_tag(r));
  TEST_ASSERT_EQUAL_HEX64(1ull << 4, ERR_DATA(r, ERR_DEV_IN_USE)->users);
  err_free(e);
  TEST_ASSERT_TRUE(exists(3));
  TEST_ASSERT_NULL(write(4, CONTRACT_SYS_DEVICE_OP_REMOVE));
  TEST_ASSERT_NULL(write(3, CONTRACT_SYS_DEVICE_OP_REMOVE));
}

static void test_a_use_never_closes_a_cycle(void) {
  make(10, NONE);
  make(11, 10);  // 11 uses 10
  err_h e = SYS_DEVICE_SET(10, OWN_PROBE, OP_USE, 11);
  const err_h r = root_of(e);
  TEST_ASSERT_NOT_NULL(r);
  TEST_ASSERT_EQUAL_HEX32(ERR_DEV_CYCLE, err_tag(r));
  TEST_ASSERT_EQUAL_UINT8(10, ERR_DATA(r, ERR_DEV_CYCLE)->id);
  TEST_ASSERT_EQUAL_UINT8(11, ERR_DATA(r, ERR_DEV_CYCLE)->dep);
  err_free(e);
  make(12, 11);  // through a third: 12 -> 11 -> 10
  TEST_ASSERT_ROOT(ERR_DEV_CYCLE, SYS_DEVICE_SET(10, OWN_PROBE, OP_USE, 12));
  TEST_ASSERT_ROOT(ERR_DEV_NOT_FOUND, SYS_DEVICE_SET(10, OWN_PROBE, OP_USE, 10));  // itself
  TEST_ASSERT_NULL(SYS_DEVICE_SET(12, OWN_PROBE, OP_USE, 10));  // the same direction: fine, twice too
  TEST_ASSERT_NULL(SYS_DEVICE_SET(12, OWN_PROBE, OP_USE, 10));
}

/* ---- the lock ------------------------------------------------------------------------------------------------- */

static void test_a_lock_held_too_long_fails_with_the_op_that_holds_it(void) {
  make(13, NONE);
  s_job_id = 13;
  start_job(job_block);
  TEST_ASSERT_TRUE(xSemaphoreTake(s_blocked, WAIT_MS) == pdTRUE);
  err_h e = write(13, OWN_PROBE_OP_PING);
  const err_h r = root_of(e);
  TEST_ASSERT_NOT_NULL(r);
  TEST_ASSERT_EQUAL_HEX32(ERR_DEV_BUSY, err_tag(r));
  TEST_ASSERT_EQUAL_HEX16(OWN_PROBE_OP_BLOCK, ERR_DATA(r, ERR_DEV_BUSY)->op);
  err_free(e);
  xSemaphoreGive(s_release);
  TEST_ASSERT_NULL(finish_job());
  TEST_ASSERT_NULL(write(13, OWN_PROBE_OP_PING));
}

/* ---- a step --------------------------------------------------------------------------------------------------- */

static void test_a_step_runs_only_on_its_device_when_the_device_takes_it_now(void) {
  make(14, NONE);
  const uint8_t gen = sys_device_gen(14);
  TEST_ASSERT_NULL(sys_device_step(14, gen, OWN_PROBE_OP_TICK));
  TEST_ASSERT_EQUAL_INT(1, s_dev[14]->ticks);
  // Busy: skipped at once, not after the lock timeout.
  s_job_id = 14;
  start_job(job_block);
  TEST_ASSERT_TRUE(xSemaphoreTake(s_blocked, WAIT_MS) == pdTRUE);
  const TickType_t t0 = xTaskGetTickCount();
  TEST_ASSERT_NULL(sys_device_step(14, gen, OWN_PROBE_OP_TICK));
  TEST_ASSERT_TRUE(xTaskGetTickCount() - t0 < CONFIG_SYS_DEVICE_LOCK_TIMEOUT_MS / 2);
  xSemaphoreGive(s_release);
  TEST_ASSERT_NULL(finish_job());
  TEST_ASSERT_EQUAL_INT(1, s_dev[14]->ticks);
  // Suspended: skipped.
  TEST_ASSERT_NULL(write(14, CONTRACT_SYS_DEVICE_OP_SUSPEND));
  TEST_ASSERT_NULL(sys_device_step(14, gen, OWN_PROBE_OP_TICK));
  TEST_ASSERT_EQUAL_INT(1, s_dev[14]->ticks);
  TEST_ASSERT_NULL(write(14, CONTRACT_SYS_DEVICE_OP_RESUME));
  TEST_ASSERT_NULL(sys_device_step(14, gen, OWN_PROBE_OP_TICK));
  TEST_ASSERT_EQUAL_INT(2, s_dev[14]->ticks);
  // Another device under the ID: a step of the one before is skipped.
  TEST_ASSERT_NULL(write(14, CONTRACT_SYS_DEVICE_OP_REMOVE));
  TEST_ASSERT_NULL(sys_device_step(14, gen, OWN_PROBE_OP_TICK));  // gone
  make(14, NONE);
  TEST_ASSERT_NULL(sys_device_step(14, gen, OWN_PROBE_OP_TICK));
  TEST_ASSERT_EQUAL_INT(0, s_dev[14]->ticks);
  TEST_ASSERT_NULL(sys_device_step(14, sys_device_gen(14), OWN_PROBE_OP_TICK));
  TEST_ASSERT_EQUAL_INT(1, s_dev[14]->ticks);
  // The op's own failure is returned; a step that is not a device's own op, or of no generation, is refused.
  TEST_ASSERT_NULL(SYS_DEVICE_SET(14, OWN_PROBE, OP_VALUE, 0xEE));
  TEST_ASSERT_ROOT(ERR_BAD_ARG, sys_device_step(14, sys_device_gen(14), OWN_PROBE_OP_TICK));
  TEST_ASSERT_ROOT(ERR_BAD_ARG, sys_device_step(14, 0, OWN_PROBE_OP_TICK));
  TEST_ASSERT_ROOT(ERR_BAD_ARG, sys_device_step(14, sys_device_gen(14), CONTRACT_TESTC_OP_LEVEL));
  TEST_ASSERT_ROOT(ERR_DEV_BAD_ID, sys_device_step(SYS_DEVICE_COUNT, 1, OWN_PROBE_OP_TICK));
}

static void test_deliver_waits_for_lock_runs_when_always_and_checks_generation(void) {
  make(15, NONE);
  const uint8_t gen = sys_device_gen(15);
  uint8_t val = 0x42;
  TEST_ASSERT_NULL(sys_device_deliver(15, gen, OWN_PROBE_OP_EVT, &val, sizeof(val)));
  TEST_ASSERT_EQUAL_INT(1, s_dev[15]->deliver_count);
  TEST_ASSERT_EQUAL_UINT8(0x42, s_dev[15]->deliver_val);
  TEST_ASSERT_FALSE(s_dev[15]->deliver_suspended);

  // Busy: waits for the lock rather than skipping like a step.
  s_dev[15]->hold_ms = 20;
  s_job_id = 15;
  start_job(job_block);
  TEST_ASSERT_TRUE(xSemaphoreTake(s_blocked, WAIT_MS) == pdTRUE);
  val = 0x55;
  TEST_ASSERT_NULL(sys_device_deliver(15, gen, OWN_PROBE_OP_EVT, &val, sizeof(val)));
  TEST_ASSERT_NULL(finish_job());
  s_dev[15]->hold_ms = 0;
  TEST_ASSERT_EQUAL_INT(2, s_dev[15]->deliver_count);
  TEST_ASSERT_EQUAL_UINT8(0x55, s_dev[15]->deliver_val);

  // Suspended: ALWAYS ops run, non-ALWAYS ops are refused.
  TEST_ASSERT_NULL(write(15, CONTRACT_SYS_DEVICE_OP_SUSPEND));
  val = 0x77;
  TEST_ASSERT_NULL(sys_device_deliver(15, gen, OWN_PROBE_OP_EVT, &val, sizeof(val)));
  TEST_ASSERT_EQUAL_INT(3, s_dev[15]->deliver_count);
  TEST_ASSERT_EQUAL_UINT8(0x77, s_dev[15]->deliver_val);
  TEST_ASSERT_TRUE(s_dev[15]->deliver_suspended);
  TEST_ASSERT_ROOT(ERR_DEV_SUSPENDED, sys_device_deliver(15, gen, OWN_PROBE_OP_TICK, NULL, 0));

  // Gone or replaced under the same ID: skipped cleanly without hitting the new device.
  TEST_ASSERT_NULL(write(15, CONTRACT_SYS_DEVICE_OP_REMOVE));
  TEST_ASSERT_NULL(sys_device_deliver(15, gen, OWN_PROBE_OP_EVT, &val, sizeof(val)));
  make(15, NONE);
  val = 0x99;
  TEST_ASSERT_NULL(sys_device_deliver(15, gen, OWN_PROBE_OP_EVT, &val, sizeof(val)));
  TEST_ASSERT_EQUAL_INT(0, s_dev[15]->deliver_count);
  TEST_ASSERT_NULL(sys_device_deliver(15, sys_device_gen(15), OWN_PROBE_OP_EVT, &val, sizeof(val)));
  TEST_ASSERT_EQUAL_INT(1, s_dev[15]->deliver_count);
  TEST_ASSERT_EQUAL_UINT8(0x99, s_dev[15]->deliver_val);

  // Invalid arguments.
  TEST_ASSERT_ROOT(ERR_BAD_ARG, sys_device_deliver(15, 0, OWN_PROBE_OP_EVT, &val, sizeof(val)));
  TEST_ASSERT_ROOT(ERR_BAD_ARG, sys_device_deliver(15, sys_device_gen(15), CONTRACT_SYS_DEVICE_OP_RESET, NULL, 0));
  TEST_ASSERT_ROOT(ERR_DEV_BAD_ID, sys_device_deliver(SYS_DEVICE_COUNT, 1, OWN_PROBE_OP_EVT, &val, sizeof(val)));
}

/* ---- sweeps --------------------------------------------------------------------------------------------------- */

static void test_a_sweep_runs_users_before_the_devices_they_use_and_resume_the_other_way(void) {
  follow_registry();
  make(20, NONE);
  make(21, 20);
  make(22, 21);
  s_log_n = 0;
  s_seen_n = 0;
  const int pings_21 = s_dev[21]->pings;
  TEST_ASSERT_NULL(sys_device_sweep(CONTRACT_SYS_DEVICE_OP_SUSPEND));
  TEST_ASSERT_EQUAL_INT(3, s_log_n);
  TEST_ASSERT_EQUAL_UINT8(22, s_log[0].id);
  TEST_ASSERT_EQUAL_UINT8(21, s_log[1].id);
  TEST_ASSERT_EQUAL_UINT8(20, s_log[2].id);
  TEST_ASSERT_EQUAL_INT(pings_21 + 1, s_dev[21]->pings);  // 22's suspend() reached 21, not refused
  TEST_ASSERT_TRUE(suspended(20) && suspended(21) && suspended(22));
  TEST_ASSERT_EQUAL_INT(1, seen(SYS_DEVICE_EV_SUSPENDED));  // one for the sweep
  TEST_ASSERT_EQUAL_UINT16(NONE, s_seen[0].source);
  s_log_n = 0;
  TEST_ASSERT_NULL(sys_device_sweep(CONTRACT_SYS_DEVICE_OP_RESUME));
  TEST_ASSERT_EQUAL_UINT8(20, s_log[0].id);
  TEST_ASSERT_EQUAL_UINT8(22, s_log[2].id);
  TEST_ASSERT_FALSE(suspended(22));
  TEST_ASSERT_NULL(sys_device_sweep(CONTRACT_SYS_DEVICE_OP_RESET));
  TEST_ASSERT_EQUAL_INT(1, s_dev[20]->resets);
  TEST_ASSERT_EQUAL_INT(1, s_dev[22]->resets);
  TEST_ASSERT_ROOT(ERR_DEV_UNKNOWN_OP, sys_device_sweep(OWN_PROBE_OP_PING));
}

static err_h s_create_during;

static void test_while_a_suspend_sweep_runs_a_write_from_no_device_and_create_are_refused(void) {
  make(23, NONE);  // first in the sweep: its suspend() waits for the test
  make(24, NONE);  // not reached yet
  s_suspend_block = 23;
  start_job(job_sweep_suspend);
  TEST_ASSERT_TRUE(xSemaphoreTake(s_blocked, WAIT_MS) == pdTRUE);
  TEST_ASSERT_FALSE(suspended(24));
  TEST_ASSERT_ROOT(ERR_DEV_SUSPENDED, SYS_DEVICE_SET(24, OWN_PROBE, OP_VALUE, 1));
  uint8_t v = 0xFF;
  TEST_ASSERT_NULL(SYS_DEVICE_GET(24, OWN_PROBE, OP_VALUE, &v));  // a read passes
  s_create_during = create_probe(25, NONE);
  TEST_ASSERT_ROOT(ERR_DEV_SUSPENDED, s_create_during);
  xSemaphoreGive(s_release);
  TEST_ASSERT_NULL(finish_job());
  TEST_ASSERT_TRUE(suspended(24));
  TEST_ASSERT_NULL(sys_device_sweep(CONTRACT_SYS_DEVICE_OP_RESUME));
  TEST_ASSERT_NULL(SYS_DEVICE_SET(24, OWN_PROBE, OP_VALUE, 1));  // the gate is down after the sweep
  make(25, NONE);
}

static void test_a_sweep_goes_on_past_a_device_whose_lock_is_not_free(void) {
  make(26, NONE);
  make(27, NONE);
  s_job_id = 26;
  start_job(job_block);
  TEST_ASSERT_TRUE(xSemaphoreTake(s_blocked, WAIT_MS) == pdTRUE);
  TEST_ASSERT_ROOT(ERR_DEV_BUSY, sys_device_sweep(CONTRACT_SYS_DEVICE_OP_SUSPEND));
  TEST_ASSERT_TRUE(suspended(27));
  xSemaphoreGive(s_release);
  TEST_ASSERT_NULL(finish_job());
  TEST_ASSERT_FALSE(suspended(26));
}

static void test_a_sweep_from_inside_an_op_fails_on_that_device_only(void) {
  make(28, NONE);
  make(29, NONE);
  TEST_ASSERT_ROOT(ERR_DEV_REENTRY, write(28, OWN_PROBE_OP_SWEEP));
  TEST_ASSERT_TRUE(suspended(29));
  TEST_ASSERT_FALSE(suspended(28));
}

/* ---- the interceptor ------------------------------------------------------------------------------------------ */

static int s_done_n;
static uint32_t s_done_root;
static uint8_t s_done_passed;

// LEVEL over 100 goes on as 100; 0xFF is refused.
static err_h clamp_level(uint8_t id, uint8_t dir, uint16_t op, cmd_in_t in, cmd_out_t* out) {
  (void)id, (void)op;
  if (dir != CMD_DIR_WRITE) {
    return NULL;
  }
  ERR_CHECK(in.data[0] != 0xFF, ERR_RANGE, .value = 0xFF, .min = 0, .max = 100);
  if (in.data[0] > 100) {
    out->data[0] = 100;
    out->len = 1;
  }
  return NULL;
}

static void level_done(uint8_t id, uint8_t dir, uint16_t op, cmd_in_t passed, err_h err) {
  (void)id, (void)dir, (void)op;
  s_done_n++;
  s_done_root = err_root_tag(err);
  s_done_passed = passed.len > 0 ? passed.data[0] : 0;
}

static void test_an_interceptor_changes_or_refuses_a_call_and_hears_how_each_ended(void) {
  TEST_ASSERT_NULL(sys_device_set_interceptor(MODULE_ID_TESTC, clamp_level, level_done));
  TEST_ASSERT_ROOT(ERR_BAD_ARG, sys_device_set_interceptor(MODULE_ID_TESTC, clamp_level, NULL));  // one each
  make(30, NONE);
  s_done_n = 0;
  TEST_ASSERT_NULL(SYS_DEVICE_SET(30, CONTRACT_TESTC, OP_LEVEL, 50));
  TEST_ASSERT_EQUAL_UINT8(50, s_dev[30]->level);
  TEST_ASSERT_EQUAL_INT(1, s_done_n);
  TEST_ASSERT_EQUAL_HEX32(0, s_done_root);
  TEST_ASSERT_NULL(SYS_DEVICE_SET(30, CONTRACT_TESTC, OP_LEVEL, 200));
  TEST_ASSERT_EQUAL_UINT8(100, s_dev[30]->level);  // the device got the clamped value
  TEST_ASSERT_EQUAL_UINT8(100, s_done_passed);
  TEST_ASSERT_ROOT(ERR_RANGE, SYS_DEVICE_SET(30, CONTRACT_TESTC, OP_LEVEL, 0xFF));
  TEST_ASSERT_EQUAL_INT(2, s_done_n);  // refused by the first stage: no done
  TEST_ASSERT_ROOT(ERR_DEV_NOT_FOUND, SYS_DEVICE_SET(31, CONTRACT_TESTC, OP_LEVEL, 1));
  TEST_ASSERT_EQUAL_INT(3, s_done_n);
  TEST_ASSERT_EQUAL_HEX32(ERR_DEV_NOT_FOUND, s_done_root);
  TEST_ASSERT_NULL(write(30, CONTRACT_SYS_DEVICE_OP_SUSPEND));
  TEST_ASSERT_ROOT(ERR_DEV_SUSPENDED, SYS_DEVICE_SET(30, CONTRACT_TESTC, OP_LEVEL, 1));
  TEST_ASSERT_EQUAL_HEX32(ERR_DEV_SUSPENDED, s_done_root);
}

/* ---- remote devices ------------------------------------------------------------------------------------------- */

static int s_remote_n;
static sys_device_remote_t s_remote_where;
static uint16_t s_remote_op;

static err_h remote_hook(const sys_device_remote_t* where, uint8_t dir, uint16_t op, uint8_t op_flags, cmd_in_t in,
                         cmd_out_t* out) {
  (void)dir, (void)op_flags, (void)in, (void)out;
  s_remote_n++;
  s_remote_where = *where;
  s_remote_op = op;
  return NULL;
}

static err_h create_remote(uint8_t id, uint8_t board, uint8_t remote_id) {
  uint8_t frame[2 + sizeof(sys_device_remote_t)];
  const uint16_t type = PROBE_TYPE;
  const sys_device_remote_t where = {.board = board, .remote_id = remote_id, .flags = SYS_DEVICE_REMOTE_WIRED};
  memcpy(frame, &type, sizeof(type));
  memcpy(frame + 2, &where, sizeof(where));
  return sys_device_call(id, CMD_DIR_WRITE, CONTRACT_SYS_DEVICE_OP_CREATE_REMOTE, (cmd_in_t){frame, sizeof(frame)}, NULL);
}

static void test_a_remote_device_is_checked_here_and_carried_by_the_hook(void) {
  TEST_ASSERT_NULL(create_remote(32, 5, 3));
  TEST_ASSERT_ROOT(ERR_DEV_REMOTE_DOWN, write(32, OWN_PROBE_OP_PING));  // no hook on this board
  sys_device_set_remote(remote_hook);
  s_remote_n = 0;
  TEST_ASSERT_NULL(write(32, OWN_PROBE_OP_PING));
  TEST_ASSERT_EQUAL_INT(1, s_remote_n);
  TEST_ASSERT_EQUAL_UINT8(5, s_remote_where.board);
  TEST_ASSERT_EQUAL_UINT8(3, s_remote_where.remote_id);
  TEST_ASSERT_EQUAL_HEX16(OWN_PROBE_OP_PING, s_remote_op);
  TEST_ASSERT_ROOT(ERR_DEV_LOCAL_OP, write(32, OWN_PROBE_OP_TICK));
  TEST_ASSERT_ROOT(ERR_DEV_ID_TAKEN, create_remote(33, 5, 3));  // that device has a local ID already
  TEST_ASSERT_NULL(write(32, CONTRACT_SYS_DEVICE_OP_SUSPEND));             // the flag only: no hook, no suspend()
  TEST_ASSERT_EQUAL_INT(1, s_remote_n);
  TEST_ASSERT_TRUE(suspended(32));
  TEST_ASSERT_ROOT(ERR_DEV_SUSPENDED, write(32, OWN_PROBE_OP_PING));
  TEST_ASSERT_NULL(write(32, CONTRACT_SYS_DEVICE_OP_REMOVE));
  TEST_ASSERT_FALSE(exists(32));
  TEST_ASSERT_EQUAL_INT(0, s_log_n);  // none of the type's hooks ran
}

/* ---- a board device (last: it can't be removed) --------------------------------------------------------------- */

static void test_a_board_device_is_never_removed_by_a_command_or_a_sweep(void) {
  uint8_t frame[SYS_DEVICE_CREATE_HDR + sizeof(probe_cfg_t)];
  TEST_ASSERT_NULL(create_at(frame, 40, NONE, 0, SYS_DEVICE_CREATE_SYSTEM));
  make(41, NONE);
  TEST_ASSERT_ROOT(ERR_DEV_SYSTEM, write(40, CONTRACT_SYS_DEVICE_OP_REMOVE));
  TEST_ASSERT_NULL(sys_device_sweep(CONTRACT_SYS_DEVICE_OP_REMOVE));
  TEST_ASSERT_TRUE(exists(40));
  TEST_ASSERT_FALSE(exists(41));
}

int main(void) {
  setvbuf(stdout, NULL, _IONBF, 0);  // every line out before a crash
  UNITY_BEGIN();
  RUN_TEST(test_create_installs_a_device_that_answers_its_ops);
  RUN_TEST(test_create_refuses_a_taken_id_an_unknown_type_and_a_wrong_size);
  RUN_TEST(test_create_reads_its_struct_where_the_frame_is_also_at_an_odd_address);
  RUN_TEST(test_ops_lists_the_registrys_and_the_types_ops_but_no_local_one);
  RUN_TEST(test_a_call_names_the_device_and_the_op_and_a_local_op_is_c_only);
  RUN_TEST(test_the_caller_is_the_device_whose_op_made_the_call);
  RUN_TEST(test_self_and_self_suspended_reflect_the_running_device);
  RUN_TEST(test_an_op_calling_its_own_device_is_reentry);
  RUN_TEST(test_an_event_of_an_op_reaches_an_inline_listener_after_the_outermost_call);
  RUN_TEST(test_created_and_removed_carry_the_generation_and_a_new_create_takes_a_new_one);
  RUN_TEST(test_a_failed_create_gives_back_what_it_took_last_first_and_runs_no_suspend);
  RUN_TEST(test_remove_suspends_a_working_device_then_releases_last_first);
  RUN_TEST(test_a_failing_release_still_releases_the_rest_and_frees_the_slot);
  RUN_TEST(test_a_resource_is_added_once_and_only_with_the_lock_of_its_owner);
  RUN_TEST(test_remove_of_a_device_in_use_is_refused_and_names_the_users);
  RUN_TEST(test_a_use_never_closes_a_cycle);
  RUN_TEST(test_a_lock_held_too_long_fails_with_the_op_that_holds_it);
  RUN_TEST(test_a_step_runs_only_on_its_device_when_the_device_takes_it_now);
  RUN_TEST(test_deliver_waits_for_lock_runs_when_always_and_checks_generation);
  RUN_TEST(test_a_sweep_runs_users_before_the_devices_they_use_and_resume_the_other_way);
  RUN_TEST(test_while_a_suspend_sweep_runs_a_write_from_no_device_and_create_are_refused);
  RUN_TEST(test_a_sweep_goes_on_past_a_device_whose_lock_is_not_free);
  RUN_TEST(test_a_sweep_from_inside_an_op_fails_on_that_device_only);
  RUN_TEST(test_an_interceptor_changes_or_refuses_a_call_and_hears_how_each_ended);
  RUN_TEST(test_a_remote_device_is_checked_here_and_carried_by_the_hook);
  RUN_TEST(test_a_board_device_is_never_removed_by_a_command_or_a_sweep);  // last: it stays
  return UNITY_END();
}
