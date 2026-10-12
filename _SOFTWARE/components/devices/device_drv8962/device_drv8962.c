#include "device_drv8962.h"

#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_event.h"
#include "sys_pins.h"
#include "utils.h"

#define HALF_BRIDGES 4u
#define PWM_FREQ_DEFAULT_HZ 20000u
#define RIPROPI_DEFAULT_OHMS 3090u
#define DRIVE_DEADBAND 20  // a full-bridge drive below this (0.5 %) brakes
#define WAKE_DELAY_MS 2u  // tWAKE: 1.2 ms max before the inputs are taken (a bounded wait, like the TPS55289's EN)
#define SLEEP_PULSE_US 30u      // nSLEEP reset pulse that clears the fault latches
#define SLEEP_WAKE_US 1000u     // and the wait after it
#define VREF_MIN_MV 50u         // a floating VREF sets an undefined chopping threshold: always drive 50 .. 3300
#define VREF_MAX_MV 3300u
#define AIPROPI_UA_PER_A 212u   // IPROPI output: 212 uA per A (datasheet), so I(mA) = V(mV) * 10^6 / (212 * R)

typedef struct {
  int16_t drive;  // PWM steps (SYS_HBRIDGE_DRIVE_MAX = full)
  int32_t last_current_mA;
  bool fault;
  uint8_t reason;  // sys_hbridge_fault_reason_e
} channel_t;

typedef struct {
  sys_device_base_t base;  // first: id, suspended (every channel coasted)
  bool started;            // the channels were braked once: uninstall coasts them
  bool nfault_subscribed;
  uint8_t nfault_sub;  // sys_event handle: IO_EV_FALLING of nFAULT -> our ON_EVENT
  uint8_t topology;    // drv8962_topology_e
  bool turn_off_at_ocp;
  uint32_t pwm_freq_Hz;
  sys_io_pin_t pin[DRV8962_PINS];
  uint16_t used_mask;    // bit per pin: made a pin of its device (released on uninstall)
  uint16_t locked_mask;  // bit per pin: locked on its device (we drive it with SET_LOCKED_LEVEL)
  uint32_t ripropi_ohms[HALF_BRIDGES];
  uint32_t current_limit_mA[HALF_BRIDGES];  // per channel
  channel_t ch[HALF_BRIDGES];
} drv_t;

static uint8_t channel_count(const drv_t* c) {
  return c->topology == DRV8962_TOPOLOGY_2_FULL_BRIDGES ? 2 : 4;
}

// Full bridge: channel 0 = half bridges 0+1, channel 1 = 2+3. Half bridges: one each.
static void channel_bridges(const drv_t* c, uint8_t channel, uint8_t* first, uint8_t* last) {
  if (c->topology == DRV8962_TOPOLOGY_2_FULL_BRIDGES) {
    *first = channel == 0 ? 0 : 2;
    *last = *first + 1;
  } else {
    *first = *last = channel;
  }
}

static bool has_pin(const drv_t* c, uint8_t idx) {
  return sys_io_pin_valid(c->pin[idx]);
}

static err_h check_channel(const drv_t* c, uint8_t channel) {
  ERR_CHECK(channel < channel_count(c), ERR_DEV_BAD_PIN, .id = c->base.id, .pin = channel);
  return NULL;
}

/* ---- Chip access: the router and the registry add the context ---- */

// IPROPI of a half bridge as mA (0 without the pin).
static err_h sample_current_mA(const drv_t* c, uint8_t bridge, int32_t* out_mA) {
  *out_mA = 0;
  const uint8_t idx = DRV8962_PIN_IPROPI1 + bridge;
  if (!has_pin(c, idx)) {
    return NULL;
  }
  int32_t mV = 0;
  ERR_TRY(sys_io_get_voltage(c->pin[idx], &mV));
  *out_mA = (int32_t)(((int64_t)mV * 1000000) / ((int64_t)AIPROPI_UA_PER_A * c->ripropi_ohms[bridge]));
  return NULL;
}

