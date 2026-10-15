/*
 * Power manager: one board input feeding always-on loads (reserve) and the regulator rails listed in the board
 * description. Keeps the rails' allocation at or under the limit (the source's budget, or budget + margin when soft),
 * splits what's left between rails without a user current limit, estimates battery charge, and answers power
 * events with the response matrix. See SYS_POWER_MANAGER.MD.
 */
#include "sys_power_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "sys_power_monitor.h"
#include "sys_power_usb_pd.h"
#include "sys_power_vreg.h"
#include "utils.h"

ERR_MODULE_DEFINE(ERR_MOD_SYS_POWER_MANAGER, "power_manager", ERR_SYS_POWER_MANAGER_MAP)

#define LIPO_FULL_CELL_MV 4200
#define BOARD_TARGET (-1)  // a response for every rail

/* A rail as set through POWER_VREG (the guard sees every set). */
typedef struct {
  uint32_t set_mV;      // 0 until a voltage is set
  uint32_t user_mA;     // user current limit; 0 = automatic share
  uint32_t applied_mA;  // limit currently programmed
  bool enabled;
} rail_t;

static const sys_power_manager_cfg_t k_cfg_default = {
    .enabled = 1, .strict = 1, .soft_margin_pct = 20, .source_pin = SYS_IO_PIN_NONE, .battery_level = 1,
    .pd_max_auto = 1, .ext_input = SYS_POWER_EXT_BENCH};

static const sys_power_board_t* s_board;
static sys_power_safe_state_f s_safe_state;
static rail_t s_rails[CONFIG_SYS_POWER_MANAGER_MAX_CONSUMERS];
static sys_power_manager_cfg_t s_cfg = k_cfg_default;
static sys_power_psu_t s_psu;
static sys_power_battery_t s_battery;
/* The response matrix: row 0 the board, row 1 + i rail i. Stored as rows 0 .. consumer_count. */
static uint8_t s_responses[1 + CONFIG_SYS_POWER_MANAGER_MAX_CONSUMERS][SYS_POWER_KIND_COUNT];
static sys_power_status_t s_status = {.battery_pct = SYS_POWER_PCT_UNKNOWN};
static uint32_t s_alert_mA;  // input alert as programmed, 0 = none programmed
static bool s_recompute;     // a setting changed: recompute the budget on the next tick
static bool s_pin_ready;
static uint32_t s_pd_req_mV, s_pd_req_mA;  // what the manager last asked the charger for (0 = nothing yet)
static bool s_input_seen;
static bool s_input_error_reported;
static bool s_ovp_latched;
static bool s_low_latched;
static bool s_critical_latched;
static bool s_over_latched;

U_RECURSIVE_MUTEX_DEFINE(sys_pwrm_mutex);
U_TASK_DEFINE(sys_pwrm_task_h, CONFIG_SYS_POWER_MANAGER_TASK_STACK);

#define LOCK() U_RECURSIVE_MUTEX_LOCK(sys_pwrm_mutex, WAIT_FOREVER)
#define UNLOCK() U_RECURSIVE_MUTEX_UNLOCK(sys_pwrm_mutex)

/* ---- Helpers ---- */

static uint32_t root_tag(err_h e) {
  uint32_t left = CONFIG_ERRORS_POOL_BLOCKS + 1;
  while (e != NULL && e->next != NULL && left-- > 0) {
    e = e->next;
  }
  return e != NULL ? e->tag : 0;
}

static int consumer_index(uint16_t id) {
  for (uint8_t i = 0; i < s_board->consumer_count; i++) {
    if (s_board->consumers[i].vreg == id) {
      return i;
    }
  }
  return -1;
}

static bool same_channel(sys_power_channel_t c, uint16_t device, uint8_t channel) {
  return c.device != SYS_DEVICE_ID_NONE && c.device == device && c.channel == channel;
}

// Input power a rail draws at mV / mA output.
static uint32_t rail_input_mW(uint32_t mV, uint32_t mA) {
  return (uint32_t)(((uint64_t)mV * mA / 1000u) * 100u / s_board->vreg_efficiency_pct);
}

static bool is_auto(const rail_t* r) {
  return r->enabled && r->set_mV > 0 && r->user_mA == 0;
}

static bool is_fixed(const rail_t* r) {
  return r->enabled && r->set_mV > 0 && r->user_mA > 0;
}

static uint32_t fixed_mW(void) {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < s_board->consumer_count; i++) {
    if (is_fixed(&s_rails[i])) {
      sum += rail_input_mW(s_rails[i].set_mV, s_rails[i].user_mA);
    }
  }
  return sum;
}

// What the rails are held to: the budget, plus the margin when soft.
static uint32_t limit_of(uint32_t budget_mW) {
  return s_cfg.strict ? budget_mW : (uint32_t)((uint64_t)budget_mW * (100u + s_cfg.soft_margin_pct) / 100u);
}

