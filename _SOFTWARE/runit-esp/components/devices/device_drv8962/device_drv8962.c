#include "device_drv8962.h"

#include "esp_rom_sys.h"
#include "sys_event.h"
#include "sys_pins.h"
#include "utils.h"

#define HALF_BRIDGES         4u
#define PWM_FREQ_DEFAULT_HZ  20000u
#define RIPROPI_DEFAULT_OHMS 3090u
#define DRIVE_DEADBAND       20  // a full-bridge drive below this (0.5 %) brakes
#define WAKE_DELAY_MS        2u  // tWAKE: 1.2 ms max before the inputs are taken (a bounded wait, like the TPS55289's EN)
#define SLEEP_PULSE_US       30u      // nSLEEP reset pulse that clears the fault latches
#define SLEEP_WAKE_US        1000u     // and the wait after it
#define VREF_MIN_MV          50u         // a floating VREF sets an undefined chopping threshold: always drive 50 .. 3300
#define VREF_MAX_MV          3300u
#define AIPROPI_UA_PER_A     212u   // IPROPI output: 212 uA per A (datasheet), so I(mA) = V(mV) * 10^6 / (212 * R)
#define IPROPI_HYSTERESIS_PCT 10u   // an IPROPI window re-arms once the voltage is this far back under the limit's
#define IPROPI_SAMPLES        2u    // ADC readings in a row over the limit before the event (one PWM spike does not count)

typedef struct {
  int16_t drive;  // PWM steps (SYS_HBRIDGE_DRIVE_MAX = full)
  int32_t last_current_mA;
  bool fault;
  uint8_t reason;  // sys_hbridge_fault_reason_e
} channel_t;

typedef struct {
  sys_device_base_t base;  // first: id, suspended (every channel coasted)
  bool started;            // the channels were braked once: uninstall coasts them
  uint8_t topology;        // drv8962_topology_e
  bool turn_off_at_ocp;
  uint32_t pwm_freq_Hz;
  /* Every signal, as taken (sys_pins.h): EN, nSLEEP and nFAULT locked (EN and nSLEEP driven with sys_pins_drive),
     IPROPI locked once its window is armed, IN and VREF not (their PWM duty and voltage are IO writes). */
  sys_pins_owned_t pin[DRV8962_PINS];
  uint32_t ripropi_ohms[HALF_BRIDGES];
  uint32_t current_limit_mA;  // the chip's, every channel (one VREF); 0 = none
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
  return sys_io_pin_valid(c->pin[idx].pin);
}

// The channel a half bridge belongs to.
static uint8_t bridge_channel(const drv_t* c, uint8_t bridge) {
  return c->topology == DRV8962_TOPOLOGY_2_FULL_BRIDGES ? (uint8_t)(bridge / 2u) : bridge;
}

static err_h check_channel(const drv_t* c, uint8_t channel) {
  ERR_CHECK(channel < channel_count(c), ERR_DEV_BAD_PIN, .id = c->base.id, .pin = channel);
  return NULL;
}

// Every half bridge of the channel has its IPROPI pin: its current can be measured.
static bool channel_sensed(const drv_t* c, uint8_t channel) {
  uint8_t first, last;
  channel_bridges(c, channel, &first, &last);
  for (uint8_t i = first; i <= last; i++) {
    if (!has_pin(c, DRV8962_PIN_IPROPI1 + i)) {
      return false;
    }
  }
  return true;
}

