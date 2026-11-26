/*
 * DRV8962: quad half-bridge motor driver, no bus: every signal is a pin of another device. The whole device is this
 * file: nothing includes it, the rest of the firmware reaches it by its ID, the HBRIDGE contract, POWER_MONITOR (PMON:
 * a channel's current, only with its IPROPI pins wired) and its own ops (the current limit, a channel's fault, the live
 * settings).
 *
 * The half bridges are two full bridges (channel 0 = OUT1+OUT2, channel 1 = OUT3+OUT4) or four half bridges, by
 * `topology`. IN1..IN4 carry the PWM, EN1..EN4 enable the half bridges, IPROPI1..IPROPI4 are the current sense outputs
 * (ADC pins, each armed at the current limit), nSLEEP wakes and resets the chip, nFAULT reports a fault and VREF (a DAC
 * pin) sets the chopping current of the whole chip. A fault is latched per channel until CLEAR_FAULT: with
 * `turn_off_at_ocp` the channel brakes, and HBRIDGE_EV_* is published once.
 */
#include <string.h>

#include "board_time.h"
#include "sys_device_driver.h"
#include "sys_device_event_contract.h"
#include "sys_event.h"
#include "sys_hbridge_contract.h"
#include "sys_io_driver.h"
#include "sys_power_monitor_contract.h"
#include "utils.h"

SYS_DEVICE_TYPE_DECLARE(DRV8962, 0x0049)

#define DRV8962_CURRENT_LIMIT_MAX_MA 10000u

//#ref-enum @alias DRV8962 Topology
typedef enum drv8962_topology_e {
  DRV8962_TOPOLOGY_2_FULL_BRIDGES = 0,  //@alias Two full bridges @description Two full bridges: ch0 (OUT1+OUT2), ch1 (OUT3+OUT4)
  DRV8962_TOPOLOGY_4_HALF_BRIDGES = 1,  //@alias Four half bridges @description Four half bridges: ch0..ch3 (OUT1..OUT4)
} drv8962_topology_e;

/** @brief LIMIT's value. */
typedef struct __attribute__((packed)) {
  uint32_t limit_mA;  /**< Current limit in mA, every channel (0 = no limit) */
} drv8962_limit_t;

/** @brief FAULT's answer. */
typedef struct __attribute__((packed)) {
  uint8_t fault;   /**< 1 = fault present, 0 = no fault */
  uint8_t reason;  /**< Fault reason (sys_hbridge_fault_reason_e) */
} drv8962_fault_t;

/** @brief SETTINGS' value, both sides: the settings of drv8962_cfg_t that change live, with the same names. */
typedef struct __attribute__((packed)) {
  uint8_t topology;           /**< drv8962_topology_e */
  uint32_t current_limit_mA;  /**< The chip's current limit, every channel; 0 = disabled */
  uint8_t turn_off_at_ocp;    /**< 1 = brake a channel over the limit */
} drv8962_settings_t;

/*******************************************************************************
 * OWN_DRV8962 : own operations
 *   OP_CURRENT_LIMIT - the chip's chopping limit (VREF), every channel
 *   OP_CLEAR_FAULT   - nSLEEP reset pulse clears the chip's latches; the channel brakes
 *   OP_FAULT         - a channel's latched fault
 *   OP_SETTINGS      - the live settings, both sides
 *   OP_PIN           - nFAULT fell or an IPROPI pin rose (LOCAL | ALWAYS | TAIL)
 ******************************************************************************/
#define OWN_DRV8962_OPERATIONS(X, P)                                                       \
  X(P, OP_CURRENT_LIMIT, 0x00, cmd_none_t,             drv8962_limit_t,        0)          \
  X(P, OP_CLEAR_FAULT,   0x01, cmd_none_t,             hbridge_channel_args_t, 0)          \
  X(P, OP_FAULT,         0x02, hbridge_channel_args_t, drv8962_fault_t,        0)          \
  X(P, OP_SETTINGS,      0x03, cmd_none_t,             drv8962_settings_t,     0)          \
  X(P, OP_PIN,           0x04, cmd_none_t,             sys_event_t,                        \
    SYS_DEVICE_OP_LOCAL | SYS_DEVICE_OP_ALWAYS | SYS_DEVICE_OP_TAIL)

