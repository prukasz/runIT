/* Host unit tests of sys_device_policy (sys_device_policy.c + router/sys_device_policy_router.c) with real
 * sys_device, sys_event and errors.
 * Run: python runit-esp/test_host/run.py sys_device_policy */
#include <stdatomic.h>
#include <string.h>

#include "err_sink.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host_assert.h"
#include "module_id.h"
#include "router_queue_errors.h"
#include "sys_device.h"
#include "sys_device_driver.h"
#include "sys_device_events.h"
#include "sys_device_policy.h"
#include "sys_device_policy_router.h"
#include "unity.h"

/* ---- Synthetic test errors (LOW, MEDIUM, HIGH, CRITICAL) ---- */

MODULE_ID_DECLARE(MODULE_ID_POLT, 0x3D)
#define ERR_POLT_MAP(X, M)                    \
  X(M, ERR_POLT_LOW,  1, ERR_SEV_LOW,      {}) \
  X(M, ERR_POLT_MED,  2, ERR_SEV_MEDIUM,   {}) \
  X(M, ERR_POLT_HIGH, 3, ERR_SEV_HIGH,     {}) \
  X(M, ERR_POLT_CRIT, 4, ERR_SEV_CRITICAL, {})
ERR_TAGS_DECLARE(MODULE_ID_POLT, ERR_POLT_MAP)
ERR_MODULE_DEFINE(MODULE_ID_POLT, ERR_POLT_MAP)

/* ---- Minimal fake device driver for real sys_device hooks ---- */

SYS_DEVICE_TYPE_DECLARE(PDEV, 0x0071)

typedef struct __attribute__((packed)) {
  uint8_t dummy;
} pdev_cfg_t;

typedef struct {
  uint8_t id;
} pdev_t;

#define SYS_DEVICE_CTX pdev_t

static atomic_int s_suspends[SYS_DEVICE_COUNT];
static atomic_int s_resets[SYS_DEVICE_COUNT];

OP_DECLARE_WRITE(CONTRACT_SYS_DEVICE, OP_RESET) {
  atomic_fetch_add(&s_resets[c->id], 1);
  return NULL;
}

static err_h pdev_create(uint8_t id, pdev_t* c, const pdev_cfg_t* cfg) {
  (void)cfg;
  c->id = id;
  return NULL;
}

static err_h pdev_suspend(pdev_t* c) {
  atomic_fetch_add(&s_suspends[c->id], 1);
  return NULL;
}

static err_h pdev_resume(pdev_t* c) {
  (void)c;
  return NULL;
}

static const sys_device_op_t k_pdev_ops[] = {
    OP_ENTRY_W(CONTRACT_SYS_DEVICE, OP_RESET),
};

SYS_DEVICE_TYPE_DEFINE(PDEV, pdev_cfg_t, pdev_create, pdev_suspend, pdev_resume, k_pdev_ops)

/* ---- Callbacks & error sink ---- */

static atomic_int s_vm_stops;
static atomic_int s_actions_run;
static atomic_uint s_last_action;
static atomic_bool s_action_should_fail;
static atomic_int s_fault_resp_reported;

static err_h mock_vm_stop(uint8_t id) {
  (void)id;
  atomic_fetch_add(&s_vm_stops, 1);
  return NULL;
}

static err_h mock_action_runner(uint32_t action, err_h chain) {
  (void)chain;
  atomic_store(&s_last_action, action);
  atomic_fetch_add(&s_actions_run, 1);
  return atomic_load(&s_action_should_fail) ? ERR_NEW(ERR_POLT_HIGH) : NULL;
}

static void test_sink(err_h chain, const cmd_req_t* cmd) {
  (void)cmd;
  if (err_find(chain, ERR_DEV_FAULT_RESPONSE) != NULL) {
    atomic_fetch_add(&s_fault_resp_reported, 1);
  }
  err_h resp = sys_device_policy_handle(chain);
  err_free(resp);
}

