/*
 * Power manager: one board input feeding always-on loads (reserve) and the
 * vreg rails listed in the board description. Keeps the rails' allocation at
 * or under the budget of the active source, splits what's left between rails
 * without a user current limit, estimates battery charge, and answers power
 * events with the response matrix. See SYS_POWER.MD "Power manager".
 */
#include <string.h>
#include <sdkconfig.h>
#include "sys_device.h"
#include "sys_power.h"
#include "sys_settings.h"
#include "utils.h"

#define PWR_PCT_UNKNOWN 0xFF
#define PWR_SETTINGS_PSU "pwr_psu"
#define PWR_SETTINGS_BATTERY "pwr_battery"
#define PWR_LIPO_FULL_CELL_MV 4200

typedef struct {
  uint32_t max_mV;
  uint32_t max_mA;
} pwr_psu_settings_t;

typedef struct {
  uint8_t chemistry; /* sys_power_battery_e */
  uint8_t cells;
  uint8_t low_pct;
  uint8_t critical_pct;
} pwr_battery_settings_t;

/* Budget state of one rail, as set through sys_power. */
typedef struct {
  uint32_t set_mV;        /* 0 until a voltage is set through sys_power */
  uint32_t user_mA;       /* user current limit; 0 = automatic share */
  uint32_t applied_mA;    /* limit currently programmed */
  bool enabled;
} pwr_consumer_state_t;

static const sys_power_board_t* s_board;
static pwr_consumer_state_t s_consumers[CONFIG_SYS_POWER_MAX_CONSUMERS];
static pwr_psu_settings_t s_psu;
static pwr_battery_settings_t s_battery;
static uint8_t s_responses[SYS_PWR_EVENT_COUNT];
static sys_power_safe_state_f s_safe_state;
static sys_power_status_t s_status = {.battery_pct = PWR_PCT_UNKNOWN};
static uint32_t s_programmed_input_mA;
static bool s_pins_ready;
static bool s_input_error_reported;
static bool s_ovp_latched;
static bool s_low_latched;
static bool s_critical_latched;
static bool s_over_budget_latched;

R_RECURSIVE_MUTEX_DEFINE(s_power_mutex);
R_TASK_DEFINE(s_power_task, CONFIG_SYS_POWER_TASK_STACK_SIZE);

#define PWR_LOCK() R_RECURSIVE_MUTEX_LOCK(s_power_mutex, portMAX_DELAY)
#define PWR_UNLOCK() R_RECURSIVE_MUTEX_UNLOCK(s_power_mutex)

/* ============================================================================ helpers */

static int consumer_index(uint8_t device_id) {
  for (uint8_t i = 0; i < s_board->consumer_count; i++) {
    if (s_board->consumers[i].vreg_id == device_id) return i;
  }
  return -1;
}

static bool same_channel(sys_power_channel_t a, uint8_t device_id, uint8_t channel) {
  return a.device_id != SYS_POWER_NO_DEVICE && a.device_id == device_id && a.channel == channel;
}

/* Input power a rail draws at mV / mA output. */
static uint32_t rail_input_mW(uint32_t mV, uint32_t mA) {
  return (uint32_t)(((uint64_t)mV * mA / 1000u) * 100u / s_board->vreg_efficiency_pct);
}

static bool is_auto(const pwr_consumer_state_t* c) {
  return c->enabled && c->set_mV > 0 && c->user_mA == 0;
}

static bool is_fixed(const pwr_consumer_state_t* c) {
  return c->enabled && c->set_mV > 0 && c->user_mA > 0;
}

static uint32_t fixed_mW(void) {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < s_board->consumer_count; i++) {
    if (is_fixed(&s_consumers[i])) sum += rail_input_mW(s_consumers[i].set_mV, s_consumers[i].user_mA);
  }
  return sum;
}

/* ============================================================================ raw vreg calls */

#undef OWNER
#define OWNER OWNER_SYS_POWER_VREG_SET_ENABLE
static SE_MUST_USE err_h vreg_hw_set_enable(uint8_t device_id, bool state) {
  SYS_DEV_DISPATCH(device_id, SYS_DEVICE_CONTRACT_POWER_VREG, sys_power_vreg_contract_t, set_enable, state);
}

