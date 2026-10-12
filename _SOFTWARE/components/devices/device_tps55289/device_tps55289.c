#include "device_tps55289.h"
#include "esp_timer.h"
#include "sys_event.h"
#include "sys_i2c.h"
#include "sys_pins.h"
#include "utils.h"

#define I2C_SPEED_HZ 400000u
#define TPS55289_SHUNT_MOHM 10u  // current sense resistor of the board
#define EN_WAKEUP_MS 10u         // I2C is up ~10 ms after EN rises

#define REG_REF_LSB 0x00u
#define REG_IOUT_LIMIT 0x02u
#define REG_VOUT_SR 0x03u
#define REG_VOUT_FS 0x04u
#define REG_CDC 0x05u
#define REG_MODE 0x06u
#define REG_STATUS 0x07u

#define IOUT_LIMIT_EN 0x80u   // current limit on; [6:0] = VISP - VISN in 0.5 mV steps
#define VOUT_SR_SLEW 0x03u    // [1:0] slew rate
#define VOUT_SR_OCP_DELAY 0x30u  // [5:4] OCP response delay
#define VOUT_FS_EXT_FB 0x80u  // external feedback divider
#define VOUT_FS_INTFB 0x03u   // internal feedback ratio
#define REF_MAX 0x07FFu       // 11-bit reference
#define CDC_FAULT_MASKS 0xE0u // SC_MASK / OCP_MASK / OVP_MASK (bits 7 / 6 / 5 = TPS55289_FAULT_* << 5)
#define CDC_FAULT_SHIFT 5u
#define MODE_OE 0x80u         // output enable
#define MODE_HICCUP 0x20u     // short circuit: 76 ms off, then soft start
#define MODE_DISCHG 0x10u     // output discharge while OE is off (EN high)
#define STATUS_SCP 0x80u
#define STATUS_OCP 0x40u
#define STATUS_OVP 0x20u

/* VOUT_SR[1:0]: slew rate of output voltage changes (reset default 2.5 mV/us). */
#define SLEW_1_25_MV_US 0

#define TPS_DEFAULT_VOLTAGE_MV 5000
/* Raising the output charges its capacitors (VOUT_SR slew, set to the slowest 1.25 mV/us;
   3.6 ms soft start after OE). With a high current limit that inrush comes
   straight from the input and can pull a USB source down, so every rise runs
   at the soft-rise current limit (TPS_SOFT_RISE_MA) and the wanted limit is applied after this
   settle time. Note: the limit is VISP - VISN in 0.5 mV steps over the sense
   resistor, specified only at 30 / 50 mV (datasheet 6.5), so limits below ~1 A
   (a few mV) are coarse: on the PCB 250 / 350 mA settings held ~150 / ~240 mA. */
#define TPS_RAMP_SETTLE_MS 1000
/* The output rises in whole-volt steps from TPS_RAMP_START_MV to the target, one step every
   TPS_RAMP_STEP_MS, so the load's capacitors charge gradually. The chip itself slews 1.25 mV/us, so
   without the steps a rail reaches 20 V in ~16 ms. The ramp counts toward the settle window below. */
#define TPS_RAMP_START_MV 1000
#define TPS_RAMP_STEP_MV 1000
#define TPS_RAMP_STEP_MS 50
/* The settle is also the startup window in which the chip's OCP flag is ignored (a load's inrush keeps
   the chip in current limit while its capacitors charge). The calls that start a rise (enable, voltage
   up) wait it out, so they take this long. The flag is cleared at its end; an overload that persists
   sets it again and is reported. */
/* (2026-10-10) The rise now runs as steps from a timer (tps_rise_step): the calls that start it return
   at once, each step is a short op under the registry lock, and VREG_EV_READY marks the end. */
/* Current limit while the output rises; 0 = no soft-rise limit, the output rises at the wanted
   limit (the inrush then comes straight from the input and can pull a USB source down). With a
   limit, keep it at or above TPS_LIMIT_FLOOR_MA: the limit is VISP - VISN over the 10 mOhm sense
   resistor, so 200 mA is 2 mV - within the chip's offset, and a rail then flags OCP just charging
   its capacitors (seen on rail A). 500 mA = 5 mV. */