SYS_DEVICE_OWN_DECLARE(OWN_DRV8962)

/* Indexes into drv8962_cfg_t.pin. */
enum {
  PIN_IN1 = 0,
  PIN_EN1 = 4,
  PIN_IPROPI1 = 8,
  PIN_NSLEEP = 12,
  PIN_NFAULT = 13,
  PIN_VREF = 14,
  DRV8962_PINS = 15,
};

/** @brief CREATE's struct (wire format). A signal not wired is SYS_IO_PIN_NONE. */
typedef struct __attribute__((packed)) {
  uint8_t topology;                /**< Half-bridge topology (drv8962_topology_e) */
  sys_io_pin_t pin[DRV8962_PINS];  /**< Signals mapped to external device pins */
  uint32_t pwm_freq_Hz;            /**< PWM frequency (0 selects 20 kHz default) */
  uint32_t ripropi_ohms[4];        /**< Current-sense resistor for each half bridge */
  uint32_t current_limit_mA;       /**< The chip's current limit, every channel (one VREF); 0 = disabled */
  uint8_t turn_off_at_ocp;         /**< 1 = brake channel upon overcurrent, 0 = continue */
} drv8962_cfg_t;

#define HALF_BRIDGES          4u
#define PWM_FREQ_DEFAULT_HZ   20000u
#define RIPROPI_DEFAULT_OHMS  3090u
#define DRIVE_DEADBAND        20     // a full-bridge drive below this (0.5 %) brakes
#define WAKE_DELAY_MS         2u     // tWAKE: 1.2 ms max before the inputs are taken
#define SLEEP_PULSE_US        30u    // nSLEEP reset pulse that clears the fault latches
#define SLEEP_WAKE_US         1000u  // and the wait after it
#define VREF_MIN_MV           50u    // a floating VREF sets an undefined threshold: always 50 .. 3300 mV
#define VREF_MAX_MV           3300u
#define AIPROPI_UA_PER_A      212u   // IPROPI: 212 uA per A, so I(mA) = V(mV) * 10^6 / (212 * R)
#define IPROPI_HYSTERESIS_PCT 10u    // an IPROPI window re-arms this far under the limit's voltage
#define IPROPI_SAMPLES        2u     // readings in a row over the limit before the event (one PWM spike does not count)

typedef struct {
  int16_t drive;
  int32_t last_mA;
  bool fault;
  uint8_t reason;  // sys_hbridge_fault_reason_e
} channel_t;

typedef struct {
  uint8_t id;
  uint8_t topology;
  bool turn_off_at_ocp;
  /* Every signal, each locked: EN and nSLEEP outputs, nFAULT an input, IN (PWM), IPROPI (ADC) and VREF (DAC), whose
   * duty, window and voltage are IO writes. */
  sys_io_claim_t pin[DRV8962_PINS];
  uint32_t ripropi_ohms[HALF_BRIDGES];
  uint32_t current_limit_mA;  // 0 = none
  channel_t ch[HALF_BRIDGES];
} drv_t;

#define SYS_DEVICE_CTX drv_t

static bool has_pin(const drv_t* c, uint8_t idx) {
  return sys_io_pin_valid(c->pin[idx].pin);
}

static uint8_t channel_count(const drv_t* c) {
  return c->topology == DRV8962_TOPOLOGY_2_FULL_BRIDGES ? 2 : 4;
}

// The half bridges of a channel: a full bridge is two (first, first + 1), a half bridge one.
static uint8_t first_bridge(const drv_t* c, uint8_t ch) {
  return c->topology == DRV8962_TOPOLOGY_2_FULL_BRIDGES ? (uint8_t)(ch * 2u) : ch;
}

static uint8_t last_bridge(const drv_t* c, uint8_t ch) {
  return (uint8_t)(first_bridge(c, ch) + (c->topology == DRV8962_TOPOLOGY_2_FULL_BRIDGES ? 1u : 0u));
}

static err_h check_channel(const drv_t* c, uint8_t ch) {
  ERR_CHECK(ch < channel_count(c), ERR_DEV_BAD_PIN, .id = c->id, .pin = ch);
  return NULL;
}

/* ---- Current ---- */