#undef OWNER
#define OWNER OWNER_SYS_POWER_VREG_SET_VOLTAGE
static SE_MUST_USE err_h vreg_hw_set_voltage(uint8_t device_id, uint32_t voltage_mV) {
  SYS_DEV_DISPATCH(device_id, SYS_DEVICE_CONTRACT_POWER_VREG, sys_power_vreg_contract_t, set_voltage, voltage_mV);
}

#undef OWNER
#define OWNER OWNER_SYS_POWER_VREG_SET_CURRENT
static SE_MUST_USE err_h vreg_hw_set_current(uint8_t device_id, uint32_t current_mA) {
  SYS_DEV_DISPATCH(device_id, SYS_DEVICE_CONTRACT_POWER_VREG, sys_power_vreg_contract_t, set_current, current_mA);
}

/* ============================================================================ events + response matrix */

#undef OWNER
#define OWNER OWNER_SYS_POWER_EVENTS

/* Every rail when target is SYS_POWER_NO_DEVICE (board level), else that rail. */
static void apply_response(sys_power_events_e event, uint8_t target) {
  sys_power_response_e response = (sys_power_response_e)s_responses[event];
  if (response == SYS_POWER_RESPONSE_SAFE_STATE) {
    SE_REPORT(s_safe_state ? s_safe_state() : sys_device_suspend_all());
    return;
  }
  if (response != SYS_POWER_RESPONSE_DISABLE && response != SYS_POWER_RESPONSE_RESET) return;
  for (uint8_t i = 0; i < s_board->consumer_count; i++) {
    uint8_t id = s_board->consumers[i].vreg_id;
    if (target != SYS_POWER_NO_DEVICE && target != id) continue;
    SE_REPORT(response == SYS_POWER_RESPONSE_DISABLE ? sys_device_suspend(id) : sys_device_reset(id));
  }
}

/* Board-level event: response, then publish it for the listeners. */
static void emit_board_event(sys_power_events_e event, int32_t value, uint8_t hops) {
  apply_response(event, SYS_POWER_NO_DEVICE);
  SE_REPORT(sys_power_publish(SYS_POWER_NO_DEVICE, 0, event, value, hops));
}

/* Queued listener of every power event: applies the response matrix to the
   rails, their monitor channels and the input channel. */
static SE_MUST_USE err_h on_power_event(const sys_event_t* ev, void* ctx) {
  (void)ctx;
  // SYS_POWER_NO_DEVICE: the manager's own event, already answered.
  if (ev->device_id == SYS_POWER_NO_DEVICE || ev->event >= SYS_PWR_EVENT_COUNT) return NULL;

  /* Hardware faults (OVP .. OTP) are reported as errors too, so they reach the
     app's error stream with the response taken; events alone go only to
     subscribed listeners. MEDIUM: the response matrix is the reaction. */
  if (ev->event >= SYS_PWR_EVENT_OVP && ev->event <= SYS_PWR_EVENT_OTP) {
    SE_RAISE(ERR_POWER_FAULT, .source_id = ev->device_id, .channel = ev->channel, .event = ev->event,
             .response = s_responses[ev->event], .value = ev->value);
  }

  PWR_LOCK();
  if (consumer_index(ev->device_id) >= 0) {
    apply_response((sys_power_events_e)ev->event, ev->device_id);
  } else if (same_channel(s_board->input, ev->device_id, ev->channel)) {
    emit_board_event((sys_power_events_e)ev->event, ev->value, SYS_EVENT_CAUSED_BY(ev));
  } else {
    for (uint8_t i = 0; i < s_board->consumer_count; i++) {
      if (same_channel(s_board->consumers[i].monitor, ev->device_id, ev->channel)) {
        apply_response((sys_power_events_e)ev->event, s_board->consumers[i].vreg_id);
      }
    }
  }
  PWR_UNLOCK();
  return NULL;
}

/* ============================================================================ budget */

#undef OWNER
#define OWNER OWNER_SYS_POWER_BUDGET

/* Program every automatic rail with its share of what the reserve and the
   fixed rails leave. Sets *starved when a share rounds down to nothing. */