#define TPS_SOFT_RISE_MA 1000
/* (2026-10-10) A fixed value now: the "0 = no soft-rise limit" option and tps_rise_limit() were removed, nothing used them. */

/* Lowest limit written to the chip. Below ~5 mV sense the offset dominates: on
   the PCB rail A's chip sat in current limit with no load at a 200 mA (2 mV)
   setting and flagged OCP right after enable. Lower requests are raised to it. */
#define TPS_LIMIT_FLOOR_MA 500

/* With `discharge` on, SET_ENABLE off clears OE and keeps EN high this long: the DISCHG sink (40..170 mA,
   datasheet 6.5) only runs while the chip is powered, and EN low would end it at once. 100 ms takes ~200 uF from
   20 V to below 0.8 V at the minimum 40 mA; the board's output capacitance is not known yet. */
#define TPS_DISCHARGE_MS 100

static const tps55289_protection_t k_default_protection = {
    .ocp_delay = 0, .hiccup = 1, .fault_mask = TPS55289_FAULT_ALL, .discharge = 0};

/* Own op: one step of a rise, sent by the rise timer through sys_device_call (so it runs under the
   registry lock like any op). Harmless from outside: without a rise in progress it does nothing. */
#define TPS_OP_RISE_STEP SYS_DEVICE_OWN_OP(0x00)

typedef enum {
  RISE_IDLE = 0,  // no rise: the wanted limit is on the chip
  RISE_RAMP,      // stepping the output up at the soft-rise limit
  RISE_SETTLE,    // at the target, waiting out the settle window
  RISE_FALL,      // output off (OE), discharging; the next step drives EN low
} rise_e;

typedef struct {
  sys_device_base_t base;  // first: id, suspended (output off + EN low, writes refused until RESUME)
  sys_dev_pin_t intr;  // FB/INT: its falling edge runs our ON_EVENT
  sys_dev_pin_t en;    // rail enable: locked, driven by tps_set_en
  sys_i2c_dev_t i2c;
  esp_timer_handle_t rise_timer;  // one-shot: the next rise step

  /* Wanted settings. The hardware EN pin has priority: while the rail is off EN
     is low, and the TPS55289 forgets every register, so these are written
     again each time EN goes high (tps_apply_config). */
  uint16_t last_voltage_mV;
  uint16_t last_current_limit_mA;
  bool last_enable_state;

  uint16_t out_mV;        // voltage last written to the chip (the ramp's position)
  uint8_t rise;           // rise_e; while rising the chip's OCP flag is the inrush, not reported
  int64_t rise_start_us;  // start of the settle window
  tps55289_protection_t prot;  // wanted protection, written with the other settings
} tps_t;

static inline uint16_t tps_limit(uint16_t mA) {
  return mA < TPS_LIMIT_FLOOR_MA ? TPS_LIMIT_FLOOR_MA : mA;
}

/* ---- Chip access: the registry adds the context ---- */

static err_h chip_set_output_enable(tps_t* c, bool enable) {
  return sys_i2c_reg_update(&c->i2c, REG_MODE, MODE_OE, enable ? MODE_OE : 0);
}

// The caller range-checked the wanted limit and raised it to the floor (tps_limit): 500..5500 mA = 10..110 steps.
static err_h chip_set_current_limit(tps_t* c, uint16_t limit_mA) {
  const uint8_t steps = (uint8_t)((uint32_t)limit_mA * TPS55289_SHUNT_MOHM / 500u);  // 0.5 mV steps
  const uint8_t data = IOUT_LIMIT_EN | steps;
  return sys_i2c_reg_write(&c->i2c, REG_IOUT_LIMIT, &data, 1);
}

/* Read-modify-write: a failed read is returned instead of writing a register built from 0
   (a lost VOUT_FS read alone would turn a 10 V request into ~21 V). A voltage the
   feedback ratio can't reach is refused, not clamped. */