static uint32_t limit_mV(const drv_t* c, uint32_t ohms) {
  const uint64_t mV = (uint64_t)c->current_limit_mA * AIPROPI_UA_PER_A * ohms / 1000000u;
  return mV > UINT16_MAX ? UINT16_MAX : (uint32_t)mV;
}

// A channel's current (full bridge: the larger half bridge; 0 for a half bridge without IPROPI), remembered.
static err_h read_current_mA(drv_t* c, uint8_t ch, int32_t* out_mA) {
  *out_mA = 0;
  for (uint8_t b = first_bridge(c, ch); b <= last_bridge(c, ch); b++) {
    if (!has_pin(c, PIN_IPROPI1 + b)) {
      continue;
    }
    int32_t mV = 0;
    ERR_TRY(sys_io_get_voltage(c->pin[PIN_IPROPI1 + b].pin, &mV));
    const int32_t mA = (int32_t)(((int64_t)mV * 1000000) / ((int64_t)AIPROPI_UA_PER_A * c->ripropi_ohms[b]));
    *out_mA = mA > *out_mA ? mA : *out_mA;
  }
  c->ch[ch].last_mA = *out_mA;
  return NULL;
}

static bool over_limit(const drv_t* c, int32_t mA) {
  return c->current_limit_mA > 0 && mA >= 0 && (uint32_t)mA >= c->current_limit_mA;
}

/* VREF sets the chopping threshold of the whole chip (VREF / RIPROPI): set through the smallest IPROPI resistor, so no
 * half bridge passes the limit; no limit: VREF at its maximum. */
static err_h apply_vref(drv_t* c) {
  if (!has_pin(c, PIN_VREF)) {
    return NULL;
  }
  uint32_t ohms = c->ripropi_ohms[0];
  for (uint8_t b = 1; b < HALF_BRIDGES; b++) {
    ohms = c->ripropi_ohms[b] < ohms ? c->ripropi_ohms[b] : ohms;
  }
  uint32_t mV = limit_mV(c, ohms);
  mV = mV == 0 || mV > VREF_MAX_MV ? VREF_MAX_MV : mV < VREF_MIN_MV ? VREF_MIN_MV : mV;
  return SYS_IO_WRITE(c->pin[PIN_VREF].pin, OP_VOLTAGE, .voltage_mV = (int32_t)mV);
}

// Every IPROPI window at the limit's voltage (an event after IPROPI_SAMPLES readings over it); off without a limit.
static err_h arm_ipropi(drv_t* c) {
  err_h err = NULL;
  for (uint8_t b = 0; b < HALF_BRIDGES; b++) {
    if (!has_pin(c, PIN_IPROPI1 + b)) {
      continue;
    }
    const uint16_t up = (uint16_t)limit_mV(c, c->ripropi_ohms[b]);
    const sys_io_intr_cfg_t cfg = {
        .mode = up > 0 ? SYS_IO_INTR_MODE_RISING_EDGE : SYS_IO_INTR_DISABLE,
        .adc = {.threshold_up_mV = up, .hysteresis_mV = (uint16_t)(up * IPROPI_HYSTERESIS_PCT / 100u),
                .event_counter_threshold = IPROPI_SAMPLES}};
    ERR_KEEP_FIRST(err, SYS_IO_WRITE(c->pin[PIN_IPROPI1 + b].pin, OP_INTR, .cfg = cfg));
  }
  return err;
}

// The limit to VREF (nothing kept when that fails), then the IPROPI windows.
static err_h set_limit(drv_t* c, uint32_t limit_mA) {
  const uint32_t old = c->current_limit_mA;
  c->current_limit_mA = limit_mA;
  err_h err = apply_vref(c);
  if (err != NULL) {
    c->current_limit_mA = old;
    return err;
  }
  return arm_ipropi(c);
}

/* ---- Outputs ---- */

static err_h set_in(drv_t* c, uint8_t bridge, uint32_t duty) {
  return has_pin(c, PIN_IN1 + bridge) ? SYS_IO_WRITE(c->pin[PIN_IN1 + bridge].pin, OP_PWM_DUTY, .duty = duty) : NULL;
}

/* EN of every half bridge of the channel (brake: on, coast: off), its IN low. Every pin is written even if one
 * fails (the safe-state path). */