static SE_MUST_USE err_h rebalance(bool* starved) {
  *starved = false;
  uint32_t used = s_board->reserve_mW + fixed_mW();
  uint32_t pool = (s_status.budget_mW > used) ? s_status.budget_mW - used : 0;
  uint8_t auto_count = 0;
  for (uint8_t i = 0; i < s_board->consumer_count; i++) {
    if (is_auto(&s_consumers[i])) auto_count++;
  }

  err_h err = NULL;
  uint32_t allocated = used;
  for (uint8_t i = 0; i < s_board->consumer_count; i++) {
    pwr_consumer_state_t* c = &s_consumers[i];
    if (!is_auto(c)) continue;
    uint32_t share_mW = pool / auto_count;
    uint32_t cap_mA = (uint32_t)((uint64_t)share_mW * s_board->vreg_efficiency_pct / 100u * 1000u / c->set_mV);
    if (cap_mA > s_board->max_mA) cap_mA = s_board->max_mA;
    if (cap_mA == 0) {
      *starved = true;
      continue;
    }
    if (cap_mA != c->applied_mA) {
      err_h set_err = vreg_hw_set_current(s_board->consumers[i].vreg_id, cap_mA);
      err_h root = set_err ? SE_get_error_root(set_err) : NULL;
      if (root && root->tag == ERR_DEV_SUSPENDED) {
        /* A rail suspended by a fault is off: skip it instead of failing the
           change another rail asked for; it gets its share after resume. */
        SE_release(set_err);
      } else if (set_err) {
        SYS_DEV_TEARDOWN_STEP(err, set_err); /* the old limit stays programmed and counted */
      } else {
        c->applied_mA = cap_mA;
      }
    }
    allocated += rail_input_mW(c->set_mV, c->applied_mA);
  }
  s_status.allocated_mW = allocated;
  return err;
}

/* Reject a change that would put the reserve plus the fixed rails over the
   budget, or turn on an automatic rail when nothing is left to share. */
static SE_MUST_USE err_h check_budget(uint8_t device_id, const pwr_consumer_state_t* next, const pwr_consumer_state_t* prev) {
  if (!is_fixed(next) && !is_auto(next)) return NULL;
  uint32_t others = fixed_mW() - (is_fixed(prev) ? rail_input_mW(prev->set_mV, prev->user_mA) : 0);
  uint32_t used = s_board->reserve_mW + others;
  uint32_t requested = is_fixed(next) ? rail_input_mW(next->set_mV, next->user_mA) : 1u;
  if (used + requested > s_status.budget_mW) {
    uint32_t available = (s_status.budget_mW > used) ? s_status.budget_mW - used : 0;
    SE_FAIL(ERR_POWER_BUDGET_EXCEEDED, device_id, requested, available);
  }
  return NULL;
}

/* Apply one rail change: budget check, then hardware in the order that never
   exceeds the budget in between (lower the current before raising the voltage),
   then rebalance the automatic rails. Rolls the state back on failure. */
static SE_MUST_USE err_h change_rail(uint8_t device_id, int index, pwr_consumer_state_t next, err_h (*apply)(uint8_t, uint32_t), uint32_t value) {
  pwr_consumer_state_t prev = s_consumers[index];
  SE_TRY(check_budget(device_id, &next, &prev));

  bool starved = false;
  bool grows = next.set_mV > prev.set_mV || (next.enabled && !prev.enabled) || next.user_mA > prev.applied_mA;
  s_consumers[index] = next;
  err_h err = grows ? rebalance(&starved) : NULL;
  if (SE_IS_OK(err)) err = apply(device_id, value);
  if (SE_IS_ERR(err)) s_consumers[index] = prev;
  SYS_DEV_TEARDOWN_STEP(err, rebalance(&starved));
  return err;
}

static SE_MUST_USE err_h apply_enable(uint8_t device_id, uint32_t state) {
  return vreg_hw_set_enable(device_id, state != 0);
}

/* ============================================================================ vreg API (budgeted) */

#undef OWNER
#define OWNER OWNER_SYS_POWER_VREG_SET_ENABLE
err_h sys_power_vreg_set_enable(uint8_t device_id, bool state) {
  if (s_board == NULL) SE_FAIL(ERR_BASE_INVALID_STATE, 0);
  int index = consumer_index(device_id);
  if (index < 0) return vreg_hw_set_enable(device_id, state);
  PWR_LOCK();
  pwr_consumer_state_t next = s_consumers[index];
  next.enabled = state;
  err_h err = change_rail(device_id, index, next, apply_enable, state);
  PWR_UNLOCK();
  return err;
}