// A limit of 0 disables the check; a negative reading is never over a limit.
static bool current_at_or_over(int32_t mA, uint32_t limit_mA) {
  return limit_mA > 0 && mA >= 0 && (uint32_t)mA >= limit_mA;
}

// Channel current (full bridge: the larger of its two half bridges), remembered.
static err_h read_channel_current_mA(drv_t* c, uint8_t channel, int32_t* out_mA) {
  uint8_t first, last;
  channel_bridges(c, channel, &first, &last);
  ERR_TRY(sample_current_mA(c, first, out_mA));
  if (last != first) {
    int32_t other = 0;
    ERR_TRY(sample_current_mA(c, last, &other));
    if (other > *out_mA) {
      *out_mA = other;
    }
  }
  c->ch[channel].last_current_mA = *out_mA;
  return NULL;
}

// EN and nSLEEP are locked on their devices: this driver drives them with SET_LOCKED_LEVEL.
static err_h set_locked_pin(const drv_t* c, uint8_t idx, bool level) {
  return sys_pins_set_locked_level(c->pin[idx], level);
}

/* Outputs on (brake) or off (coast): EN and IN of every half bridge of the channel. Every pin is written even if an
   earlier write fails (the safe-state path); the first failure is returned. */
static err_h chip_set_outputs(drv_t* c, uint8_t channel, bool enabled) {
  uint8_t first, last;
  channel_bridges(c, channel, &first, &last);
  err_h err = NULL;
  for (uint8_t i = first; i <= last; i++) {
    if (has_pin(c, DRV8962_PIN_EN1 + i)) {
      SYS_DEVICE_KEEP_FIRST(err, set_locked_pin(c, DRV8962_PIN_EN1 + i, enabled));
    }
    if (has_pin(c, DRV8962_PIN_IN1 + i)) {
      SYS_DEVICE_KEEP_FIRST(err, sys_io_set_pwm_duty(c->pin[DRV8962_PIN_IN1 + i], 0));
    }
  }
  c->ch[channel].drive = 0;
  return err;
}

static err_h chip_brake(drv_t* c, uint8_t channel) {
  return chip_set_outputs(c, channel, true);
}

static err_h chip_coast(drv_t* c, uint8_t channel) {
  return chip_set_outputs(c, channel, false);
}

static uint32_t drive_to_duty(int32_t magnitude) {
  return (uint32_t)magnitude * IO_PWM_DUTY_MAX / SYS_HBRIDGE_DRIVE_MAX;
}

static err_h enable_bridges(const drv_t* c, uint8_t first, uint8_t last) {
  for (uint8_t i = first; i <= last; i++) {
    if (has_pin(c, DRV8962_PIN_EN1 + i)) {
      ERR_TRY(set_locked_pin(c, DRV8962_PIN_EN1 + i, true));
    }
  }
  return NULL;
}

static err_h chip_set_drive(drv_t* c, uint8_t channel, int16_t drive) {
  if (c->topology == DRV8962_TOPOLOGY_2_FULL_BRIDGES) {
    if (drive > -DRIVE_DEADBAND && drive < DRIVE_DEADBAND) {
      return chip_brake(c, channel);
    }
    uint8_t b1, b2;
    channel_bridges(c, channel, &b1, &b2);
    ERR_TRY(enable_bridges(c, b1, b2));
    const uint32_t comp_duty = IO_PWM_DUTY_MAX - drive_to_duty(drive < 0 ? -drive : drive);
    if (drive > 0) {
      // Forward (slow decay): IN1 = 100 %, IN2 = the PWM complement
      ERR_TRY(sys_io_set_pwm_duty(c->pin[DRV8962_PIN_IN1 + b1], IO_PWM_DUTY_MAX));
      ERR_TRY(sys_io_set_pwm_duty(c->pin[DRV8962_PIN_IN1 + b2], comp_duty));
    } else {
      // Reverse (slow decay): IN1 = the PWM complement, IN2 = 100 %
      ERR_TRY(sys_io_set_pwm_duty(c->pin[DRV8962_PIN_IN1 + b1], comp_duty));
      ERR_TRY(sys_io_set_pwm_duty(c->pin[DRV8962_PIN_IN1 + b2], IO_PWM_DUTY_MAX));
    }
  } else {
    ERR_TRY(enable_bridges(c, channel, channel));
    ERR_TRY(sys_io_set_pwm_duty(c->pin[DRV8962_PIN_IN1 + channel], drive_to_duty(drive)));
  }
  c->ch[channel].drive = drive;
  return NULL;
}

