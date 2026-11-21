/* Host tests of sys_system (safe state, deadman on SESSION_EV_GONE, time broadcast, router ops). */
#include <string.h>

#include "board_time.h"
#include "esp_system.h"
#include "host_assert.h"
#include "router.h"
#include "router_queue.h"
#include "sys_device.h"
#include "sys_event.h"
#include "sys_session.h"
#include "sys_session_events.h"
#include "sys_system.h"
#include "sys_system_router.h"
#include "unity.h"

/* ---- Stubs for sys_device_sweep, router_origin, router_run_urgent, sys_session_* ---- */
static int s_sweep_calls;
static uint16_t s_last_sweep_op;
static bool s_fail_resume_sweep;

err_h sys_device_sweep(uint16_t op) {
  s_sweep_calls++;
  s_last_sweep_op = op;
  if (s_fail_resume_sweep && op == CONTRACT_SYS_DEVICE_OP_RESUME) {
    return ERR_NEW(ERR_BAD_ARG);
  }
  return NULL;
}

static cmd_req_t s_origin;
static bool s_has_origin;
const cmd_req_t* router_origin(void) {
  return s_has_origin ? &s_origin : NULL;
}

static int s_urgent_calls;
err_h router_run_urgent(void (*fn)(void)) {
  s_urgent_calls++;
  fn();
  return NULL;
}

static uint32_t s_deadman_ms;
static bool s_deadman_safety;
static uint8_t s_deadman_peer;
uint32_t sys_session_deadman_get(uint8_t peer) {
  return peer == s_deadman_peer ? s_deadman_ms : 0u;
}
err_h sys_session_deadman_set(uint8_t peer, uint32_t timeout_ms, bool safety) {
  s_deadman_peer = peer;
  s_deadman_ms = timeout_ms;
  s_deadman_safety = safety;
  return NULL;
}

static int s_bcast_calls;
static uint8_t s_bcast_flags;
static uint8_t s_bcast_data[32];
static uint32_t s_bcast_len;
err_h sys_session_broadcast(cmd_in_t req, uint8_t flags) {
  s_bcast_calls++;
  s_bcast_flags = flags;
  s_bcast_len = req.len < sizeof(s_bcast_data) ? req.len : sizeof(s_bcast_data);
  memcpy(s_bcast_data, req.data, s_bcast_len);
  return NULL;
}

static err_h route_sys(uint8_t dir, uint8_t op, const void* args, uint32_t args_len, uint8_t* reply,
                       uint32_t reply_cap, uint32_t* reply_len) {
  uint8_t req[32];
  req[0] = MODULE_ID_SYS;
  req[1] = dir;
  req[2] = op;
  if (args_len > 0) {
    memcpy(req + 3, args, args_len);
  }
  cmd_out_t out = {.data = reply, .cap = reply_cap, .len = 0};
  err_h e = router_route((cmd_in_t){req, 3u + args_len}, &out);
  if (reply_len != NULL) {
    *reply_len = out.len;
  }
  return e;
}

void setUp(void) {
  s_sweep_calls = 0;
  s_last_sweep_op = 0;
  s_fail_resume_sweep = false;
  s_has_origin = false;
  s_urgent_calls = 0;
  s_deadman_ms = 0;
  s_deadman_safety = false;
  s_deadman_peer = 0;
  s_bcast_calls = 0;
  s_bcast_len = 0;
  host_restart_reset();
  /* Ensure clean non-safe state */
  err_free(sys_system_leave_safe_state());
  s_sweep_calls = 0;
}

void tearDown(void) {}

