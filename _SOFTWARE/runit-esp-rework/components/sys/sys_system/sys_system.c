#include "sys_system.h"

#include <string.h>

#include "board_time.h"
#include "board_timer.h"
#include "esp_log.h"
#include "esp_system.h"
#include "router_queue.h"
#include "sdkconfig.h"
#include "sys_device.h"
#include "sys_session.h"
#include "sys_system_router.h"

#define TAG __FILE_NAME__

#define RESTART_DELAY_MS 100u  // lets the answer leave before the restart

_Static_assert(CONFIG_SYS_SYSTEM_DEADMAN_DEFAULT_MS == 0 ||
                   (CONFIG_SYS_SYSTEM_DEADMAN_DEFAULT_MS >= SYS_SESSION_TIMEOUT_MIN_MS &&
                    CONFIG_SYS_SYSTEM_DEADMAN_DEFAULT_MS <= SYS_SESSION_TIMEOUT_MAX_MS),
               "CONFIG_SYS_SYSTEM_DEADMAN_DEFAULT_MS: 0, or a session's timeout");

static struct {
  volatile uint32_t deadman_ms;  // the dead-man's time, 0 = off: a single word, read by the gone listener
  uint32_t pending;              // bit per sys_safe_reason_e waiting for the urgent task
  board_timer_t time_timer;      // the root's clock to every board
  board_timer_t restart_timer;   // RESTART, once
  /* The safe state as noted for SAFE: the reason of the last entry (0 = not in it) and its tick. Single words, a torn
   * pair only skews the age by one entry. */
  volatile uint8_t safe_reason;
  volatile TickType_t safe_since;
} s_sys;

/* ----------------------------------------------------------------------------
 * Safe state: every device suspended
 * ------------------------------------------------------------------------- */

void sys_system_enter_safe_state(sys_safe_reason_e reason) {
  ESP_LOGW(TAG, "safe state (reason %d)", (int)reason);
  s_sys.safe_since = xTaskGetTickCount();
  s_sys.safe_reason = (uint8_t)reason;
  err_report(sys_device_sweep(CONTRACT_SYS_DEVICE_OP_SUSPEND));
}

err_h sys_system_leave_safe_state(void) {
  ERR_TRY(sys_device_sweep(CONTRACT_SYS_DEVICE_OP_RESUME));
  s_sys.safe_reason = 0;
  return NULL;
}

void sys_system_safe_state_get(uint8_t* reason, uint32_t* for_ms) {
  *reason = s_sys.safe_reason;
  *for_ms = *reason == 0 ? 0u : (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount() - s_sys.safe_since);
}

/* ----------------------------------------------------------------------------
 * Dead-man: the silence of the master's or a client's session (SYS_SESSION.MD "Gone peers"). Its end only notes the
 * reason and runs on_urgent on the router's urgent task (router_run_urgent: the sweep takes every device's lock),
 * which enters the safe state.
 * ------------------------------------------------------------------------- */

static void on_urgent(void);

static void trigger(sys_safe_reason_e reason) {
  __atomic_fetch_or(&s_sys.pending, 1u << reason, __ATOMIC_RELAXED);
  err_report(router_run_urgent(on_urgent));
}

static void on_urgent(void) {
  const uint32_t pending = __atomic_exchange_n(&s_sys.pending, 0u, __ATOMIC_RELAXED);
  if (pending & (1u << SYS_SAFE_DEADMAN)) {
    sys_system_enter_safe_state(SYS_SAFE_DEADMAN);
  }
}

// A command for every board, from the root: never answered.
static void broadcast(const uint8_t* req, uint32_t len, uint8_t flags) {
  err_report(sys_session_call(SYS_SESSION_ID_BROADCAST, (cmd_in_t){req, len}, (uint8_t)(flags | ROUTER_FLAG_NOREPLY), 0,
                              NULL, NULL));
}

void sys_system_broadcast_time(void) {
  const uint64_t unix_ms = board_time_unix_ms();
  if (unix_ms == 0 || !sys_session_is_root()) {
    return;
  }
  uint8_t req[3 + sizeof(unix_ms)] = {MODULE_ID_SYS, CMD_DIR_WRITE, SYS_OP_TIME};
  memcpy(req + 3, &unix_ms, sizeof(unix_ms));
  broadcast(req, sizeof(req), 0);
}

static void on_time_tick(void* arg) {
  (void)arg;
  sys_system_broadcast_time();
}

// A peer is reached: a board that has just come up takes the root's time.
static void on_reached(uint8_t peer) {
  (void)peer;
  sys_system_broadcast_time();
}

// A peer gone: a client's (or the master's) session ended, by silence or its link lost. Armed: the safe state, and the
// root's dead-man stops every board (a slave's own follows its master's session, which stays up when only the app is
// gone).
static void on_gone(uint8_t peer, uint8_t role) {
  (void)peer;
  if (s_sys.deadman_ms > 0 && (role == SYS_SESSION_CLIENT || role == SYS_SESSION_MASTER)) {
    trigger(SYS_SAFE_DEADMAN);
    if (sys_session_is_root()) {
      const uint8_t req[] = {MODULE_ID_SYS, CMD_DIR_WRITE, SYS_OP_STOP};
      broadcast(req, sizeof(req), ROUTER_FLAG_URGENT);
    }
  }
}

err_h sys_system_deadman_set(uint32_t timeout_ms) {
  ERR_TRY(sys_session_set_timeout(timeout_ms > 0 ? timeout_ms : CONFIG_SYS_SESSION_TIMEOUT_MS));
  s_sys.deadman_ms = timeout_ms;
  return NULL;
}

uint32_t sys_system_deadman_get(void) {
  return s_sys.deadman_ms;
}

/* ----------------------------------------------------------------------------
 * Restart, init
 * ------------------------------------------------------------------------- */

static void restart_now(void* arg) {
  (void)arg;
  esp_restart();
}

void sys_system_restart_later(void) {
  board_timer_once(&s_sys.restart_timer, RESTART_DELAY_MS);
}

err_h sys_system_init(void) {
  ERR_TRY(sys_session_on_gone(on_gone));
  ERR_TRY(sys_session_on_reached(on_reached));
  board_timer_init(&s_sys.restart_timer, restart_now, NULL);
  board_timer_init(&s_sys.time_timer, on_time_tick, NULL);
  board_timer_start(&s_sys.time_timer, CONFIG_SYS_SYSTEM_TIME_PERIOD_S * 1000u);
  return sys_system_deadman_set(CONFIG_SYS_SYSTEM_DEADMAN_DEFAULT_MS);
}