static err_h vreg_direct(uint16_t id, uint16_t op, uint32_t value) {
  if (op == VREG_OP_SET_ENABLE) {
    const vreg_enable_args_t a = {.enable = value != 0};
    return sys_device_call_direct(id, op, &a, sizeof(a), NULL, 0, NULL);
  }
  return sys_device_call_direct(id, op, &value, sizeof(value), NULL, 0, NULL);  // voltage / current: one u32
}

/* ---- Events and the response matrix ---- */

// The response to `kind` for a rail index or BOARD_TARGET (its own row). Disabled: notify only.
static uint8_t response_of(uint8_t kind, int target) {
  return s_cfg.enabled ? s_responses[target == BOARD_TARGET ? 0 : 1 + target][kind] : SYS_POWER_RESPONSE_NOTIFY;
}

// `target`: a rail index, or BOARD_TARGET for every rail.
static void apply_response(uint8_t kind, int target) {
  const uint8_t response = response_of(kind, target);
  if (response == SYS_POWER_RESPONSE_SAFE_STATE) {
    if (s_safe_state != NULL) {
      s_safe_state();
    } else {
      err_report(sys_device_suspend_all());
    }
    return;
  }
  if (response != SYS_POWER_RESPONSE_DISABLE && response != SYS_POWER_RESPONSE_RESET) {
    return;
  }
  const uint16_t op = response == SYS_POWER_RESPONSE_DISABLE ? SYS_DEVICE_OP_SUSPEND : SYS_DEVICE_OP_RESET;
  for (uint8_t i = 0; i < s_board->consumer_count; i++) {
    if (target != BOARD_TARGET && target != i) {
      continue;
    }
    err_h err = sys_device_call(s_board->consumers[i].vreg, op, NULL, 0, NULL, 0, NULL);
    if (root_tag(err) == ERR_DEV_NOT_FOUND) {
      err_free(err);  // the rail isn't created: nothing to stop
    } else {
      err_report(err);
    }
  }
}

// A board-level event of the manager: the response, then the event for the listeners.
static void emit(uint8_t kind, uint16_t event, uint8_t flags, int32_t value) {
  apply_response(kind, BOARD_TARGET);
  err_report(sys_event_publish(&(sys_event_t){
      .source = SYS_DEVICE_ID_NONE, .event = event, .channel = 0, .flags = flags, .value = value}));
}
#define EMIT(KIND, NAME, value) emit(SYS_POWER_KIND_##KIND, PWRM_EV_##NAME, PWRM_EV_##NAME##_FLAGS, (value))

static uint8_t kind_of(uint16_t event) {
  switch (event) {
    case VREG_EV_OVP:
      return SYS_POWER_KIND_OVP;
    case VREG_EV_OCP:
      return SYS_POWER_KIND_OCP_CRITICAL;
    case VREG_EV_SCP:
      return SYS_POWER_KIND_SPC;
    case PMON_EV_OCP_WARNING:
      return SYS_POWER_KIND_OCP_WARNING;
    case PMON_EV_OCP_CRITICAL:
      return SYS_POWER_KIND_OCP_CRITICAL;
    default:
      return SYS_POWER_KIND_NONE;  // READY and anything new: not a fault
  }
}

/* Queued listener of the VREG and monitor events: the rails, their monitors and the input get the response. A
 * hardware fault is also reported as an error, with the response taken, so it reaches the errors stream. */
static err_h on_event(const sys_event_t* ev, void* ctx) {
  (void)ctx;
  const uint8_t kind = kind_of(ev->event);
  if (kind == SYS_POWER_KIND_NONE) {
    return NULL;
  }
  LOCK();
  int target = -2;  // not ours
  const int rail = consumer_index(ev->source);
  if (rail >= 0) {
    target = rail;
  } else if (same_channel(s_board->input, ev->source, ev->channel)) {
    target = BOARD_TARGET;
  } else {
    for (uint8_t i = 0; i < s_board->consumer_count; i++) {
      if (same_channel(s_board->consumers[i].monitor, ev->source, ev->channel)) {
        target = i;
      }
    }
  }
  if (target != -2) {
    err_report(ERR_NEW(ERR_PWRM_FAULT, .source = ev->source, .channel = ev->channel, .kind = kind,
                       .response = response_of(kind, target), .value = ev->value));
    apply_response(kind, target);
  }
  UNLOCK();
  return NULL;
}

/* ---- Budget ---- */

/* Programs every automatic rail with its share of what the reserve and the fixed rails leave under the limit.
 * Disabled: every automatic rail gets the board's max. Sets *starved when a share rounds down to nothing. */