#undef OWNER
#define OWNER OWNER_SYS_POWER_VREG_SET_VOLTAGE
err_h sys_power_vreg_set_voltage(uint8_t device_id, uint32_t voltage_mV) {
  if (s_board == NULL) SE_FAIL(ERR_BASE_INVALID_STATE, 0);
  SE_CHECK_IN_RANGE(voltage_mV, 0, s_board->max_mV);
  int index = consumer_index(device_id);
  if (index < 0) return vreg_hw_set_voltage(device_id, voltage_mV);
  PWR_LOCK();
  pwr_consumer_state_t next = s_consumers[index];
  next.set_mV = voltage_mV;
  err_h err = change_rail(device_id, index, next, vreg_hw_set_voltage, voltage_mV);
  PWR_UNLOCK();
  return err;
}

#undef OWNER
#define OWNER OWNER_SYS_POWER_VREG_SET_CURRENT
err_h sys_power_vreg_set_current(uint8_t device_id, uint32_t current_mA) {
  if (s_board == NULL) SE_FAIL(ERR_BASE_INVALID_STATE, 0);
  SE_CHECK_IN_RANGE(current_mA, 0, s_board->max_mA);
  int index = consumer_index(device_id);
  if (index < 0) {
    if (current_mA == 0) SE_FAIL(ERR_INVALID_VAL_UI32, .val = 0, .min = 1, .max = s_board->max_mA);
    return vreg_hw_set_current(device_id, current_mA);
  }
  PWR_LOCK();
  pwr_consumer_state_t next = s_consumers[index];
  next.user_mA = current_mA; /* 0 = back to the automatic share */
  err_h err;
  if (current_mA == 0) {
    bool starved = false;
    s_consumers[index] = next;
    err = rebalance(&starved);
  } else {
    err = change_rail(device_id, index, next, vreg_hw_set_current, current_mA);
    if (SE_IS_OK(err)) s_consumers[index].applied_mA = current_mA;
  }
  PWR_UNLOCK();
  return err;
}

/* ============================================================================ source + battery */

#undef OWNER
#define OWNER OWNER_SYS_POWER_SOURCE

static sys_power_source_e detect_source(void) {
  if (s_board->source_pin_count > 0) {
    for (uint8_t i = 0; i < s_board->source_pin_count; i++) {
      const sys_power_source_pin_t* rule = &s_board->source_pins[i];
      bool level = false;
      err_h err = sys_io_get_level(rule->pin, &level);
      if (SE_IS_ERR(err)) {
        SE_release(err);
        continue;
      }
      if (level == rule->active_level) return rule->source;
    }
    return SYS_POWER_SOURCE_UNKNOWN;
  }
  // No indicator pins on this board: USB-PD if it offers power, then an
  // entered PSU, then a configured battery.
  if (s_board->usb_pd_id != SYS_POWER_NO_DEVICE) {
    int32_t mV = 0, mA = 0;
    err_h err = sys_power_usb_pd_get_limits(s_board->usb_pd_id, &mV, &mA);
    bool offered = SE_IS_OK(err) && mA > 0;
    SE_release(err);
    if (offered) return SYS_POWER_SOURCE_USB_PD;
  }
  if (s_psu.max_mA > 0) return SYS_POWER_SOURCE_PSU;
  if (s_battery.chemistry != SYS_POWER_BATTERY_NONE) return SYS_POWER_SOURCE_BATTERY;
  return SYS_POWER_SOURCE_UNKNOWN;
}

/* Source current limit before the hardware cap. */
static uint32_t source_current_mA(sys_power_source_e source) {
  switch (source) {
    case SYS_POWER_SOURCE_USB_PD: {
      int32_t mV = 0, mA = 0;
      err_h err = sys_power_usb_pd_get_limits(s_board->usb_pd_id, &mV, &mA);
      bool ok = SE_IS_OK(err) && mA > 0;
      SE_release(err);
      return ok ? (uint32_t)mA : s_board->unknown_source_mA;
    }
    case SYS_POWER_SOURCE_PSU:
      return (s_psu.max_mA > 0) ? s_psu.max_mA : s_board->unknown_source_mA;
    case SYS_POWER_SOURCE_BATTERY:
      return s_board->max_mA;
    default:
      return s_board->unknown_source_mA;
  }
}