static err_h chip_set_voltage(tps_t* c, uint16_t voltage_mV) {
  uint8_t vout_fs = 0;
  ERR_TRY(sys_i2c_reg_read(&c->i2c, REG_VOUT_FS, &vout_fs, 1));
  /* External feedback: VOUT depends on the board's divider, which the device doesn't
     know (datasheet 7.3.12). Only internal feedback is supported. */
  ERR_CHECK(!(vout_fs & VOUT_FS_EXT_FB), ERR_POWER_EXT_FEEDBACK, .id = c->base.id);
  // INTFB[1:0]: reference step (in 0.1 mV) and lowest output voltage
  static const struct {
    uint16_t step_100uV;
    uint16_t min_mV;
  } k_intfb[4] = {{25, 200}, {50, 400}, {75, 600}, {100, 800}};
  const uint32_t step = k_intfb[vout_fs & VOUT_FS_INTFB].step_100uV;
  const uint32_t min_mV = k_intfb[vout_fs & VOUT_FS_INTFB].min_mV;
  const uint32_t max_mV = min_mV + REF_MAX * step / 10u;
  SYS_DEV_CHECK_IN_RANGE(c->base.id, voltage_mV, min_mV, max_mV);
  const uint16_t ref = (uint16_t)((voltage_mV - min_mV) * 10u / step);
  ERR_TRY(sys_i2c_reg_write_u16(&c->i2c, REG_REF_LSB, ref, SYS_I2C_LE));
  c->out_mV = voltage_mV;
  return NULL;
}

static err_h chip_set_slew_rate(tps_t* c, uint8_t rate) {
  return sys_i2c_reg_update(&c->i2c, REG_VOUT_SR, VOUT_SR_SLEW, rate);
}

/* The protection registers: OCP delay (VOUT_SR[5:4]), hiccup and discharge (MODE[5:4], OE kept), and which
   faults pull FB/INT low (CDC[7:5]: 1 = indicated, datasheet 7.6.5). */
static err_h chip_set_protection(tps_t* c) {
  const tps55289_protection_t* p = &c->prot;
  ERR_TRY(sys_i2c_reg_update(&c->i2c, REG_VOUT_SR, VOUT_SR_OCP_DELAY, (uint8_t)(p->ocp_delay << 4)));
  ERR_TRY(sys_i2c_reg_update(&c->i2c, REG_MODE, MODE_HICCUP | MODE_DISCHG,
                             (p->hiccup ? MODE_HICCUP : 0) | (p->discharge ? MODE_DISCHG : 0)));
  return sys_i2c_reg_update(&c->i2c, REG_CDC, CDC_FAULT_MASKS, (uint8_t)(p->fault_mask << CDC_FAULT_SHIFT));
}

// Reading STATUS clears its fault bits.
static err_h chip_get_status(tps_t* c, uint8_t* status) {
  return sys_i2c_reg_read(&c->i2c, REG_STATUS, status, 1);
}

/* ---- Rail control ---- */

/* True while the chip is powered and answers I2C: always without an EN pin,
   else only while the rail is enabled (EN high) and not suspended (EN low). */
static bool tps_powered(const tps_t* c) {
  return !sys_io_pin_valid(c->en.pin) || (c->last_enable_state && !c->base.suspended);
}

/* Drive EN; without an EN pin only the MODE register's output-enable bit switches the rail.
   The 10 ms after EN rises is the one wait inside an op: the chip's own I2C start-up. */
static err_h tps_set_en(tps_t* c, bool high) {
  if (!sys_io_pin_valid(c->en.pin)) {
    return NULL;
  }
  ERR_TRY(sys_dev_pin_drive(&c->en, high));
  if (high) {
    vTaskDelay(MSEC(EN_WAKEUP_MS));
  }
  return NULL;
}

/* ---- Rise: ramp + settle as timer steps ---- */

// esp_timer task. Only the ID crosses over: a REMOVE stops the timer first, and a step already waiting
// for the lock then finds no device (ERR_DEV_NOT_FOUND, reported).
static void tps_rise_timer_cb(void* arg) {
  err_report(sys_device_call((uint16_t)(uintptr_t)arg, TPS_OP_RISE_STEP, NULL, 0, NULL, 0, NULL));
}