// IPROPI of a half bridge as mA (0 without the pin).
static err_h sample_current_mA(const drv_t* c, uint8_t bridge, int32_t* out_mA) {
  *out_mA = 0;
  const uint8_t idx = DRV8962_PIN_IPROPI1 + bridge;
  if (!has_pin(c, idx)) {
    return NULL;
  }
  int32_t mV = 0;
  ERR_TRY(sys_io_get_voltage(c->pin[idx].pin, &mV));
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

/* Outputs on (brake) or off (coast): EN and IN of every half bridge of the channel. Every pin is written even if an
   earlier write fails (the safe-state path); the first failure is returned. */
static err_h chip_set_outputs(drv_t* c, uint8_t channel, bool enabled) {
  uint8_t first, last;
  channel_bridges(c, channel, &first, &last);
  err_h err = NULL;
  for (uint8_t i = first; i <= last; i++) {
    ERR_KEEP_FIRST(err, sys_pins_drive(&c->pin[DRV8962_PIN_EN1 + i], enabled));
    if (has_pin(c, DRV8962_PIN_IN1 + i)) {
      ERR_KEEP_FIRST(err, sys_io_set_pwm_duty(c->pin[DRV8962_PIN_IN1 + i].pin, 0));
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
    ERR_TRY(sys_pins_drive(&c->pin[DRV8962_PIN_EN1 + i], true));
  }
  return NULL;
}

static err_h set_in_duty(const drv_t* c, uint8_t bridge, uint32_t duty) {
  return sys_io_set_pwm_duty(c->pin[DRV8962_PIN_IN1 + bridge].pin, duty);
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
      ERR_TRY(set_in_duty(c, b1, IO_PWM_DUTY_MAX));
      ERR_TRY(set_in_duty(c, b2, comp_duty));
    } else {
      // Reverse (slow decay): IN1 = the PWM complement, IN2 = 100 %
      ERR_TRY(set_in_duty(c, b1, comp_duty));
      ERR_TRY(set_in_duty(c, b2, IO_PWM_DUTY_MAX));
    }
  } else {
    ERR_TRY(enable_bridges(c, channel, channel));
    ERR_TRY(set_in_duty(c, channel, drive_to_duty(drive)));
  }
  c->ch[channel].drive = drive;
  return NULL;
}

/* VREF sets the chopping threshold of the whole chip (ITRIP x AIPROPI = VREF / RIPROPI): one limit for every half
   bridge. Set through the smallest IPROPI resistor, so no half bridge passes the limit (one with a larger resistor trips
   lower); no limit = no regulation (VREF at its 3.3 V maximum). */
static err_h chip_apply_current_limit(drv_t* c) {
  if (!has_pin(c, DRV8962_PIN_VREF)) {
    return NULL;
  }
  uint32_t ripropi_min = c->ripropi_ohms[0];
  for (uint8_t i = 1; i < HALF_BRIDGES; i++) {
    ripropi_min = c->ripropi_ohms[i] < ripropi_min ? c->ripropi_ohms[i] : ripropi_min;
  }
  uint64_t vref_mV = (uint64_t)c->current_limit_mA * AIPROPI_UA_PER_A * ripropi_min / 1000000u;
  if (vref_mV == 0 || vref_mV > VREF_MAX_MV) {
    vref_mV = VREF_MAX_MV;
  } else if (vref_mV < VREF_MIN_MV) {
    vref_mV = VREF_MIN_MV;
  }
  return sys_io_set_voltage(c->pin[DRV8962_PIN_VREF].pin, (int32_t)vref_mV);
}

/* The IPROPI window of a half bridge: the ADC pin's rising threshold at the voltage of the current limit (an event
   after IPROPI_SAMPLES readings over it, re-armed IPROPI_HYSTERESIS_PCT under it); none without a limit. The pin is
   locked to this device; a locked pin refuses IO writes, so it is unlocked for the change. */