static void test_safe_state_enter_leave_and_failed_resume_keeps_note(void) {
  uint8_t reply[64];
  uint32_t rlen = 0;

  /* STOP enters safe state with SYS_SAFE_STOP and sweeps SUSPEND */
  TEST_ASSERT_NULL(route_sys(CMD_DIR_WRITE, SYS_OP_STOP, NULL, 0, reply, sizeof(reply), &rlen));
  TEST_ASSERT_EQUAL_INT(1, s_sweep_calls);
  TEST_ASSERT_EQUAL_HEX16(CONTRACT_SYS_DEVICE_OP_SUSPEND, s_last_sweep_op);

  /* OP_SAFE reports reason == SYS_SAFE_STOP */
  TEST_ASSERT_NULL(route_sys(CMD_DIR_READ, SYS_OP_SAFE, NULL, 0, reply, sizeof(reply), &rlen));
  TEST_ASSERT_EQUAL_UINT32(sizeof(sys_system_safe_t), rlen);
  sys_system_safe_t safe;
  memcpy(&safe, reply, sizeof(safe));
  TEST_ASSERT_EQUAL_UINT8(SYS_SAFE_STOP, safe.reason);

  /* Failed RESUME sweep keeps the safe-state note */
  s_fail_resume_sweep = true;
  err_h e = route_sys(CMD_DIR_WRITE, SYS_OP_SAFE_LEAVE, NULL, 0, reply, sizeof(reply), &rlen);
  TEST_ASSERT_NOT_NULL(e);
  err_free(e);
  uint8_t reason = 0;
  uint32_t for_ms = 0;
  sys_system_safe_state_get(&reason, &for_ms);
  TEST_ASSERT_EQUAL_UINT8(SYS_SAFE_STOP, reason);

  /* Successful RESUME clears the note */
  s_fail_resume_sweep = false;
  TEST_ASSERT_NULL(route_sys(CMD_DIR_WRITE, SYS_OP_SAFE_LEAVE, NULL, 0, reply, sizeof(reply), &rlen));
  TEST_ASSERT_EQUAL_HEX16(CONTRACT_SYS_DEVICE_OP_RESUME, s_last_sweep_op);
  sys_system_safe_state_get(&reason, &for_ms);
  TEST_ASSERT_EQUAL_UINT8(0, reason);
  TEST_ASSERT_EQUAL_UINT32(0, for_ms);
}

static void test_session_gone_safety_triggers_deadman_and_urgent_stop_broadcast(void) {
  /* Non-safety session gone -> ignored */
  TEST_ASSERT_NULL(SYS_EVENT_PUBLISH(SESSION_EV_GONE, 0xF0, 0, .safety = 0));
  TEST_ASSERT_EQUAL_INT(0, s_urgent_calls);
  TEST_ASSERT_EQUAL_INT(0, s_bcast_calls);

  /* Safety session gone -> urgent deadman safe state + urgent SYS_OP_STOP broadcast */
  TEST_ASSERT_NULL(SYS_EVENT_PUBLISH(SESSION_EV_GONE, 0xF0, 0, .safety = 1));
  TEST_ASSERT_EQUAL_INT(1, s_urgent_calls);
  TEST_ASSERT_EQUAL_HEX16(CONTRACT_SYS_DEVICE_OP_SUSPEND, s_last_sweep_op);
  uint8_t reason = 0;
  uint32_t for_ms = 0;
  sys_system_safe_state_get(&reason, &for_ms);
  TEST_ASSERT_EQUAL_UINT8(SYS_SAFE_DEADMAN, reason);

  TEST_ASSERT_EQUAL_INT(1, s_bcast_calls);
  TEST_ASSERT_EQUAL_HEX8(ROUTER_FLAG_URGENT, s_bcast_flags);
  TEST_ASSERT_EQUAL_UINT32(3, s_bcast_len);
  TEST_ASSERT_EQUAL_UINT8(MODULE_ID_SYS, s_bcast_data[0]);
  TEST_ASSERT_EQUAL_UINT8(CMD_DIR_WRITE, s_bcast_data[1]);
  TEST_ASSERT_EQUAL_UINT8(SYS_OP_STOP, s_bcast_data[2]);
}

