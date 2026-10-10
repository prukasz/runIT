#include "esp_system.h"
#include "router_internal.h"

/* sys router (ROUTER_KEY_SYS): link check, stop, restart, dead-man.
 *
 *   key  op              request              response
 *   0x01 PING            -                    { u32 uptime_ms }
 *   0x02 STOP            -                    -        safe state, reason STOP
 *   0x03 RESTART         -                    -        restarts ~100 ms after the answer
 *   0x04 DEADMAN_SET     sys_deadman_t        -
 *   0x05 DEADMAN_GET     -                    sys_deadman_t
 *
 * Whether a command runs urgent is the client's choice (ROUTER_FLAG_URGENT). */

#define OP_PING 0x01u
#define OP_STOP 0x02u
#define OP_RESTART 0x03u
#define OP_DEADMAN_SET 0x04u
#define OP_DEADMAN_GET 0x05u

#define RESTART_DELAY_MS 100u  // lets the answer leave before the restart

typedef struct __attribute__((packed)) {
  uint32_t timeout_ms;   // 0 = off
  uint8_t on_link_lost;  // 1 = also on router_link_lost
} sys_deadman_t;

static void restart_now(TimerHandle_t t) {
  (void)t;
  esp_restart();
}

U_TIMER_DEFINE(router_restart_timer, MSEC(RESTART_DELAY_MS), false, restart_now);

ROUTER_OP_NOARGS(op_ping) {
  const uint32_t uptime_ms = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());
  return cmd_reply(out, out_cap, out_len, &uptime_ms, sizeof(uptime_ms));
}

ROUTER_OP_NOARGS(op_stop) {
  router_enter_safe_state(ROUTER_SAFE_STOP);
  return NULL;
}

ROUTER_OP_NOARGS(op_restart) {
  U_TIMER_START(router_restart_timer);
  return NULL;
}

ROUTER_OP(op_deadman_set, sys_deadman_t) {
  ERR_CHECK(a->on_link_lost <= 1, ERR_CMD_BAD_ARG);
  router_deadman_set(a->timeout_ms, a->on_link_lost != 0);
  return NULL;
}

ROUTER_OP_NOARGS(op_deadman_get) {
  uint32_t timeout_ms;
  bool on_link_lost;
  router_deadman_get(&timeout_ms, &on_link_lost);
  const sys_deadman_t r = {.timeout_ms = timeout_ms, .on_link_lost = on_link_lost};
  return cmd_reply(out, out_cap, out_len, &r, sizeof(r));
}

static const router_entry_t k_ops[] = {
    {OP_PING, op_ping},
    {OP_STOP, op_stop},
    {OP_RESTART, op_restart},
    {OP_DEADMAN_SET, op_deadman_set},
    {OP_DEADMAN_GET, op_deadman_get},
};

static ROUTER_MODULE(sys_router, k_ops)

err_h router_sys_register(void) {
  return router_register(ROUTER_KEY_SYS, sys_router);
}