/* LiPo resting voltage per cell -> charge percent. */
static const struct {
  uint16_t mV;
  uint8_t pct;
} k_lipo_curve[] = {
    {4200, 100}, {4150, 95}, {4110, 90}, {4080, 85}, {4020, 80}, {3980, 75}, {3950, 70}, {3910, 65}, {3870, 60}, {3850, 55}, {3840, 50},
    {3820, 45},  {3800, 40}, {3790, 35}, {3770, 30}, {3750, 25}, {3730, 20}, {3710, 15}, {3690, 10}, {3610, 5},  {3270, 0},
};

static uint8_t lipo_pct(int32_t cell_mV) {
  const size_t n = sizeof(k_lipo_curve) / sizeof(k_lipo_curve[0]);
  if (cell_mV >= k_lipo_curve[0].mV) return 100;
  if (cell_mV <= k_lipo_curve[n - 1].mV) return 0;
  for (size_t i = 1; i < n; i++) {
    if (cell_mV >= k_lipo_curve[i].mV) {
      int32_t span_mV = k_lipo_curve[i - 1].mV - k_lipo_curve[i].mV;
      int32_t span_pct = k_lipo_curve[i - 1].pct - k_lipo_curve[i].pct;
      return (uint8_t)(k_lipo_curve[i].pct + (cell_mV - k_lipo_curve[i].mV) * span_pct / span_mV);
    }
  }
  return 0;
}

/* Threshold event with hysteresis: raised once at or below the threshold,
   re-armed when the charge rises CONFIG_SYS_POWER_BATTERY_HYSTERESIS_PCT above it. */
static void battery_threshold(uint8_t pct, uint8_t threshold, bool* latched, sys_power_events_e event) {
  if (!*latched && pct <= threshold) {
    *latched = true;
    emit_board_event(event, pct, 0);
  } else if (*latched && pct >= threshold + CONFIG_SYS_POWER_BATTERY_HYSTERESIS_PCT) {
    *latched = false;
  }
}

static void update_battery(bool have_input) {
  if (s_status.source != SYS_POWER_SOURCE_BATTERY || s_battery.chemistry == SYS_POWER_BATTERY_NONE || s_battery.cells == 0 || !have_input) {
    s_status.battery_pct = PWR_PCT_UNKNOWN;
    return;
  }
  uint8_t pct = lipo_pct(s_status.input_mV / s_battery.cells);
  s_status.battery_pct = pct;
  battery_threshold(pct, s_battery.critical_pct, &s_critical_latched, SYS_PWR_EVENT_BATTERY_CRITICAL);
  battery_threshold(pct, s_battery.low_pct, &s_low_latched, SYS_PWR_EVENT_BATTERY_LOW);
}

/* ============================================================================ manager task */

#undef OWNER
#define OWNER OWNER_SYS_POWER_MANAGER

static void setup_source_pins(void) {
  if (s_pins_ready) return;
  err_h err = NULL;
  for (uint8_t i = 0; i < s_board->source_pin_count; i++) {
    SYS_DEV_TEARDOWN_STEP(err, sys_io_set_mode(s_board->source_pins[i].pin));
  }
  s_pins_ready = SE_IS_OK(err);
  SE_release(err); /* the pin's device may not be installed yet; retried next tick */
}

/* Hardware trip on the input: critical at the source current, warning below it. */
static void program_input_alerts(uint32_t source_mA) {
  if (s_board->input.device_id == SYS_POWER_NO_DEVICE || source_mA == s_programmed_input_mA) return;
  err_h err = NULL;
  uint32_t warn_mA = source_mA * CONFIG_SYS_POWER_INPUT_WARNING_PCT / 100u;
  SYS_DEV_TEARDOWN_STEP(err, sys_power_monitor_set_alert(s_board->input.device_id, s_board->input.channel, SYS_PWR_EVENT_OCP_CRITICAL, (int32_t)source_mA));
  SYS_DEV_TEARDOWN_STEP(err, sys_power_monitor_set_alert(s_board->input.device_id, s_board->input.channel, SYS_PWR_EVENT_OCP_WARNING, (int32_t)warn_mA));
  if (SE_IS_OK(err)) {
    s_programmed_input_mA = source_mA;
  } else if (s_programmed_input_mA == 0) {
    SE_release(err); /* monitor not installed yet */
  } else {
    SE_REPORT(err);
  }
}