static err_h rebalance(bool* starved) {
  *starved = false;
  const uint32_t limit = limit_of(s_status.budget_mW);
  const uint32_t used = s_board->reserve_mW + fixed_mW();
  const uint32_t pool = limit > used ? limit - used : 0;
  uint32_t auto_count = 0;
  for (uint8_t i = 0; i < s_board->consumer_count; i++) {
    auto_count += is_auto(&s_rails[i]) ? 1u : 0u;
  }
  err_h err = NULL;
  uint32_t allocated = used;
  for (uint8_t i = 0; i < s_board->consumer_count; i++) {
    rail_t* r = &s_rails[i];
    if (!is_auto(r)) {
      continue;
    }
    const uint32_t share_mW = pool / auto_count;
    uint32_t cap_mA = s_cfg.enabled ? (uint32_t)((uint64_t)share_mW * s_board->vreg_efficiency_pct / 100u * 1000u /
                                                 r->set_mV)
                                    : s_board->max_mA;
    cap_mA = cap_mA > s_board->max_mA ? s_board->max_mA : cap_mA;
    if (cap_mA == 0) {
      *starved = true;
      continue;
    }
    if (cap_mA < s_board->consumers[i].min_mA) {
      // The regulator can't limit this low: the limit doesn't cover the rail at this voltage. The old limit stays.
      SYS_DEVICE_KEEP_FIRST(err, ERR_NEW(ERR_PWRM_BUDGET_EXCEEDED, .id = s_board->consumers[i].vreg,
                                         .requested_mW = rail_input_mW(r->set_mV, s_board->consumers[i].min_mA),
                                         .available_mW = share_mW));
      allocated += rail_input_mW(r->set_mV, r->applied_mA);
      continue;
    }
    if (cap_mA != r->applied_mA) {
      err_h set_err = vreg_direct(s_board->consumers[i].vreg, VREG_OP_SET_CURRENT, cap_mA);
      const uint32_t root = root_tag(set_err);
      if (root == ERR_DEV_SUSPENDED || root == ERR_DEV_NOT_FOUND) {
        err_free(set_err);  // off (a fault) or gone: it gets its share when it is back
      } else if (set_err != NULL) {
        SYS_DEVICE_KEEP_FIRST(err, set_err);  // the old limit stays programmed and counted
      } else {
        r->applied_mA = cap_mA;
      }
    }
    allocated += rail_input_mW(r->set_mV, r->applied_mA);
  }
  s_status.allocated_mW = allocated;
  return err;
}

/* Refuses a change that would put the reserve plus the fixed rails over the limit, or turn on an automatic rail when
 * nothing is left to share. Disabled: everything fits. */
static err_h check_budget(uint16_t id, const rail_t* next, const rail_t* prev) {
  if (!s_cfg.enabled || (!is_fixed(next) && !is_auto(next))) {
    return NULL;
  }
  const uint32_t limit = limit_of(s_status.budget_mW);
  const uint32_t others = fixed_mW() - (is_fixed(prev) ? rail_input_mW(prev->set_mV, prev->user_mA) : 0);
  const uint32_t used = s_board->reserve_mW + others;
  const uint32_t requested = is_fixed(next) ? rail_input_mW(next->set_mV, next->user_mA) : 1u;
  ERR_CHECK(used + requested <= limit, ERR_PWRM_BUDGET_EXCEEDED, .id = id, .requested_mW = requested,
            .available_mW = limit > used ? limit - used : 0);
  return NULL;
}

/* One rail change: the budget check, then the hardware in an order that never exceeds the limit in between (the other
 * automatic rails lowered first when this one grows), then the rebalance. Rolled back on a failure. */
static err_h change_rail(int i, rail_t next, uint16_t op, uint32_t value) {
  const uint16_t id = s_board->consumers[i].vreg;
  const rail_t prev = s_rails[i];
  ERR_TRY(check_budget(id, &next, &prev));
  bool starved = false;
  const bool grows = next.set_mV > prev.set_mV || (next.enabled && !prev.enabled) || next.user_mA > prev.applied_mA;
  s_rails[i] = next;
  err_h err = grows ? rebalance(&starved) : NULL;
  if (err == NULL) {
    err = vreg_direct(id, op, value);
  }
  if (err != NULL) {
    s_rails[i] = prev;
  }
  SYS_DEVICE_KEEP_FIRST(err, rebalance(&starved));
  return err;
}

/* The POWER_VREG guard: every SET_ENABLE / SET_VOLTAGE / SET_CURRENT of a budgeted rail, remote commands included.
 * SET_CURRENT 0 = back to the automatic share. Other devices pass. */