/* VREF sets the chopping threshold of the whole chip (ITRIP x AIPROPI = VREF / RIPROPI). It follows the largest
   channel limit, each seen through its own IPROPI resistor; no limit anywhere = no regulation (VREF at its 3.3 V
   maximum). */
static err_h chip_apply_current_limit(drv_t* c) {
  if (!has_pin(c, DRV8962_PIN_VREF)) {
    return NULL;
  }
  uint64_t vref_mV = 0;
  for (uint8_t ch = 0; ch < channel_count(c); ch++) {
    uint8_t first, last;
    channel_bridges(c, ch, &first, &last);
    const uint64_t mV = (uint64_t)c->current_limit_mA[ch] * AIPROPI_UA_PER_A * c->ripropi_ohms[first] / 1000000u;
    if (mV > vref_mV) {
      vref_mV = mV;
    }
  }
  if (vref_mV == 0 || vref_mV > VREF_MAX_MV) {
    vref_mV = VREF_MAX_MV;
  } else if (vref_mV < VREF_MIN_MV) {
    vref_mV = VREF_MIN_MV;
  }
  return sys_io_set_voltage(c->pin[DRV8962_PIN_VREF], (int32_t)vref_mV);
}

/* ---- Faults ---- */

// Latches the fault and brakes the channel (turn_off_at_ocp). The caller publishes it after the state is final.
static err_h mark_channel_fault(drv_t* c, uint8_t channel, uint8_t reason) {
  c->ch[channel].fault = true;
  c->ch[channel].reason = reason;
  return c->turn_off_at_ocp ? chip_brake(c, channel) : NULL;
}

static err_h publish_channel_fault(const drv_t* c, uint8_t channel) {
  const channel_t* st = &c->ch[channel];
  const int32_t mA = st->last_current_mA;
  switch (st->reason) {
    case SYS_HBRIDGE_FAULT_OVERCURRENT:
      return SYS_EVENT_PUBLISH(HBRIDGE, OVERCURRENT, c->base.id, channel, mA);
    case SYS_HBRIDGE_FAULT_THERMAL_OR_UVLO:
      return SYS_EVENT_PUBLISH(HBRIDGE, THERMAL_OR_UVLO, c->base.id, channel, mA);
    default:
      return SYS_EVENT_PUBLISH(HBRIDGE, EXTERNAL, c->base.id, channel, mA);
  }
}

/* nFAULT fell: finds the faulted channels, then publishes them. The chip says only "something is wrong", so the
   currents decide: a channel at its limit, else the one channel that was driving, else a chip-wide shutdown (thermal
   or supply). The publish comes last: an inline listener may call back into this device. */
