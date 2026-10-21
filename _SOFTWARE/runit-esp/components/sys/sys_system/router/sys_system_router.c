#include "sys_system_router.h"

#include "esp_log.h"
#include "esp_system.h"
#include "sys_system.h"
#include "board_time.h"

#define TAG __FILE_NAME__

/* Sys router (ROUTER_KEY_SYS): link check, stop, restart, dead-man, bye, time, the safe state.
 *
 *   code command     side   request / answer
 *   0x01 PING        read   -> { u32 uptime_ms }
 *   0x02 STOP        write  -        safe state, reason STOP
 *   0x03 RESTART     write  -        restarts ~100 ms after the answer
 *   0x04 DEADMAN     both   sys_deadman_t
 *   0x06 BYE         write  -        the client leaves on purpose: the dead-man is disarmed
 *   0x07 TIME        both   sys_time_t: the board's wall clock (UTC), set by the client; read 0 = never set
 *   0x09 SAFE        read   -> sys_safe_t   the safe state: why it was entered (0 = not in it) and how long ago
 *   0x0A SAFE_LEAVE  write  -        every device resumed (the application's leave), the note cleared
 *
 * Whether a command runs urgent is the client's choice (ROUTER_FLAG_URGENT). */

#define OP_PING        0x01u
#define OP_STOP        0x02u
#define OP_RESTART     SYS_SYSTEM_OP_RESTART
#define OP_DEADMAN     0x04u
#define OP_BYE         0x06u
#define OP_TIME        0x07u
#define OP_SAFE        0x09u
#define OP_SAFE_LEAVE  0x0Au

#define RESTART_DELAY_MS 100u  // lets the answer leave before the restart

typedef struct __attribute__((packed)) {
  uint32_t timeout_ms;   /**< Time without a command before the safe state in ms (0 = off) */
  uint8_t on_link_lost;  /**< 1 = enter safe state on link lost, 0 = ignore */
} sys_deadman_t;

typedef struct __attribute__((packed)) {
  uint64_t unix_ms;  /**< UTC, milliseconds since 1970-01-01; read: 0 = no client set it since the power came on */
} sys_time_t;

typedef struct __attribute__((packed)) {
  uint8_t reason;   /**< sys_safe_reason_e of the last entry; 0 = not in the safe state */
  uint32_t for_ms;  /**< Time since that entry */
} sys_safe_t;

static void restart_now(TimerHandle_t t) {
  (void)t;
  esp_restart();
}

U_TIMER_DEFINE(sys_restart_timer, MSEC(RESTART_DELAY_MS), false, restart_now);

ROUTER_OP_NOARGS(op_ping) {
  const uint32_t uptime_ms = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());
  return cmd_reply(out, out_cap, out_len, &uptime_ms, sizeof(uptime_ms));
}

ROUTER_OP_NOARGS(op_stop) {
  sys_system_enter_safe_state(SYS_SAFE_STOP);
  return NULL;
}

ROUTER_OP_NOARGS(op_restart) {
  U_TIMER_START(sys_restart_timer);
  return NULL;
}

ROUTER_OP(op_deadman_set, sys_deadman_t) {
  ERR_CHECK(args->on_link_lost <= 1, ERR_CMD_BAD_ARG);
  sys_system_deadman_set(args->timeout_ms, args->on_link_lost != 0);
  return NULL;
}

ROUTER_OP_NOARGS(op_deadman_get) {
  uint32_t timeout_ms;
  bool on_link_lost;
  sys_system_deadman_get(&timeout_ms, &on_link_lost);
  const sys_deadman_t r = {.timeout_ms = timeout_ms, .on_link_lost = on_link_lost};
  return cmd_reply(out, out_cap, out_len, &r, sizeof(r));
}

// The client is leaving on purpose (its Disconnect, its window closing): the board goes on with what it runs, and a dead-man the client
// armed must not stop it for the silence that follows. A link that drops without it (power, range) is still a lost link.
ROUTER_OP_NOARGS(op_bye) {
  sys_system_deadman_set(0, false);
  ESP_LOGI(TAG, "client left: dead-man off");
  return NULL;
}

ROUTER_OP(op_time_set, sys_time_t) {
  ERR_CHECK(board_time_set(args->unix_ms), ERR_CMD_BAD_ARG);
  return NULL;
}

ROUTER_OP_NOARGS(op_time_get) {
  const sys_time_t r = {.unix_ms = board_time_unix_ms()};
  return cmd_reply(out, out_cap, out_len, &r, sizeof(r));
}

ROUTER_OP_NOARGS(op_safe_get) {
  uint8_t reason;
  uint32_t for_ms;
  sys_system_safe_state_get(&reason, &for_ms);
  const sys_safe_t r = {.reason = reason, .for_ms = for_ms};
  return cmd_reply(out, out_cap, out_len, &r, sizeof(r));
}

ROUTER_OP_NOARGS(op_safe_leave) {
  return sys_system_leave_safe_state();
}

static const router_op_t k_ops[] = {
    ROUTER_R(OP_PING, op_ping),
    ROUTER_W(OP_STOP, op_stop),
    ROUTER_W(OP_RESTART, op_restart),
    ROUTER_RW(OP_DEADMAN, op_deadman_get, op_deadman_set),
    ROUTER_W(OP_BYE, op_bye),
    ROUTER_RW(OP_TIME, op_time_get, op_time_set),
    ROUTER_R(OP_SAFE, op_safe_get),
    ROUTER_W(OP_SAFE_LEAVE, op_safe_leave),
};

ROUTER_MODULE(sys_system_router, k_ops)