static err_h vreg_guard(uint16_t id, uint16_t op, const uint8_t* args, uint32_t args_len, bool* handled) {
  const int i = consumer_index(id);
  sys_device_error_policy_t exists;  // only to ask the registry whether the device is there, without its lock
  if (i < 0 || (op != VREG_OP_SET_ENABLE && op != VREG_OP_SET_VOLTAGE && op != VREG_OP_SET_CURRENT) ||
      !sys_device_error_policy_get(id, &exists)) {
    return NULL;  // not a budgeted rail, or not created: the registry answers (ERR_DEV_NOT_FOUND)
  }
  *handled = true;
  const uint32_t need = op == VREG_OP_SET_ENABLE ? sizeof(vreg_enable_args_t) : sizeof(uint32_t);
  ERR_CHECK(args_len == need, ERR_CMD_BAD_LEN, .got = (uint16_t)args_len, .need = (uint16_t)need);
  uint32_t value = 0;
  memcpy(&value, args, need);
  err_h err = NULL;
  LOCK();
  rail_t next = s_rails[i];
  if (op == VREG_OP_SET_ENABLE) {
    next.enabled = value != 0;
    err = change_rail(i, next, op, value);
  } else if (op == VREG_OP_SET_VOLTAGE) {
    if (value > s_board->max_mV) {
      err = ERR_NEW(ERR_DEV_RANGE, .id = id, .value = (int32_t)value, .min = 0, .max = (int32_t)s_board->max_mV);
    } else {
      next.set_mV = value;
      err = change_rail(i, next, op, value);
    }
  } else if (value > s_board->max_mA) {
    err = ERR_NEW(ERR_DEV_RANGE, .id = id, .value = (int32_t)value, .min = 0, .max = (int32_t)s_board->max_mA);
  } else if (value == 0) {
    bool starved = false;
    next.user_mA = 0;
    s_rails[i] = next;
    err = rebalance(&starved);
  } else {
    next.user_mA = value;
    err = change_rail(i, next, op, value);
    if (err == NULL) {
      s_rails[i].applied_mA = value;
    }
  }
  UNLOCK();
  return err;
}

/* ---- Source and battery ---- */

static sys_power_source_e detect_source(void) {
  if (sys_io_pin_valid(s_cfg.source_pin)) {
    if (!s_pin_ready) {
      err_h err = sys_io_set_mode(s_cfg.source_pin, SYS_IO_MODE_INPUT);
      const uint32_t root = root_tag(err);
      s_pin_ready = err == NULL || root == ERR_IO_PIN_IN_USE;  // already configured: read it as it is
      err_free(err);  // its device may not exist yet: retried next tick
      if (!s_pin_ready) {
        return SYS_POWER_SOURCE_UNKNOWN;
      }
    }
    bool level = false;
    err_h err = sys_io_get_level(s_cfg.source_pin, &level);
    if (err != NULL) {
      err_free(err);
      return SYS_POWER_SOURCE_UNKNOWN;
    }
    return level == (s_cfg.battery_level != 0) ? SYS_POWER_SOURCE_BATTERY : SYS_POWER_SOURCE_USB;
  }
  // The board's eFuse power-good pins: the external input wins (the board's mux prefers it), then USB-C.
  if (s_board->source_pins != NULL) {
    bool ext = false, usb = false;
    err_h err = sys_io_get_level(s_board->source_pins->ext_ok, &ext);
    err_free(err);  // its device may not exist yet: the other ways below
    err = sys_io_get_level(s_board->source_pins->usb_ok, &usb);
    err_free(err);
    if (ext) {
      // The eFuse cannot tell a battery from a bench supply: the configuration says which is on the external input.
      return s_cfg.ext_input == SYS_POWER_EXT_BATTERY ? SYS_POWER_SOURCE_BATTERY : SYS_POWER_SOURCE_PSU;
    }
    if (usb) {
      return SYS_POWER_SOURCE_USB;
    }
  }
  // No source pin: an entered PSU, then a configured battery.
  if (s_psu.max_mA > 0) {
    return SYS_POWER_SOURCE_PSU;
  }
  return s_battery.chemistry != SYS_POWER_BATTERY_NONE ? SYS_POWER_SOURCE_BATTERY : SYS_POWER_SOURCE_UNKNOWN;
}

/* The charger's highest offer, capped by the board. false: no sink, not created yet, or nothing offered. */
static bool highest_offer(uint32_t* mV, uint32_t* mA) {
  if (s_board->usb_pd == SYS_DEVICE_ID_NONE) {
    return false;
  }
  int32_t offered_mV = 0, offered_mA = 0;
  err_h err = sys_power_usb_pd_get_limits(s_board->usb_pd, &offered_mV, &offered_mA);
  const bool ok = err == NULL && offered_mV > 0 && offered_mA > 0;
  err_free(err);
  if (!ok) {
    return false;
  }
  *mV = (uint32_t)offered_mV > s_board->max_mV ? s_board->max_mV : (uint32_t)offered_mV;
  *mA = (uint32_t)offered_mA > s_board->max_mA ? s_board->max_mA : (uint32_t)offered_mA;
  return true;
}

// The pd_max_auto setting: on a USB source, ask for the highest offer whenever it differs from the last request.
static void auto_negotiate(void) {
  uint32_t mV = 0, mA = 0;
  if (!highest_offer(&mV, &mA)) {
    s_pd_req_mV = s_pd_req_mA = 0;  // a new charger is asked again
    return;
  }
  if (mV == s_pd_req_mV && mA == s_pd_req_mA) {
    return;
  }
  s_pd_req_mV = mV;  // recorded first: a refusal is reported once, not every tick
  s_pd_req_mA = mA;
  err_report(sys_power_usb_pd_set_settings(s_board->usb_pd, mV, mA));
}

