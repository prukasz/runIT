#include "sys_system_router.h"

#include "esp_log.h"
#include "esp_system.h"
#include "sys_system.h"
#include "board_time.h"

#define TAG __FILE_NAME__

#define RESTART_DELAY_MS 100u  // lets the answer leave before the restart

static void restart_now(TimerHandle_t t) {
  (void)t;
  esp_restart();
}

U_TIMER_DEFINE(sys_restart_timer, MSEC(RESTART_DELAY_MS), false, restart_now);

ROUTER_OP_NOARGS(op_ping) {
  const uint32_t uptime_ms = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());
  return cmd_reply(out, &uptime_ms, sizeof(uptime_ms));
}

ROUTER_OP_NOARGS(op_stop) {
  sys_system_enter_safe_state(SYS_SAFE_STOP);
  return NULL;
}

ROUTER_OP_NOARGS(op_restart) {
  xTimerStart(sys_restart_timer, 0);
  return NULL;
}

// A client with no session (a provider's command binding) has no silence the dead-man could notice: refused.
ROUTER_OP(op_deadman_set, sys_system_deadman_t) {
  const cmd_req_t* origin = router_origin();
  ERR_CHECK(origin == NULL || origin->src != ROUTER_SRC_NONE, ERR_CMD_BAD_ARG);
  return sys_system_deadman_set(args->timeout_ms);
}

ROUTER_OP_NOARGS(op_deadman_get) {
  const sys_system_deadman_t r = {.timeout_ms = sys_system_deadman_get()};
  return cmd_reply(out, &r, sizeof(r));
}

// The client is leaving on purpose (its Disconnect, its window closing): the board goes on with what it runs, and a dead-man the client
// armed must not stop it for the silence that follows. A link that drops without it (power, range) ends its session.
ROUTER_OP_NOARGS(op_bye) {
  ERR_TRY(sys_system_deadman_set(0));
  ESP_LOGI(TAG, "client left: dead-man off");
  return NULL;
}

ROUTER_OP(op_time_set, sys_system_time_t) {
  ERR_CHECK(board_time_set(args->unix_ms), ERR_CMD_BAD_ARG);
  sys_system_broadcast_time();  // the root: every board takes it
  return NULL;
}

ROUTER_OP_NOARGS(op_time_get) {
  const sys_system_time_t r = {.unix_ms = board_time_unix_ms()};
  return cmd_reply(out, &r, sizeof(r));
}

ROUTER_OP_NOARGS(op_safe_get) {
  uint8_t reason;
  uint32_t for_ms;
  sys_system_safe_state_get(&reason, &for_ms);
  const sys_system_safe_t r = {.reason = reason, .for_ms = for_ms};
  return cmd_reply(out, &r, sizeof(r));
}

ROUTER_OP_NOARGS(op_safe_leave) {
  return sys_system_leave_safe_state();
}

static const router_op_t k_ops[] = {
    ROUTER_R(SYS_SYSTEM_OP_PING, op_ping),
    ROUTER_W(SYS_SYSTEM_OP_STOP, op_stop),
    ROUTER_W(SYS_SYSTEM_OP_RESTART, op_restart),
    ROUTER_RW(SYS_SYSTEM_OP_DEADMAN, op_deadman_get, op_deadman_set),
    ROUTER_W(SYS_SYSTEM_OP_BYE, op_bye),
    ROUTER_RW(SYS_SYSTEM_OP_TIME, op_time_get, op_time_set),
    ROUTER_R(SYS_SYSTEM_OP_SAFE, op_safe_get),
    ROUTER_W(SYS_SYSTEM_OP_SAFE_LEAVE, op_safe_leave),
};

ROUTER_MODULE(sys_system_router, k_ops)