EVENT_HANDLER(ON_EVENT) {
  (void)a;
  drv_t* c = ctx;
  if (c->base.suspended) {
    return NULL;  // coasted already; the fault line may still be low
  }
  const uint8_t n = channel_count(c);
  uint8_t active_count = 0;
  int last_active = -1;
  uint8_t over_count = 0;
  uint8_t faulted = 0;  // bit per channel
  err_h err = NULL;

  // Sample every channel (a failed read counts as 0 mA). The classification below dispatches each fault once.
  for (uint8_t ch = 0; ch < n; ch++) {
    int32_t mA = 0;
    SYS_DEVICE_KEEP_FIRST(err, read_channel_current_mA(c, ch, &mA));
    const int16_t drive = c->ch[ch].drive;
    if (drive >= DRIVE_DEADBAND || drive <= -DRIVE_DEADBAND) {
      active_count++;
      last_active = ch;
    }
    if (current_at_or_over(mA, c->current_limit_mA[ch])) {
      over_count++;
    }
  }

  if (over_count > 0) {
    for (uint8_t ch = 0; ch < n; ch++) {
      if (current_at_or_over(c->ch[ch].last_current_mA, c->current_limit_mA[ch])) {
        SYS_DEVICE_KEEP_FIRST(err, mark_channel_fault(c, ch, SYS_HBRIDGE_FAULT_OVERCURRENT));
        faulted |= (uint8_t)(1u << ch);
      }
    }
  } else if (active_count == 1 && last_active >= 0) {
    // Exactly one channel was driving when the fault occurred: the culprit.
    SYS_DEVICE_KEEP_FIRST(err, mark_channel_fault(c, (uint8_t)last_active, SYS_HBRIDGE_FAULT_OVERCURRENT));
    faulted |= (uint8_t)(1u << last_active);
  } else {
    // Chip-wide thermal shutdown (OTSD) or supply UVLO.
    for (uint8_t ch = 0; ch < n; ch++) {
      SYS_DEVICE_KEEP_FIRST(err, mark_channel_fault(c, ch, SYS_HBRIDGE_FAULT_THERMAL_OR_UVLO));
      faulted |= (uint8_t)(1u << ch);
    }
  }

  for (uint8_t ch = 0; ch < n; ch++) {
    if (faulted & (1u << ch)) {
      SYS_DEVICE_KEEP_FIRST(err, publish_channel_fault(c, ch));
    }
  }
  return err;
}

/* ---- HBRIDGE contract ---- */

HBRIDGE_HANDLER(SET_DRIVE) {
  drv_t* c = ctx;
  ERR_TRY(sys_device_check_active(&c->base));
  ERR_TRY(check_channel(c, a->channel));
  const int32_t min = c->topology == DRV8962_TOPOLOGY_2_FULL_BRIDGES ? -SYS_HBRIDGE_DRIVE_MAX : 0;
  ERR_CHECK(a->drive >= min && a->drive <= SYS_HBRIDGE_DRIVE_MAX, ERR_DEV_RANGE, .id = c->base.id, .value = a->drive,
            .min = min, .max = SYS_HBRIDGE_DRIVE_MAX);
  return chip_set_drive(c, a->channel, a->drive);
}

HBRIDGE_HANDLER(BRAKE) {
  drv_t* c = ctx;
  ERR_TRY(sys_device_check_active(&c->base));
  ERR_TRY(check_channel(c, a->channel));
  return chip_brake(c, a->channel);
}

HBRIDGE_HANDLER(COAST) {
  drv_t* c = ctx;
  ERR_TRY(sys_device_check_active(&c->base));
  ERR_TRY(check_channel(c, a->channel));
  return chip_coast(c, a->channel);
}

/* A read, allowed while suspended. A channel over its limit is latched and braked here too (the fault line is edge
   triggered: a reading may be the first to see it) and published. */
HBRIDGE_HANDLER(GET_CURRENT) {
  drv_t* c = ctx;
  ERR_TRY(check_channel(c, a->channel));
  int32_t mA = 0;
  ERR_TRY(read_channel_current_mA(c, a->channel, &mA));
  const uint32_t limit = c->current_limit_mA[a->channel];
  if (!c->base.suspended && !c->ch[a->channel].fault && limit > 0 && mA > 0 && (uint32_t)mA > limit) {
    ERR_TRY(mark_channel_fault(c, a->channel, SYS_HBRIDGE_FAULT_OVERCURRENT));
    ERR_TRY(publish_channel_fault(c, a->channel));
  }
  return cmd_reply(out, out_cap, out_len, &mA, sizeof(mA));
}

/* ---- Own ops ---- */