static void manager_tick(void) {
  PWR_LOCK();
  setup_source_pins();

  // Input measurement; a failure is reported once, until it works again.
  bool have_input = false;
  if (s_board->input.device_id != SYS_POWER_NO_DEVICE) {
    int32_t mV = 0, mA = 0;
    err_h err = sys_power_monitor_get_voltage(s_board->input.device_id, s_board->input.channel, &mV);
    if (SE_IS_OK(err)) err = sys_power_monitor_get_current(s_board->input.device_id, s_board->input.channel, &mA);
    have_input = SE_IS_OK(err) && mV > 0;
    if (SE_IS_ERR(err) && !s_input_error_reported) {
      s_input_error_reported = true;
      SE_REPORT(err);
    } else {
      SE_release(err);
      if (have_input) s_input_error_reported = false;
    }
    s_status.input_mV = have_input ? mV : 0;
    s_status.input_mA = have_input ? mA : 0;
  }

  sys_power_source_e source = detect_source();
  if (source != (sys_power_source_e)s_status.source) {
    s_status.source = source;
    emit_board_event(SYS_PWR_EVENT_SOURCE_CHANGED, source, 0);
  }

  // Over-voltage on the input (latched until it drops back under the cap).
  if (have_input && (uint32_t)s_status.input_mV > s_board->max_mV) {
    if (!s_ovp_latched) {
      s_ovp_latched = true;
      emit_board_event(SYS_PWR_EVENT_OVP, s_status.input_mV, 0);
    }
  } else {
    s_ovp_latched = false;
  }

  // Budget = measured input voltage x source current, both capped by the board.
  uint32_t mV = have_input ? (uint32_t)s_status.input_mV : 0;
  if (mV > s_board->max_mV) mV = s_board->max_mV;
  uint32_t mA = source_current_mA(source);
  if (mA > s_board->max_mA) mA = s_board->max_mA;
  uint32_t budget = (uint32_t)((uint64_t)mV * mA / 1000u);
  s_status.source_mV = mV;
  s_status.source_mA = mA;
  program_input_alerts(mA);

  uint32_t delta = (budget > s_status.budget_mW) ? budget - s_status.budget_mW : s_status.budget_mW - budget;
  if (delta >= CONFIG_SYS_POWER_BUDGET_STEP_MW || (budget == 0) != (s_status.budget_mW == 0)) {
    s_status.budget_mW = budget;
    bool starved = false;
    SE_REPORT(rebalance(&starved));
    uint32_t committed = s_board->reserve_mW + fixed_mW();
    bool over = committed > budget || starved;
    if (over && !s_over_budget_latched) {
      s_over_budget_latched = true;
      SE_REPORT(SE_ERR_NEW(ERR_POWER_SOURCE_BELOW_ALLOCATION, .allocated_mW = committed, .budget_mW = budget));
      emit_board_event(SYS_PWR_EVENT_BUDGET_EXCEEDED, (int32_t)budget, 0);
    } else if (!over) {
      s_over_budget_latched = false;
    }
  }

  update_battery(have_input);
  PWR_UNLOCK();
}

static void power_task(void* arg) {
  (void)arg;
  while (1) {
    manager_tick();
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(CONFIG_SYS_POWER_TASK_PERIOD_MS));
  }
}

static void wake_manager(void) {
  if (s_power_task != NULL) xTaskNotifyGive(s_power_task);
}

/* ============================================================================ configuration API */

#undef OWNER
#define OWNER OWNER_SYS_POWER_SETTINGS

static void load_settings(void) {
  bool found = false;
  SE_REPORT(sys_settings_load(PWR_SETTINGS_PSU, &s_psu, sizeof(s_psu), &found));
  if (!found) memset(&s_psu, 0, sizeof(s_psu));
  SE_REPORT(sys_settings_load(PWR_SETTINGS_BATTERY, &s_battery, sizeof(s_battery), &found));
  if (!found) memset(&s_battery, 0, sizeof(s_battery));
}

err_h sys_power_set_psu(uint32_t max_mV, uint32_t max_mA) {
  if (s_board == NULL) SE_FAIL(ERR_BASE_INVALID_STATE, 0);
  pwr_psu_settings_t psu = {.max_mV = max_mV, .max_mA = max_mA};
  if (max_mV == 0 && max_mA == 0) {
    SE_TRY(sys_settings_erase(PWR_SETTINGS_PSU));
  } else {
    SE_CHECK_IN_RANGE(max_mV, 1, UINT32_MAX);
    SE_CHECK_IN_RANGE(max_mA, 1, UINT32_MAX);
    SE_TRY(sys_settings_store(PWR_SETTINGS_PSU, &psu, sizeof(psu)));
  }
  PWR_LOCK();
  s_psu = psu;
  PWR_UNLOCK();
  wake_manager();
  return NULL;
}