static err_h chip_halt(drv_t* c, uint8_t ch, bool brake) {
  err_h err = NULL;
  for (uint8_t b = first_bridge(c, ch); b <= last_bridge(c, ch); b++) {
    ERR_KEEP_FIRST(err, sys_io_claim_drive(&c->pin[PIN_EN1 + b], brake));
    ERR_KEEP_FIRST(err, set_in(c, b, 0));
  }
  c->ch[ch].drive = 0;
  return err;
}

static err_h halt_all(drv_t* c, bool brake) {
  err_h err = NULL;
  for (uint8_t ch = 0; ch < channel_count(c); ch++) {
    ERR_KEEP_FIRST(err, chip_halt(c, ch, brake));
  }
  return err;
}

/* A full bridge in slow decay: forward IN1 100 % and IN2 the complement of the drive, reverse the other way round. A
 * half bridge: its IN at the drive. */
static err_h chip_drive(drv_t* c, uint8_t ch, int16_t drive) {
  const bool full = c->topology == DRV8962_TOPOLOGY_2_FULL_BRIDGES;
  if (full && drive > -DRIVE_DEADBAND && drive < DRIVE_DEADBAND) {
    return chip_halt(c, ch, true);
  }
  const uint8_t b1 = first_bridge(c, ch), b2 = last_bridge(c, ch);
  for (uint8_t b = b1; b <= b2; b++) {
    ERR_TRY(sys_io_claim_drive(&c->pin[PIN_EN1 + b], true));
  }
  const uint32_t duty = (uint32_t)(drive < 0 ? -drive : drive) * IO_PWM_DUTY_MAX / SYS_HBRIDGE_DRIVE_MAX;
  if (full) {
    ERR_TRY(set_in(c, b1, drive > 0 ? IO_PWM_DUTY_MAX : IO_PWM_DUTY_MAX - duty));
    ERR_TRY(set_in(c, b2, drive > 0 ? IO_PWM_DUTY_MAX - duty : IO_PWM_DUTY_MAX));
  } else {
    ERR_TRY(set_in(c, b1, duty));
  }
  c->ch[ch].drive = drive;
  return NULL;
}

/* ---- Faults ---- */

// Latches a channel's fault and brakes it (turn_off_at_ocp); the caller publishes once the state is final.
static err_h mark_fault(drv_t* c, uint8_t ch, uint8_t reason) {
  c->ch[ch].fault = true;
  c->ch[ch].reason = reason;
  return c->turn_off_at_ocp ? chip_halt(c, ch, true) : NULL;
}

static err_h publish_fault(const drv_t* c, uint8_t ch) {
  const int32_t mA = c->ch[ch].last_mA;
  switch (c->ch[ch].reason) {
    case SYS_HBRIDGE_FAULT_OVERCURRENT:
      return SYS_EVENT_PUBLISH(HBRIDGE_EV_OVERCURRENT, c->id, ch, .mA = mA);
    case SYS_HBRIDGE_FAULT_THERMAL_OR_UVLO:
      return SYS_EVENT_PUBLISH(HBRIDGE_EV_THERMAL_OR_UVLO, c->id, ch, .mA = mA);
    default:
      return SYS_EVENT_PUBLISH(HBRIDGE_EV_EXTERNAL, c->id, ch, .mA = mA);
  }
}

// A channel found over its limit (an IPROPI window, a CURRENT read): latched once, published.
static err_h latch_overcurrent(drv_t* c, uint8_t ch) {
  if (c->ch[ch].fault) {
    return NULL;
  }
  err_h err = mark_fault(c, ch, SYS_HBRIDGE_FAULT_OVERCURRENT);
  ERR_KEEP_FIRST(err, publish_fault(c, ch));
  return err;
}

/* nFAULT fell: the chip says only "something is wrong", so the currents decide: the channels at their limit, else the
 * one channel that was driving, else every channel (thermal shutdown or supply UVLO). */
