#include "esp_log.h"
#include <sys/time.h>

#include "esp_system.h"
#include "router_internal.h"

#define TAG __FILE_NAME__

/* sys router (ROUTER_KEY_SYS): link check, stop, restart, dead-man, bye, time.
 *
 *   key  op              request              response
 *   0x01 PING            -                    { u32 uptime_ms }
 *   0x02 STOP            -                    -        safe state, reason STOP
 *   0x03 RESTART         -                    -        restarts ~100 ms after the answer
 *   0x04 DEADMAN_SET     sys_deadman_t        -
 *   0x05 DEADMAN_GET     -                    sys_deadman_t
 *   0x06 BYE             -                    -        the client leaves on purpose: the dead-man is disarmed
 *   0x07 TIME_SET        sys_time_set_t       -        the board's wall clock (UTC), set by the client
 *   0x08 TIME_GET        -                    sys_time_t
 *
 * Whether a command runs urgent is the client's choice (ROUTER_FLAG_URGENT). */

#define OP_PING 0x01u
#define OP_STOP 0x02u
#define OP_RESTART ROUTER_SYS_OP_RESTART
#define OP_DEADMAN_SET 0x04u
#define OP_DEADMAN_GET 0x05u
#define OP_BYE 0x06u
#define OP_TIME_SET 0x07u
#define OP_TIME_GET 0x08u

#define RESTART_DELAY_MS 100u  // lets the answer leave before the restart

typedef struct __attribute__((packed)) {
  uint32_t timeout_ms;   /**< Time without a command before the safe state in ms (0 = off) */
  uint8_t on_link_lost;  /**< 1 = enter safe state on link lost, 0 = ignore */
} sys_deadman_t;

typedef struct __attribute__((packed)) {
  uint64_t unix_ms;  /**< UTC, milliseconds since 1970-01-01 */
} sys_time_set_t;

typedef struct __attribute__((packed)) {
  uint64_t unix_ms;  /**< The board's wall clock: UTC in ms; meaningless while `synced` is 0 (counts from 1970 at the start) */
  uint8_t synced;    /**< 1 = the clock holds a date (a client set it; it survives a software restart, not a loss of power) */
  uint32_t uptime_ms;  /**< The board's time since the start, the base of the log lines (`I (854) tag`) */
} sys_time_t;

// No battery clock: the time is a client's gift, kept over a software restart (the RTC counter runs on) and lost with the power. The
// system clock is kept (settimeofday), so every consumer that reads it (an error record, a block) sees one time. A clock that was never set
// counts from 1970 at the start: anything before 2020 is "not set".
#define CLOCK_SET_AFTER_MS 1577836800000ull  // 2020-01-01 UTC

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
  ERR_CHECK(args->on_link_lost <= 1, ERR_CMD_BAD_ARG);
  router_deadman_set(args->timeout_ms, args->on_link_lost != 0);
  return NULL;
}

ROUTER_OP_NOARGS(op_deadman_get) {
  uint32_t timeout_ms;
  bool on_link_lost;
  router_deadman_get(&timeout_ms, &on_link_lost);
  const sys_deadman_t r = {.timeout_ms = timeout_ms, .on_link_lost = on_link_lost};
  return cmd_reply(out, out_cap, out_len, &r, sizeof(r));
}

// The client is leaving on purpose (its Disconnect, its window closing): the board goes on with what it runs, and a dead-man the client
// armed must not stop it for the silence that follows. A link that drops without it (power, range) is still a lost link.
ROUTER_OP_NOARGS(op_bye) {
  router_deadman_set(0, false);
  ESP_LOGI(TAG, "client left: dead-man off");
  return NULL;
}

ROUTER_OP(op_time_set, sys_time_set_t) {
  const struct timeval tv = {.tv_sec = (time_t)(args->unix_ms / 1000u), .tv_usec = (suseconds_t)((args->unix_ms % 1000u) * 1000u)};
  ERR_CHECK(settimeofday(&tv, NULL) == 0, ERR_CMD_BAD_ARG);
  return NULL;
}

ROUTER_OP_NOARGS(op_time_get) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  const uint64_t now_ms = (uint64_t)tv.tv_sec * 1000u + (uint64_t)tv.tv_usec / 1000u;
  const sys_time_t r = {.unix_ms = now_ms, .synced = now_ms >= CLOCK_SET_AFTER_MS, .uptime_ms = esp_log_timestamp()};
  return cmd_reply(out, out_cap, out_len, &r, sizeof(r));
}

static const router_entry_t k_ops[] = {
    {OP_PING, op_ping},
    {OP_STOP, op_stop},
    {OP_RESTART, op_restart},
    {OP_DEADMAN_SET, op_deadman_set},
    {OP_DEADMAN_GET, op_deadman_get},
    {OP_BYE, op_bye},
    {OP_TIME_SET, op_time_set},
    {OP_TIME_GET, op_time_get},
};

static ROUTER_MODULE(sys_router, k_ops)

err_h router_sys_register(void) {
  return router_register(ROUTER_KEY_SYS, sys_router);
}
