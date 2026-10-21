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
#define BOARD_TARGET      (-1)  // a response for every rail
#define NO_TARGET         (-2)  // an event of a device that isn't ours

/* A rail as set through POWER_VREG (the guard sees every set). */
typedef struct {
  uint32_t set_mV;      // 0 until a voltage is set
  uint32_t user_mA;     // user current limit; 0 = automatic share
  uint32_t applied_mA;  // limit currently programmed
  bool enabled;
} rail_t;

#define CFG_DEFAULT {.enabled = 1, .strict = 1, .soft_margin_pct = 20, .source = SYS_POWER_SOURCE_UNKNOWN}
#define PD_DEFAULT  {.mode = SYS_POWER_PD_MAX, .voltage_mV = 0}

/* The settings read before init (GET_CONFIG, GET_PD): they start at the defaults. Under the mutex. */
static struct {
  sys_power_manager_cfg_t cfg;
  sys_power_pd_t pd;
} s_pm_preset = {.cfg = CFG_DEFAULT, .pd = PD_DEFAULT};

/* An offer of the charger: a profile and the voltage / current asked of it. */
typedef struct {
  uint32_t mV, mA;
  uint8_t type;  // sys_power_usb_pd_option_type_e
} offer_t;

/* Everything else, under the mutex. Zero until init. */
static struct {
  const sys_power_board_t* board;  // NULL until init
  sys_power_safe_state_f safe_state;
  rail_t rails[CONFIG_SYS_POWER_MANAGER_MAX_CONSUMERS];
  /* The response matrix: row 0 the board, row 1 + i rail i. Stored as rows 0 .. consumer_count. */
  uint8_t responses[1 + CONFIG_SYS_POWER_MANAGER_MAX_CONSUMERS][SYS_POWER_KIND_COUNT];
  sys_power_psu_t psu;
  sys_power_battery_t battery;
  sys_power_status_t status;
  uint32_t alert_mA;               // input alert as programmed, 0 = none programmed
  bool recompute;                  // a setting changed: recompute the budget on the next tick
  bool starved;                    // the last rebalance left an automatic rail nothing
  bool input_seen;                 // the input monitor answered at least once
  bool input_error_reported;       // a failing input read is reported once, until it works again
  /* What the manager last asked the charger for (0 = nothing yet). */
  struct { uint32_t mV, mA; } pd_req;
  /* Each raised once, re-armed when its condition clears. */
  struct { bool ovp, low, critical, over; } latched;
} s_pm;

U_RECURSIVE_MUTEX_DEFINE(sys_pwrm_mutex);
U_TASK_DEFINE(sys_pwrm_task_h, CONFIG_SYS_POWER_MANAGER_TASK_STACK);

#define LOCK()   U_RECURSIVE_MUTEX_LOCK(sys_pwrm_mutex, WAIT_FOREVER)
#define UNLOCK() U_RECURSIVE_MUTEX_UNLOCK(sys_pwrm_mutex)

/* ---- Helpers ---- */