static void test_deadman_bye_ping_time_and_tasks_router_ops(void) {
  uint8_t reply[128];
  uint32_t rlen = 0;

  /* PING returns 4-byte uptime */
  TEST_ASSERT_NULL(route_sys(CMD_DIR_READ, SYS_OP_PING, NULL, 0, reply, sizeof(reply), &rlen));
  TEST_ASSERT_EQUAL_UINT32(4, rlen);

  /* DEADMAN / BYE refused without session origin or from CMD_SRC_SELF */
  sys_system_deadman_t dm = {.timeout_ms = 5000};
  s_has_origin = false;
  err_h e = route_sys(CMD_DIR_WRITE, SYS_OP_DEADMAN, &dm, sizeof(dm), reply, sizeof(reply), &rlen);
  TEST_ASSERT_NOT_NULL(e);
  TEST_ASSERT_EQUAL_HEX32(ERR_BAD_ARG, err_root_tag(e));
  err_free(e);

  s_has_origin = true;
  s_origin.src = CMD_SRC_SELF;
  e = route_sys(CMD_DIR_WRITE, SYS_OP_BYE, NULL, 0, reply, sizeof(reply), &rlen);
  TEST_ASSERT_NOT_NULL(e);
  TEST_ASSERT_EQUAL_HEX32(ERR_BAD_ARG, err_root_tag(e));
  err_free(e);

  /* Valid client origin: DEADMAN set/get, then BYE disarms with safety=false */
  s_origin.src = 0xF0;
  TEST_ASSERT_NULL(route_sys(CMD_DIR_WRITE, SYS_OP_DEADMAN, &dm, sizeof(dm), reply, sizeof(reply), &rlen));
  TEST_ASSERT_EQUAL_UINT32(5000, s_deadman_ms);
  TEST_ASSERT_TRUE(s_deadman_safety);

  TEST_ASSERT_NULL(route_sys(CMD_DIR_READ, SYS_OP_DEADMAN, NULL, 0, reply, sizeof(reply), &rlen));
  sys_system_deadman_t got_dm;
  memcpy(&got_dm, reply, sizeof(got_dm));
  TEST_ASSERT_EQUAL_UINT32(5000, got_dm.timeout_ms);

  TEST_ASSERT_NULL(route_sys(CMD_DIR_WRITE, SYS_OP_BYE, NULL, 0, reply, sizeof(reply), &rlen));
  TEST_ASSERT_EQUAL_UINT32(0, s_deadman_ms);
  TEST_ASSERT_FALSE(s_deadman_safety);

  /* TIME: setting 0 rejected; setting valid unix_ms broadcasts TIME; SESSION_EV_REACHED also broadcasts */
  sys_system_time_t tm = {.unix_ms = 0};
  e = route_sys(CMD_DIR_WRITE, SYS_OP_TIME, &tm, sizeof(tm), reply, sizeof(reply), &rlen);
  TEST_ASSERT_NOT_NULL(e);
  TEST_ASSERT_EQUAL_HEX32(ERR_BAD_ARG, err_root_tag(e));
  err_free(e);

  tm.unix_ms = 1700000000000ULL;
  TEST_ASSERT_NULL(route_sys(CMD_DIR_WRITE, SYS_OP_TIME, &tm, sizeof(tm), reply, sizeof(reply), &rlen));
  TEST_ASSERT_EQUAL_INT(1, s_bcast_calls);
  TEST_ASSERT_EQUAL_UINT8(SYS_OP_TIME, s_bcast_data[2]);

  TEST_ASSERT_NULL(SYS_EVENT_PUBLISH(SESSION_EV_REACHED, 0x02, 0));
  TEST_ASSERT_EQUAL_INT(2, s_bcast_calls);

  /* OP_TASKS returns count + task status rows */
  TEST_ASSERT_NULL(route_sys(CMD_DIR_READ, SYS_OP_TASKS, NULL, 0, reply, sizeof(reply), &rlen));
  TEST_ASSERT_EQUAL_UINT8(1, reply[0]);
  TEST_ASSERT_EQUAL_UINT32(1u + sizeof(sys_system_task_t), rlen);
}

int main(void) {
  TEST_ASSERT_NULL(router_root_init());

  UNITY_BEGIN();
  RUN_TEST(test_safe_state_enter_leave_and_failed_resume_keeps_note);
  RUN_TEST(test_session_gone_safety_triggers_deadman_and_urgent_stop_broadcast);
  RUN_TEST(test_deadman_bye_ping_time_and_tasks_router_ops);
  return UNITY_END();
}