static err_h on_nfault(drv_t* c) {
  const uint8_t n = channel_count(c);
  uint8_t over = 0, driving = 0, faulted = 0;
  err_h err = NULL;
  for (uint8_t ch = 0; ch < n; ch++) {
    int32_t mA = 0;
    ERR_KEEP_FIRST(err, read_current_mA(c, ch, &mA));  // a failed read counts as 0 mA
    over |= over_limit(c, mA) ? (uint8_t)(1u << ch) : 0u;
    driving |= c->ch[ch].drive >= DRIVE_DEADBAND || c->ch[ch].drive <= -DRIVE_DEADBAND ? (uint8_t)(1u << ch) : 0u;
  }
  const bool one_driving = driving != 0 && (driving & (driving - 1)) == 0;
  const uint8_t reason = over || one_driving ? SYS_HBRIDGE_FAULT_OVERCURRENT : SYS_HBRIDGE_FAULT_THERMAL_OR_UVLO;
  faulted = over ? over : one_driving ? driving : (uint8_t)((1u << n) - 1u);
  for (uint8_t ch = 0; ch < n; ch++) {
    if (faulted & (1u << ch)) {
      ERR_KEEP_FIRST(err, mark_fault(c, ch, reason));
    }
  }
  for (uint8_t ch = 0; ch < n; ch++) {  // last: an inline listener may call back into this device
    if (faulted & (1u << ch)) {
      ERR_KEEP_FIRST(err, publish_fault(c, ch));
    }
  }
  return err;
}

/*******************************************************************************
 * OWN_DRV8962 : OP_PIN (event, LOCAL | ALWAYS | TAIL)
 * An IPROPI pin over the limit, or nFAULT. Nothing while suspended: coasted
 * already, and the fault line may still be low.
 ******************************************************************************/
OP_DECLARE_EVENT(OWN_DRV8962, OP_PIN) {
  if (sys_device_self_suspended()) {
    return NULL;
  }
  for (uint8_t b = 0; b < HALF_BRIDGES; b++) {
    const sys_io_pin_t p = c->pin[PIN_IPROPI1 + b].pin;
    if (has_pin(c, PIN_IPROPI1 + b) && event->key.source == p.device && event->key.channel == p.pin) {
      const uint8_t ch = c->topology == DRV8962_TOPOLOGY_2_FULL_BRIDGES ? (uint8_t)(b / 2u) : b;
      int32_t mA = 0;
      err_h err = c->ch[ch].fault ? NULL : read_current_mA(c, ch, &mA);
      ERR_KEEP_FIRST(err, latch_overcurrent(c, ch));
      return err;
    }
  }
  return on_nfault(c);
}

/* ---- HBRIDGE contract ---- */

OP_DECLARE_WRITE(CONTRACT_HBRIDGE, OP_DRIVE) {
  ERR_TRY(check_channel(c, args->channel));
  ERR_CHECK_RANGE(args->drive, c->topology == DRV8962_TOPOLOGY_2_FULL_BRIDGES ? -SYS_HBRIDGE_DRIVE_MAX : 0,
                  SYS_HBRIDGE_DRIVE_MAX);
  return chip_drive(c, args->channel, args->drive);
}

OP_DECLARE_WRITE(CONTRACT_HBRIDGE, OP_BRAKE) {
  ERR_TRY(check_channel(c, args->channel));
  return chip_halt(c, args->channel, true);
}

OP_DECLARE_WRITE(CONTRACT_HBRIDGE, OP_COAST) {
  ERR_TRY(check_channel(c, args->channel));
  return chip_halt(c, args->channel, false);
}

// A channel over its limit is latched here too: the fault line is edge triggered, a read may see it first.
OP_DECLARE_READ(CONTRACT_HBRIDGE, OP_CURRENT) {
  ERR_TRY(check_channel(c, args->channel));
  int32_t mA = 0;
  ERR_TRY(read_current_mA(c, args->channel, &mA));
  if (!sys_device_self_suspended() && over_limit(c, mA)) {
    ERR_TRY(latch_overcurrent(c, args->channel));
  }
  return CMD_REPLY(CONTRACT_HBRIDGE, OP_CURRENT, mA);
}

/* ---- POWER_MONITOR: a channel with every IPROPI pin of its half bridges ---- */

OP_DECLARE_READ(CONTRACT_PMON, OP_CURRENT) {
  bool sensed = args->channel < channel_count(c);
  for (uint8_t b = first_bridge(c, args->channel); sensed && b <= last_bridge(c, args->channel); b++) {
    sensed = has_pin(c, PIN_IPROPI1 + b);
  }
  ERR_CHECK(sensed, ERR_POWER_BAD_CHANNEL, .id = c->id, .channel = args->channel);
  int32_t mA = 0;
  ERR_TRY(read_current_mA(c, args->channel, &mA));
  return CMD_REPLY(CONTRACT_PMON, OP_CURRENT, mA);
}