static err_h arm_ipropi(drv_t* c, uint8_t bridge) {
  sys_pins_owned_t* p = &c->pin[DRV8962_PIN_IPROPI1 + bridge];
  if (!p->used) {
    return NULL;
  }
  const uint64_t mV = (uint64_t)c->current_limit_mA * AIPROPI_UA_PER_A * c->ripropi_ohms[bridge] / 1000000u;
  sys_io_intr_cfg_t cfg = {.mode = SYS_IO_INTR_DISABLE};
  if (mV > 0) {
    const uint16_t up = mV > UINT16_MAX ? UINT16_MAX : (uint16_t)mV;
    cfg = (sys_io_intr_cfg_t){.mode = SYS_IO_INTR_MODE_RISING_EDGE,
                              .adc = {.threshold_up_mV = up, .hysteresis_mV = (uint16_t)(up * IPROPI_HYSTERESIS_PCT / 100u),
                                      .event_counter_threshold = IPROPI_SAMPLES}};
  }
  if (p->locked) {
    ERR_TRY(sys_pins_unlock(p->pin));
    p->locked = false;
  }
  ERR_TRY(sys_io_configure_intr(p->pin, &cfg));
  ERR_TRY(sys_pins_lock(p->pin));
  p->locked = true;
  return NULL;
}

// Every IPROPI window follows the limit; each one is tried even if another fails.
static err_h arm_ipropi_all(drv_t* c) {
  err_h err = NULL;
  for (uint8_t b = 0; b < HALF_BRIDGES; b++) {
    ERR_KEEP_FIRST(err, arm_ipropi(c, b));
  }
  return err;
}