/* ---- Helpers ---- */

static void create_dev(uint8_t id) {
  uint8_t frame[SYS_DEVICE_CREATE_HDR + sizeof(pdev_cfg_t)] = {0};
  const uint16_t type = PDEV_TYPE;
  memcpy(frame, &type, sizeof(type));
  TEST_ASSERT_NULL(sys_device_write(id, CONTRACT_SYS_DEVICE_OP_CREATE, frame, sizeof(frame)));
}

static bool is_suspended(uint8_t id) {
  sys_device_info_t info = {0};
  TEST_ASSERT_NULL(SYS_DEVICE_GET(id, CONTRACT_SYS_DEVICE, OP_INFO, &info));
  return info.suspended != 0;
}

static bool wait_for_atomic(atomic_int* counter, int want, int timeout_ms) {
  for (int t = 0; t < timeout_ms; t += 5) {
    if (atomic_load(counter) >= want) return true;
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  return atomic_load(counter) >= want;
}

static bool wait_for_suspended(uint8_t id, int timeout_ms) {
  for (int t = 0; t < timeout_ms; t += 5) {
    if (is_suspended(id)) return true;
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  return is_suspended(id);
}

static err_h make_dev_err(uint8_t id, err_h cause) {
  return ERR_WRAP(cause, ERR_DEV_ID, .id = id);
}

void setUp(void) {
  for (uint32_t i = 0; i < SYS_DEVICE_COUNT; i++) {
    atomic_store(&s_suspends[i], 0);
    atomic_store(&s_resets[i], 0);
  }
  atomic_store(&s_vm_stops, 0);
  atomic_store(&s_actions_run, 0);
  atomic_store(&s_last_action, 0);
  atomic_store(&s_action_should_fail, false);
  atomic_store(&s_fault_resp_reported, 0);
  sys_device_policy_set_vm_stop(mock_vm_stop);
  sys_device_policy_set_action_runner(mock_action_runner);
}

void tearDown(void) {
  err_free(sys_device_sweep(CONTRACT_SYS_DEVICE_OP_REMOVE));
}

/* ---- Tests ---- */

static void test_created_gives_default_policy_and_removed_clears_it(void) {
  sys_device_error_policy_t p;
  TEST_ASSERT_FALSE(sys_device_policy_get(1, &p));

  create_dev(1);
  TEST_ASSERT_TRUE(sys_device_policy_get(1, &p));
  TEST_ASSERT_EQUAL_UINT8(SYS_DEVICE_IMPORTANCE_LOW, p.importance);
  for (uint32_t i = 0; i < SYS_DEVICE_ERR_LEVELS; i++) TEST_ASSERT_EQUAL_UINT32(0, p.actions[i]);

  /* Stale REMOVED with wrong generation must not wipe the current device's policy. */
  TEST_ASSERT_NULL(SYS_EVENT_PUBLISH(SYS_DEVICE_EV_REMOVED, 1, 0, .gen = 250));
  TEST_ASSERT_TRUE(sys_device_policy_get(1, &p));

  TEST_ASSERT_NULL(SYS_DEVICE_SET(1, CONTRACT_SYS_DEVICE, OP_REMOVE));
  TEST_ASSERT_FALSE(sys_device_policy_get(1, &p));
}

static void test_set_get_validation_and_router(void) {
  create_dev(2);
  sys_device_error_policy_t bad_imp = {.importance = 99};
  err_h e = sys_device_policy_set(2, &bad_imp);
  TEST_ASSERT_ERR_TAG(ERR_DEV_RANGE, e);
  err_free(e);

  sys_device_error_policy_t bad_resp = {.importance = SYS_DEVICE_IMPORTANCE_MEDIUM, .actions = {0, 0, 0xFF000099u, 0}};
  e = sys_device_policy_set(2, &bad_resp);
  TEST_ASSERT_ERR_TAG(ERR_DEV_BAD_ARG, e);
  err_free(e);

  /* Router write + read roundtrip */
  uint8_t wr[3 + sizeof(sys_device_policy_entry_t)] = {MODULE_ID_POLICY, CMD_DIR_WRITE, POLICY_OP_POLICY};
  sys_device_policy_entry_t entry = {
      .id = 2,
      .policy = {.importance = SYS_DEVICE_IMPORTANCE_HIGH, .actions = {0, SYS_DEVICE_RESPONSE_RESET, SYS_DEVICE_RESPONSE_SUSPEND, 0}},
  };
  memcpy(wr + 3, &entry, sizeof(entry));
  uint8_t buf[64];
  cmd_out_t out = {.data = buf, .cap = sizeof(buf)};
  TEST_ASSERT_NULL(sys_device_policy_router(NULL, (cmd_in_t){wr, sizeof(wr)}, &out));

  uint8_t rd[4] = {MODULE_ID_POLICY, CMD_DIR_READ, POLICY_OP_POLICY, 2};
  out.len = 0;
  TEST_ASSERT_NULL(sys_device_policy_router(NULL, (cmd_in_t){rd, sizeof(rd)}, &out));
  TEST_ASSERT_EQUAL_UINT32(sizeof(sys_device_policy_entry_t), out.len);
  sys_device_policy_entry_t got;
  memcpy(&got, buf, sizeof(got));
  TEST_ASSERT_EQUAL_UINT8(SYS_DEVICE_IMPORTANCE_HIGH, got.policy.importance);
  TEST_ASSERT_EQUAL_UINT32(SYS_DEVICE_RESPONSE_RESET, got.policy.actions[1]);
  TEST_ASSERT_EQUAL_UINT32(SYS_DEVICE_RESPONSE_SUSPEND, got.policy.actions[2]);
}

static void test_builtin_suspend_and_reset_responses(void) {
  create_dev(3);
  sys_device_error_policy_t p = {
      .importance = SYS_DEVICE_IMPORTANCE_HIGH,
      .actions = {0, SYS_DEVICE_RESPONSE_RESET, SYS_DEVICE_RESPONSE_SUSPEND, 0},
  };
  TEST_ASSERT_NULL(sys_device_policy_set(3, &p));

  /* MEDIUM -> RESET */
  err_h e1 = make_dev_err(3, ERR_NEW(ERR_POLT_MED));
  TEST_ASSERT_NULL(sys_device_policy_handle(e1));
  err_free(e1);
  TEST_ASSERT_TRUE(wait_for_atomic(&s_resets[3], 1, 500));
  TEST_ASSERT_FALSE(is_suspended(3));

  /* HIGH -> SUSPEND */
  err_h e2 = make_dev_err(3, ERR_NEW(ERR_POLT_HIGH));
  TEST_ASSERT_NULL(sys_device_policy_handle(e2));
  err_free(e2);
  TEST_ASSERT_TRUE(wait_for_suspended(3, 500));
  TEST_ASSERT_EQUAL_INT(1, atomic_load(&s_suspends[3]));
}

static void test_critical_runs_system_fault_once_across_two_devices(void) {
  create_dev(4);
  create_dev(5);
  sys_device_error_policy_t p = {.importance = SYS_DEVICE_IMPORTANCE_LOW, .actions = {0, 0, 0, 0x01000010u}};
  TEST_ASSERT_NULL(sys_device_policy_set(4, &p));
  TEST_ASSERT_NULL(sys_device_policy_set(5, &p));

  /* Chain touching BOTH dev 4 and dev 5 over a CRITICAL root error. */
  err_h chain = make_dev_err(4, make_dev_err(5, ERR_NEW(ERR_POLT_CRIT)));
  TEST_ASSERT_NULL(sys_device_policy_handle(chain));
  err_free(chain);

  TEST_ASSERT_TRUE(wait_for_atomic(&s_actions_run, 2, 500));
  TEST_ASSERT_TRUE(is_suspended(4));
  TEST_ASSERT_TRUE(is_suspended(5));
  /* system_fault() (vm_stop + sweep SUSPEND) must run ONCE, not twice! */
  TEST_ASSERT_EQUAL_INT(1, atomic_load(&s_vm_stops));
  TEST_ASSERT_EQUAL_INT(1, atomic_load(&s_suspends[4]));
  TEST_ASSERT_EQUAL_INT(1, atomic_load(&s_suspends[5]));
}

static void test_importance_none_stops_walk_and_err_remote_stops_walk(void) {
  create_dev(6);
  create_dev(7);
  sys_device_error_policy_t p_none = {.importance = SYS_DEVICE_IMPORTANCE_NONE, .actions = {0, 0, SYS_DEVICE_RESPONSE_SUSPEND, 0}};
  sys_device_error_policy_t p_med = {.importance = SYS_DEVICE_IMPORTANCE_MEDIUM, .actions = {0, 0, SYS_DEVICE_RESPONSE_SUSPEND, 0}};
  TEST_ASSERT_NULL(sys_device_policy_set(6, &p_none));
  TEST_ASSERT_NULL(sys_device_policy_set(7, &p_med));

  /* Dev 6 has NONE -> stops walk before dev 7. */
  err_h c1 = make_dev_err(6, make_dev_err(7, ERR_NEW(ERR_POLT_HIGH)));
  TEST_ASSERT_NULL(sys_device_policy_handle(c1));
  err_free(c1);
  vTaskDelay(pdMS_TO_TICKS(30));
  TEST_ASSERT_FALSE(is_suspended(6));
  TEST_ASSERT_FALSE(is_suspended(7));

  /* ERR_REMOTE stops walk: dev 7 below ERR_REMOTE belongs to another board and is not suspended. */
  err_h c2 = ERR_WRAP(make_dev_err(7, ERR_NEW(ERR_POLT_HIGH)), ERR_REMOTE, .board = 2, .seq = 1);
  TEST_ASSERT_NULL(sys_device_policy_handle(c2));
  err_free(c2);
  vTaskDelay(pdMS_TO_TICKS(30));
  TEST_ASSERT_FALSE(is_suspended(7));
}

static void test_action_runner_failure_reports_fault_response_without_loop(void) {
  create_dev(8);
  sys_device_error_policy_t p = {.importance = SYS_DEVICE_IMPORTANCE_MEDIUM, .actions = {0, 0, 0x01000022u, 0}};
  TEST_ASSERT_NULL(sys_device_policy_set(8, &p));
  atomic_store(&s_action_should_fail, true);

  err_h e = make_dev_err(8, ERR_NEW(ERR_POLT_HIGH));
  TEST_ASSERT_NULL(sys_device_policy_handle(e));
  err_free(e);

  TEST_ASSERT_TRUE(wait_for_atomic(&s_fault_resp_reported, 1, 500));
  vTaskDelay(pdMS_TO_TICKS(30));
  /* Exactly one action attempt and one ERR_DEV_FAULT_RESPONSE (no recursion loop). */
  TEST_ASSERT_EQUAL_INT(1, atomic_load(&s_actions_run));
  TEST_ASSERT_EQUAL_INT(1, atomic_load(&s_fault_resp_reported));
  TEST_ASSERT_EQUAL_UINT32(0x01000022u, atomic_load(&s_last_action));
}

static void test_critical_before_device_and_missing_device_and_dedup_and_low_sev(void) {
  create_dev(9);

  /* 1. CRITICAL node before any ERR_DEV_ID triggers system_fault() once even with a device below it. */
  err_h c_top = ERR_WRAP(make_dev_err(9, ERR_NEW(ERR_POLT_HIGH)), ERR_POLT_CRIT);
  TEST_ASSERT_NULL(sys_device_policy_handle(c_top));
  err_free(c_top);
  TEST_ASSERT_TRUE(wait_for_suspended(9, 500));
  TEST_ASSERT_EQUAL_INT(1, atomic_load(&s_vm_stops));
  TEST_ASSERT_NULL(SYS_DEVICE_SET(9, CONTRACT_SYS_DEVICE, OP_RESUME));

  /* 2. Missing device (e.g. failed CREATE): HIGH is ignored, CRITICAL still runs system_fault(). */
  atomic_store(&s_vm_stops, 0);
  err_h c_miss_hi = make_dev_err(30, ERR_NEW(ERR_POLT_HIGH));
  TEST_ASSERT_NULL(sys_device_policy_handle(c_miss_hi));
  err_free(c_miss_hi);
  vTaskDelay(pdMS_TO_TICKS(25));
  TEST_ASSERT_EQUAL_INT(0, atomic_load(&s_vm_stops));
  TEST_ASSERT_FALSE(is_suspended(9));

  err_h c_miss_cr = make_dev_err(30, ERR_NEW(ERR_POLT_CRIT));
  TEST_ASSERT_NULL(sys_device_policy_handle(c_miss_cr));
  err_free(c_miss_cr);
  TEST_ASSERT_TRUE(wait_for_suspended(9, 500));
  TEST_ASSERT_EQUAL_INT(1, atomic_load(&s_vm_stops));
  TEST_ASSERT_NULL(SYS_DEVICE_SET(9, CONTRACT_SYS_DEVICE, OP_RESUME));

  /* 3. Same device appearing twice in one chain is acted on only once (handled bitmask). */
  sys_device_error_policy_t p_crit = {
      .importance = SYS_DEVICE_IMPORTANCE_CRITICAL,
      .actions = {0x01000033u, 0, 0x01000044u, 0},
  };
  TEST_ASSERT_NULL(sys_device_policy_set(9, &p_crit));
  atomic_store(&s_actions_run, 0);
  err_h c_dup = make_dev_err(9, make_dev_err(9, ERR_NEW(ERR_POLT_HIGH)));
  TEST_ASSERT_NULL(sys_device_policy_handle(c_dup));
  err_free(c_dup);
  TEST_ASSERT_TRUE(wait_for_atomic(&s_actions_run, 1, 500));
  vTaskDelay(pdMS_TO_TICKS(25));
  TEST_ASSERT_EQUAL_INT(1, atomic_load(&s_actions_run));
  TEST_ASSERT_EQUAL_UINT32(0x01000044u, atomic_load(&s_last_action));

  /* 4. IMPORTANCE_CRITICAL admits ERR_SEV_LOW and runs actions[0]. */
  atomic_store(&s_actions_run, 0);
  err_h c_low = make_dev_err(9, ERR_NEW(ERR_POLT_LOW));
  TEST_ASSERT_NULL(sys_device_policy_handle(c_low));
  err_free(c_low);
  TEST_ASSERT_TRUE(wait_for_atomic(&s_actions_run, 1, 500));
  TEST_ASSERT_EQUAL_UINT32(0x01000033u, atomic_load(&s_last_action));
}

int main(void) {
  TEST_ASSERT_NULL(sys_event_init());
  TEST_ASSERT_NULL(sys_device_policy_init());
  TEST_ASSERT_NULL(sys_device_policy_init());  // idempotent
  err_set_sink(test_sink);

  UNITY_BEGIN();
  RUN_TEST(test_created_gives_default_policy_and_removed_clears_it);
  RUN_TEST(test_set_get_validation_and_router);
  RUN_TEST(test_builtin_suspend_and_reset_responses);
  RUN_TEST(test_critical_runs_system_fault_once_across_two_devices);
  RUN_TEST(test_importance_none_stops_walk_and_err_remote_stops_walk);
  RUN_TEST(test_action_runner_failure_reports_fault_response_without_loop);
  RUN_TEST(test_critical_before_device_and_missing_device_and_dedup_and_low_sev);
  return UNITY_END();
}