// POWER_MONITOR needs an IPROPI pin; the other ops are always there.
static bool has_op(const drv_t* c, uint16_t op) {
  if ((op >> 8) != MODULE_ID_PMON) {
    return true;
  }
  for (uint8_t b = 0; b < HALF_BRIDGES; b++) {
    if (has_pin(c, PIN_IPROPI1 + b)) {
      return true;
    }
  }
  return false;
}

/* ---- Own ops ---- */

OP_DECLARE_WRITE(OWN_DRV8962, OP_CURRENT_LIMIT) {
  ERR_CHECK_RANGE(args->limit_mA, 0, DRV8962_CURRENT_LIMIT_MAX_MA);
  return set_limit(c, args->limit_mA);
}

// nSLEEP low for 30 us resets every latch of the chip; only the channel asked for is cleared here, and brakes.
OP_DECLARE_WRITE(OWN_DRV8962, OP_CLEAR_FAULT) {
  ERR_TRY(check_channel(c, args->channel));
  if (c->pin[PIN_NSLEEP].locked) {
    ERR_TRY(sys_io_claim_drive(&c->pin[PIN_NSLEEP], false));
    board_time_delay_us(SLEEP_PULSE_US);
    ERR_TRY(sys_io_claim_drive(&c->pin[PIN_NSLEEP], true));
    board_time_delay_us(SLEEP_WAKE_US);
  }
  c->ch[args->channel].fault = false;
  c->ch[args->channel].reason = SYS_HBRIDGE_FAULT_NONE;
  return chip_halt(c, args->channel, true);
}

OP_DECLARE_READ(OWN_DRV8962, OP_FAULT) {
  ERR_TRY(check_channel(c, args->channel));
  return CMD_REPLY(OWN_DRV8962, OP_FAULT, .fault = c->ch[args->channel].fault, .reason = c->ch[args->channel].reason);
}

OP_DECLARE_READ(OWN_DRV8962, OP_SETTINGS) {
  return CMD_REPLY(OWN_DRV8962, OP_SETTINGS, .topology = c->topology, .current_limit_mA = c->current_limit_mA,
                   .turn_off_at_ocp = c->turn_off_at_ocp);
}

/* The settings create takes, changed live. A new topology coasts every channel of the old one, forgets their state (a
 * channel number is another bridge now) and brakes every channel of the new one, as create does. */
OP_DECLARE_WRITE(OWN_DRV8962, OP_SETTINGS) {
  ERR_CHECK_RANGE(args->topology, DRV8962_TOPOLOGY_2_FULL_BRIDGES, DRV8962_TOPOLOGY_4_HALF_BRIDGES);
  ERR_CHECK_RANGE(args->current_limit_mA, 0, DRV8962_CURRENT_LIMIT_MAX_MA);
  ERR_CHECK(args->turn_off_at_ocp <= 1, ERR_BAD_ARG);
  c->turn_off_at_ocp = args->turn_off_at_ocp != 0;
  err_h err = set_limit(c, args->current_limit_mA);
  if (args->topology != c->topology) {
    ERR_KEEP_FIRST(err, halt_all(c, false));
    c->topology = args->topology;
    memset(c->ch, 0, sizeof(c->ch));
    ERR_KEEP_FIRST(err, halt_all(c, true));
  }
  return err;
}

/* ---- Lifecycle ---- */

// Every channel braked.
OP_DECLARE_WRITE(CONTRACT_SYS_DEVICE, OP_RESET) {
  return halt_all(c, true);
}

// Every channel coasted, each reached even if another fails; also the last state before REMOVE.
static err_h suspend(drv_t* c) {
  return halt_all(c, false);
}

// The channels stay coasted: the next drive engages them.
static err_h resume(drv_t* c CMD_UNUSED) {
  return NULL;
}