// The source's current limit before the board cap.
static uint32_t source_current_mA(sys_power_source_e source) {
  switch (source) {
    case SYS_POWER_SOURCE_PSU:
      return s_psu.max_mA > 0 ? s_psu.max_mA : s_board->max_mA;  // not entered: the board's own limit (its eFuse)
    case SYS_POWER_SOURCE_BATTERY:
      return s_board->max_mA;  // a battery isn't current-limited, the board is
    case SYS_POWER_SOURCE_USB: {
      if (s_board->usb_pd != SYS_DEVICE_ID_NONE) {
        int32_t mV = 0, mA = 0;
        err_h err = sys_power_usb_pd_get_limits(s_board->usb_pd, &mV, &mA);
        const bool offered = err == NULL && mA > 0;
        err_free(err);  // not created yet, or nothing offered: the assumed current
        if (offered) {
          return (uint32_t)mA;
        }
      }
      return s_board->unknown_source_mA;
    }
    default:
      return s_board->unknown_source_mA;
  }
}

// LiPo resting voltage per cell -> charge % (linear interpolation between points, 100% above 4200mV, 0% below 3270mV).
static const struct {
  uint16_t mV;
  uint8_t pct;
} k_lipo_curve[] = {
    {4200, 100}, {4150, 95}, {4110, 90}, {4080, 85}, {4020, 80}, {3980, 75}, {3950, 70},
    {3910, 65},  {3870, 60}, {3850, 55}, {3840, 50}, {3820, 45}, {3800, 40}, {3790, 35},
    {3770, 30},  {3750, 25}, {3730, 20}, {3710, 15}, {3690, 10}, {3610, 5},  {3270, 0},
};

static uint8_t lipo_pct(int32_t cell_mV) {
  const size_t n = ARRAY_LEN(k_lipo_curve);
  if (cell_mV >= k_lipo_curve[0].mV) {
    return 100;
  }
  if (cell_mV <= k_lipo_curve[n - 1].mV) {
    return 0;
  }
  for (size_t i = 1; i < n; i++) {
    if (cell_mV >= k_lipo_curve[i].mV) {
      const int32_t span_mV = k_lipo_curve[i - 1].mV - k_lipo_curve[i].mV;
      const int32_t span_pct = k_lipo_curve[i - 1].pct - k_lipo_curve[i].pct;
      return (uint8_t)(k_lipo_curve[i].pct + (cell_mV - k_lipo_curve[i].mV) * span_pct / span_mV);
    }
  }
  return 0;
}

static void update_battery(bool have_input) {
  if (s_status.source != SYS_POWER_SOURCE_BATTERY || s_battery.chemistry == SYS_POWER_BATTERY_NONE ||
      s_battery.cells == 0 || !have_input) {
    s_status.battery_pct = SYS_POWER_PCT_UNKNOWN;
    return;
  }
  const uint8_t pct = lipo_pct(s_status.input_mV / s_battery.cells);
  s_status.battery_pct = pct;
  // Raised once at or below the threshold, re-armed CONFIG_SYS_POWER_MANAGER_BATTERY_HYSTERESIS_PCT above it.
  if (!s_critical_latched && pct <= s_battery.critical_pct) {
    s_critical_latched = true;
    EMIT(BATTERY_CRITICAL, BATTERY_CRITICAL, pct);
  } else if (s_critical_latched && pct >= s_battery.critical_pct + CONFIG_SYS_POWER_MANAGER_BATTERY_HYSTERESIS_PCT) {
    s_critical_latched = false;
  }
  if (!s_low_latched && pct <= s_battery.low_pct) {
    s_low_latched = true;
    EMIT(BATTERY_LOW, BATTERY_LOW, pct);
  } else if (s_low_latched && pct >= s_battery.low_pct + CONFIG_SYS_POWER_MANAGER_BATTERY_HYSTERESIS_PCT) {
    s_low_latched = false;
  }
}

/* ---- The task ---- */

// A rail whose device is gone (REMOVE, or not created yet) counts as off: a new one starts from scratch.
static void forget_removed_rails(void) {
  for (uint8_t i = 0; i < s_board->consumer_count; i++) {
    sys_device_info_t info;
    err_h err = sys_device_call(s_board->consumers[i].vreg, SYS_DEVICE_OP_INFO, NULL, 0, &info, sizeof(info), NULL);
    if (root_tag(err) == ERR_DEV_NOT_FOUND) {
      s_rails[i] = (rail_t){0};
    }
    err_free(err);
  }
}

