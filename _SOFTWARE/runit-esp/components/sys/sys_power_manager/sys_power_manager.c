/*
 * Optional runtime power manager: dynamic task auto-started by LAYOUT and freed by STOP, runtime loads (RAIL /
 * RAIL_REMOVE / SYS_DEVICE_EV_REMOVED), automatic source detection + USB-PD + LiPo curve, 3-mode budget with
 * POWER_VREG clamping interceptor (emitting PWRM_EV_LIMIT_REACHED), and fault response matrix using unified
 * sys_power_kind_e event codes. See SYS_POWER_MANAGER.MD.
 */
#include "sys_power_manager.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "sys_device_events.h"
#include "sys_device_hooks.h"
#include "sys_power_monitor_contract.h"
#include "sys_power_usb_pd_contract.h"
#include "sys_power_vreg_contract.h"
#include "utils.h"

ERR_MODULE_DEFINE(MODULE_ID_PWRM, ERR_SYS_POWER_MANAGER_MAP)

#define LIPO_FULL_CELL_MV 4200
#define LIPO_CELLS_MIN    2  // the board does not run from one cell
#define BOARD_TARGET      (-1)
#define NO_TARGET         (-2)

typedef struct {
  uint32_t set_mV;      // 0 until a voltage is set
  uint32_t user_mA;     // user current limit; 0 = automatic share
  uint32_t applied_mA;  // limit currently programmed
  bool enabled;
} rail_state_t;

typedef struct {
  sys_power_rail_t cfg;
  rail_state_t st;
  rail_state_t undo;
  uint8_t responses[SYS_POWER_KIND_COUNT];
} rail_slot_t;

typedef struct {
  uint32_t mV, mA;
  uint8_t type;
} offer_t;

static const sys_power_layout_t k_default_layout = {
    .max_mV = 5000,
    .max_mA = 500,
    .reserve_mW = 1000,
    .unknown_source_mA = 500,
    .vreg_efficiency_pct = 90,
    .input_device = SYS_DEVICE_ID_NONE,
    .usb_pd = SYS_DEVICE_ID_NONE,
    .usb_ok = SYS_IO_PIN_NONE,
    .ext_ok = SYS_IO_PIN_NONE,
};

static const sys_power_manager_cfg_t k_default_cfg = {
    .mode = SYS_POWER_BUDGET_STRICT, .soft_margin_pct = 20, .source = SYS_POWER_SOURCE_UNKNOWN};
static const sys_power_pd_t k_default_pd = {.mode = SYS_POWER_PD_MAX, .voltage_mV = 0};

static const uint8_t k_default_responses[SYS_POWER_KIND_COUNT] = {
    [SYS_POWER_KIND_OVP] = SYS_POWER_RESPONSE_DISABLE,
    [SYS_POWER_KIND_UVP] = SYS_POWER_RESPONSE_NOTIFY,
    [SYS_POWER_KIND_SPC] = SYS_POWER_RESPONSE_DISABLE,
    [SYS_POWER_KIND_OCP_WARNING] = SYS_POWER_RESPONSE_NOTIFY,
    [SYS_POWER_KIND_OCP_CRITICAL] = SYS_POWER_RESPONSE_NOTIFY,
    [SYS_POWER_KIND_OTP] = SYS_POWER_RESPONSE_SAFE_STATE,
    [SYS_POWER_KIND_BATTERY_LOW] = SYS_POWER_RESPONSE_NOTIFY,
    [SYS_POWER_KIND_BATTERY_CRITICAL] = SYS_POWER_RESPONSE_SAFE_STATE,
    [SYS_POWER_KIND_BUDGET_EXCEEDED] = SYS_POWER_RESPONSE_DISABLE,
    [SYS_POWER_KIND_SOURCE_CHANGED] = SYS_POWER_RESPONSE_NOTIFY,
};

/* All manager state in one place, guarded by sys_pwrm_mutex. */
static struct {
  bool running;
  sys_power_safe_state_f safe_state;
  sys_power_layout_t layout;
  sys_power_manager_cfg_t cfg;
  sys_power_pd_t pd;
  sys_power_psu_t psu;
  sys_power_battery_t battery;
  sys_power_status_t status;
  uint8_t board_responses[SYS_POWER_KIND_COUNT];
  rail_slot_t rails[CONFIG_SYS_POWER_MANAGER_MAX_CONSUMERS];
  uint8_t rail_count;
  uint32_t alert_mA;
  uint16_t subs[3];
  bool recompute;
  bool starved;
  bool input_seen;
  bool input_error_reported;
  struct { uint32_t mV, mA; } pd_req;
  struct { bool ovp, low, critical, over; } latched;
} s_pm = {.cfg = k_default_cfg, .pd = k_default_pd};

U_RECURSIVE_MUTEX_DEFINE(sys_pwrm_mutex);

static TaskHandle_t s_task;
static volatile bool s_stop_req;
static bool s_interceptor_installed;

/* ---- Helpers ---- */

static bool has_device(uint16_t id) { return id < SYS_DEVICE_COUNT; }
static bool limits_active(void) { return s_pm.cfg.mode != SYS_POWER_BUDGET_DISABLED; }
static bool is_auto(const rail_state_t* r) { return r->enabled && r->set_mV > 0 && r->user_mA == 0; }
static bool is_fixed(const rail_state_t* r) { return r->enabled && r->set_mV > 0 && r->user_mA > 0; }
static bool is_vreg_set(uint16_t op) {
  return op == CONTRACT_VREG_OP_ENABLE || op == CONTRACT_VREG_OP_VOLTAGE || op == CONTRACT_VREG_OP_CURRENT;
}
static bool is_own_call(void) { return xSemaphoreGetMutexHolder(sys_pwrm_mutex) == xTaskGetCurrentTaskHandle(); }