static err_h create(uint8_t id, drv_t* c, const drv8962_cfg_t* cfg) {
  ERR_CHECK_RANGE(cfg->topology, DRV8962_TOPOLOGY_2_FULL_BRIDGES, DRV8962_TOPOLOGY_4_HALF_BRIDGES);
  ERR_CHECK(cfg->turn_off_at_ocp <= 1, ERR_BAD_ARG);
  ERR_CHECK_RANGE(cfg->current_limit_mA, 0, DRV8962_CURRENT_LIMIT_MAX_MA);
  c->id = id;
  c->topology = cfg->topology;
  c->turn_off_at_ocp = cfg->turn_off_at_ocp != 0;
  c->current_limit_mA = cfg->current_limit_mA;
  for (uint8_t b = 0; b < HALF_BRIDGES; b++) {
    c->ripropi_ohms[b] = cfg->ripropi_ohms[b] ? cfg->ripropi_ohms[b] : RIPROPI_DEFAULT_OHMS;
  }
  const uint32_t freq = cfg->pwm_freq_Hz ? cfg->pwm_freq_Hz : PWM_FREQ_DEFAULT_HZ;
  for (uint8_t b = 0; b < HALF_BRIDGES; b++) {
    // IN: PWM at the chip's frequency, off. EN: locked outputs, low (coasting until the first brake).
    ERR_TRY(sys_io_claim(id, &c->pin[PIN_IN1 + b], cfg->pin[PIN_IN1 + b], SYS_IO_MODE_PWM, 0, 0));
    if (has_pin(c, PIN_IN1 + b)) {
      ERR_TRY(SYS_IO_WRITE(cfg->pin[PIN_IN1 + b], OP_PWM_FREQ, .frequency_Hz = freq));
      ERR_TRY(set_in(c, b, 0));
    }
    ERR_TRY(sys_io_claim_output(id, &c->pin[PIN_EN1 + b], cfg->pin[PIN_EN1 + b], false));
    // IPROPI: ADC, its window's rise to OP_PIN.
    ERR_TRY(sys_io_claim(id, &c->pin[PIN_IPROPI1 + b], cfg->pin[PIN_IPROPI1 + b], SYS_IO_MODE_ADC, IO_EV_RISING,
                         OWN_DRV8962_OP_PIN));
  }
  // nSLEEP high wakes the chip.
  ERR_TRY(sys_io_claim_output(id, &c->pin[PIN_NSLEEP], cfg->pin[PIN_NSLEEP], true));
  if (has_pin(c, PIN_NSLEEP)) {
    vTaskDelay(MSEC(WAKE_DELAY_MS));
  }
  // VREF driven (a floating VREF sets an undefined threshold), the IPROPI windows at the limit.
  ERR_TRY(sys_io_claim(id, &c->pin[PIN_VREF], cfg->pin[PIN_VREF], SYS_IO_MODE_DAC, 0, 0));
  ERR_TRY(set_limit(c, c->current_limit_mA));
  ERR_TRY(halt_all(c, true));
  // nFAULT last: its fall reaches OP_PIN (inline: ADC reads, no delay).
  return sys_io_claim_input(id, &c->pin[PIN_NFAULT], cfg->pin[PIN_NFAULT], IO_EV_FALLING, OWN_DRV8962_OP_PIN,
                            SYS_IO_INTR_MODE_FALLING_EDGE);
}

static const sys_device_op_t k_ops[] = {
    OP_ENTRY_W(CONTRACT_SYS_DEVICE, OP_RESET),
    OP_ENTRY_W(CONTRACT_HBRIDGE, OP_DRIVE),  OP_ENTRY_W(CONTRACT_HBRIDGE, OP_BRAKE),
    OP_ENTRY_W(CONTRACT_HBRIDGE, OP_COAST),  OP_ENTRY_R(CONTRACT_HBRIDGE, OP_CURRENT),
    OP_ENTRY_R(CONTRACT_PMON, OP_CURRENT),
    OP_ENTRY_W(OWN_DRV8962, OP_CURRENT_LIMIT),  OP_ENTRY_W(OWN_DRV8962, OP_CLEAR_FAULT),
    OP_ENTRY_R(OWN_DRV8962, OP_FAULT),          OP_ENTRY(OWN_DRV8962, OP_SETTINGS),
    OP_ENTRY_W(OWN_DRV8962, OP_PIN),
};

SYS_DEVICE_TYPE_DEFINE_HAS_OP(DRV8962, drv8962_cfg_t, create, suspend, resume, k_ops, has_op)