SYS_DEVICE_HANDLER(set_current_limit_h, drv8962_limit_t) {
  (void)out;
  (void)out_cap;
  (void)out_len;
  drv_t* c = ctx;
  ERR_TRY(sys_device_check_active(&c->base));
  ERR_TRY(check_channel(c, a->channel));
  ERR_CHECK(a->limit_mA <= DRV8962_CURRENT_LIMIT_MAX_MA, ERR_DEV_RANGE, .id = c->base.id, .value = (int32_t)a->limit_mA,
            .min = 0, .max = DRV8962_CURRENT_LIMIT_MAX_MA);
  const uint32_t old = c->current_limit_mA[a->channel];
  c->current_limit_mA[a->channel] = a->limit_mA;
  err_h err = chip_apply_current_limit(c);
  if (err != NULL) {
    c->current_limit_mA[a->channel] = old;  // not applied: not kept
  }
  return err;
}

// nSLEEP low for 30 us resets the chip's fault latches (every channel's); only the asked channel is cleared here.
SYS_DEVICE_HANDLER(clear_fault_h, hbridge_channel_args_t) {
  (void)out;
  (void)out_cap;
  (void)out_len;
  drv_t* c = ctx;
  ERR_TRY(sys_device_check_active(&c->base));
  ERR_TRY(check_channel(c, a->channel));
  if (has_pin(c, DRV8962_PIN_NSLEEP)) {
    ERR_TRY(set_locked_pin(c, DRV8962_PIN_NSLEEP, false));
    esp_rom_delay_us(SLEEP_PULSE_US);
    ERR_TRY(set_locked_pin(c, DRV8962_PIN_NSLEEP, true));
    esp_rom_delay_us(SLEEP_WAKE_US);
  }
  c->ch[a->channel].fault = false;
  c->ch[a->channel].reason = SYS_HBRIDGE_FAULT_NONE;
  return chip_brake(c, a->channel);
}

SYS_DEVICE_HANDLER(get_fault_h, hbridge_channel_args_t) {
  const drv_t* c = ctx;
  ERR_TRY(check_channel(c, a->channel));
  const drv8962_fault_t fault = {.fault = c->ch[a->channel].fault, .reason = c->ch[a->channel].reason};
  return cmd_reply(out, out_cap, out_len, &fault, sizeof(fault));
}

/* ---- Lifecycle ---- */

// Releases the hardware; REMOVE and the create rollback. Each step runs only if it was built, none returns early.
static err_h uninstall(void* ctx) {
  drv_t* c = ctx;
  err_h err = NULL;
  if (c->nfault_subscribed) {
    SYS_DEVICE_KEEP_FIRST(err, sys_event_unsubscribe(c->nfault_sub));
    c->nfault_subscribed = false;
  }
  if (c->started) {
    for (uint8_t i = 0; i < channel_count(c); i++) {
      SYS_DEVICE_KEEP_FIRST(err, chip_coast(c, i));
    }
    c->started = false;
  }
  for (uint8_t idx = 0; idx < DRV8962_PINS; idx++) {
    if (c->locked_mask & (1u << idx)) {
      SYS_DEVICE_KEEP_FIRST(err, sys_pins_unlock(c->pin[idx]));
    }
    if (c->used_mask & (1u << idx)) {
      SYS_DEVICE_KEEP_FIRST(err, sys_io_release(c->pin[idx]));
    }
  }
  c->locked_mask = 0;
  c->used_mask = 0;
  return err;
}

// Every channel braked (outputs on, inputs low).
DEVICE_HANDLER(RESET) {
  drv_t* c = ctx;
  ERR_TRY(sys_device_check_active(&c->base));
  err_h err = NULL;
  for (uint8_t i = 0; i < channel_count(c); i++) {
    SYS_DEVICE_KEEP_FIRST(err, chip_brake(c, i));
  }
  return err;
}

// Fault safe state: every channel coasted, every one reached even if another fails.
DEVICE_HANDLER(SUSPEND) {
  drv_t* c = ctx;
  err_h err = NULL;
  c->base.suspended = true;
  for (uint8_t i = 0; i < channel_count(c); i++) {
    SYS_DEVICE_KEEP_FIRST(err, chip_coast(c, i));
  }
  return err;
}

// The channels stay coasted: the next drive command engages them.
DEVICE_HANDLER(RESUME) {
  drv_t* c = ctx;
  c->base.suspended = false;
  return NULL;
}