static int rail_index(uint16_t id) {
  for (uint8_t i = 0; i < s_pm.rail_count; i++) {
    if (s_pm.rails[i].cfg.vreg == id) return i;
  }
  return -1;
}

static bool same_ch(uint8_t dev, uint8_t ch, uint16_t source, uint16_t channel) {
  return has_device(dev) && dev == source && ch == channel;
}

static uint32_t rail_input_mW(uint32_t mV, uint32_t mA) {
  return (uint32_t)(((uint64_t)mV * mA / 1000u) * 100u / s_pm.layout.vreg_efficiency_pct);
}

static uint32_t mA_for_mW(uint32_t avail_mW, uint32_t mV) {
  if (mV == 0) return s_pm.layout.max_mA;
  const uint32_t mA = (uint32_t)((uint64_t)avail_mW * s_pm.layout.vreg_efficiency_pct / 100u * 1000u / mV);
  return mA > s_pm.layout.max_mA ? s_pm.layout.max_mA : mA;
}

static uint32_t mV_for_mW(uint32_t avail_mW, uint32_t mA) {
  if (mA == 0) return s_pm.layout.max_mV;
  const uint32_t mV = (uint32_t)((uint64_t)avail_mW * s_pm.layout.vreg_efficiency_pct / 100u * 1000u / mA);
  return mV > s_pm.layout.max_mV ? s_pm.layout.max_mV : mV;
}

static uint32_t fixed_mW(void) {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < s_pm.rail_count; i++) {
    if (is_fixed(&s_pm.rails[i].st)) sum += rail_input_mW(s_pm.rails[i].st.set_mV, s_pm.rails[i].st.user_mA);
  }
  return sum;
}

static uint32_t limit_of(uint32_t budget_mW) {
  if (s_pm.cfg.mode == SYS_POWER_BUDGET_MARGIN) {
    return (uint32_t)((uint64_t)budget_mW * (100u + s_pm.cfg.soft_margin_pct) / 100u);
  }
  return budget_mW;
}

static err_h vreg_direct(uint16_t id, uint16_t op, uint32_t value) {
  if (op == CONTRACT_VREG_OP_ENABLE) {
    const vreg_enable_args_t a = {.enable = value != 0};
    return sys_device_write(id, op, &a, sizeof(a));
  }
  return sys_device_write(id, op, &value, sizeof(value));
}

static void wake(void) {
  s_pm.recompute = true;
  if (s_task != NULL) xTaskNotifyGive(s_task);
}

/* ---- Events and response matrix ---- */

static uint8_t response_of(uint8_t kind, int target) {
  if (!limits_active()) return SYS_POWER_RESPONSE_NOTIFY;
  return target == BOARD_TARGET ? s_pm.board_responses[kind] : s_pm.rails[target].responses[kind];
}

static void apply_response(uint8_t kind, int target) {
  const uint8_t resp = response_of(kind, target);
  if (resp == SYS_POWER_RESPONSE_SAFE_STATE) {
    if (s_pm.safe_state != NULL) s_pm.safe_state();
    else err_report(sys_device_sweep(CONTRACT_SYS_DEVICE_OP_SUSPEND));
    return;
  }
  if (resp != SYS_POWER_RESPONSE_DISABLE && resp != SYS_POWER_RESPONSE_RESET) return;
  const uint16_t op = resp == SYS_POWER_RESPONSE_DISABLE ? CONTRACT_SYS_DEVICE_OP_SUSPEND : CONTRACT_SYS_DEVICE_OP_RESET;
  for (uint8_t i = 0; i < s_pm.rail_count; i++) {
    if (target != BOARD_TARGET && target != i) continue;
    err_h err = sys_device_write(s_pm.rails[i].cfg.vreg, op, NULL, 0);
    (void)err_drop(&err, ERR_DEV_NOT_FOUND);
    err_report(err);
  }
}