// The chip's limit to VREF; nothing is kept when that fails. The IPROPI windows are the caller's to re-arm.
static err_h apply_limit(drv_t* c, uint32_t limit_mA) {
  const uint32_t old = c->current_limit_mA;
  c->current_limit_mA = limit_mA;
  err_h err = chip_apply_current_limit(c);
  if (err != NULL) {
    c->current_limit_mA = old;
  }
  return err;
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

/* A channel found over its limit (an IPROPI window, a CURRENT read: the fault line is edge triggered, so either may be
   the first to see it): latched, braked with turn_off_at_ocp, published. Latched until CLEAR_FAULT: a faulted channel
   publishes nothing more. */
static err_h latch_overcurrent(drv_t* c, uint8_t channel) {
  if (c->ch[channel].fault) {
    return NULL;
  }
  err_h err = mark_channel_fault(c, channel, SYS_HBRIDGE_FAULT_OVERCURRENT);
  ERR_KEEP_FIRST(err, publish_channel_fault(c, channel));
  return err;
}

// An IPROPI pin went over the limit: its channel's current read (a failed read publishes the last one), then latched.
static err_h on_ipropi_over(drv_t* c, uint8_t bridge) {
  const uint8_t ch = bridge_channel(c, bridge);
  if (c->ch[ch].fault) {
    return NULL;
  }
  int32_t mA = 0;
  err_h err = read_channel_current_mA(c, ch, &mA);
  ERR_KEEP_FIRST(err, latch_overcurrent(c, ch));
  return err;
}

/* nFAULT fell: finds the faulted channels, then publishes them. The chip says only "something is wrong", so the
   currents decide: a channel at its limit, else the one channel that was driving, else a chip-wide shutdown (thermal
   or supply). The publish comes last: an inline listener may call back into this device. */
static err_h on_nfault(drv_t* c) {
  const uint8_t n = channel_count(c);
  uint8_t active_count = 0;
  int last_active = -1;
  uint8_t over_count = 0;
  uint8_t faulted = 0;  // bit per channel
  err_h err = NULL;

  // Sample every channel (a failed read counts as 0 mA). The classification below dispatches each fault once.
  for (uint8_t ch = 0; ch < n; ch++) {
    int32_t mA = 0;
    ERR_KEEP_FIRST(err, read_channel_current_mA(c, ch, &mA));
    const int16_t drive = c->ch[ch].drive;
    if (drive >= DRIVE_DEADBAND || drive <= -DRIVE_DEADBAND) {
      active_count++;
      last_active = ch;
    }
    if (current_at_or_over(mA, c->current_limit_mA)) {
      over_count++;
    }
  }

  if (over_count > 0) {
    for (uint8_t ch = 0; ch < n; ch++) {
      if (current_at_or_over(c->ch[ch].last_current_mA, c->current_limit_mA)) {
        ERR_KEEP_FIRST(err, mark_channel_fault(c, ch, SYS_HBRIDGE_FAULT_OVERCURRENT));
        faulted |= (uint8_t)(1u << ch);
      }
    }
  } else if (active_count == 1 && last_active >= 0) {
    // Exactly one channel was driving when the fault occurred: the culprit.
    ERR_KEEP_FIRST(err, mark_channel_fault(c, (uint8_t)last_active, SYS_HBRIDGE_FAULT_OVERCURRENT));
    faulted |= (uint8_t)(1u << last_active);
  } else {
    // Chip-wide thermal shutdown (OTSD) or supply UVLO.
    for (uint8_t ch = 0; ch < n; ch++) {
      ERR_KEEP_FIRST(err, mark_channel_fault(c, ch, SYS_HBRIDGE_FAULT_THERMAL_OR_UVLO));
      faulted |= (uint8_t)(1u << ch);
    }
  }

  for (uint8_t ch = 0; ch < n; ch++) {
    if (faulted & (1u << ch)) {
      ERR_KEEP_FIRST(err, publish_channel_fault(c, ch));
    }
  }
  return err;
}

// An IPROPI window (its pin's RISING) or nFAULT (its FALLING). Nothing while suspended: coasted already, and the
// fault line may still be low.
OP_WRITE(EVENT, ON_EVENT) {
  drv_t* c = ctx;
  if (c->base.suspended) {
    return NULL;
  }
  for (uint8_t b = 0; b < HALF_BRIDGES; b++) {
    const sys_io_pin_t p = c->pin[DRV8962_PIN_IPROPI1 + b].pin;
    if (has_pin(c, DRV8962_PIN_IPROPI1 + b) && args->event == IO_EV_RISING && args->source == p.device &&
        args->channel == p.pin) {
      return on_ipropi_over(c, b);
    }
  }
  return on_nfault(c);
}

/* ---- HBRIDGE contract ---- */

OP_WRITE(HBRIDGE, DRIVE) {
  drv_t* c = ctx;
  ERR_TRY(check_channel(c, args->channel));
  const int32_t min = c->topology == DRV8962_TOPOLOGY_2_FULL_BRIDGES ? -SYS_HBRIDGE_DRIVE_MAX : 0;
  SYS_DEV_CHECK_IN_RANGE(c->base.id, args->drive, min, SYS_HBRIDGE_DRIVE_MAX);
  return chip_set_drive(c, args->channel, args->drive);
}

OP_WRITE(HBRIDGE, BRAKE) {
  drv_t* c = ctx;
  ERR_TRY(check_channel(c, args->channel));
  return chip_brake(c, args->channel);
}

OP_WRITE(HBRIDGE, COAST) {
  drv_t* c = ctx;
  ERR_TRY(check_channel(c, args->channel));
  return chip_coast(c, args->channel);
}

// A read, answered while suspended. A channel over its limit is latched here too (latch_overcurrent).
OP_READ(HBRIDGE, CURRENT) {
  drv_t* c = ctx;
  ERR_TRY(check_channel(c, args->channel));
  int32_t mA = 0;
  ERR_TRY(read_channel_current_mA(c, args->channel, &mA));
  if (!c->base.suspended && current_at_or_over(mA, c->current_limit_mA)) {
    ERR_TRY(latch_overcurrent(c, args->channel));
  }
  return cmd_reply(out, &mA, sizeof(mA));
}

/* ---- POWER_MONITOR: a channel's current from its IPROPI pins (full bridge: the larger half bridge). Offered only with
   an IPROPI pin wired (has_op); a channel without its pins answers ERR_POWER_BAD_CHANNEL. A read, answered while
   suspended. ---- */

OP_READ(PMON, CURRENT) {
  drv_t* c = ctx;
  ERR_CHECK(args->channel < channel_count(c) && channel_sensed(c, args->channel), ERR_POWER_BAD_CHANNEL,
            .id = c->base.id, .channel = args->channel);
  int32_t mA = 0;
  ERR_TRY(read_channel_current_mA(c, args->channel, &mA));
  return cmd_reply(out, &mA, sizeof(mA));
}

// The POWER_MONITOR ops need an IPROPI pin; the others are always there.
static bool has_op(const void* ctx, uint16_t op) {
  const drv_t* c = ctx;
  if ((op >> 8) != SYS_DEVICE_CONTRACT_POWER_MONITOR) {
    return true;
  }
  for (uint8_t i = 0; i < HALF_BRIDGES; i++) {
    if (has_pin(c, DRV8962_PIN_IPROPI1 + i)) {
      return true;
    }
  }
  return false;
}

/* ---- Own ops ---- */

SYS_DEVICE_HANDLER(set_current_limit_h, drv8962_limit_t) {
  (void)out;
  drv_t* c = ctx;
  SYS_DEV_CHECK_IN_RANGE(c->base.id, args->limit_mA, 0, DRV8962_CURRENT_LIMIT_MAX_MA);
  ERR_TRY(apply_limit(c, args->limit_mA));
  return arm_ipropi_all(c);
}

// nSLEEP low for 30 us resets the chip's fault latches (every channel's); only the asked channel is cleared here.
SYS_DEVICE_HANDLER(clear_fault_h, hbridge_channel_args_t) {
  (void)out;
  drv_t* c = ctx;
  ERR_TRY(check_channel(c, args->channel));
  if (has_pin(c, DRV8962_PIN_NSLEEP)) {
    ERR_TRY(sys_pins_drive(&c->pin[DRV8962_PIN_NSLEEP], false));
    esp_rom_delay_us(SLEEP_PULSE_US);
    ERR_TRY(sys_pins_drive(&c->pin[DRV8962_PIN_NSLEEP], true));
    esp_rom_delay_us(SLEEP_WAKE_US);
  }
  c->ch[args->channel].fault = false;
  c->ch[args->channel].reason = SYS_HBRIDGE_FAULT_NONE;
  return chip_brake(c, args->channel);
}

SYS_DEVICE_HANDLER(get_fault_h, hbridge_channel_args_t) {
  const drv_t* c = ctx;
  ERR_TRY(check_channel(c, args->channel));
  const drv8962_fault_t fault = {.fault = c->ch[args->channel].fault, .reason = c->ch[args->channel].reason};
  return cmd_reply(out, &fault, sizeof(fault));
}

// The live settings, as the write takes them. A read: answered while suspended.
SYS_DEVICE_HANDLER(get_settings_h, sys_device_none_t) {
  (void)args;
  const drv_t* c = ctx;
  const drv8962_settings_t s = {
      .topology = c->topology, .current_limit_mA = c->current_limit_mA, .turn_off_at_ocp = c->turn_off_at_ocp};
  return cmd_reply(out, &s, sizeof(s));
}

/* The settings create takes, changed without a new CREATE. The limit goes to VREF (nothing is kept when that fails). A
   new topology coasts every channel of the old one, forgets their state (a channel number is another bridge now) and
   brakes every channel of the new one, as create does. */
SYS_DEVICE_HANDLER(set_settings_h, drv8962_settings_t) {
  (void)out;
  drv_t* c = ctx;
  SYS_DEV_CHECK_IN_RANGE(c->base.id, args->topology, 0, DRV8962_TOPOLOGY_4_HALF_BRIDGES);
  SYS_DEV_CHECK_IN_RANGE(c->base.id, args->current_limit_mA, 0, DRV8962_CURRENT_LIMIT_MAX_MA);
  ERR_CHECK(args->turn_off_at_ocp <= 1, ERR_CMD_BAD_ARG);
  ERR_TRY(apply_limit(c, args->current_limit_mA));
  c->turn_off_at_ocp = args->turn_off_at_ocp != 0;
  err_h err = arm_ipropi_all(c);
  if (args->topology != c->topology) {
    for (uint8_t i = 0; i < channel_count(c); i++) {
      ERR_KEEP_FIRST(err, chip_coast(c, i));
    }
    c->topology = args->topology;
    memset(c->ch, 0, sizeof(c->ch));
    for (uint8_t i = 0; i < channel_count(c); i++) {
      ERR_KEEP_FIRST(err, chip_brake(c, i));
    }
  }
  return err;
}

/* ---- Lifecycle ---- */

// Releases the hardware; REMOVE and the create rollback. Every channel coasted (while EN is still ours), then every
// pin released; each step runs, none returns early.
static err_h uninstall(void* ctx) {
  drv_t* c = ctx;
  err_h err = NULL;
  if (c->started) {
    for (uint8_t i = 0; i < channel_count(c); i++) {
      ERR_KEEP_FIRST(err, chip_coast(c, i));
    }
    c->started = false;
  }
  for (uint8_t idx = 0; idx < DRV8962_PINS; idx++) {
    ERR_KEEP_FIRST(err, sys_pins_release(&c->pin[idx]));
  }
  return err;
}

// Every channel braked (outputs on, inputs low).
OP_WRITE(SYS_DEVICE, RESET) {
  drv_t* c = ctx;
  err_h err = NULL;
  for (uint8_t i = 0; i < channel_count(c); i++) {
    ERR_KEEP_FIRST(err, chip_brake(c, i));
  }
  return err;
}

// Fault safe state: every channel coasted, every one reached even if another fails.
static err_h suspend_hook(void* ctx) {
  drv_t* c = ctx;
  err_h err = NULL;
  for (uint8_t i = 0; i < channel_count(c); i++) {
    ERR_KEEP_FIRST(err, chip_coast(c, i));
  }
  return err;
}

// The channels stay coasted: the next drive command engages them.
static err_h resume_hook(void* ctx) {
  (void)ctx;
  return NULL;
}

/* ---- Create ---- */

static err_h check_cfg(const drv_t* c, const drv8962_cfg_t* cfg) {
  const uint16_t id = c->base.id;
  SYS_DEV_CHECK_IN_RANGE(id, cfg->topology, 0, DRV8962_TOPOLOGY_4_HALF_BRIDGES);
  ERR_CHECK(cfg->turn_off_at_ocp <= 1, ERR_CMD_BAD_ARG);
  SYS_DEV_CHECK_IN_RANGE(id, cfg->current_limit_mA, 0, DRV8962_CURRENT_LIMIT_MAX_MA);
  return NULL;
}

static err_h create(void* ctx, const void* cfg_in) {
  drv_t* c = ctx;
  const drv8962_cfg_t* cfg = cfg_in;
  ERR_TRY(check_cfg(c, cfg));
  c->topology = cfg->topology;
  c->turn_off_at_ocp = cfg->turn_off_at_ocp != 0;
  c->pwm_freq_Hz = cfg->pwm_freq_Hz ? cfg->pwm_freq_Hz : PWM_FREQ_DEFAULT_HZ;
  for (uint8_t idx = 0; idx < DRV8962_PINS; idx++) {
    c->pin[idx].pin = cfg->pin[idx];  // what has_pin reads, before each pin is taken
  }
  for (uint8_t i = 0; i < HALF_BRIDGES; i++) {
    c->ripropi_ohms[i] = cfg->ripropi_ohms[i] ? cfg->ripropi_ohms[i] : RIPROPI_DEFAULT_OHMS;
  }
  c->current_limit_mA = cfg->current_limit_mA;

  // 1. IN pins: PWM at the chip's frequency, off
  for (uint8_t i = 0; i < HALF_BRIDGES; i++) {
    const uint8_t idx = DRV8962_PIN_IN1 + i;
    ERR_TRY(sys_pins_take(&c->base, &c->pin[idx], cfg->pin[idx], SYS_IO_MODE_PWM, 0));
    if (has_pin(c, idx)) {
      ERR_TRY(sys_io_set_pwm_frequency(cfg->pin[idx], c->pwm_freq_Hz));
      ERR_TRY(sys_io_set_pwm_duty(cfg->pin[idx], 0));
    }
  }
  // 2. EN pins: outputs, locked (low = coasting until the first brake)
  for (uint8_t i = 0; i < HALF_BRIDGES; i++) {
    const uint8_t idx = DRV8962_PIN_EN1 + i;
    ERR_TRY(sys_pins_take_output(&c->base, &c->pin[idx], cfg->pin[idx], false));
  }
  // 3. nSLEEP: high wakes the chip
  ERR_TRY(sys_pins_take_output(&c->base, &c->pin[DRV8962_PIN_NSLEEP], cfg->pin[DRV8962_PIN_NSLEEP], true));
  if (has_pin(c, DRV8962_PIN_NSLEEP)) {
    vTaskDelay(MSEC(WAKE_DELAY_MS));
  }
  // 4. IPROPI: ADC, its window's events to our ON_EVENT (inline, like nFAULT's)
  for (uint8_t i = 0; i < HALF_BRIDGES; i++) {
    const uint8_t idx = DRV8962_PIN_IPROPI1 + i;
    ERR_TRY(sys_pins_take(&c->base, &c->pin[idx], cfg->pin[idx], SYS_IO_MODE_ADC, IO_EV_RISING));
  }
  // 5. VREF: driven (a floating VREF sets an undefined chopping threshold)
  ERR_TRY(sys_pins_take(&c->base, &c->pin[DRV8962_PIN_VREF], cfg->pin[DRV8962_PIN_VREF], SYS_IO_MODE_DAC, 0));
  ERR_TRY(chip_apply_current_limit(c));
  // 6. IPROPI windows at the limit
  ERR_TRY(arm_ipropi_all(c));
  // 7. every channel braked, then nFAULT (its falling edge reaches our ON_EVENT: inline, ADC reads, no delay)
  c->started = true;
  for (uint8_t i = 0; i < channel_count(c); i++) {
    ERR_TRY(chip_brake(c, i));
  }
  return sys_pins_take_input(&c->base, &c->pin[DRV8962_PIN_NFAULT], cfg->pin[DRV8962_PIN_NFAULT], IO_EV_FALLING,
                             SYS_IO_INTR_MODE_FALLING_EDGE);
}

/* ---- Type ---- */

static const sys_device_op_t k_ops[] = {
    OP_ENTRY_W(SYS_DEVICE, RESET),
    OP_ENTRY_W(HBRIDGE, DRIVE),
    OP_ENTRY_W(HBRIDGE, BRAKE),
    OP_ENTRY_W(HBRIDGE, COAST),
    OP_ENTRY_R(HBRIDGE, CURRENT),
    OP_ENTRY_R(PMON, CURRENT),
    OP_ENTRY_W_ALWAYS(EVENT, ON_EVENT),
    /* Device own ops */
    {.op = DRV8962_OP_CURRENT_LIMIT, .write = set_current_limit_h},
    {.op = DRV8962_OP_CLEAR_FAULT, .write = clear_fault_h},
    {.op = DRV8962_OP_FAULT, .read = get_fault_h},
    {.op = DRV8962_OP_SETTINGS, .read = get_settings_h, .write = set_settings_h},
};

static const sys_device_type_t k_type = {
    .type      = SYS_DEVICE_TYPE_DRV8962,
    .cfg_size  = sizeof(drv8962_cfg_t),
    .ctx_size  = sizeof(drv_t),
    .create    = create,
    .uninstall = uninstall,
    .suspend   = suspend_hook,
    .resume    = resume_hook,
    .ops       = k_ops,
    .op_count  = ARRAY_LEN(k_ops),
    .has_op    = has_op,
};

SYS_DEVICE_TYPE_REGISTER(k_type)