/* ---- Create ---- */

// Marks a pin as made ours (released on uninstall) after the mode is set.
static err_h use_pin(drv_t* c, uint8_t idx, sys_io_mode_e mode) {
  ERR_TRY(sys_io_prepare(c->pin[idx], mode));
  c->used_mask |= (uint16_t)(1u << idx);
  return NULL;
}

static err_h lock_pin(drv_t* c, uint8_t idx) {
  ERR_TRY(sys_pins_lock(c->pin[idx]));
  c->locked_mask |= (uint16_t)(1u << idx);
  return NULL;
}

static err_h check_cfg(const drv_t* c, const drv8962_cfg_t* cfg) {
  const uint16_t id = c->base.id;
  ERR_CHECK(cfg->topology <= DRV8962_TOPOLOGY_4_HALF_BRIDGES, ERR_DEV_RANGE, .id = id, .value = cfg->topology, .min = 0,
            .max = DRV8962_TOPOLOGY_4_HALF_BRIDGES);
  ERR_CHECK(cfg->turn_off_at_ocp <= 1, ERR_CMD_BAD_ARG);
  for (uint8_t i = 0; i < HALF_BRIDGES; i++) {
    ERR_CHECK(cfg->current_limit_mA[i] <= DRV8962_CURRENT_LIMIT_MAX_MA, ERR_DEV_RANGE, .id = id,
              .value = (int32_t)cfg->current_limit_mA[i], .min = 0, .max = DRV8962_CURRENT_LIMIT_MAX_MA);
  }
  return NULL;
}

// The registry allocated `ctx` (id set); on a failure it calls uninstall() and frees it.
static err_h create(void* ctx, const void* cfg_in) {
  drv_t* c = ctx;
  const drv8962_cfg_t* cfg = cfg_in;
  ERR_TRY(check_cfg(c, cfg));
  c->topology = cfg->topology;
  c->turn_off_at_ocp = cfg->turn_off_at_ocp != 0;
  c->pwm_freq_Hz = cfg->pwm_freq_Hz ? cfg->pwm_freq_Hz : PWM_FREQ_DEFAULT_HZ;
  memcpy(c->pin, cfg->pin, sizeof(c->pin));
  for (uint8_t i = 0; i < HALF_BRIDGES; i++) {
    c->ripropi_ohms[i] = cfg->ripropi_ohms[i] ? cfg->ripropi_ohms[i] : RIPROPI_DEFAULT_OHMS;
    c->current_limit_mA[i] = cfg->current_limit_mA[i];
  }
  for (uint8_t idx = 0; idx < DRV8962_PINS; idx++) {
    if (has_pin(c, idx)) {
      ERR_TRY(sys_device_use(&c->base, c->pin[idx].device));
    }
  }

  // 1. IN pins: PWM at the chip's frequency, off
  for (uint8_t i = 0; i < HALF_BRIDGES; i++) {
    const uint8_t idx = DRV8962_PIN_IN1 + i;
    if (!has_pin(c, idx)) {
      continue;
    }
    ERR_TRY(use_pin(c, idx, SYS_IO_MODE_PWM));
    ERR_TRY(sys_io_set_pwm_frequency(c->pin[idx], c->pwm_freq_Hz));
    ERR_TRY(sys_io_set_pwm_duty(c->pin[idx], 0));
  }
  // 2. EN pins: outputs, locked (low = coasting until the first brake)
  for (uint8_t i = 0; i < HALF_BRIDGES; i++) {
    const uint8_t idx = DRV8962_PIN_EN1 + i;
    if (!has_pin(c, idx)) {
      continue;
    }
    ERR_TRY(use_pin(c, idx, SYS_IO_MODE_OUTPUT_PUSH_PULL));
    ERR_TRY(sys_io_set_level(c->pin[idx], false));
    ERR_TRY(lock_pin(c, idx));
  }
  // 3. nSLEEP: high wakes the chip
  if (has_pin(c, DRV8962_PIN_NSLEEP)) {
    ERR_TRY(use_pin(c, DRV8962_PIN_NSLEEP, SYS_IO_MODE_OUTPUT_PUSH_PULL));
    ERR_TRY(sys_io_set_level(c->pin[DRV8962_PIN_NSLEEP], true));
    ERR_TRY(lock_pin(c, DRV8962_PIN_NSLEEP));
    vTaskDelay(MSEC(WAKE_DELAY_MS));
  }
  // 4. IPROPI: ADC
  for (uint8_t i = 0; i < HALF_BRIDGES; i++) {
    if (has_pin(c, DRV8962_PIN_IPROPI1 + i)) {
      ERR_TRY(use_pin(c, DRV8962_PIN_IPROPI1 + i, SYS_IO_MODE_ADC));
    }
  }
  // 5. VREF: driven (a floating VREF sets an undefined chopping threshold)
  if (has_pin(c, DRV8962_PIN_VREF)) {
    ERR_TRY(use_pin(c, DRV8962_PIN_VREF, SYS_IO_MODE_DAC));
    ERR_TRY(chip_apply_current_limit(c));
  }
  // 6. every channel braked, then nFAULT (its falling edge reaches our ON_EVENT: inline, ADC reads, no delay)
  c->started = true;
  for (uint8_t i = 0; i < channel_count(c); i++) {
    ERR_TRY(chip_brake(c, i));
  }
  if (has_pin(c, DRV8962_PIN_NFAULT)) {
    ERR_TRY(sys_io_prepare_input(c->pin[DRV8962_PIN_NFAULT]));
    c->used_mask |= (uint16_t)(1u << DRV8962_PIN_NFAULT);
    const sys_event_sub_t sub = {.source = c->pin[DRV8962_PIN_NFAULT].device, .event = IO_EV_FALLING,
                                 .channel = c->pin[DRV8962_PIN_NFAULT].pin, .inline_call = true, .device = c->base.id};
    ERR_TRY(sys_event_subscribe(&sub, &c->nfault_sub));
    c->nfault_subscribed = true;
    const sys_io_intr_cfg_t intr_cfg = {.mode = SYS_IO_INTR_MODE_FALLING_EDGE};
    ERR_TRY(sys_io_configure_intr(c->pin[DRV8962_PIN_NFAULT], &intr_cfg));
    ERR_TRY(lock_pin(c, DRV8962_PIN_NFAULT));
  }
  return NULL;
}