#define EMIT(KIND, NAME, ...)                                                          \
  do {                                                                                 \
    apply_response(SYS_POWER_KIND_##KIND, BOARD_TARGET);                               \
    err_report(SYS_EVENT_PUBLISH(PWRM_EV_##NAME, SYS_DEVICE_ID_NONE, 0, __VA_ARGS__)); \
  } while (0)

static void emit_limit_reached(uint8_t device, uint32_t req_mW, uint32_t lim_mW) {
  err_report(SYS_EVENT_PUBLISH(PWRM_EV_LIMIT_REACHED, device, 0, .device = device, .requested_mW = req_mW,
                               .limit_mW = lim_mW));
}

static err_h fail_budget(uint8_t id, uint32_t req_mW, uint32_t avail_mW) {
  emit_limit_reached(id, req_mW, avail_mW);
  return ERR_NEW(ERR_PWRM_BUDGET_EXCEEDED, .id = id, .requested_mW = req_mW, .available_mW = avail_mW);
}

/* Queued listener for VREG and PMON events: EVT_CODE(tag) maps directly to sys_power_kind_e. */
static err_h on_event(const sys_event_t* ev, void* ctx) {
  (void)ctx;
  const uint16_t kind = EVT_CODE(ev->key.tag);
  if (kind == SYS_POWER_KIND_NONE || kind >= SYS_POWER_KIND_COUNT) return NULL;
  int32_t value = 0;
  if (ev->len >= sizeof(value)) memcpy(&value, ev->data, sizeof(value));

  U_RMUTEX_GUARD(sys_pwrm_mutex);
  if (!s_pm.running) return NULL;
  int target = rail_index(ev->key.source);
  if (target < 0 && same_ch(s_pm.layout.input_device, s_pm.layout.input_channel, ev->key.source, ev->key.channel)) {
    target = BOARD_TARGET;
  }
  for (uint8_t i = 0; i < s_pm.rail_count && target < 0; i++) {
    if (same_ch(s_pm.rails[i].cfg.monitor_device, s_pm.rails[i].cfg.monitor_channel, ev->key.source, ev->key.channel)) {
      target = i;
    }
  }
  if (target != NO_TARGET && target >= BOARD_TARGET) {
    err_report(ERR_NEW(ERR_PWRM_FAULT, .source = ev->key.source, .channel = (uint8_t)ev->key.channel,
                       .kind = (uint8_t)kind, .response = response_of((uint8_t)kind, target), .value = value));
    apply_response((uint8_t)kind, target);
  }
  return NULL;
}

static void remove_rail_at(int idx) {
  if (idx < s_pm.rail_count - 1) {
    memmove(&s_pm.rails[idx], &s_pm.rails[idx + 1], (size_t)(s_pm.rail_count - 1 - idx) * sizeof(s_pm.rails[0]));
  }
  s_pm.rail_count--;
  s_pm.recompute = true;
}

static err_h on_device_removed(const sys_event_t* ev, void* ctx) {
  (void)ctx;
  bool removed = false;
  {
    U_RMUTEX_GUARD(sys_pwrm_mutex);
    const int idx = s_pm.running ? rail_index(ev->key.source) : -1;
    if (idx >= 0) {
      remove_rail_at(idx);
      removed = true;
    }
  }
  if (removed) wake();
  return NULL;
}

/* ---- Budget & POWER_VREG interceptor ---- */

static err_h rebalance(void) {
  s_pm.starved = false;
  const uint32_t limit = limit_of(s_pm.status.budget_mW);
  const uint32_t used = s_pm.layout.reserve_mW + fixed_mW();
  const uint32_t pool = limit > used ? limit - used : 0;
  uint32_t auto_count = 0;
  for (uint8_t i = 0; i < s_pm.rail_count; i++) auto_count += is_auto(&s_pm.rails[i].st) ? 1u : 0u;

  err_h err = NULL;
  uint32_t allocated = used;
  for (uint8_t i = 0; i < s_pm.rail_count; i++) {
    rail_state_t* r = &s_pm.rails[i].st;
    if (!is_auto(r)) continue;
    const uint32_t share_mW = pool / auto_count;
    const uint32_t cap_mA = limits_active() ? mA_for_mW(share_mW, r->set_mV) : s_pm.layout.max_mA;
    if (cap_mA == 0) {
      s_pm.starved = true;
      continue;
    }
    if (limits_active() && cap_mA < s_pm.rails[i].cfg.min_mA) {
      ERR_KEEP_FIRST(err, ERR_NEW(ERR_PWRM_BUDGET_EXCEEDED, .id = s_pm.rails[i].cfg.vreg,
                                  .requested_mW = rail_input_mW(r->set_mV, s_pm.rails[i].cfg.min_mA),
                                  .available_mW = share_mW));
      allocated += rail_input_mW(r->set_mV, r->applied_mA);
      continue;
    }
    if (cap_mA != r->applied_mA) {
      err_h e = vreg_direct(s_pm.rails[i].cfg.vreg, CONTRACT_VREG_OP_CURRENT, cap_mA);
      const uint32_t root = err_root_tag(e);
      if (root == ERR_DEV_SUSPENDED || root == ERR_DEV_NOT_FOUND) err_free(e);
      else if (e != NULL) ERR_KEEP_FIRST(err, e);
      else r->applied_mA = cap_mA;
    }
    allocated += rail_input_mW(r->set_mV, r->applied_mA);
  }
  s_pm.status.allocated_mW = allocated;
  return err;
}

static err_h commit_rail(int i, rail_state_t next, bool grows, cmd_out_t* out, uint32_t rewrite_val, bool rewrite) {
  const rail_state_t prev = s_pm.rails[i].st;
  s_pm.rails[i].undo = prev;
  s_pm.rails[i].st = next;
  err_h err = grows ? rebalance() : NULL;
  if (err == NULL && rewrite) err = cmd_reply(out, &rewrite_val, sizeof(rewrite_val));
  if (err != NULL) {
    s_pm.rails[i].st = prev;
    if (grows) ERR_KEEP_FIRST(err, rebalance());
  }
  return err;
}

/* Clamps ENABLE / VOLTAGE / CURRENT writes of registered rails to the active budget and emits LIMIT_REACHED. */
static err_h vreg_take(uint8_t id, uint8_t dir, uint16_t op, cmd_in_t in, cmd_out_t* out) {
  if (!s_pm.running || dir != CMD_DIR_WRITE || !is_vreg_set(op) || is_own_call()) return NULL;
  U_RMUTEX_GUARD(sys_pwrm_mutex);
  const int i = rail_index(id);
  if (i < 0) return NULL;
  const uint32_t need = op == CONTRACT_VREG_OP_ENABLE ? sizeof(vreg_enable_args_t) : sizeof(uint32_t);
  ERR_CHECK(in.len == need, ERR_CMD_BAD_LEN, .got = (uint16_t)in.len, .need = (uint16_t)need);
  uint32_t value = 0;
  memcpy(&value, in.data, need);

  const rail_state_t prev = s_pm.rails[i].st;
  rail_state_t next = prev;

  if (!limits_active()) {
    if (op == CONTRACT_VREG_OP_ENABLE) next.enabled = value != 0;
    else if (op == CONTRACT_VREG_OP_VOLTAGE) next.set_mV = value;
    else next.applied_mA = (next.user_mA = value) != 0 ? value : s_pm.layout.max_mA;
    s_pm.rails[i].undo = prev;
    s_pm.rails[i].st = next;
    return NULL;
  }

  const uint32_t limit = limit_of(s_pm.status.budget_mW);
  const uint32_t others = fixed_mW() - (is_fixed(&prev) ? rail_input_mW(prev.set_mV, prev.user_mA) : 0);
  const uint32_t used = s_pm.layout.reserve_mW + others;
  const uint32_t avail_mW = limit > used ? limit - used : 0;
  const uint32_t min_mA = s_pm.rails[i].cfg.min_mA;
  const uint32_t floor_mA = min_mA > 0 ? min_mA : 1u;

  if (op == CONTRACT_VREG_OP_ENABLE) {
    const bool en = value != 0;
    next.enabled = en;
    if (en && prev.set_mV > 0) {
      const uint32_t req_mW = rail_input_mW(prev.set_mV, prev.user_mA > 0 ? prev.user_mA : floor_mA);
      if (avail_mW == 0 || req_mW > avail_mW) {
        const uint32_t fit_mA = prev.user_mA > 0 ? mA_for_mW(avail_mW, prev.set_mV) : 0;
        if (fit_mA < min_mA || fit_mA == 0) return fail_budget(id, req_mW, avail_mW);
        emit_limit_reached(id, req_mW, avail_mW);
        ERR_TRY(vreg_direct(id, CONTRACT_VREG_OP_CURRENT, fit_mA));
        next.user_mA = next.applied_mA = fit_mA;
      }
    }
    return commit_rail(i, next, en && !prev.enabled, out, 0, false);
  }

  if (op == CONTRACT_VREG_OP_VOLTAGE) {
    uint32_t target_mV = value > s_pm.layout.max_mV ? s_pm.layout.max_mV : value;
    bool hit = target_mV < value;
    const uint32_t req_mW = rail_input_mW(value, prev.user_mA > 0 ? prev.user_mA : floor_mA);
    if (prev.enabled && target_mV > 0) {
      const uint32_t check_mA = prev.user_mA > 0 ? prev.user_mA : min_mA;
      if (check_mA > 0 && rail_input_mW(target_mV, check_mA) > avail_mW) {
        hit = true;
        const uint32_t fit_mA = prev.user_mA > 0 ? mA_for_mW(avail_mW, target_mV) : 0;
        if (fit_mA >= min_mA && fit_mA > 0) {
          ERR_TRY(vreg_direct(id, CONTRACT_VREG_OP_CURRENT, fit_mA));
          next.user_mA = next.applied_mA = fit_mA;
        } else {
          const uint32_t fit_mV = mV_for_mW(avail_mW, floor_mA);
          if (fit_mV == 0) return fail_budget(id, req_mW, avail_mW);
          if (prev.user_mA > 0 && min_mA > 0 && min_mA != prev.applied_mA) {
            ERR_TRY(vreg_direct(id, CONTRACT_VREG_OP_CURRENT, min_mA));
            next.user_mA = next.applied_mA = min_mA;
          }
          target_mV = fit_mV;
        }
      }
    }
    if (hit) emit_limit_reached(id, req_mW, avail_mW);
    next.set_mV = target_mV;
    return commit_rail(i, next, target_mV > prev.set_mV, out, target_mV, target_mV != value);
  }

  /* CONTRACT_VREG_OP_CURRENT */
  if (value == 0) {
    next.user_mA = 0;
    s_pm.rails[i].undo = prev;
    s_pm.rails[i].st = next;
    ERR_TRY(rebalance());
    const uint32_t share = s_pm.rails[i].st.applied_mA != 0 ? s_pm.rails[i].st.applied_mA : s_pm.layout.max_mA;
    const err_h err = cmd_reply(out, &share, sizeof(share));
    if (err != NULL) s_pm.rails[i].st = prev;
    return err;
  }

  uint32_t target_mA = value > s_pm.layout.max_mA ? s_pm.layout.max_mA : value;
  bool hit = target_mA < value;
  const uint32_t req_mW = prev.set_mV > 0 ? rail_input_mW(prev.set_mV, value) : 0;
  if (prev.enabled && prev.set_mV > 0) {
    const uint32_t fit_mA = mA_for_mW(avail_mW, prev.set_mV);
    if (fit_mA < target_mA) {
      if (fit_mA < min_mA || fit_mA == 0) return fail_budget(id, req_mW, avail_mW);
      hit = true;
      target_mA = fit_mA;
    }
  }
  if (hit) emit_limit_reached(id, req_mW, avail_mW);
  next.user_mA = next.applied_mA = target_mA;
  return commit_rail(i, next, target_mA > prev.applied_mA, out, target_mA, target_mA != value);
}

static void vreg_settle(uint8_t id, uint8_t dir, uint16_t op, cmd_in_t passed, err_h err) {
  (void)passed;
  if (err == NULL || !s_pm.running || dir != CMD_DIR_WRITE || !is_vreg_set(op) || is_own_call()) return;
  U_RMUTEX_GUARD(sys_pwrm_mutex);
  const int i = rail_index(id);
  if (i < 0) return;
  s_pm.rails[i].st = s_pm.rails[i].undo;
  if (limits_active()) err_report(rebalance());
}

/* ---- Source, USB-PD & LiPo battery ---- */

static sys_power_source_e detect_source(void) {
  if (!sys_io_pin_valid(s_pm.layout.usb_ok)) return (sys_power_source_e)s_pm.cfg.source;
  bool ext = false, usb = false;
  err_free(sys_io_get_level(s_pm.layout.ext_ok, &ext));
  err_free(sys_io_get_level(s_pm.layout.usb_ok, &usb));
  if (ext) return s_pm.battery.chemistry != SYS_POWER_BATTERY_NONE ? SYS_POWER_SOURCE_BATTERY : SYS_POWER_SOURCE_PSU;
  return usb ? SYS_POWER_SOURCE_USB : SYS_POWER_SOURCE_UNKNOWN;
}

static bool pick_offer(uint32_t want_mV, offer_t* out) {
  if (!has_device(s_pm.layout.usb_pd)) return false;
  sys_power_usb_pd_option_t opt[SYS_POWER_USB_PD_OPTIONS_MAX];
  uint8_t n = 0;
  err_h err = sys_power_usb_pd_list_options(s_pm.layout.usb_pd, opt, ARRAY_LEN(opt), &n);
  if (err != NULL) {
    err_free(err);
    return false;
  }
  if (want_mV == 0) {
    for (uint8_t i = 0; i < n; i++) want_mV = opt[i].max_mV > want_mV ? opt[i].max_mV : want_mV;
  }
  if (want_mV > s_pm.layout.max_mV) want_mV = s_pm.layout.max_mV;
  const sys_power_usb_pd_option_t* best = NULL;
  uint32_t mV = 0;
  for (uint8_t i = 0; i < n && best == NULL; i++) {
    if (opt[i].type != SYS_POWER_USB_PD_OPTION_FIXED && opt[i].min_mV <= want_mV && want_mV <= opt[i].max_mV) {
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
  out->mA = best->max_mA > s_pm.layout.max_mA ? s_pm.layout.max_mA : best->max_mA;
  out->type = best->type;
  return true;
}

static void update_pd(sys_power_source_e source) {
  const uint32_t want_mV =
      s_pm.pd.mode == SYS_POWER_PD_VOLTAGE ? s_pm.pd.voltage_mV : s_pm.pd.mode == SYS_POWER_PD_MAX ? 0 : 5000;
  offer_t o = {0};
  const bool usb = source == SYS_POWER_SOURCE_USB && pick_offer(want_mV, &o);
  s_pm.status.pd_mV = usb ? o.mV : 0;
  s_pm.status.pd_mA = usb ? o.mA : 0;
  s_pm.status.pd_type = usb ? o.type : SYS_POWER_PD_TYPE_NONE;
  if (!usb || s_pm.pd.mode == SYS_POWER_PD_OFF) {
    s_pm.pd_req.mV = s_pm.pd_req.mA = 0;
    return;
  }
  if (o.mV == s_pm.pd_req.mV && o.mA == s_pm.pd_req.mA) return;
  s_pm.pd_req.mV = o.mV;
  s_pm.pd_req.mA = o.mA;
  err_report(SYS_DEVICE_SET(s_pm.layout.usb_pd, CONTRACT_PD, OP_SETTINGS, .voltage_mV = o.mV, .current_mA = o.mA));
  if (s_pm.pd.mode == SYS_POWER_PD_VOLTAGE && o.mV != s_pm.pd.voltage_mV) {
    err_report(SYS_EVENT_PUBLISH(PWRM_EV_PD_FIXED, SYS_DEVICE_ID_NONE, 0, .mV = (int32_t)o.mV));
  }
}

static uint32_t source_current_mA(sys_power_source_e source) {
  switch (source) {
    case SYS_POWER_SOURCE_PSU:
      return s_pm.psu.max_mA > 0 ? s_pm.psu.max_mA : s_pm.layout.max_mA;
    case SYS_POWER_SOURCE_BATTERY:
      return s_pm.layout.max_mA;
    case SYS_POWER_SOURCE_USB:
      return s_pm.status.pd_mA > 0 ? s_pm.status.pd_mA : s_pm.layout.unknown_source_mA;
    default:
      return s_pm.layout.unknown_source_mA;
  }
}

static const struct { uint16_t mV; uint8_t pct; } k_lipo_curve[] = {
    {4200, 100}, {4150, 95}, {4110, 90}, {4080, 85}, {4020, 80}, {3980, 75}, {3950, 70},
    {3910, 65},  {3870, 60}, {3850, 55}, {3840, 50}, {3820, 45}, {3800, 40}, {3790, 35},
    {3770, 30},  {3750, 25}, {3730, 20}, {3710, 15}, {3690, 10}, {3610, 5},  {3270, 0},
};

static uint8_t lipo_pct(int32_t cell_mV) {
  const size_t n = ARRAY_LEN(k_lipo_curve);
  if (cell_mV >= k_lipo_curve[0].mV) return 100;
  if (cell_mV <= k_lipo_curve[n - 1].mV) return 0;
  for (size_t i = 1; i < n; i++) {
    if (cell_mV >= k_lipo_curve[i].mV) {
      const int32_t span_mV = k_lipo_curve[i - 1].mV - k_lipo_curve[i].mV;
      const int32_t span_pct = k_lipo_curve[i - 1].pct - k_lipo_curve[i].pct;
      return (uint8_t)(k_lipo_curve[i].pct + (cell_mV - k_lipo_curve[i].mV) * span_pct / span_mV);
    }
  }
  return 0;
}

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
  if (battery_crossed(&s_pm.latched.critical, pct, s_pm.battery.critical_pct)) EMIT(BATTERY_CRITICAL, BATTERY_CRITICAL, .pct = pct);
  if (battery_crossed(&s_pm.latched.low, pct, s_pm.battery.low_pct)) EMIT(BATTERY_LOW, BATTERY_LOW, .pct = pct);
}

/* ---- Periodic tick & dynamic task ---- */

static void program_input_alerts(uint32_t limit_mA) {
  if (!limits_active() || !has_device(s_pm.layout.input_device) || limit_mA == 0 || limit_mA == s_pm.alert_mA) return;
  const uint32_t warn_mA = limit_mA * CONFIG_SYS_POWER_MANAGER_INPUT_WARNING_PCT / 100u;
  err_h err = SYS_DEVICE_SET(s_pm.layout.input_device, CONTRACT_PMON, OP_ALERT, .channel = s_pm.layout.input_channel,
                             .alert = SYS_POWER_ALERT_OCP_CRITICAL, .threshold_mA = (int32_t)limit_mA);
  ERR_KEEP_FIRST(err, SYS_DEVICE_SET(s_pm.layout.input_device, CONTRACT_PMON, OP_ALERT,
                                     .channel = s_pm.layout.input_channel, .alert = SYS_POWER_ALERT_OCP_WARNING,
                                     .threshold_mA = (int32_t)warn_mA));
  if (err == NULL) s_pm.alert_mA = limit_mA;
  else if (s_pm.alert_mA != 0 || !err_drop(&err, ERR_DEV_NOT_FOUND)) err_report(err);
}

static bool read_input(bool* have_input) {
  *have_input = false;
  if (!has_device(s_pm.layout.input_device)) return true;
  int32_t mV = 0, mA = 0;
  err_h err = SYS_DEVICE_GET(s_pm.layout.input_device, CONTRACT_PMON, OP_VOLTAGE, &mV, .channel = s_pm.layout.input_channel);
  if (err == NULL) err = SYS_DEVICE_GET(s_pm.layout.input_device, CONTRACT_PMON, OP_CURRENT, &mA, .channel = s_pm.layout.input_channel);
  if (err_drop(&err, ERR_DEV_SUSPENDED) || (!s_pm.input_seen && err_drop(&err, ERR_DEV_NOT_FOUND))) return false;
  if (err == NULL) {
    s_pm.input_seen = true;
    s_pm.input_error_reported = false;
  } else if (!s_pm.input_error_reported) {
    s_pm.input_error_reported = true;
    err_report(err);
  } else {
    err_free(err);
  }
  *have_input = err == NULL && mV > 0;
  s_pm.status.input_mV = *have_input ? mV : 0;
  s_pm.status.input_mA = *have_input ? mA : 0;
  return true;
}

static void update_budget(sys_power_source_e source, bool have_input) {
  if (have_input && (uint32_t)s_pm.status.input_mV > s_pm.layout.max_mV) {
    if (!s_pm.latched.ovp) {
      s_pm.latched.ovp = true;
      EMIT(OVP, INPUT_OVP, .mV = s_pm.status.input_mV);
    }
  } else {
    s_pm.latched.ovp = false;
  }

  const bool measured = has_device(s_pm.layout.input_device);
  uint32_t mV = have_input ? (uint32_t)s_pm.status.input_mV : measured ? 0 : s_pm.layout.max_mV;
  if (mV > s_pm.layout.max_mV) mV = s_pm.layout.max_mV;
  uint32_t mA = source_current_mA(source);
  if (mA > s_pm.layout.max_mA) mA = s_pm.layout.max_mA;
  const uint32_t budget = (uint32_t)((uint64_t)mV * mA / 1000u);
  s_pm.status.source_mV = mV;
  s_pm.status.source_mA = mA;
  const uint32_t lim_mA = limit_of(mA) > s_pm.layout.max_mA ? s_pm.layout.max_mA : limit_of(mA);
  program_input_alerts(lim_mA);

  const uint32_t old = s_pm.status.budget_mW;
  const uint32_t delta = budget > old ? budget - old : old - budget;
  if (!s_pm.recompute && delta < CONFIG_SYS_POWER_MANAGER_BUDGET_STEP_MW && (budget == 0) == (old == 0)) return;
  s_pm.recompute = false;
  s_pm.status.budget_mW = budget;
  s_pm.status.limit_mW = limit_of(budget);
  err_report(rebalance());
  const uint32_t committed = s_pm.layout.reserve_mW + fixed_mW();
  const bool over = limits_active() && (committed > s_pm.status.limit_mW || s_pm.starved);
  if (over && !s_pm.latched.over) {
    s_pm.latched.over = true;
    err_report(ERR_NEW(ERR_PWRM_BELOW_ALLOCATION, .allocated_mW = committed, .limit_mW = s_pm.status.limit_mW));
    EMIT(BUDGET_EXCEEDED, BUDGET_EXCEEDED, .limit_mW = (int32_t)s_pm.status.limit_mW);
  } else if (!over) {
    s_pm.latched.over = false;
  }
}

static void tick(void) {
  U_RMUTEX_GUARD(sys_pwrm_mutex);
  if (!s_pm.running) return;
  bool have_input;
  const bool readable = read_input(&have_input);
  const sys_power_source_e source = detect_source();
  if (source != (sys_power_source_e)s_pm.status.source) {
    s_pm.status.source = (uint8_t)source;
    EMIT(SOURCE_CHANGED, SOURCE_CHANGED, .source = source);
  }
  update_pd(source);
  if (readable) {
    update_budget(source, have_input);
    update_battery(have_input);
  }
  s_pm.status.flags = (uint8_t)((limits_active() ? SYS_POWER_FLAG_ENABLED : 0) |
                                (s_pm.cfg.mode == SYS_POWER_BUDGET_STRICT ? SYS_POWER_FLAG_STRICT : 0) |
                                (s_pm.latched.over ? SYS_POWER_FLAG_OVER_BUDGET : 0) |
                                (s_pm.input_seen ? SYS_POWER_FLAG_INPUT_SEEN : 0));
}

static void power_task(void* arg) {
  (void)arg;
  while (!s_stop_req) {
    tick();
    ulTaskNotifyTake(pdTRUE, MSEC(CONFIG_SYS_POWER_MANAGER_PERIOD_MS));
  }
  s_task = NULL;
  vTaskDelete(NULL);
}

/* ---- Settings ---- */

static err_h check_range(int32_t value, int32_t min, int32_t max) {
  ERR_CHECK_RANGE(value, min, max);
  return NULL;
}

err_h sys_power_manager_set_config(const sys_power_manager_cfg_t* cfg) {
  ERR_CHECK(s_pm.running, ERR_PWRM_NOT_READY);
  ERR_TRY(check_range(cfg->mode, SYS_POWER_BUDGET_DISABLED, SYS_POWER_BUDGET_MARGIN));
  ERR_TRY(check_range(cfg->soft_margin_pct, 0, 100));
  ERR_TRY(check_range(cfg->source, SYS_POWER_SOURCE_UNKNOWN, SYS_POWER_SOURCE_BATTERY));
  {
    U_RMUTEX_GUARD(sys_pwrm_mutex);
    s_pm.cfg = *cfg;
    s_pm.alert_mA = 0;
  }
  wake();
  return NULL;
}

err_h sys_power_manager_get_config(sys_power_manager_cfg_t* out) {
  U_RMUTEX_GUARD(sys_pwrm_mutex);
  *out = s_pm.cfg;
  return NULL;
}

err_h sys_power_manager_set_pd(const sys_power_pd_t* pd) {
  ERR_CHECK(s_pm.running, ERR_PWRM_NOT_READY);
  ERR_TRY(check_range(pd->mode, SYS_POWER_PD_OFF, SYS_POWER_PD_VOLTAGE));
  if (pd->mode == SYS_POWER_PD_VOLTAGE) ERR_TRY(check_range((int32_t)pd->voltage_mV, 1, (int32_t)s_pm.layout.max_mV));
  {
    U_RMUTEX_GUARD(sys_pwrm_mutex);
    s_pm.pd = *pd;
    if (pd->mode != SYS_POWER_PD_VOLTAGE) s_pm.pd.voltage_mV = 0;
    s_pm.pd_req.mV = s_pm.pd_req.mA = 0;
  }
  wake();
  return NULL;
}

err_h sys_power_manager_get_pd(sys_power_pd_t* out) {
  U_RMUTEX_GUARD(sys_pwrm_mutex);
  *out = s_pm.pd;
  return NULL;
}

err_h sys_power_manager_set_psu(const sys_power_psu_t* psu) {
  ERR_CHECK(s_pm.running, ERR_PWRM_NOT_READY);
  if (psu->max_mV != 0 || psu->max_mA != 0) {
    ERR_TRY(check_range((int32_t)psu->max_mV, 1, INT32_MAX));
    ERR_TRY(check_range((int32_t)psu->max_mA, 1, INT32_MAX));
  }
  {
    U_RMUTEX_GUARD(sys_pwrm_mutex);
    s_pm.psu = *psu;
  }
  wake();
  return NULL;
}

err_h sys_power_manager_get_psu(sys_power_psu_t* out) {
  U_RMUTEX_GUARD(sys_pwrm_mutex);
  *out = s_pm.psu;
  return NULL;
}

err_h sys_power_manager_set_battery(const sys_power_battery_t* battery) {
  ERR_CHECK(s_pm.running, ERR_PWRM_NOT_READY);
  ERR_TRY(check_range(battery->chemistry, SYS_POWER_BATTERY_NONE, SYS_POWER_BATTERY_LIPO));
  sys_power_battery_t b = {0};
  if (battery->chemistry != SYS_POWER_BATTERY_NONE) {
    ERR_TRY(check_range(battery->cells, LIPO_CELLS_MIN, (int32_t)(s_pm.layout.max_mV / LIPO_FULL_CELL_MV)));
    ERR_TRY(check_range(battery->low_pct, 2, 100));
    ERR_TRY(check_range(battery->critical_pct, 1, battery->low_pct - 1));
    b = *battery;
  }
  {
    U_RMUTEX_GUARD(sys_pwrm_mutex);
    s_pm.battery = b;
    s_pm.latched.low = s_pm.latched.critical = false;
  }
  wake();
  return NULL;
}

err_h sys_power_manager_get_battery(sys_power_battery_t* out) {
  U_RMUTEX_GUARD(sys_pwrm_mutex);
  *out = s_pm.battery;
  return NULL;
}

static err_h matrix_row_ptr(uint8_t target, uint8_t** out) {
  if (target == SYS_POWER_TARGET_BOARD) {
    *out = s_pm.board_responses;
    return NULL;
  }
  const int idx = rail_index(target);
  ERR_CHECK(idx >= 0, ERR_PWRM_NOT_RAIL, .id = target);
  *out = s_pm.rails[idx].responses;
  return NULL;
}

err_h sys_power_manager_set_matrix(const sys_power_response_row_t* rows, uint8_t count) {
  ERR_CHECK(s_pm.running, ERR_PWRM_NOT_READY);
  uint8_t* dst[1 + CONFIG_SYS_POWER_MANAGER_MAX_CONSUMERS];
  ERR_CHECK(count <= ARRAY_LEN(dst), ERR_CMD_BAD_LEN, .got = count, .need = (uint16_t)ARRAY_LEN(dst));
  U_RMUTEX_GUARD(sys_pwrm_mutex);
  for (uint8_t i = 0; i < count; i++) {
    ERR_TRY(matrix_row_ptr(rows[i].target, &dst[i]));
    for (uint8_t k = 0; k < SYS_POWER_KIND_COUNT; k++) {
      ERR_TRY(check_range(rows[i].responses[k], SYS_POWER_RESPONSE_NOTIFY, SYS_POWER_RESPONSE_SAFE_STATE));
    }
  }
  for (uint8_t i = 0; i < count; i++) memcpy(dst[i], rows[i].responses, SYS_POWER_KIND_COUNT);
  return NULL;
}

err_h sys_power_manager_get_matrix(sys_power_response_row_t* rows, uint8_t cap, uint8_t* count) {
  ERR_CHECK(s_pm.running, ERR_PWRM_NOT_READY);
  U_RMUTEX_GUARD(sys_pwrm_mutex);
  const uint8_t n = (uint8_t)(1 + s_pm.rail_count);
  ERR_CHECK(cap >= n, ERR_CMD_BAD_LEN, .got = cap, .need = n);
  rows[0].target = SYS_POWER_TARGET_BOARD;
  memcpy(rows[0].responses, s_pm.board_responses, SYS_POWER_KIND_COUNT);
  for (uint8_t i = 0; i < s_pm.rail_count; i++) {
    rows[1 + i].target = s_pm.rails[i].cfg.vreg;
    memcpy(rows[1 + i].responses, s_pm.rails[i].responses, SYS_POWER_KIND_COUNT);
  }
  *count = n;
  return NULL;
}

err_h sys_power_manager_get_status(sys_power_status_t* out) {
  ERR_CHECK(s_pm.running, ERR_PWRM_NOT_READY);
  U_RMUTEX_GUARD(sys_pwrm_mutex);
  *out = s_pm.status;
  return NULL;
}

/* ---- Runtime lifecycle & loads ---- */

static err_h start_manager(const sys_power_layout_t* layout, sys_power_safe_state_f safe_state) {
  memset(&s_pm, 0, sizeof(s_pm));
  s_pm.layout = *layout;
  s_pm.cfg = k_default_cfg;
  s_pm.pd = k_default_pd;
  s_pm.safe_state = safe_state;
  s_pm.status.battery_pct = SYS_POWER_PCT_UNKNOWN;
  s_pm.status.pd_type = SYS_POWER_PD_TYPE_NONE;
  memcpy(s_pm.board_responses, k_default_responses, SYS_POWER_KIND_COUNT);
  s_pm.running = true;

  if (!s_interceptor_installed) {
    err_h err = sys_device_set_interceptor(MODULE_ID_VREG, vreg_take, vreg_settle);
    if (err != NULL) {
      s_pm.running = false;
      return err;
    }
    s_interceptor_installed = true;
  }

  static const struct { uint32_t tag; sys_event_handler_f fn; } k_subs[3] = {
      {EVT_TAG(MODULE_ID_VREG, SYS_EVENT_ANY_CODE), on_event},
      {EVT_TAG(MODULE_ID_PMON, SYS_EVENT_ANY_CODE), on_event},
      {SYS_DEVICE_EV_REMOVED, on_device_removed},
  };
  for (uint32_t i = 0; i < ARRAY_LEN(k_subs); i++) {
    err_h err = sys_event_subscribe(
        (sys_event_key_t){.tag = k_subs[i].tag, .source = SYS_EVENT_ANY_SOURCE, .channel = SYS_EVENT_ANY_CHANNEL},
        k_subs[i].fn, NULL, SYS_EVENT_RUN_QUEUED_DEVICE, &s_pm.subs[i]);
    if (err != NULL) {
      err_free(sys_power_manager_stop());
      return err;
    }
  }

  s_pm.recompute = true;
  tick();

  s_stop_req = false;
  if (xTaskCreate(power_task, "pwrm", CONFIG_SYS_POWER_MANAGER_TASK_STACK, NULL,
                  CONFIG_SYS_POWER_MANAGER_TASK_PRIO, &s_task) != pdPASS ||
      s_task == NULL) {
    err_free(sys_power_manager_stop());
    return ERR_NEW(ERR_PWRM_NOT_READY);
  }
  return NULL;
}

err_h sys_power_manager_init(sys_power_safe_state_f safe_state) {
  U_RMUTEX_GUARD(sys_pwrm_mutex);
  ERR_CHECK(!s_pm.running, ERR_BUSY);
  return start_manager(&k_default_layout, safe_state);
}

err_h sys_power_manager_stop(void) {
  if (!s_pm.running && s_task == NULL) return NULL;
  s_stop_req = true;
  if (s_task != NULL) {
    xTaskNotifyGive(s_task);
    for (uint32_t i = 0; i < 40 && s_task != NULL; i++) vTaskDelay(MSEC(5));
  }
  U_RMUTEX_GUARD(sys_pwrm_mutex);
  for (uint32_t i = 0; i < ARRAY_LEN(s_pm.subs); i++) {
    if (s_pm.subs[i] != 0) err_free(sys_event_unsubscribe(s_pm.subs[i]));
  }
  memset(&s_pm, 0, sizeof(s_pm));
  s_pm.cfg = k_default_cfg;
  s_pm.pd = k_default_pd;
  return NULL;
}

err_h sys_power_manager_set_layout(const sys_power_layout_t* layout) {
  ERR_TRY(check_range(layout->vreg_efficiency_pct, 1, 100));
  U_RMUTEX_GUARD(sys_pwrm_mutex);
  if (!s_pm.running) return start_manager(layout, NULL);
  s_pm.layout = *layout;
  s_pm.alert_mA = 0;
  s_pm.input_seen = s_pm.input_error_reported = false;
  s_pm.recompute = true;
  tick();
  wake();
  return NULL;
}

err_h sys_power_manager_add_rail(const sys_power_rail_t* rail) {
  U_RMUTEX_GUARD(sys_pwrm_mutex);
  ERR_CHECK(s_pm.running, ERR_PWRM_NOT_READY);
  ERR_CHECK(rail_index(rail->vreg) < 0, ERR_PWRM_RAIL_TAKEN, .id = rail->vreg);
  ERR_CHECK(s_pm.rail_count < CONFIG_SYS_POWER_MANAGER_MAX_CONSUMERS, ERR_FULL,
            .max = CONFIG_SYS_POWER_MANAGER_MAX_CONSUMERS);
  rail_slot_t* slot = &s_pm.rails[s_pm.rail_count++];
  memset(slot, 0, sizeof(*slot));
  slot->cfg = *rail;
  memcpy(slot->responses, s_pm.board_responses, SYS_POWER_KIND_COUNT);
  wake();
  return NULL;
}

err_h sys_power_manager_remove_rail(const sys_power_rail_ref_t* ref) {
  U_RMUTEX_GUARD(sys_pwrm_mutex);
  ERR_CHECK(s_pm.running, ERR_PWRM_NOT_READY);
  const int idx = rail_index(ref->vreg);
  ERR_CHECK(idx >= 0, ERR_PWRM_NOT_RAIL, .id = ref->vreg);
  remove_rail_at(idx);
  wake();
  return NULL;
}