static int consumer_index(uint16_t id) {
  for (uint8_t i = 0; i < s_pm.board->consumer_count; i++) {
    if (s_pm.board->consumers[i].vreg == id) {
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
  return (uint32_t)(((uint64_t)mV * mA / 1000u) * 100u / s_pm.board->vreg_efficiency_pct);
}

static bool is_auto(const rail_t* r) {
  return r->enabled && r->set_mV > 0 && r->user_mA == 0;
}

static bool is_fixed(const rail_t* r) {
  return r->enabled && r->set_mV > 0 && r->user_mA > 0;
}

static uint32_t fixed_mW(void) {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < s_pm.board->consumer_count; i++) {
    if (is_fixed(&s_pm.rails[i])) {
      sum += rail_input_mW(s_pm.rails[i].set_mV, s_pm.rails[i].user_mA);
    }
  }
  return sum;
}

// What the rails are held to: the budget, plus the margin when soft.
static uint32_t limit_of(uint32_t budget_mW) {
  const sys_power_manager_cfg_t* c = &s_pm_preset.cfg;
  return c->strict ? budget_mW : (uint32_t)((uint64_t)budget_mW * (100u + c->soft_margin_pct) / 100u);
}

static err_h vreg_direct(uint16_t id, uint16_t op, uint32_t value) {
  if (op == VREG_OP_ENABLE) {
    const vreg_enable_args_t a = {.enable = value != 0};
    return sys_device_call_direct(id, CMD_DIR_WRITE, op, &a, sizeof(a), NULL, 0, NULL);
  }
  return sys_device_call_direct(id, CMD_DIR_WRITE, op, &value, sizeof(value), NULL, 0, NULL);  // voltage / current: one u32
}

/* ---- Events and the response matrix ---- */

// The response to `kind` for a rail index or BOARD_TARGET (its own row). Disabled: notify only.
static uint8_t response_of(uint8_t kind, int target) {
  return s_pm_preset.cfg.enabled ? s_pm.responses[target == BOARD_TARGET ? 0 : 1 + target][kind] : SYS_POWER_RESPONSE_NOTIFY;
}

// `target`: a rail index, or BOARD_TARGET for every rail.
static void apply_response(uint8_t kind, int target) {
  const uint8_t response = response_of(kind, target);
  if (response == SYS_POWER_RESPONSE_SAFE_STATE) {
    if (s_pm.safe_state != NULL) {
      s_pm.safe_state();
    } else {
      err_report(sys_device_sweep(SYS_DEVICE_OP_SUSPEND));
    }
    return;
  }
  if (response != SYS_POWER_RESPONSE_DISABLE && response != SYS_POWER_RESPONSE_RESET) {
    return;
  }
  const uint16_t op = response == SYS_POWER_RESPONSE_DISABLE ? SYS_DEVICE_OP_SUSPEND : SYS_DEVICE_OP_RESET;
  for (uint8_t i = 0; i < s_pm.board->consumer_count; i++) {
    if (target != BOARD_TARGET && target != i) {
      continue;
    }
    err_h err = sys_device_write(s_pm.board->consumers[i].vreg, op, NULL, 0);
    if (err_root_tag(err) == ERR_DEV_NOT_FOUND) {
      err_free(err);  // the rail isn't created: nothing to stop
    } else {
      err_report(err);
    }
  }
}

// An event of the manager for the listeners: source SYS_DEVICE_ID_NONE, channel 0.
static void publish(uint16_t event, uint8_t flags, int32_t value) {
  err_report(sys_event_publish(&(sys_event_t){
      .source = SYS_DEVICE_ID_NONE, .event = event, .channel = 0, .flags = flags, .value = value}));
}

// A board-level event of the manager: the response, then the event.
static void emit(uint8_t kind, uint16_t event, uint8_t flags, int32_t value) {
  apply_response(kind, BOARD_TARGET);
  publish(event, flags, value);
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
  int target = NO_TARGET;
  const int rail = consumer_index(ev->source);
  if (rail >= 0) {
    target = rail;
  } else if (same_channel(s_pm.board->input, ev->source, ev->channel)) {
    target = BOARD_TARGET;
  } else {
    for (uint8_t i = 0; i < s_pm.board->consumer_count && target == NO_TARGET; i++) {
      if (same_channel(s_pm.board->consumers[i].monitor, ev->source, ev->channel)) target = i;
    }
  }
  if (target != NO_TARGET) {
    err_report(ERR_NEW(ERR_PWRM_FAULT, .source = ev->source, .channel = ev->channel, .kind = kind,
                       .response = response_of(kind, target), .value = ev->value));
    apply_response(kind, target);
  }
  UNLOCK();
  return NULL;
}

/* ---- Budget ---- */

/* Programs every automatic rail with its share of what the reserve and the fixed rails leave under the limit.
 * Disabled: every automatic rail gets the board's max. Sets s_pm.starved when a share rounds down to nothing. */
static err_h rebalance(void) {
  s_pm.starved = false;
  const uint32_t limit = limit_of(s_pm.status.budget_mW);
  const uint32_t used = s_pm.board->reserve_mW + fixed_mW();
  const uint32_t pool = limit > used ? limit - used : 0;
  uint32_t auto_count = 0;
  for (uint8_t i = 0; i < s_pm.board->consumer_count; i++) {
    auto_count += is_auto(&s_pm.rails[i]) ? 1u : 0u;
  }
  err_h err = NULL;
  uint32_t allocated = used;
  for (uint8_t i = 0; i < s_pm.board->consumer_count; i++) {
    rail_t* r = &s_pm.rails[i];
    if (!is_auto(r)) {
      continue;
    }
    const uint32_t share_mW = pool / auto_count;
    uint32_t cap_mA = s_pm_preset.cfg.enabled ? (uint32_t)((uint64_t)share_mW * s_pm.board->vreg_efficiency_pct / 100u * 1000u /
                                                 r->set_mV)
                                    : s_pm.board->max_mA;
    cap_mA = cap_mA > s_pm.board->max_mA ? s_pm.board->max_mA : cap_mA;
    if (cap_mA == 0) {
      s_pm.starved = true;
      continue;
    }
    if (cap_mA < s_pm.board->consumers[i].min_mA) {
      // The regulator can't limit this low: the limit doesn't cover the rail at this voltage. The old limit stays.
      ERR_KEEP_FIRST(err, ERR_NEW(ERR_PWRM_BUDGET_EXCEEDED, .id = s_pm.board->consumers[i].vreg,
                                         .requested_mW = rail_input_mW(r->set_mV, s_pm.board->consumers[i].min_mA),
                                         .available_mW = share_mW));
      allocated += rail_input_mW(r->set_mV, r->applied_mA);
      continue;
    }
    if (cap_mA != r->applied_mA) {
      err_h set_err = vreg_direct(s_pm.board->consumers[i].vreg, VREG_OP_CURRENT, cap_mA);
      const uint32_t root = err_root_tag(set_err);
      if (root == ERR_DEV_SUSPENDED || root == ERR_DEV_NOT_FOUND) {
        err_free(set_err);  // off (a fault) or gone: it gets its share when it is back
      } else if (set_err != NULL) {
        ERR_KEEP_FIRST(err, set_err);  // the old limit stays programmed and counted
      } else {
        r->applied_mA = cap_mA;
      }
    }
    allocated += rail_input_mW(r->set_mV, r->applied_mA);
  }
  s_pm.status.allocated_mW = allocated;
  return err;
}

/* Refuses a change that would put the reserve plus the fixed rails over the limit, or turn on an automatic rail when
 * nothing is left to share. Disabled: everything fits. */
static err_h check_budget(uint16_t id, const rail_t* next, const rail_t* prev) {
  if (!s_pm_preset.cfg.enabled || (!is_fixed(next) && !is_auto(next))) {
    return NULL;
  }
  const uint32_t limit = limit_of(s_pm.status.budget_mW);
  const uint32_t others = fixed_mW() - (is_fixed(prev) ? rail_input_mW(prev->set_mV, prev->user_mA) : 0);
  const uint32_t used = s_pm.board->reserve_mW + others;
  const uint32_t requested = is_fixed(next) ? rail_input_mW(next->set_mV, next->user_mA) : 1u;
  ERR_CHECK(used + requested <= limit, ERR_PWRM_BUDGET_EXCEEDED, .id = id, .requested_mW = requested,
            .available_mW = limit > used ? limit - used : 0);
  return NULL;
}

/* One rail change: the budget check, then the hardware in an order that never exceeds the limit in between (the other
 * automatic rails lowered first when this one grows), then the rebalance. Rolled back on a failure. */
static err_h change_rail(int i, rail_t next, uint16_t op, uint32_t value) {
  const uint16_t id = s_pm.board->consumers[i].vreg;
  const rail_t prev = s_pm.rails[i];
  ERR_TRY(check_budget(id, &next, &prev));
  const bool grows = next.set_mV > prev.set_mV || (next.enabled && !prev.enabled) || next.user_mA > prev.applied_mA;
  s_pm.rails[i] = next;
  err_h err = grows ? rebalance() : NULL;
  if (err == NULL) {
    err = vreg_direct(id, op, value);
  }
  if (err != NULL) {
    s_pm.rails[i] = prev;
  }
  ERR_KEEP_FIRST(err, rebalance());
  return err;
}

/* The POWER_VREG guard: every SET_ENABLE / SET_VOLTAGE / SET_CURRENT of a budgeted rail, remote commands included.
 * SET_CURRENT 0 = back to the automatic share. Other devices pass. */
static err_h vreg_guard(uint16_t id, uint8_t dir, uint16_t op, const uint8_t* args, uint32_t args_len, bool* handled) {
  const int i = consumer_index(id);
  sys_device_error_policy_t exists;  // only to ask the registry whether the device is there, without its lock
  if (i < 0 || dir != CMD_DIR_WRITE || (op != VREG_OP_ENABLE && op != VREG_OP_VOLTAGE && op != VREG_OP_CURRENT) ||
      !sys_device_error_policy_get(id, &exists)) {
    return NULL;  // not a budgeted rail, or not created: the registry answers (ERR_DEV_NOT_FOUND)
  }
  *handled = true;
  const uint32_t need = op == VREG_OP_ENABLE ? sizeof(vreg_enable_args_t) : sizeof(uint32_t);
  ERR_CHECK(args_len == need, ERR_CMD_BAD_LEN, .got = (uint16_t)args_len, .need = (uint16_t)need);
  uint32_t value = 0;
  memcpy(&value, args, need);
  err_h err = NULL;
  LOCK();
  rail_t next = s_pm.rails[i];
  if (op == VREG_OP_ENABLE) {
    next.enabled = value != 0;
    err = change_rail(i, next, op, value);
  } else if (op == VREG_OP_VOLTAGE) {
    if (value > s_pm.board->max_mV) {
      err = ERR_NEW(ERR_DEV_RANGE, .id = id, .value = (int32_t)value, .min = 0, .max = (int32_t)s_pm.board->max_mV);
    } else {
      next.set_mV = value;
      err = change_rail(i, next, op, value);
    }
  } else if (value > s_pm.board->max_mA) {
    err = ERR_NEW(ERR_DEV_RANGE, .id = id, .value = (int32_t)value, .min = 0, .max = (int32_t)s_pm.board->max_mA);
  } else if (value == 0) {
    next.user_mA = 0;
    s_pm.rails[i] = next;
    err = rebalance();
  } else {
    next.user_mA = value;
    err = change_rail(i, next, op, value);
    if (err == NULL) {
      s_pm.rails[i].applied_mA = value;
    }
  }
  UNLOCK();
  return err;
}

/* ---- Source and battery ---- */

/* The board's eFuse power-good pins say which input is live; the external one wins (the board's mux prefers it). The
 * eFuse can't tell a battery from a bench supply: a configured battery makes it BATTERY, else PSU. Without the pins
 * the source is what SET_CONFIG says. */
static sys_power_source_e detect_source(void) {
  const sys_power_source_pins_t* pins = s_pm.board->source_pins;
  if (pins == NULL) return (sys_power_source_e)s_pm_preset.cfg.source;
  bool ext = false, usb = false;
  err_free(sys_io_get_level(pins->ext_ok, &ext));  // its device may not exist yet: UNKNOWN until it does
  err_free(sys_io_get_level(pins->usb_ok, &usb));
  if (ext) return s_pm.battery.chemistry != SYS_POWER_BATTERY_NONE ? SYS_POWER_SOURCE_BATTERY : SYS_POWER_SOURCE_PSU;
  return usb ? SYS_POWER_SOURCE_USB : SYS_POWER_SOURCE_UNKNOWN;
}

/* ---- USB-PD ---- */

/* The offer for `want_mV` (0 = the highest offered), capped by the board: a range profile (PPS, then AVS) that holds
 * the voltage gives it, else the highest fixed profile at or below it; the current is the most that profile offers.
 * The same choice the sink makes. false: no sink, not created yet, or nothing fits. */
static bool pick_offer(uint32_t want_mV, offer_t* out) {
  if (s_pm.board->usb_pd == SYS_DEVICE_ID_NONE) return false;
  sys_power_usb_pd_option_t opt[SYS_POWER_USB_PD_OPTIONS_MAX];
  uint8_t n = 0;
  err_h err = sys_power_usb_pd_list_options(s_pm.board->usb_pd, opt, ARRAY_LEN(opt), &n);
  if (err != NULL) {
    err_free(err);
    return false;
  }
  if (want_mV == 0) {
    for (uint8_t i = 0; i < n; i++) want_mV = opt[i].max_mV > want_mV ? opt[i].max_mV : want_mV;
  }
  want_mV = want_mV > s_pm.board->max_mV ? s_pm.board->max_mV : want_mV;
  const sys_power_usb_pd_option_t* best = NULL;
  uint32_t mV = 0;
  for (uint8_t i = 0; i < n && best == NULL; i++) {
    const bool range = opt[i].type != SYS_POWER_USB_PD_OPTION_FIXED;
    if (range && opt[i].min_mV <= want_mV && want_mV <= opt[i].max_mV) {
      best = &opt[i];
      mV = want_mV;
    }
  }
  for (uint8_t i = 0; i < n && mV == 0; i++) {
    if (opt[i].type == SYS_POWER_USB_PD_OPTION_FIXED && opt[i].max_mV <= want_mV &&
        (best == NULL || opt[i].max_mV > best->max_mV)) {
      best = &opt[i];
    }
  }
  if (best == NULL) return false;
  out->mV = mV != 0 ? mV : best->max_mV;
  out->mA = best->max_mA > s_pm.board->max_mA ? s_pm.board->max_mA : best->max_mA;
  out->type = best->type;
  return true;
}

/* The PD setting on a USB source: the offer goes to the status, and is asked of the charger whenever it differs from
 * the last request (OFF asks nothing: the charger's 5 V). A fixed profile instead of the set voltage: PD_FIXED, once. */
static void update_pd(sys_power_source_e source) {
  const sys_power_pd_t pd = s_pm_preset.pd;
  const uint32_t want_mV = pd.mode == SYS_POWER_PD_VOLTAGE ? pd.voltage_mV : pd.mode == SYS_POWER_PD_MAX ? 0 : 5000;
  offer_t o = {0};
  const bool usb = source == SYS_POWER_SOURCE_USB && pick_offer(want_mV, &o);
  s_pm.status.pd_mV = usb ? o.mV : 0;
  s_pm.status.pd_mA = usb ? o.mA : 0;
  s_pm.status.pd_type = usb ? o.type : SYS_POWER_PD_TYPE_NONE;
  if (!usb || pd.mode == SYS_POWER_PD_OFF) {
    s_pm.pd_req.mV = s_pm.pd_req.mA = 0;  // a new charger is asked again
    return;
  }
  if (o.mV == s_pm.pd_req.mV && o.mA == s_pm.pd_req.mA) return;
  s_pm.pd_req.mV = o.mV;  // recorded first: a refusal is reported once, not every tick
  s_pm.pd_req.mA = o.mA;
  err_report(sys_power_usb_pd_set_settings(s_pm.board->usb_pd, o.mV, o.mA));
  if (pd.mode == SYS_POWER_PD_VOLTAGE && o.mV != pd.voltage_mV) {
    publish(PWRM_EV_PD_FIXED, PWRM_EV_PD_FIXED_FLAGS, (int32_t)o.mV);
  }
}

// The source's current limit before the board cap.
static uint32_t source_current_mA(sys_power_source_e source) {
  switch (source) {
    case SYS_POWER_SOURCE_PSU:
      return s_pm.psu.max_mA > 0 ? s_pm.psu.max_mA : s_pm.board->max_mA;  // not entered: the board's eFuse limit
    case SYS_POWER_SOURCE_BATTERY:
      return s_pm.board->max_mA;  // a battery isn't current-limited, the board is
    case SYS_POWER_SOURCE_USB:  // the offer in use (update_pd); none: the assumed current
      return s_pm.status.pd_mA > 0 ? s_pm.status.pd_mA : s_pm.board->unknown_source_mA;
    default:
      return s_pm.board->unknown_source_mA;
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

// A battery threshold: true once at or below it (raise the event), re-armed HYSTERESIS_PCT above it.
static bool battery_crossed(bool* latched, uint8_t pct, uint8_t threshold) {
  if (!*latched && pct <= threshold) return *latched = true;
  if (*latched && pct >= threshold + CONFIG_SYS_POWER_MANAGER_BATTERY_HYSTERESIS_PCT) *latched = false;
  return false;
}

static void update_battery(bool have_input) {
  if (s_pm.status.source != SYS_POWER_SOURCE_BATTERY || s_pm.battery.chemistry == SYS_POWER_BATTERY_NONE ||
      s_pm.battery.cells == 0 || !have_input) {
    s_pm.status.battery_pct = SYS_POWER_PCT_UNKNOWN;
    return;
  }
  const uint8_t pct = lipo_pct(s_pm.status.input_mV / s_pm.battery.cells);
  s_pm.status.battery_pct = pct;
  if (battery_crossed(&s_pm.latched.critical, pct, s_pm.battery.critical_pct)) {
    EMIT(BATTERY_CRITICAL, BATTERY_CRITICAL, pct);
  }
  if (battery_crossed(&s_pm.latched.low, pct, s_pm.battery.low_pct)) EMIT(BATTERY_LOW, BATTERY_LOW, pct);
}

/* ---- The task ---- */

// The input's hardware trip: critical at the limit's current, warning below it. Disabled: left as it is.
static void program_input_alerts(uint32_t limit_mA) {
  const sys_power_channel_t in = s_pm.board->input;
  if (!s_pm_preset.cfg.enabled || in.device == SYS_DEVICE_ID_NONE || limit_mA == 0 || limit_mA == s_pm.alert_mA) return;
  const uint32_t warn_mA = limit_mA * CONFIG_SYS_POWER_MANAGER_INPUT_WARNING_PCT / 100u;
  err_h err = sys_power_monitor_set_alert(in.device, in.channel, SYS_POWER_ALERT_OCP_CRITICAL, (int32_t)limit_mA);
  ERR_KEEP_FIRST(err, sys_power_monitor_set_alert(in.device, in.channel, SYS_POWER_ALERT_OCP_WARNING, (int32_t)warn_mA));
  if (err == NULL) {
    s_pm.alert_mA = limit_mA;
  } else if (s_pm.alert_mA == 0 && err_root_tag(err) == ERR_DEV_NOT_FOUND) {
    err_free(err);  // the monitor isn't created yet
  } else {
    err_report(err);
  }
}

// Input measurement. false: skip this tick (the monitor is suspended, or not created yet and never seen).
static bool read_input(bool* have_input) {
  *have_input = false;
  if (s_pm.board->input.device == SYS_DEVICE_ID_NONE) {
    return true;
  }
  int32_t mV = 0, mA = 0;
  err_h err = sys_power_monitor_get_voltage(s_pm.board->input.device, s_pm.board->input.channel, &mV);
  if (err == NULL) {
    err = sys_power_monitor_get_current(s_pm.board->input.device, s_pm.board->input.channel, &mA);
  }
  const uint32_t root = err_root_tag(err);
  if (root == ERR_DEV_SUSPENDED || (root == ERR_DEV_NOT_FOUND && !s_pm.input_seen)) {
    err_free(err);  // can't be read now: keep the last status, recompute nothing
    return false;
  }
  if (err == NULL) {
    s_pm.input_seen = true;
    s_pm.input_error_reported = false;
  } else if (!s_pm.input_error_reported) {
    s_pm.input_error_reported = true;  // reported once, until it works again
    err_report(err);
  } else {
    err_free(err);
  }
  *have_input = err == NULL && mV > 0;
  s_pm.status.input_mV = *have_input ? mV : 0;
  s_pm.status.input_mA = *have_input ? mA : 0;
  return true;
}

// Over-voltage, budget, rebalance and the over-budget check, from a fresh input reading.
static void update_budget(sys_power_source_e source, bool have_input) {
  // Over-voltage on the input, latched until it drops back under the board's max.
  if (have_input && (uint32_t)s_pm.status.input_mV > s_pm.board->max_mV) {
    if (!s_pm.latched.ovp) {
      s_pm.latched.ovp = true;
      EMIT(OVP, INPUT_OVP, s_pm.status.input_mV);
    }
  } else {
    s_pm.latched.ovp = false;
  }

  // Budget = measured input voltage x the source's current, both capped by the board.
  uint32_t mV = have_input ? (uint32_t)s_pm.status.input_mV : 0;
  mV = mV > s_pm.board->max_mV ? s_pm.board->max_mV : mV;
  uint32_t mA = source_current_mA(source);
  mA = mA > s_pm.board->max_mA ? s_pm.board->max_mA : mA;
  const uint32_t budget = (uint32_t)((uint64_t)mV * mA / 1000u);
  s_pm.status.source_mV = mV;
  s_pm.status.source_mA = mA;
  const uint32_t limit_mA = limit_of(mA) > s_pm.board->max_mA ? s_pm.board->max_mA : limit_of(mA);
  program_input_alerts(limit_mA);

  const uint32_t old = s_pm.status.budget_mW;
  const uint32_t delta = budget > old ? budget - old : old - budget;
  if (!s_pm.recompute && delta < CONFIG_SYS_POWER_MANAGER_BUDGET_STEP_MW && (budget == 0) == (old == 0)) return;
  s_pm.recompute = false;
  s_pm.status.budget_mW = budget;
  s_pm.status.limit_mW = limit_of(budget);
  err_report(rebalance());
  const uint32_t committed = s_pm.board->reserve_mW + fixed_mW();
  const bool over = s_pm_preset.cfg.enabled && (committed > s_pm.status.limit_mW || s_pm.starved);
  if (over && !s_pm.latched.over) {
    s_pm.latched.over = true;
    err_report(ERR_NEW(ERR_PWRM_BELOW_ALLOCATION, .allocated_mW = committed, .limit_mW = s_pm.status.limit_mW));
    EMIT(BUDGET_EXCEEDED, BUDGET_EXCEEDED, (int32_t)s_pm.status.limit_mW);
  } else if (!over) {
    s_pm.latched.over = false;
  }
}

static void tick(void) {
  LOCK();
  bool have_input;
  const bool readable = read_input(&have_input);  // false: the budget and the battery estimate wait for the input
  const sys_power_source_e source = detect_source();
  if (source != (sys_power_source_e)s_pm.status.source) {
    s_pm.status.source = (uint8_t)source;
    EMIT(SOURCE_CHANGED, SOURCE_CHANGED, source);
  }
  update_pd(source);
  if (readable) {
    update_budget(source, have_input);
    update_battery(have_input);
  }
  s_pm.status.flags = (uint8_t)((s_pm_preset.cfg.enabled ? SYS_POWER_FLAG_ENABLED : 0) |
                                (s_pm_preset.cfg.strict ? SYS_POWER_FLAG_STRICT : 0) |
                                (s_pm.latched.over ? SYS_POWER_FLAG_OVER_BUDGET : 0) |
                                (s_pm.input_seen ? SYS_POWER_FLAG_INPUT_SEEN : 0));
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
  s_pm.recompute = true;
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
  ERR_CHECK(s_pm.board != NULL, ERR_PWRM_NOT_READY);
  ERR_TRY(check_range(cfg->enabled, 0, 1));
  ERR_TRY(check_range(cfg->strict, 0, 1));
  ERR_TRY(check_range(cfg->soft_margin_pct, 0, 100));
  ERR_TRY(check_range(cfg->source, SYS_POWER_SOURCE_UNKNOWN, SYS_POWER_SOURCE_BATTERY));
  LOCK();
  s_pm_preset.cfg = *cfg;
  s_pm.alert_mA = 0;  // reprogram the input alerts for the new limit
  UNLOCK();
  wake();
  return NULL;
}

void sys_power_manager_get_config(sys_power_manager_cfg_t* out) {
  LOCK();
  *out = s_pm_preset.cfg;
  UNLOCK();
}

err_h sys_power_manager_set_pd(const sys_power_pd_t* pd) {
  ERR_CHECK(s_pm.board != NULL, ERR_PWRM_NOT_READY);
  ERR_TRY(check_range(pd->mode, SYS_POWER_PD_OFF, SYS_POWER_PD_VOLTAGE));
  if (pd->mode == SYS_POWER_PD_VOLTAGE) ERR_TRY(check_range((int32_t)pd->voltage_mV, 1, (int32_t)s_pm.board->max_mV));
  LOCK();
  s_pm_preset.pd = *pd;
  if (pd->mode != SYS_POWER_PD_VOLTAGE) s_pm_preset.pd.voltage_mV = 0;
  s_pm.pd_req.mV = s_pm.pd_req.mA = 0;  // asked again on the next tick
  UNLOCK();
  wake();
  return NULL;
}

void sys_power_manager_get_pd(sys_power_pd_t* out) {
  LOCK();
  *out = s_pm_preset.pd;
  UNLOCK();
}

err_h sys_power_manager_set_psu(const sys_power_psu_t* psu) {
  ERR_CHECK(s_pm.board != NULL, ERR_PWRM_NOT_READY);
  if (psu->max_mV != 0 || psu->max_mA != 0) {  // 0 / 0 removes the supply
    ERR_TRY(check_range((int32_t)psu->max_mV, 1, INT32_MAX));
    ERR_TRY(check_range((int32_t)psu->max_mA, 1, INT32_MAX));
  }
  LOCK();
  s_pm.psu = *psu;
  UNLOCK();
  wake();
  return NULL;
}

err_h sys_power_manager_set_battery(const sys_power_battery_t* battery) {
  ERR_CHECK(s_pm.board != NULL, ERR_PWRM_NOT_READY);
  ERR_TRY(check_range(battery->chemistry, SYS_POWER_BATTERY_NONE, SYS_POWER_BATTERY_LIPO));
  sys_power_battery_t b = {0};
  if (battery->chemistry != SYS_POWER_BATTERY_NONE) {
    ERR_TRY(check_range(battery->cells, 1, (int32_t)(s_pm.board->max_mV / LIPO_FULL_CELL_MV)));
    ERR_TRY(check_range(battery->low_pct, 2, 100));
    ERR_TRY(check_range(battery->critical_pct, 1, battery->low_pct - 1));
    b = *battery;
  }
  LOCK();
  s_pm.battery = b;
  s_pm.latched.low = false;
  s_pm.latched.critical = false;
  UNLOCK();
  wake();
  return NULL;
}

void sys_power_manager_get_psu(sys_power_psu_t* out) {
  LOCK();
  *out = s_pm.psu;
  UNLOCK();
}

void sys_power_manager_get_battery(sys_power_battery_t* out) {
  LOCK();
  *out = s_pm.battery;
  UNLOCK();
}

// The matrix row of a target: 0 for the board, 1 + i for rail i.
static err_h row_of(uint16_t target, uint8_t* row) {
  if (target == SYS_POWER_TARGET_BOARD) {
    *row = 0;
    return NULL;
  }
  const int rail = consumer_index(target);
  ERR_CHECK(rail >= 0, ERR_PWRM_NOT_RAIL, .id = target);
  *row = (uint8_t)(1 + rail);
  return NULL;
}

err_h sys_power_manager_set_matrix(const sys_power_response_row_t* rows, uint8_t count) {
  ERR_CHECK(s_pm.board != NULL, ERR_PWRM_NOT_READY);
  uint8_t at[1 + CONFIG_SYS_POWER_MANAGER_MAX_CONSUMERS];
  ERR_CHECK(count <= ARRAY_LEN(at), ERR_CMD_BAD_LEN, .got = count, .need = (uint16_t)ARRAY_LEN(at));
  for (uint8_t i = 0; i < count; i++) {  // all checked before anything changes
    ERR_TRY(row_of(rows[i].target, &at[i]));
    for (uint8_t k = 0; k < SYS_POWER_KIND_COUNT; k++) {
      ERR_TRY(check_range(rows[i].responses[k], SYS_POWER_RESPONSE_NOTIFY, SYS_POWER_RESPONSE_SAFE_STATE));
    }
  }
  LOCK();
  for (uint8_t i = 0; i < count; i++) memcpy(s_pm.responses[at[i]], rows[i].responses, SYS_POWER_KIND_COUNT);
  UNLOCK();
  return NULL;
}

err_h sys_power_manager_get_matrix(sys_power_response_row_t* rows, uint8_t cap, uint8_t* count) {
  ERR_CHECK(s_pm.board != NULL, ERR_PWRM_NOT_READY);
  const uint8_t n = (uint8_t)(1 + s_pm.board->consumer_count);
  ERR_CHECK(cap >= n, ERR_CMD_BAD_LEN, .got = cap, .need = n);
  LOCK();
  for (uint8_t i = 0; i < n; i++) {
    rows[i].target = i == 0 ? SYS_POWER_TARGET_BOARD : s_pm.board->consumers[i - 1].vreg;
    memcpy(rows[i].responses, s_pm.responses[i], SYS_POWER_KIND_COUNT);
  }
  UNLOCK();
  *count = n;
  return NULL;
}

err_h sys_power_manager_get_status(sys_power_status_t* out) {
  ERR_CHECK(s_pm.board != NULL, ERR_PWRM_NOT_READY);
  LOCK();
  *out = s_pm.status;
  UNLOCK();
  return NULL;
}

/* ---- Init ---- */

err_h sys_power_manager_init(const sys_power_board_t* board, sys_power_safe_state_f safe_state) {
  ERR_CHECK(board != NULL && s_pm.board == NULL, ERR_PWRM_BAD_ARG, .value = 0, .min = 0, .max = 0);
  ERR_TRY(check_range(board->consumer_count, 0, CONFIG_SYS_POWER_MANAGER_MAX_CONSUMERS));
  ERR_TRY(check_range(board->vreg_efficiency_pct, 1, 100));
  ERR_CHECK(board->consumer_count == 0 || board->consumers != NULL, ERR_PWRM_BAD_ARG, .value = 0, .min = 1, .max = 1);
  // The defaults: the stored records (SYS_ACTIONS_ID_POWER_*) are run by whoever starts the project.
  s_pm_preset.cfg = (sys_power_manager_cfg_t)CFG_DEFAULT;
  s_pm_preset.pd = (sys_power_pd_t)PD_DEFAULT;
  s_pm.safe_state = safe_state;
  s_pm.status.battery_pct = SYS_POWER_PCT_UNKNOWN;
  s_pm.status.pd_type = SYS_POWER_PD_TYPE_NONE;

  // Responses: the board-config defaults (a rail without its own takes the board's; none = Notify, 0).
  if (board->responses != NULL) memcpy(s_pm.responses[0], board->responses, SYS_POWER_KIND_COUNT);
  for (uint8_t i = 0; i < board->consumer_count; i++) {
    const uint8_t* own = board->consumers[i].responses;
    memcpy(s_pm.responses[1 + i], own != NULL ? own : s_pm.responses[0], SYS_POWER_KIND_COUNT);
  }
  s_pm.board = board;  // last: the API answers ERR_PWRM_NOT_READY until here

  ERR_TRY(sys_device_set_guard(SYS_DEVICE_CONTRACT_POWER_VREG, vreg_guard));
  // Every event of the two contracts, queued (the event task): filtered in on_event.
  uint8_t handle;
  ERR_TRY(sys_event_subscribe_fn(SYS_EVENT_ANY_SOURCE, SYS_EVENT_ID(VREG_CONTRACT, SYS_EVENT_ANY_IN),
                                 SYS_EVENT_ANY_CHANNEL, on_event, NULL, &handle));
  ERR_TRY(sys_event_subscribe_fn(SYS_EVENT_ANY_SOURCE, SYS_EVENT_ID(PMON_CONTRACT, SYS_EVENT_ANY_IN),
                                 SYS_EVENT_ANY_CHANNEL, on_event, NULL, &handle));

  s_pm.recompute = true;
  U_TASK_START(sys_pwrm_task_h, power_task, NULL, CONFIG_SYS_POWER_MANAGER_TASK_PRIO);
  ERR_CHECK(sys_pwrm_task_h != NULL, ERR_PWRM_NOT_READY);
  return NULL;
}