static err_h tps_rise_schedule(tps_t* c, uint32_t delay_ms) {
  (void)esp_timer_stop(c->rise_timer);  // ESP_ERR_INVALID_STATE when it isn't running
  return ERR_ESP(esp_timer_start_once(c->rise_timer, (uint64_t)delay_ms * 1000u));
}

// The rise stops where it is: the output keeps its last step and the soft-rise limit (off, suspend, reset).
static void tps_rise_cancel(tps_t* c) {
  c->rise = RISE_IDLE;
  if (c->rise_timer != NULL) {
    (void)esp_timer_stop(c->rise_timer);
  }
}

/* One step. RAMP: the next whole step above the output, or the target (then SETTLE). SETTLE: once the
   window since rise_start_us is over, the wanted limit, STATUS read (clears the OCP the inrush latched;
   an overload that persists sets it again and is reported), VREG_EV_READY. */
static err_h tps_rise_step(tps_t* c) {
  if (c->rise == RISE_FALL) {
    c->rise = RISE_IDLE;
    return tps_set_en(c, false);  // discharged: the hardware switch off
  }
  if (c->rise == RISE_RAMP) {
    const uint32_t next_mV = ((uint32_t)c->out_mV / TPS_RAMP_STEP_MV + 1u) * TPS_RAMP_STEP_MV;
    if (next_mV < c->last_voltage_mV) {
      ERR_TRY(chip_set_voltage(c, (uint16_t)next_mV));
      return tps_rise_schedule(c, TPS_RAMP_STEP_MS);
    }
    ERR_TRY(chip_set_voltage(c, c->last_voltage_mV));
    c->rise = RISE_SETTLE;
  }
  const int64_t elapsed_ms = (esp_timer_get_time() - c->rise_start_us) / 1000;
  if (elapsed_ms < TPS_RAMP_SETTLE_MS) {
    return tps_rise_schedule(c, (uint32_t)(TPS_RAMP_SETTLE_MS - elapsed_ms));
  }
  ERR_TRY(chip_set_current_limit(c, tps_limit(c->last_current_limit_mA)));
  uint8_t status;
  ERR_TRY(chip_get_status(c, &status));
  c->rise = RISE_IDLE;
  return SYS_EVENT_PUBLISH(VREG, READY, c->base.id, 0, c->last_voltage_mV);
}

// The soft-rise limit is on the chip: start the settle window and the first step now.
static err_h tps_rise_run(tps_t* c) {
  c->rise = RISE_RAMP;
  c->rise_start_us = esp_timer_get_time();
  return tps_rise_step(c);
}

/* Write every wanted setting; called right after EN goes high (the chip lost them).
   An enabled output starts at the soft-rise limit and rises from the timer. */
static err_h tps_apply_config(tps_t* c) {
  ERR_TRY(chip_set_protection(c));
  // Slowest output slew: voltage changes charge the output capacitors gently.
  ERR_TRY(chip_set_slew_rate(c, SLEW_1_25_MV_US));
  const uint16_t start_mA = c->last_enable_state ? TPS_SOFT_RISE_MA : c->last_current_limit_mA;
  ERR_TRY(chip_set_current_limit(c, tps_limit(start_mA)));
  const bool ramp = c->last_enable_state && c->last_voltage_mV > TPS_RAMP_START_MV;
  ERR_TRY(chip_set_voltage(c, ramp ? TPS_RAMP_START_MV : c->last_voltage_mV));
  ERR_TRY(chip_set_output_enable(c, c->last_enable_state));
  return c->last_enable_state ? tps_rise_run(c) : NULL;
}

/* Rise stopped, output off, then EN low; every step runs (EN goes low even if the I2C disable fails). With
   `discharge` (SET_ENABLE off, not SUSPEND: a fault doesn't wait) EN stays high for TPS_DISCHARGE_MS while the
   chip sinks the output, and the timer's step drives it low. */
static err_h tps_rail_off(tps_t* c, bool chip_powered, bool discharge) {
  err_h err = NULL;
  tps_rise_cancel(c);
  if (chip_powered) {
    SYS_DEVICE_KEEP_FIRST(err, chip_set_output_enable(c, false));
  }
  if (discharge && err == NULL && chip_powered && c->prot.discharge && sys_io_pin_valid(c->en.pin)) {
    c->rise = RISE_FALL;
    err = tps_rise_schedule(c, TPS_DISCHARGE_MS);
    if (err == NULL) {
      return NULL;
    }
    c->rise = RISE_IDLE;
  }
  SYS_DEVICE_KEEP_FIRST(err, tps_set_en(c, false));
  return err;
}

