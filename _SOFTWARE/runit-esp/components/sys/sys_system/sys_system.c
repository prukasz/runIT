#include "sys_system.h"

#include "esp_log.h"
#include "router.h"
#include "sdkconfig.h"
#include "sys_session.h"

#define TAG __FILE_NAME__

_Static_assert(CONFIG_SYS_SYSTEM_DEADMAN_DEFAULT_MS == 0 ||
                   (CONFIG_SYS_SYSTEM_DEADMAN_DEFAULT_MS >= SYS_SESSION_TIMEOUT_MIN_MS &&
                    CONFIG_SYS_SYSTEM_DEADMAN_DEFAULT_MS <= SYS_SESSION_TIMEOUT_MAX_MS),
               "CONFIG_SYS_SYSTEM_DEADMAN_DEFAULT_MS: 0, or a session's timeout");

static struct {
  volatile uint32_t deadman_ms;  // the dead-man's time, 0 = off: a single word, read by the gone listener
  uint32_t pending;              // bit per sys_safe_reason_e waiting for the urgent task
  sys_safe_state_f safe_state;
  sys_safe_leave_f safe_leave;
  /* The safe state as noted for SAFE: the reason of the last entry (0 = not in it) and its tick. Written by
   * whoever enters it (the router's task, the dead-man, the power manager), read by SAFE: single words, a torn
   * pair only skews the age by one entry. */
  volatile uint8_t safe_reason;
  volatile TickType_t safe_since;
} s_sys;

/* ----------------------------------------------------------------------------
 * Safe state
 * ------------------------------------------------------------------------- */

void sys_system_set_safe_state(sys_safe_state_f fn) {
  s_sys.safe_state = fn;
}

void sys_system_set_safe_leave(sys_safe_leave_f fn) {
  s_sys.safe_leave = fn;
}

void sys_system_enter_safe_state(sys_safe_reason_e reason) {
  ESP_LOGW(TAG, "safe state (reason %d)", (int)reason);
  s_sys.safe_since = xTaskGetTickCount();
  s_sys.safe_reason = (uint8_t)reason;
  if (s_sys.safe_state != NULL) {
    s_sys.safe_state(reason);
  }
}

err_h sys_system_leave_safe_state(void) {
  if (s_sys.safe_leave != NULL) {
    ERR_TRY(s_sys.safe_leave());
  }
  s_sys.safe_reason = 0;
  return NULL;
}

void sys_system_safe_state_get(uint8_t* reason, uint32_t* for_ms) {
  *reason = s_sys.safe_reason;
  *for_ms = *reason == 0 ? 0u : (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount() - s_sys.safe_since);
}

/* ----------------------------------------------------------------------------
 * Dead-man: the silence of a client's session (SYS_SESSION.MD "Gone peers"). Its end only notes the reason and runs
 * on_urgent on the router's urgent task (router_run_urgent: the handler may block on I/O), which enters the safe
 * state.
 * ------------------------------------------------------------------------- */

static void on_urgent(void);

static void trigger(sys_safe_reason_e reason) {
  __atomic_fetch_or(&s_sys.pending, 1u << reason, __ATOMIC_RELAXED);
  err_report(router_run_urgent(on_urgent));
}

// A peer gone: a client's (or the parent's) session ended, by silence or its link lost. Armed: the safe state.
static void on_gone(uint8_t peer, uint8_t via) {
  (void)peer;
  if (s_sys.deadman_ms > 0 && (via == SYS_SESSION_CLIENT || via == SYS_SESSION_PARENT)) {
    trigger(SYS_SAFE_DEADMAN);
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

// Urgent task: the reasons noted since the last wake.
static void on_urgent(void) {
  const uint32_t pending = __atomic_exchange_n(&s_sys.pending, 0u, __ATOMIC_RELAXED);
  if (pending & (1u << SYS_SAFE_DEADMAN)) {
    sys_system_enter_safe_state(SYS_SAFE_DEADMAN);
  }
}

err_h sys_system_init(void) {
  ERR_TRY(sys_session_on_gone(on_gone));
  return sys_system_deadman_set(CONFIG_SYS_SYSTEM_DEADMAN_DEFAULT_MS);
}