/* ---- Type ---- */

// An op not listed (a contract the DRV8962 doesn't have) answers ERR_DEV_UNKNOWN_OP; OPS lists this table.
static const sys_device_op_t k_ops[] = {
    DEVICE_ENTRY(RESET),
    DEVICE_ENTRY(SUSPEND),
    DEVICE_ENTRY(RESUME),
    HBRIDGE_ENTRY(SET_DRIVE),
    HBRIDGE_ENTRY(BRAKE),
    HBRIDGE_ENTRY(COAST),
    HBRIDGE_ENTRY(GET_CURRENT),
    EVENT_ENTRY(ON_EVENT),
    //@op DRV8962_OP_SET_CURRENT_LIMIT @alias Set current limit @description The chopping current limit of a channel; VREF follows the largest
    {DRV8962_OP_SET_CURRENT_LIMIT, set_current_limit_h},
    //@op DRV8962_OP_CLEAR_FAULT @alias Clear fault @description Clears the chip's fault latches with an nSLEEP reset pulse; the channel brakes
    {DRV8962_OP_CLEAR_FAULT, clear_fault_h},
    //@op DRV8962_OP_GET_FAULT @alias Read fault @description Whether a channel has a fault, and why @returns drv8962_fault_t
    {DRV8962_OP_GET_FAULT, get_fault_h},
};

SYS_DEVICE_TYPE_DEFINE(.type = SYS_DEVICE_TYPE_DRV8962, .cfg_size = sizeof(drv8962_cfg_t), .ctx_size = sizeof(drv_t),
                       .create = create, .uninstall = uninstall, SYS_DEVICE_OPS(k_ops))