/* On: EN high, then every setting again (EN low cleared them); an enabled rail stays as it is. Off:
   output disabled, then EN low - the hardware switch has the last word. */
static err_h tps_set_enable(tps_t* c, bool state) {
  if (state && c->last_enable_state && tps_powered(c)) {
    return NULL;  // already on: re-applying would drop the output to the ramp start
  }
  const bool was_powered = tps_powered(c);
  c->last_enable_state = state;
  if (!state) {
    return tps_rail_off(c, was_powered, true);
  }
  tps_rise_cancel(c);
  ERR_TRY(tps_set_en(c, true));
  return tps_apply_config(c);
}

/* ---- POWER_VREG contract ---- */

// On: returns once the rise started; VREG_EV_READY when the output is settled.
VREG_HANDLER(SET_ENABLE) {
  tps_t* c = ctx;
  ERR_TRY(sys_device_check_active(&c->base));
  ERR_CHECK(args->enable <= 1, ERR_CMD_BAD_ARG);
  return tps_set_enable(c, args->enable);
}

// Higher on a running rail: a rise (ramp at the soft-rise limit, settle, VREG_EV_READY). Lower, or the
// output off: written at once. During a rise the ramp just follows the new target.
VREG_HANDLER(SET_VOLTAGE) {
  tps_t* c = ctx;
  ERR_TRY(sys_device_check_active(&c->base));
  SYS_DEV_CHECK_IN_RANGE(c->base.id, args->voltage_mV, TPS55289_MIN_VOLTAGE_MV, TPS55289_MAX_VOLTAGE_MV);
  c->last_voltage_mV = (uint16_t)args->voltage_mV;
  if (!tps_powered(c)) {
    return NULL;  // applied by the next enable
  }
  const bool higher = c->last_voltage_mV > c->out_mV;
  if (!c->last_enable_state || !higher) {
    ERR_TRY(chip_set_voltage(c, c->last_voltage_mV));
    if (c->rise == RISE_RAMP) {
      c->rise = RISE_SETTLE;  // target reached: the pending step finishes the settle
    }
    return NULL;
  }
  if (c->rise != RISE_IDLE) {
    c->rise = RISE_RAMP;  // the soft-rise limit is already on: keep ramping, new settle window
    c->rise_start_us = esp_timer_get_time();
    return tps_rise_schedule(c, TPS_RAMP_STEP_MS);
  }
  ERR_TRY(chip_set_current_limit(c, TPS_SOFT_RISE_MA));
  return tps_rise_run(c);
}

// During a rise only stored: the end of the rise writes it.
VREG_HANDLER(SET_CURRENT) {
  tps_t* c = ctx;
  ERR_TRY(sys_device_check_active(&c->base));
  SYS_DEV_CHECK_IN_RANGE(c->base.id, args->current_mA, TPS55289_MIN_CURRENT_MA, TPS55289_MAX_CURRENT_MA);
  c->last_current_limit_mA = (uint16_t)args->current_mA;
  if (!tps_powered(c) || c->rise != RISE_IDLE) {
    return NULL;
  }
  return chip_set_current_limit(c, tps_limit(c->last_current_limit_mA));
}

/* ---- Own ops: protection, a rise step (the timer) ---- */

// Stored and, while the chip is powered, written at once; otherwise written with the next enable.
SYS_DEVICE_HANDLER(set_protection_h, tps55289_protection_t) {
  (void)out;
  (void)out_cap;
  (void)out_len;
  tps_t* c = ctx;
  ERR_TRY(sys_device_check_active(&c->base));
  SYS_DEV_CHECK_IN_RANGE(c->base.id, args->ocp_delay, 0, TPS55289_OCP_DELAY_MAX);
  SYS_DEV_CHECK_IN_RANGE(c->base.id, args->fault_mask, 0, TPS55289_FAULT_ALL);
  ERR_CHECK(args->hiccup <= 1 && args->discharge <= 1, ERR_CMD_BAD_ARG);
  c->prot = *args;
  return tps_powered(c) ? chip_set_protection(c) : NULL;
}

