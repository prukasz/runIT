#include "sys_system.h"

#include "esp_log.h"
#include "router.h"
#include "sdkconfig.h"

#define TAG __FILE_NAME__

static void deadman_expired(TimerHandle_t t);

U_TIMER_DEFINE(sys_deadman_timer, 1, false, deadman_expired);

static U_SPINLOCK_DEFINE(s_lock);  // dead-man config
static uint32_t s_deadman_ms = CONFIG_SYS_SYSTEM_DEADMAN_DEFAULT_MS;  // dead-man config, under s_lock

static struct {
  bool deadman_link_lost;  // dead-man config, under s_lock
  uint32_t pending;        // bit per sys_safe_reason_e waiting for the urgent task
  sys_safe_state_f safe_state;
  sys_safe_leave_f safe_leave;
  /* The safe state as noted for SAFE_GET: the reason of the last entry (0 = not in it) and its tick. Written by
   * whoever enters it (the router's task, the dead-man, the power manager), read by SAFE_GET: single words, a torn
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
 * Dead-man
 *
 * The timer and a lost link only note the reason and wake the router's urgent
 * task (the handler may block on I/O); the task enters the safe state.
 * ------------------------------------------------------------------------- */

static void trigger(sys_safe_reason_e reason) {
  __atomic_fetch_or(&s_sys.pending, 1u << reason, __ATOMIC_RELAXED);
  router_wake_urgent();
}

static void deadman_expired(TimerHandle_t t) {
  (void)t;
  trigger(SYS_SAFE_DEADMAN);
}

void sys_system_deadman_set(uint32_t timeout_ms, bool on_link_lost) {
  U_CRITICAL_ENTER(s_lock);
  s_deadman_ms = timeout_ms;
  s_sys.deadman_link_lost = on_link_lost;
  U_CRITICAL_EXIT(s_lock);
  if (timeout_ms > 0) {
    xTimerChangePeriod(sys_deadman_timer, MSEC(timeout_ms), 0);  // also (re)starts it
  } else {
    U_TIMER_STOP(sys_deadman_timer);
  }
}

void sys_system_deadman_get(uint32_t* timeout_ms, bool* on_link_lost) {
  U_CRITICAL_ENTER(s_lock);
  *timeout_ms = s_deadman_ms;
  *on_link_lost = s_sys.deadman_link_lost;
  U_CRITICAL_EXIT(s_lock);
}

// Link task: any frame from any link proves the client is alive; re-arms after a trigger.
static void on_frame(void) {
  uint32_t timeout_ms;
  bool on_link_lost;
  sys_system_deadman_get(&timeout_ms, &on_link_lost);
  if (timeout_ms > 0) {
    U_TIMER_RESET(sys_deadman_timer);
  }
}

void sys_system_link_lost(void) {
  uint32_t timeout_ms;
  bool link_lost;
  sys_system_deadman_get(&timeout_ms, &link_lost);
  if (timeout_ms > 0 && link_lost) {
    trigger(SYS_SAFE_LINK_LOST);
  }
}

// Urgent task: the reasons noted since the last wake, in their order.
static void on_urgent(void) {
  const uint32_t pending = __atomic_exchange_n(&s_sys.pending, 0u, __ATOMIC_RELAXED);
  if (pending & (1u << SYS_SAFE_DEADMAN)) {
    sys_system_enter_safe_state(SYS_SAFE_DEADMAN);
  }
  if (pending & (1u << SYS_SAFE_LINK_LOST)) {
    sys_system_enter_safe_state(SYS_SAFE_LINK_LOST);
  }
}

static const router_watch_t k_watch = {.frame = on_frame, .urgent = on_urgent};

err_h sys_system_init(void) {
  router_set_watch(&k_watch);
  uint32_t timeout_ms;
  bool link_lost;
  sys_system_deadman_get(&timeout_ms, &link_lost);
  if (timeout_ms > 0) {
    sys_system_deadman_set(timeout_ms, link_lost);
  }
  return NULL;
}