// The input's hardware trip: critical at the limit's current, warning below it. Disabled: left as it is.
static void program_input_alerts(uint32_t limit_mA) {
  if (!s_cfg.enabled || s_board->input.device == SYS_DEVICE_ID_NONE || limit_mA == 0 || limit_mA == s_alert_mA) {
    return;
  }
  const uint32_t warn_mA = limit_mA * CONFIG_SYS_POWER_MANAGER_INPUT_WARNING_PCT / 100u;
  err_h err = sys_power_monitor_set_alert(s_board->input.device, s_board->input.channel, SYS_POWER_ALERT_OCP_CRITICAL,
                                          (int32_t)limit_mA);
  SYS_DEVICE_KEEP_FIRST(err, sys_power_monitor_set_alert(s_board->input.device, s_board->input.channel,
                                                         SYS_POWER_ALERT_OCP_WARNING, (int32_t)warn_mA));
  if (err == NULL) {
    s_alert_mA = limit_mA;
  } else if (s_alert_mA == 0 && root_tag(err) == ERR_DEV_NOT_FOUND) {
    err_free(err);  // the monitor isn't created yet
  } else {
    err_report(err);
  }
}

// Input measurement. false: skip this tick (the monitor is suspended, or not created yet and never seen).
static bool read_input(bool* have_input) {
  *have_input = false;
  if (s_board->input.device == SYS_DEVICE_ID_NONE) {
    return true;
  }
  int32_t mV = 0, mA = 0;
  err_h err = sys_power_monitor_get_voltage(s_board->input.device, s_board->input.channel, &mV);
  if (err == NULL) {
    err = sys_power_monitor_get_current(s_board->input.device, s_board->input.channel, &mA);
  }
  const uint32_t root = root_tag(err);
  if (root == ERR_DEV_SUSPENDED || (root == ERR_DEV_NOT_FOUND && !s_input_seen)) {
    err_free(err);  // can't be read now: keep the last status, recompute nothing
    return false;
  }
  if (err == NULL) {
    s_input_seen = true;
    s_input_error_reported = false;
  } else if (!s_input_error_reported) {
    s_input_error_reported = true;  // reported once, until it works again
    err_report(err);
  } else {
    err_free(err);
  }
  *have_input = err == NULL && mV > 0;
  s_status.input_mV = *have_input ? mV : 0;
  s_status.input_mA = *have_input ? mA : 0;
  return true;
}

// Over-voltage, budget, rebalance and the over-budget check, from a fresh input reading.
static void update_budget(sys_power_source_e source, bool have_input) {
  // Over-voltage on the input, latched until it drops back under the board's max.
  if (have_input && (uint32_t)s_status.input_mV > s_board->max_mV) {
    if (!s_ovp_latched) {
      s_ovp_latched = true;
      EMIT(OVP, INPUT_OVP, s_status.input_mV);
    }
  } else {
    s_ovp_latched = false;
  }

  // Budget = measured input voltage x the source's current, both capped by the board.
  uint32_t mV = have_input ? (uint32_t)s_status.input_mV : 0;
  mV = mV > s_board->max_mV ? s_board->max_mV : mV;
  uint32_t mA = source_current_mA(source);
  mA = mA > s_board->max_mA ? s_board->max_mA : mA;
  const uint32_t budget = (uint32_t)((uint64_t)mV * mA / 1000u);
  s_status.source_mV = mV;
  s_status.source_mA = mA;
  const uint32_t limit_mA = limit_of(mA) > s_board->max_mA ? s_board->max_mA : limit_of(mA);
  program_input_alerts(limit_mA);

  const uint32_t delta = budget > s_status.budget_mW ? budget - s_status.budget_mW : s_status.budget_mW - budget;
  if (!s_recompute && delta < CONFIG_SYS_POWER_MANAGER_BUDGET_STEP_MW && (budget == 0) == (s_status.budget_mW == 0)) {
    return;
  }
  s_recompute = false;
  s_status.budget_mW = budget;
  s_status.limit_mW = limit_of(budget);
  bool starved = false;
  err_report(rebalance(&starved));
  const uint32_t committed = s_board->reserve_mW + fixed_mW();
  const bool over = s_cfg.enabled && (committed > s_status.limit_mW || starved);
  if (over && !s_over_latched) {
    s_over_latched = true;
    err_report(ERR_NEW(ERR_PWRM_BELOW_ALLOCATION, .allocated_mW = committed, .limit_mW = s_status.limit_mW));
    EMIT(BUDGET_EXCEEDED, BUDGET_EXCEEDED, (int32_t)s_status.limit_mW);
  } else if (!over) {
    s_over_latched = false;
  }
}

static void tick(void) {
  LOCK();
  forget_removed_rails();
  bool have_input;
  const bool readable = read_input(&have_input);  // false: the budget and the battery estimate wait for the input
  const sys_power_source_e source = detect_source();
  if (source != (sys_power_source_e)s_status.source) {
    s_status.source = (uint8_t)source;
    EMIT(SOURCE_CHANGED, SOURCE_CHANGED, source);
  }
  if (source == SYS_POWER_SOURCE_USB && s_cfg.pd_max_auto) {
    auto_negotiate();
  } else {
    s_pd_req_mV = s_pd_req_mA = 0;
  }
  if (readable) {
    update_budget(source, have_input);
    update_battery(have_input);
  }
  s_status.flags = (uint8_t)((s_cfg.enabled ? SYS_POWER_FLAG_ENABLED : 0) | (s_cfg.strict ? SYS_POWER_FLAG_STRICT : 0) |
                             (s_over_latched ? SYS_POWER_FLAG_OVER_BUDGET : 0) |
                             (s_input_seen ? SYS_POWER_FLAG_INPUT_SEEN : 0));
  UNLOCK();
}

