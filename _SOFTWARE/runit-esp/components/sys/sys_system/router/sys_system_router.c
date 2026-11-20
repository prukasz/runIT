#include "sys_system_router.h"

#include <string.h>

#include "board_time.h"
#include "esp_log.h"
#include "router_queue.h"
#include "sys_session.h"
#include "sys_system.h"

#define TAG __FILE_NAME__

#define TASKS_MAX 32u  // the tasks a TASKS read takes in (TaskStatus_t on the router's stack); one answer holds fewer

_Static_assert(sizeof(((sys_system_task_t*)0)->name) == CONFIG_FREERTOS_MAX_TASK_NAME_LEN, "a task's name");

ROUTER_READ(SYS, OP_PING) {
  return CMD_REPLY(SYS, OP_PING, (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount()));
}

ROUTER_WRITE(SYS, OP_STOP) {
  sys_system_enter_safe_state(SYS_SAFE_STOP);
  return NULL;
}

ROUTER_WRITE(SYS, OP_RESTART) {
  sys_system_restart_later();
  return NULL;
}

// The dead-man of the session the command came on.
ROUTER_READ(SYS, OP_DEADMAN) {
  const cmd_req_t* origin = router_origin();
  ERR_CHECK(origin != NULL && origin->src != CMD_SRC_NONE && origin->src != CMD_SRC_SELF, ERR_CMD_BAD_ARG);
  return CMD_REPLY(SYS, OP_DEADMAN, .timeout_ms = sys_session_deadman_get(origin->src));
}

// A command with no session (a provider's command binding) has no silence the dead-man could notice: refused.
ROUTER_WRITE(SYS, OP_DEADMAN) {
  const cmd_req_t* origin = router_origin();
  ERR_CHECK(origin != NULL && origin->src != CMD_SRC_NONE && origin->src != CMD_SRC_SELF, ERR_CMD_BAD_ARG);
  return sys_session_deadman_set(origin->src, args->timeout_ms, true);
}

// The client is leaving on purpose (its Disconnect, its window closing): the board goes on with what it runs, and a
// dead-man the client armed must not stop it for the silence that follows. A link that drops without it (power,
// range) ends its session.
ROUTER_WRITE(SYS, OP_BYE) {
  const cmd_req_t* origin = router_origin();
  ERR_CHECK(origin != NULL && origin->src != CMD_SRC_NONE && origin->src != CMD_SRC_SELF, ERR_CMD_BAD_ARG);
  ERR_TRY(sys_session_deadman_set(origin->src, 0, false));
  ESP_LOGI(TAG, "client left: dead-man off");
  return NULL;
}

ROUTER_READ(SYS, OP_TIME) {
  return CMD_REPLY(SYS, OP_TIME, .unix_ms = board_time_unix_ms());
}

ROUTER_WRITE(SYS, OP_TIME) {
  ERR_CHECK(board_time_set(args->unix_ms), ERR_CMD_BAD_ARG);
  sys_system_broadcast_time();  // on every open session
  return NULL;
}

ROUTER_READ(SYS, OP_SAFE) {
  uint8_t reason;
  uint32_t for_ms;
  sys_system_safe_state_get(&reason, &for_ms);
  return CMD_REPLY(SYS, OP_SAFE, .reason = reason, .for_ms = for_ms);
}

ROUTER_WRITE(SYS, OP_SAFE_LEAVE) {
  return sys_system_leave_safe_state();
}

// Every task (as many as one answer holds): its name, the least stack it has had free and its priority, to see the
// margin of the deepest call chains on the board.
ROUTER_READ(SYS, OP_TASKS) {
  TaskStatus_t all[TASKS_MAX];
  const UBaseType_t n = uxTaskGetSystemState(all, TASKS_MAX, NULL);
  ERR_CHECK(n > 0, ERR_CMD_OUT_FULL, .need = (uint16_t)uxTaskGetNumberOfTasks(), .cap = TASKS_MAX);
  const uint32_t fit = (out->cap - 1u) / sizeof(sys_system_task_t);
  const uint8_t count = (uint8_t)(n < fit ? n : fit);
  ERR_TRY(cmd_append(out, &count, sizeof(count)));
  for (uint8_t i = 0; i < count; i++) {
    sys_system_task_t row = {
        .stack_free = (uint16_t)(all[i].usStackHighWaterMark > UINT16_MAX ? UINT16_MAX : all[i].usStackHighWaterMark),
        .priority = (uint8_t)all[i].uxCurrentPriority,
    };
    strncpy(row.name, all[i].pcTaskName, sizeof(row.name));
    ERR_TRY(cmd_append(out, &row, sizeof(row)));
  }
  return NULL;
}

static const router_op_t k_ops[] = {
    ROUTER_ENTRY_R(SYS, OP_PING),       ROUTER_ENTRY_W(SYS, OP_STOP), ROUTER_ENTRY_W(SYS, OP_RESTART),
    ROUTER_ENTRY(SYS, OP_DEADMAN),      ROUTER_ENTRY_W(SYS, OP_BYE),  ROUTER_ENTRY(SYS, OP_TIME),
    ROUTER_ENTRY_R(SYS, OP_SAFE),       ROUTER_ENTRY_W(SYS, OP_SAFE_LEAVE), ROUTER_ENTRY_R(SYS, OP_TASKS),
};

ROUTER_MODULE(sys_system_router, k_ops)

ROUTER_MODULE_REGISTER(MODULE_ID_SYS, sys_system_router)