SYS_DEVICE_HANDLER(get_protection_h, sys_device_none_t) {
  (void)args;
  const tps_t* c = ctx;
  return cmd_reply(out, out_cap, out_len, &c->prot, sizeof(c->prot));
}

// A failed step ends the rise where it is (soft-rise limit kept); the timer reports the error.
SYS_DEVICE_HANDLER(rise_step_h, sys_device_none_t) {
  (void)args;
  (void)out;
  (void)out_cap;
  (void)out_len;
  tps_t* c = ctx;
  if (c->base.suspended || c->rise == RISE_IDLE) {
    return NULL;
  }
  err_h err = tps_rise_step(c);
  if (err != NULL) {
    c->rise = RISE_IDLE;
  }
  return err;
}

/* ---- EVENT contract: FB/INT fell (our only subscription) ---- */

// Publishes each fault of the status register. OCP during a rise is the inrush.
EVENT_HANDLER(ON_EVENT) {
  (void)args;
  tps_t* c = ctx;
  if (!tps_powered(c)) {
    return NULL;  // EN low: nothing to read, no fault possible
  }
  uint8_t status;
  ERR_TRY(chip_get_status(c, &status));
  err_h err = NULL;
  if (status & STATUS_OVP) {
    SYS_DEVICE_KEEP_FIRST(err, SYS_EVENT_PUBLISH(VREG, OVP, c->base.id, 0, c->last_voltage_mV));
  }
  if ((status & STATUS_OCP) && c->rise == RISE_IDLE) {
    SYS_DEVICE_KEEP_FIRST(err, SYS_EVENT_PUBLISH(VREG, OCP, c->base.id, 0, c->last_current_limit_mA));
  }
  if (status & STATUS_SCP) {
    SYS_DEVICE_KEEP_FIRST(err, SYS_EVENT_PUBLISH(VREG, SCP, c->base.id, 0, 0));
  }
  return err;
}

/* ---- Lifecycle ---- */

/* Back to the create defaults: rail off (EN low), 5 V / minimum current / default protection wanted. Without
   an EN pin the chip stays powered, so the protection is written now. */
DEVICE_HANDLER(RESET) {
  tps_t* c = ctx;
  ERR_TRY(sys_device_check_active(&c->base));
  c->last_voltage_mV = TPS_DEFAULT_VOLTAGE_MV;
  c->last_current_limit_mA = TPS55289_MIN_CURRENT_MA;
  c->prot = k_default_protection;
  ERR_TRY(tps_set_enable(c, false));
  return sys_io_pin_valid(c->en.pin) ? NULL : chip_set_protection(c);
}

/* Fault safe state: the rail off. The wanted state (last_enable_state) is kept
 * for RESUME. The device counts as suspended even if a step failed. */
static err_h suspend_hook(void* ctx) {
  tps_t* c = ctx;
  const bool was_powered = tps_powered(c);
  return tps_rail_off(c, was_powered, false);
}

/* Back to the wanted state: an enabled rail gets EN high and every setting
 * again (a rise). A failing resume leaves the device suspended with the rail off. */
static err_h resume_hook(void* ctx) {
  tps_t* c = ctx;
  if (c->last_enable_state || !sys_io_pin_valid(c->en.pin)) {
    err_h err = tps_set_en(c, true);
    if (err == NULL) {
      err = tps_apply_config(c);
    }
    if (err != NULL) {
      SYS_DEVICE_KEEP_FIRST(err, tps_rail_off(c, true, false));
      return err;
    }
  }
  return NULL;
}

/* ---- Create / uninstall ---- */