err_h sys_power_set_battery(sys_power_battery_e chemistry, uint8_t cells, uint8_t low_pct, uint8_t critical_pct) {
  if (s_board == NULL) SE_FAIL(ERR_BASE_INVALID_STATE, 0);
  SE_CHECK_IN_RANGE(chemistry, SYS_POWER_BATTERY_NONE, SYS_POWER_BATTERY_LIPO);
  pwr_battery_settings_t battery = {0};
  if (chemistry == SYS_POWER_BATTERY_NONE) {
    SE_TRY(sys_settings_erase(PWR_SETTINGS_BATTERY));
  } else {
    SE_CHECK_IN_RANGE(cells, 1, s_board->max_mV / PWR_LIPO_FULL_CELL_MV);
    SE_CHECK_IN_RANGE(low_pct, 2, 100);
    SE_CHECK_IN_RANGE(critical_pct, 1, low_pct - 1);
    battery = (pwr_battery_settings_t){.chemistry = chemistry, .cells = cells, .low_pct = low_pct, .critical_pct = critical_pct};
    SE_TRY(sys_settings_store(PWR_SETTINGS_BATTERY, &battery, sizeof(battery)));
  }
  PWR_LOCK();
  s_battery = battery;
  s_low_latched = false;
  s_critical_latched = false;
  PWR_UNLOCK();
  wake_manager();
  return NULL;
}

err_h sys_power_set_response(sys_power_events_e event, sys_power_response_e response) {
  SE_CHECK_IN_RANGE(event, SYS_PWR_EVENT_OVP, SYS_PWR_EVENT_COUNT - 1);
  SE_CHECK_IN_RANGE(response, SYS_POWER_RESPONSE_NOTIFY, SYS_POWER_RESPONSE_SAFE_STATE);
  PWR_LOCK();
  s_responses[event] = (uint8_t)response;
  PWR_UNLOCK();
  return NULL;
}

err_h sys_power_get_status(sys_power_status_t* out) {
  SE_CHECK_NOT_NULL(out);
  PWR_LOCK();
  *out = s_status;
  PWR_UNLOCK();
  return NULL;
}

uint32_t sys_power_get_max_mV(void) {
  return s_board ? s_board->max_mV : 0;
}

uint32_t sys_power_get_max_mA(void) {
  return s_board ? s_board->max_mA : 0;
}

void sys_power_register_safe_state(sys_power_safe_state_f fn) {
  s_safe_state = fn;
}

#undef OWNER
#define OWNER OWNER_SYS_POWER_MANAGER
err_h sys_power_init(const sys_power_board_t* board) {
  SE_CHECK_NOT_NULL(board);
  if (s_board != NULL) SE_FAIL(ERR_BASE_INVALID_STATE, 0);
  SE_CHECK_IN_RANGE(board->consumer_count, 0, CONFIG_SYS_POWER_MAX_CONSUMERS);
  SE_CHECK_IN_RANGE(board->vreg_efficiency_pct, 1, 100);
  if (board->consumer_count > 0) SE_CHECK_NOT_NULL(board->consumers);
  if (board->source_pin_count > 0) SE_CHECK_NOT_NULL(board->source_pins);

  if (board->responses) memcpy(s_responses, board->responses, sizeof(s_responses));
  s_board = board;
  load_settings();

  // Listens to every power event (rails, monitors, input); filtered in on_power_event.
  uint8_t sub_id;
  sys_event_subscription_t sub = {
      .domain = SYS_EVENT_DOMAIN_POWER,
      .device_id = SYS_EVENT_ANY,
      .channel = SYS_EVENT_ANY,
      .event = SYS_EVENT_ANY,
      .handler = on_power_event,
  };
  SE_TRY(sys_event_subscribe(&sub, &sub_id));

  R_TASK_START(s_power_task, power_task, NULL, CONFIG_SYS_POWER_TASK_PRIO);
  SE_CHECK_IF_ALLOCATED(s_power_task);
  return NULL;
}