static void power_task(void* arg) {
  (void)arg;
  while (1) {
    tick();
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(CONFIG_SYS_POWER_MANAGER_PERIOD_MS));
  }
}

static void wake(void) {
  s_recompute = true;
  if (sys_pwrm_task_h != NULL) {
    xTaskNotifyGive(sys_pwrm_task_h);
  }
}

/* ---- Settings ---- */

static err_h check_range(int32_t value, int32_t min, int32_t max) {
  ERR_CHECK(value >= min && value <= max, ERR_PWRM_BAD_ARG, .value = value, .min = min, .max = max);
  return NULL;
}

err_h sys_power_manager_set_config(const sys_power_manager_cfg_t* cfg) {
  ERR_CHECK(s_board != NULL, ERR_PWRM_NOT_READY);
  ERR_TRY(check_range(cfg->enabled, 0, 1));
  ERR_TRY(check_range(cfg->strict, 0, 1));
  ERR_TRY(check_range(cfg->soft_margin_pct, 0, 100));
  ERR_TRY(check_range(cfg->battery_level, 0, 1));
  ERR_TRY(check_range(cfg->pd_max_auto, 0, 1));
  ERR_TRY(check_range(cfg->ext_input, SYS_POWER_EXT_BENCH, SYS_POWER_EXT_BATTERY));
  LOCK();
  const bool pin_changed = memcmp(&cfg->source_pin, &s_cfg.source_pin, sizeof(cfg->source_pin)) != 0;
  s_cfg = *cfg;
  if (pin_changed) {
    s_pin_ready = false;
  }
  s_alert_mA = 0;  // reprogram the input alerts for the new limit
  s_pd_req_mV = s_pd_req_mA = 0;  // pd_max_auto: ask again
  UNLOCK();
  wake();
  return NULL;
}

err_h sys_power_manager_negotiate_max(void) {
  ERR_CHECK(s_board != NULL, ERR_PWRM_NOT_READY);
  uint32_t mV = 0, mA = 0;
  LOCK();
  const bool offered = highest_offer(&mV, &mA);
  UNLOCK();
  ERR_CHECK(offered, ERR_PWRM_NO_OFFER);
  ERR_TRY(sys_power_usb_pd_set_settings(s_board->usb_pd, mV, mA));
  LOCK();
  s_pd_req_mV = mV;
  s_pd_req_mA = mA;
  UNLOCK();
  wake();
  return NULL;
}

void sys_power_manager_get_config(sys_power_manager_cfg_t* out) {
  LOCK();
  *out = s_cfg;
  UNLOCK();
}

err_h sys_power_manager_set_psu(const sys_power_psu_t* psu) {
  ERR_CHECK(s_board != NULL, ERR_PWRM_NOT_READY);
  if (psu->max_mV != 0 || psu->max_mA != 0) {  // 0 / 0 removes the supply
    ERR_TRY(check_range((int32_t)psu->max_mV, 1, INT32_MAX));
    ERR_TRY(check_range((int32_t)psu->max_mA, 1, INT32_MAX));
  }
  LOCK();
  s_psu = *psu;
  UNLOCK();
  wake();
  return NULL;
}

err_h sys_power_manager_set_battery(const sys_power_battery_t* battery) {
  ERR_CHECK(s_board != NULL, ERR_PWRM_NOT_READY);
  ERR_TRY(check_range(battery->chemistry, SYS_POWER_BATTERY_NONE, SYS_POWER_BATTERY_LIPO));
  sys_power_battery_t b = {0};
  if (battery->chemistry != SYS_POWER_BATTERY_NONE) {
    ERR_TRY(check_range(battery->cells, 1, (int32_t)(s_board->max_mV / LIPO_FULL_CELL_MV)));
    ERR_TRY(check_range(battery->low_pct, 2, 100));
    ERR_TRY(check_range(battery->critical_pct, 1, battery->low_pct - 1));
    b = *battery;
  }
  LOCK();
  s_battery = b;
  s_low_latched = false;
  s_critical_latched = false;
  UNLOCK();
  wake();
  return NULL;
}

// The matrix row of a target: 0 for the board, 1 + i for rail i.
static err_h row_of(uint16_t target, uint8_t* row) {
  ERR_CHECK(s_board != NULL, ERR_PWRM_NOT_READY);
  if (target == SYS_POWER_TARGET_BOARD) {
    *row = 0;
    return NULL;
  }
  const int rail = consumer_index(target);
  ERR_CHECK(rail >= 0, ERR_PWRM_NOT_RAIL, .id = target);
  *row = (uint8_t)(1 + rail);
  return NULL;
}

