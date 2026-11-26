#include "sys_system.h"

#include <string.h>

#include "board_time.h"
#include "board_timer.h"
#include "esp_log.h"
#include "esp_system.h"
#include "router_queue.h"
#include "sdkconfig.h"
#include "sys_device.h"
#include "sys_event.h"
#include "sys_session.h"
#include "sys_session_events.h"
#include "sys_system_events.h"
#include "sys_system_router.h"

#define TAG __FILE_NAME__

#define RESTART_DELAY_MS 100u  // lets the answer leave before the restart

static struct {
  uint32_t pending;              // bit per sys_safe_reason_e waiting for the urgent task
  board_timer_t time_timer;      // this board's clock on every open session
  board_timer_t restart_timer;   // RESTART, once
  /* The safe state as noted for SAFE: the reason of the last entry (0 = not in it) and its tick. Single words, a torn
   * pair only skews the age by one entry. */
  volatile uint8_t safe_reason;
  volatile TickType_t safe_since;
} s_sys;

/* ----------------------------------------------------------------------------
 * Safe state: SAFE to its listeners (the VM stops), then every device suspended
 * ------------------------------------------------------------------------- */

err_h sys_system_enter_safe_state(sys_safe_reason_e reason, uint8_t device) {
  ESP_LOGW(TAG, "safe state (reason %d, device %u)", (int)reason, (unsigned)device);
  s_sys.safe_since = xTaskGetTickCount();
  s_sys.safe_reason = (uint8_t)reason;
  err_h err = SYS_EVENT_PUBLISH(SYS_EV_SAFE, device, (uint16_t)reason);  // inline: the listeners before the sweep
  ERR_KEEP_FIRST(err, sys_device_sweep(CONTRACT_SYS_DEVICE_OP_SUSPEND));
  return err;
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
 * Dead-man: the end of a session that has it on (SYS_SESSION.MD "The dead-man"). Its end only notes the
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
    err_report(sys_system_enter_safe_state(SYS_SAFE_DEADMAN, SYS_DEVICE_ID_NONE));
  }
}

// A command for every open session: never answered.
static void broadcast(const uint8_t* req, uint32_t len, uint8_t flags) {
  err_report(sys_session_broadcast((cmd_in_t){req, len}, flags));
}

void sys_system_broadcast_time(void) {
  const uint64_t unix_ms = board_time_unix_ms();
  if (unix_ms == 0) {
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

// A peer is reached: a board that has just come up takes this board's time.
static void on_reached(uint8_t peer) {
  (void)peer;
  sys_system_broadcast_time();
}

// A peer gone: a session with the safety flag ended, by silence or its link lost. The safe state, and every other open
// session is told to stop.
static void on_gone(uint8_t peer, uint8_t safety) {
  (void)peer;
  if (safety != 0u) {
    trigger(SYS_SAFE_DEADMAN);
    const uint8_t req[] = {MODULE_ID_SYS, CMD_DIR_WRITE, SYS_OP_STOP};
    broadcast(req, sizeof(req), ROUTER_FLAG_URGENT);
  }
}

// A session opened (SESSION_EV_REACHED): the peer is the event's source.
static err_h on_reached_event(const sys_event_t* event, void* ctx) {
  (void)ctx;
  on_reached((uint8_t)event->key.source);
  return NULL;
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

// Before app_main: the sessions' ends and openings it answers, the restart's timer and the time broadcast's.
__attribute__((constructor)) static void system_start(void) {
  uint16_t handle = 0;
  err_boot(sys_session_on_gone(on_gone));
  err_boot(sys_event_subscribe(SESSION_REACHED_FILTER, on_reached_event, NULL, SYS_EVENT_RUN_INLINE, &handle));
  board_timer_init(&s_sys.restart_timer, restart_now, NULL);
  board_timer_init(&s_sys.time_timer, on_time_tick, NULL);
  board_timer_start(&s_sys.time_timer, CONFIG_SYS_SYSTEM_TIME_PERIOD_S * 1000u);
}