// Releases the hardware; REMOVE and the create rollback. Each step runs only if it was built, none returns early.
static err_h uninstall(void* ctx) {
  tps_t* c = ctx;
  err_h err = NULL;
  if (c->rise_timer != NULL) {
    tps_rise_cancel(c);
    SYS_DEVICE_KEEP_FIRST(err, ERR_ESP(esp_timer_delete(c->rise_timer)));
    c->rise_timer = NULL;
  }
  if (sys_i2c_dev_added(&c->i2c)) {
    if (tps_powered(c)) {
      SYS_DEVICE_KEEP_FIRST(err, chip_set_output_enable(c, false));
    }
    SYS_DEVICE_KEEP_FIRST(err, sys_i2c_dev_remove(&c->i2c));
  }
  SYS_DEVICE_KEEP_FIRST(err, sys_dev_pin_release(&c->intr));
  SYS_DEVICE_KEEP_FIRST(err, sys_dev_pin_drive(&c->en, false));  // rail off
  SYS_DEVICE_KEEP_FIRST(err, sys_dev_pin_release(&c->en));
  return err;
}

static err_h create(void* ctx, const void* cfg_in) {
  tps_t* c = ctx;
  const tps55289_cfg_t* cfg = cfg_in;
  const uint16_t id = c->base.id;
  SYS_DEV_CHECK_IN_RANGE(id, cfg->i2c_bus, 0, SYS_I2C_BUS_COUNT - 1);
  SYS_DEV_CHECK_IN_RANGE(id, cfg->i2c_addr, TPS55289_I2C_ADDR_74, TPS55289_I2C_ADDR_75);
  c->last_voltage_mV = TPS_DEFAULT_VOLTAGE_MV;
  c->last_current_limit_mA = TPS55289_MIN_CURRENT_MA;
  c->prot = k_default_protection;
  sys_i2c_dev_init(&c->i2c, cfg->i2c_bus, cfg->i2c_addr, I2C_SPEED_HZ);

  const esp_timer_create_args_t timer_args = {
      .callback = tps_rise_timer_cb, .arg = (void*)(uintptr_t)id, .name = "tps_rise"};
  ERR_TRY(ERR_ESP(esp_timer_create(&timer_args, &c->rise_timer)));

  // EN must be driven high before the I2C probe below - the TPS55289's I2C
  // interface is unavailable while EN is low/floating, so probing first
  // would always fail with ERR_I2C_DEV_NOT_FOUND on a fresh boot.
  ERR_TRY(sys_dev_pin_take_output(&c->base, &c->en, cfg->en_pin, false));  // an invalid pin is no pin: nothing driven
  if (sys_io_pin_valid(c->en.pin)) {
    ERR_TRY(tps_set_en(c, true));
  }
  ERR_TRY(sys_i2c_dev_add(&c->i2c));  // probes the chip

  // FB/INT: its falling edge reaches our ON_EVENT (inline: an I2C read, no delay).
  ERR_TRY(sys_dev_pin_take_input(&c->base, &c->intr, cfg->intr_pin, IO_EV_FALLING, SYS_IO_INTR_MODE_FALLING_EDGE));

  // Probe with EN high (above), write the defaults with the output off, then
  // hand over to the hardware switch: the rail stays off (EN low) until enabled.
  ERR_TRY(tps_apply_config(c));
  return tps_set_en(c, false);
}

/* ---- Type ---- */

static const sys_device_op_t k_ops[] = {
    DEVICE_ENTRY(RESET),
    VREG_ENTRY(SET_ENABLE),  VREG_ENTRY(SET_VOLTAGE),   VREG_ENTRY(SET_CURRENT),
    EVENT_ENTRY(ON_EVENT),
    //@internal
    {TPS_OP_RISE_STEP, rise_step_h},
    //@op TPS55289_OP_SET_PROTECTION @alias Set protection @description How the chip protects itself
    {TPS55289_OP_SET_PROTECTION, set_protection_h},
    //@op TPS55289_OP_GET_PROTECTION @alias Read protection @description The protection settings @returns tps55289_protection_t
    {TPS55289_OP_GET_PROTECTION, get_protection_h},
};

SYS_DEVICE_TYPE_DEFINE(.type = SYS_DEVICE_TYPE_TPS55289, .cfg_size = sizeof(tps55289_cfg_t), .ctx_size = sizeof(tps_t),
                       .create = create, .uninstall = uninstall, .suspend = suspend_hook, .resume = resume_hook,
                       SYS_DEVICE_OPS(k_ops))