static size_t matrix_size(void) {
  return (size_t)(1 + s_board->consumer_count) * SYS_POWER_KIND_COUNT;
}

err_h sys_power_manager_set_response(uint16_t target, uint8_t kind, uint8_t response) {
  uint8_t row;
  ERR_TRY(row_of(target, &row));
  ERR_TRY(check_range(kind, SYS_POWER_KIND_OVP, SYS_POWER_KIND_COUNT - 1));
  ERR_TRY(check_range(response, SYS_POWER_RESPONSE_NOTIFY, SYS_POWER_RESPONSE_SAFE_STATE));
  LOCK();
  s_responses[row][kind] = response;
  UNLOCK();
  return NULL;
}

err_h sys_power_manager_set_responses(const uint8_t* matrix, size_t len) {
  ERR_CHECK(s_board != NULL, ERR_PWRM_NOT_READY);
  ERR_CHECK(len == matrix_size(), ERR_CMD_BAD_LEN, .got = (uint16_t)len, .need = (uint16_t)matrix_size());
  for (size_t i = 0; i < len; i++) {
    ERR_TRY(check_range(matrix[i], SYS_POWER_RESPONSE_NOTIFY, SYS_POWER_RESPONSE_SAFE_STATE));
  }
  LOCK();
  memcpy(s_responses, matrix, len);
  UNLOCK();
  return NULL;
}

err_h sys_power_manager_get_responses(uint16_t target, uint8_t out[SYS_POWER_KIND_COUNT]) {
  uint8_t row;
  ERR_TRY(row_of(target, &row));
  LOCK();
  memcpy(out, s_responses[row], SYS_POWER_KIND_COUNT);
  UNLOCK();
  return NULL;
}

err_h sys_power_manager_get_status(sys_power_status_t* out) {
  ERR_CHECK(s_board != NULL, ERR_PWRM_NOT_READY);
  LOCK();
  *out = s_status;
  UNLOCK();
  return NULL;
}

/* ---- Init ---- */

err_h sys_power_manager_init(const sys_power_board_t* board, sys_power_safe_state_f safe_state) {
  ERR_CHECK(board != NULL && s_board == NULL, ERR_PWRM_BAD_ARG, .value = 0, .min = 0, .max = 0);
  ERR_TRY(check_range(board->consumer_count, 0, CONFIG_SYS_POWER_MANAGER_MAX_CONSUMERS));
  ERR_TRY(check_range(board->vreg_efficiency_pct, 1, 100));
  ERR_CHECK(board->consumer_count == 0 || board->consumers != NULL, ERR_PWRM_BAD_ARG, .value = 0, .min = 1, .max = 1);
  s_safe_state = safe_state;
  s_cfg = k_cfg_default;  // the stored records (SYS_ACTIONS_ID_POWER_*) are run by whoever starts the project
  s_psu = (sys_power_psu_t){0};
  s_battery = (sys_power_battery_t){0};
  s_board = board;

  // Responses: the board-config defaults (a rail without its own takes the board's).
  uint8_t defaults[1 + CONFIG_SYS_POWER_MANAGER_MAX_CONSUMERS][SYS_POWER_KIND_COUNT] = {{0}};
  if (board->responses != NULL) {
    memcpy(defaults[0], board->responses, SYS_POWER_KIND_COUNT);
  }
  for (uint8_t i = 0; i < board->consumer_count; i++) {
    memcpy(defaults[1 + i], board->consumers[i].responses != NULL ? board->consumers[i].responses : defaults[0],
           SYS_POWER_KIND_COUNT);
  }
  memcpy(s_responses, defaults, matrix_size());

  ERR_TRY(sys_device_set_guard(SYS_DEVICE_CONTRACT_POWER_VREG, vreg_guard));
  // Every event of the two contracts, queued (the event task): filtered in on_event.
  uint8_t handle;
  const sys_event_sub_t vreg = {.source = SYS_EVENT_ANY_SOURCE,
                                .event = SYS_DEVICE_OP(SYS_DEVICE_CONTRACT_POWER_VREG, SYS_EVENT_ANY_IN),
                                .channel = SYS_EVENT_ANY_CHANNEL,
                                .device = SYS_DEVICE_ID_NONE,
                                .fn = on_event};
  ERR_TRY(sys_event_subscribe(&vreg, &handle));
  sys_event_sub_t pmon = vreg;
  pmon.event = SYS_DEVICE_OP(SYS_DEVICE_CONTRACT_POWER_MONITOR, SYS_EVENT_ANY_IN);
  ERR_TRY(sys_event_subscribe(&pmon, &handle));

  s_recompute = true;
  U_TASK_START(sys_pwrm_task_h, power_task, NULL, CONFIG_SYS_POWER_MANAGER_TASK_PRIO);
  ERR_CHECK(sys_pwrm_task_h != NULL, ERR_PWRM_NOT_READY);
  return NULL;
}
