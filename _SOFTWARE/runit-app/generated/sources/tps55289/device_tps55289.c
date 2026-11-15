/*
 * TPS55289: adjustable I2C buck-boost voltage regulator, type 0x0043. The whole device is this file: nothing includes
 * it, the rest of the firmware reaches it by its ID, the POWER_VREG contract (VREG) and its own op (PROTECTION,
 * 0xFF01).
 */
#include "board_time.h"
#include "sys_device_driver.h"
#include "sys_device_event.h"
#include "sys_device_timer.h"
#include "sys_event.h"
#include "sys_i2c.h"
#include "sys_io_driver.h"
#include "sys_power_vreg.h"
#include "utils.h"

SYS_DEVICE_TYPE_DECLARE(TPS55289, 0x0043)

/*******************************************************************************
 * OWN_TPS55289 : own operations (0xFFxx)
 *   OP_RISE_STEP  - timer ramp/discharge step (LOCAL)
 *   OP_PROTECTION - chip protection settings R/W (tps55289_protection_t)
 *   OP_INT        - FB/INT falling edge event (LOCAL | ALWAYS | TAIL)
 ******************************************************************************/

/** @brief Chip-level protection register settings (PROTECTION, both sides). */
typedef struct __attribute__((packed)) {
  uint8_t ocp_delay;   /**< OCP delay index (0=128us, 1=3.072ms, 2=6.144ms, 3=12.288ms) */
  uint8_t hiccup;      /**< 1 = restart on short circuit after 76ms, 0 = latch off */
  uint8_t fault_mask;  /**< Fault mask: bit 0 OVP, bit 1 OCP, bit 2 SCP */
  uint8_t discharge;   /**< 1 = active discharge on disable */
} tps55289_protection_t;

#define OWN_TPS55289_OPERATIONS(X, P)                                                               \
  X(P, OP_RISE_STEP,  0x00, cmd_none_t, cmd_none_t,            SYS_DEVICE_OP_LOCAL)             \
  X(P, OP_PROTECTION, 0x01, cmd_none_t, tps55289_protection_t, 0)                               \
  X(P, OP_INT,        0x02, cmd_none_t, sys_event_t,                                            \
    SYS_DEVICE_OP_LOCAL | SYS_DEVICE_OP_ALWAYS | SYS_DEVICE_OP_TAIL)

SYS_DEVICE_OWN_DECLARE(OWN_TPS55289)

/*******************************************************************************
 * TPS55289 : chip limits & config
 ******************************************************************************/

#define TPS55289_MIN_VOLTAGE_MV 3000u
#define TPS55289_MAX_VOLTAGE_MV 20000u
#define TPS55289_MIN_CURRENT_MA 200u
#define TPS55289_MAX_CURRENT_MA 5500u

#define TPS55289_I2C_ADDR_74 0x74u  // the MODE pin selects one of the two addresses
#define TPS55289_I2C_ADDR_75 0x75u

#define TPS55289_OCP_DELAY_MAX 3u     // VOUT_SR OCP_DELAY: 0 = 128 us, 1 = 3.072 ms, 2 = 6.144 ms, 3 = 12.288 ms
#define TPS55289_FAULT_OVP     0x01u  // fault_mask bits: the fault pulls FB/INT low, so it becomes an event
#define TPS55289_FAULT_OCP     0x02u
#define TPS55289_FAULT_SCP     0x04u
#define TPS55289_FAULT_ALL     0x07u

/** @brief CREATE's struct (wire format). If a pin is not wired, set it to SYS_IO_PIN_NONE. */
typedef struct __attribute__((packed)) {
  uint8_t i2c_bus;        /**< The bus's device ID (an I2C contract device) */
  uint8_t i2c_addr;       /**< 7-bit I2C address (0x74 or 0x75) */
  sys_io_pin_t intr_pin;  /**< Optional active-low FB/INT interrupt pin */
  sys_io_pin_t en_pin;    /**< Optional hardware enable pin */
} tps55289_cfg_t;

/* ----------------------------------------------------------------------------
 * The driver
 * ------------------------------------------------------------------------- */

#define I2C_SPEED_HZ        400000u
#define TPS55289_SHUNT_MOHM 10u  // current sense resistor of the board
#define EN_WAKEUP_MS        10u  // I2C is up ~10 ms after EN rises

#define REG_REF_LSB    0x00u
#define REG_IOUT_LIMIT 0x02u
#define REG_VOUT_SR    0x03u
#define REG_VOUT_FS    0x04u
#define REG_CDC        0x05u
#define REG_MODE       0x06u
#define REG_STATUS     0x07u

#define IOUT_LIMIT_EN     0x80u    // current limit on; [6:0] = VISP - VISN in 0.5 mV steps
#define VOUT_SR_SLEW      0x03u    // [1:0] slew rate
#define VOUT_SR_OCP_DELAY 0x30u    // [5:4] OCP response delay
#define VOUT_FS_EXT_FB    0x80u    // external feedback divider
#define VOUT_FS_INTFB     0x03u    // internal feedback ratio
#define REF_MAX           0x07FFu  // 11-bit reference
#define CDC_FAULT_MASKS   0xE0u    // SC_MASK / OCP_MASK / OVP_MASK (bits 7 / 6 / 5 = TPS55289_FAULT_* << 5)
#define CDC_FAULT_SHIFT   5u
#define MODE_OE           0x80u    // output enable
#define MODE_HICCUP       0x20u    // short circuit: 76 ms off, then soft start
#define MODE_DISCHG       0x10u    // output discharge while OE is off (EN high)
#define STATUS_SCP        0x80u
#define STATUS_OCP        0x40u
#define STATUS_OVP        0x20u

/* VOUT_SR[1:0]: slew rate of output voltage changes (reset default 2.5 mV/us). */
#define SLEW_1_25_MV_US 0

#define TPS_DEFAULT_VOLTAGE_MV 5000
/* Rise: steps up by TPS_RAMP_STEP_MV every TPS_RAMP_STEP_MS under TPS_SOFT_RISE_MA; OCP is ignored during
 * TPS_RAMP_SETTLE_MS (inrush window), then cleared and the wanted limit + VREG_EV_READY apply. */
#define TPS_RAMP_SETTLE_MS 1000
#define TPS_RAMP_START_MV  1000
#define TPS_RAMP_STEP_MV   1000
#define TPS_RAMP_STEP_MS   50
#define TPS_SOFT_RISE_MA   1000  // soft-rise cap preventing input USB sag; >= TPS_LIMIT_FLOOR_MA

// Sense offset dominates below ~5 mV (VISP - VISN, 0.5 mV/step over 10 mOhm); lower limits trigger false OCP.
#define TPS_LIMIT_FLOOR_MA 500

// With `discharge` on, OE drops first and EN stays high this long (DISCHG needs power; 100 ms at 40 mA ~ 200 uF).
#define TPS_DISCHARGE_MS   100

static const tps55289_protection_t k_default_protection = {
    .ocp_delay = 0, .hiccup = 1, .fault_mask = TPS55289_FAULT_ALL, .discharge = 0};

typedef enum {
  RISE_IDLE = 0,  // no rise: the wanted limit is on the chip
  RISE_RAMP,      // stepping the output up at the soft-rise limit
  RISE_SETTLE,    // at the target, waiting out the settle window
  RISE_FALL,      // output off (OE), discharging; the next step drives EN low
} rise_e;

typedef struct {
  uint8_t id;
  sys_io_claim_t intr;        // FB/INT: its falling edge runs our INT op
  sys_io_claim_t en;          // rail enable: locked, driven by tps_set_en
  sys_i2c_dev_t i2c;
  sys_device_timer_t timer;   // periodic while active: the next rise/discharge step (RISE_STEP)

  /* Wanted settings. The hardware EN pin has priority: while the rail is off EN is low, and the TPS55289 forgets
   * every register, so these are written again each time EN goes high (tps_apply_config). */
  uint16_t last_voltage_mV;
  uint16_t last_current_limit_mA;
  bool last_enable_state;

  uint16_t out_mV;              // voltage last written to the chip (the ramp's position)
  uint8_t rise;                 // rise_e; while rising the chip's OCP flag is the inrush, not reported
  uint64_t rise_start_ms;       // start of the settle or discharge window (board_time_uptime_ms)
  tps55289_protection_t prot;   // wanted protection, written with the other settings
} tps_t;

#define SYS_DEVICE_CTX tps_t

static inline uint16_t tps_limit(uint16_t mA) {
  return mA < TPS_LIMIT_FLOOR_MA ? TPS_LIMIT_FLOOR_MA : mA;
}

/* ---- Chip access ---- */

// The caller range-checked the wanted limit and raised it to the floor (tps_limit): 500..5500 mA = 10..110 steps.
static err_h chip_set_current_limit(tps_t* c, uint16_t limit_mA) {
  const uint8_t steps = (uint8_t)((uint32_t)limit_mA * TPS55289_SHUNT_MOHM / 500u);  // 0.5 mV steps
  const uint8_t data = IOUT_LIMIT_EN | steps;
  return sys_i2c_reg_write(&c->i2c, REG_IOUT_LIMIT, &data, 1);
}

/* Read-modify-write: a failed read is returned instead of writing a register built from 0 (a lost VOUT_FS read alone
 * would turn a 10 V request into ~21 V). A voltage the feedback ratio can't reach is refused, not clamped. */
static err_h chip_set_voltage(tps_t* c, uint16_t voltage_mV) {
  uint8_t vout_fs = 0;
  ERR_TRY(sys_i2c_reg_read(&c->i2c, REG_VOUT_FS, &vout_fs, 1));
  /* External feedback: VOUT depends on the board's divider, which the device doesn't know (datasheet 7.3.12). Only
   * internal feedback is supported. */
  ERR_CHECK(!(vout_fs & VOUT_FS_EXT_FB), ERR_POWER_EXT_FEEDBACK, .id = c->id);
  // INTFB[1:0]: reference step (in 0.1 mV) and lowest output voltage
  static const struct {
    uint16_t step_100uV;
    uint16_t min_mV;
  } k_intfb[4] = {{25, 200}, {50, 400}, {75, 600}, {100, 800}};
  const uint32_t step = k_intfb[vout_fs & VOUT_FS_INTFB].step_100uV;
  const uint32_t min_mV = k_intfb[vout_fs & VOUT_FS_INTFB].min_mV;
  const uint32_t max_mV = min_mV + REF_MAX * step / 10u;
  SYS_DEV_CHECK_IN_RANGE(c->id, voltage_mV, min_mV, max_mV);
  const uint16_t ref = (uint16_t)((voltage_mV - min_mV) * 10u / step);
  ERR_TRY(sys_i2c_reg_write_u16(&c->i2c, REG_REF_LSB, ref, SYS_I2C_LE));
  c->out_mV = voltage_mV;
  return NULL;
}

/* The protection registers: OCP delay (VOUT_SR[5:4]), hiccup and discharge (MODE[5:4], OE kept), and which faults pull
 * FB/INT low (CDC[7:5]: 1 = indicated, datasheet 7.6.5). */
static err_h chip_set_protection(tps_t* c) {
  const tps55289_protection_t* p = &c->prot;
  ERR_TRY(sys_i2c_reg_update(&c->i2c, REG_VOUT_SR, VOUT_SR_OCP_DELAY, (uint8_t)(p->ocp_delay << 4)));
  ERR_TRY(sys_i2c_reg_update(&c->i2c, REG_MODE, MODE_HICCUP | MODE_DISCHG,
                             (p->hiccup ? MODE_HICCUP : 0) | (p->discharge ? MODE_DISCHG : 0)));
  return sys_i2c_reg_update(&c->i2c, REG_CDC, CDC_FAULT_MASKS, (uint8_t)(p->fault_mask << CDC_FAULT_SHIFT));
}

/* ---- Rail control ---- */

/* True while the chip is powered and answers I2C: always without an EN pin, else only while the rail is enabled (EN
 * high) and not suspended (EN low). */
static bool tps_powered(const tps_t* c) {
  return !c->en.locked || (c->last_enable_state && !sys_device_self_suspended());
}

/* Drive EN; without an EN pin only the MODE register's output-enable bit switches the rail. The 10 ms after EN rises is
 * the one wait inside an op: the chip's own I2C start-up. */
static err_h tps_set_en(tps_t* c, bool high) {
  if (!c->en.locked) {
    return NULL;
  }
  ERR_TRY(sys_io_claim_drive(&c->en, high));
  if (high) {
    vTaskDelay(MSEC(EN_WAKEUP_MS));
  }
  return NULL;
}

/* ---- Rise: ramp + settle as timer steps ---- */

// The rise stops where it is: the output keeps its last step and the soft-rise limit (off, suspend, reset).
static void tps_rise_cancel(tps_t* c) {
  c->rise = RISE_IDLE;
  sys_device_timer_stop(&c->timer);
}

/* One step. FALL: drive EN low after the discharge window. RAMP: the next whole step above the output, or the target
 * (then SETTLE). SETTLE: once the window since rise_start_ms is over, the wanted limit, STATUS read (clears the OCP
 * the inrush latched; an overload that persists sets it again and is reported), VREG_EV_READY. Uses a periodic timer
 * so a step skipped while the lock is busy repeats on the next tick. */
static err_h tps_rise_step(tps_t* c) {
  if (c->rise == RISE_FALL) {
    if (board_time_uptime_ms() - c->rise_start_ms < TPS_DISCHARGE_MS) {
      return NULL;
    }
    tps_rise_cancel(c);
    return tps_set_en(c, false);  // discharged: the hardware switch off
  }
  if (c->rise == RISE_RAMP) {
    const uint32_t next_mV = ((uint32_t)c->out_mV / TPS_RAMP_STEP_MV + 1u) * TPS_RAMP_STEP_MV;
    if (next_mV < c->last_voltage_mV) {
      ERR_TRY(chip_set_voltage(c, (uint16_t)next_mV));
      sys_device_timer_start(&c->timer, TPS_RAMP_STEP_MS);
      return NULL;
    }
    ERR_TRY(chip_set_voltage(c, c->last_voltage_mV));
    c->rise = RISE_SETTLE;
  }
  const uint64_t elapsed_ms = board_time_uptime_ms() - c->rise_start_ms;
  if (elapsed_ms < TPS_RAMP_SETTLE_MS) {
    sys_device_timer_start(&c->timer, TPS_RAMP_STEP_MS);
    return NULL;
  }
  ERR_TRY(chip_set_current_limit(c, tps_limit(c->last_current_limit_mA)));
  uint8_t status;
  ERR_TRY(sys_i2c_reg_read(&c->i2c, REG_STATUS, &status, 1));  // clears latched fault bits
  tps_rise_cancel(c);
  return SYS_EVENT_PUBLISH(VREG_EV_READY, c->id, 0, .mV = c->last_voltage_mV);
}

// The soft-rise limit is on the chip: start the settle window and the first step now.
static err_h tps_rise_run(tps_t* c) {
  c->rise = RISE_RAMP;
  c->rise_start_ms = board_time_uptime_ms();
  err_h err = tps_rise_step(c);
  if (err != NULL) {
    tps_rise_cancel(c);
  }
  return err;
}

/* Write every wanted setting; called right after EN goes high (the chip lost them). An enabled output starts at the
 * soft-rise limit and rises from the timer. */
static err_h tps_apply_config(tps_t* c) {
  ERR_TRY(chip_set_protection(c));
  // Slowest output slew: voltage changes charge the output capacitors gently.
  ERR_TRY(sys_i2c_reg_update(&c->i2c, REG_VOUT_SR, VOUT_SR_SLEW, SLEW_1_25_MV_US));
  const uint16_t start_mA = c->last_enable_state ? TPS_SOFT_RISE_MA : c->last_current_limit_mA;
  ERR_TRY(chip_set_current_limit(c, tps_limit(start_mA)));
  const bool ramp = c->last_enable_state && c->last_voltage_mV > TPS_RAMP_START_MV;
  ERR_TRY(chip_set_voltage(c, ramp ? TPS_RAMP_START_MV : c->last_voltage_mV));
  ERR_TRY(sys_i2c_reg_update(&c->i2c, REG_MODE, MODE_OE, c->last_enable_state ? MODE_OE : 0));
  return c->last_enable_state ? tps_rise_run(c) : NULL;
}

/* Rise stopped, output off, then EN low; every step runs (EN goes low even if the I2C disable fails). With `discharge`
 * (ENABLE off, not suspend: a fault doesn't wait) EN stays high for TPS_DISCHARGE_MS while the chip sinks the output,
 * and the timer's periodic step drives it low once the window has elapsed. */
static err_h tps_rail_off(tps_t* c, bool chip_powered, bool discharge) {
  err_h err = NULL;
  tps_rise_cancel(c);
  if (chip_powered) {
    ERR_KEEP_FIRST(err, sys_i2c_reg_update(&c->i2c, REG_MODE, MODE_OE, 0));
  }
  if (discharge && err == NULL && chip_powered && c->prot.discharge && c->en.locked) {
    c->rise = RISE_FALL;
    c->rise_start_ms = board_time_uptime_ms();
    sys_device_timer_start(&c->timer, TPS_DISCHARGE_MS);
    return NULL;
  }
  ERR_KEEP_FIRST(err, tps_set_en(c, false));
  return err;
}

/* On: EN high, then every setting again (EN low cleared them); an enabled rail stays as it is. Off: output disabled,
 * then EN low - the hardware switch has the last word. */
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

/*******************************************************************************
 * CONTRACT_VREG : OP_ENABLE (write)
 * On: returns once the rise started; VREG_EV_READY when the output is settled.
 ******************************************************************************/
OP_DECLARE_WRITE(CONTRACT_VREG, OP_ENABLE) {
  ERR_CHECK(args->enable <= 1, ERR_CMD_BAD_ARG);
  return tps_set_enable(c, args->enable != 0);
}

/*******************************************************************************
 * CONTRACT_VREG : OP_VOLTAGE (write)
 * Higher on a running rail: a rise (ramp at the soft-rise limit, settle,
 * VREG_EV_READY). Lower, or the output off: written at once. During a rise the
 * ramp just follows the new target.
 ******************************************************************************/
OP_DECLARE_WRITE(CONTRACT_VREG, OP_VOLTAGE) {
  SYS_DEV_CHECK_IN_RANGE(c->id, args->voltage_mV, TPS55289_MIN_VOLTAGE_MV, TPS55289_MAX_VOLTAGE_MV);
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
    c->rise_start_ms = board_time_uptime_ms();
    sys_device_timer_start(&c->timer, TPS_RAMP_STEP_MS);
    return NULL;
  }
  ERR_TRY(chip_set_current_limit(c, TPS_SOFT_RISE_MA));
  return tps_rise_run(c);
}

/*******************************************************************************
 * CONTRACT_VREG : OP_CURRENT (write)
 * During a rise only stored: the end of the rise writes it.
 ******************************************************************************/
OP_DECLARE_WRITE(CONTRACT_VREG, OP_CURRENT) {
  SYS_DEV_CHECK_IN_RANGE(c->id, args->current_mA, TPS55289_MIN_CURRENT_MA, TPS55289_MAX_CURRENT_MA);
  c->last_current_limit_mA = (uint16_t)args->current_mA;
  if (!tps_powered(c) || c->rise != RISE_IDLE) {
    return NULL;
  }
  return chip_set_current_limit(c, tps_limit(c->last_current_limit_mA));
}

/* ---- Own ops: rise step (timer), protection, INT (FB/INT pin events) ---- */

/*******************************************************************************
 * OWN_TPS55289 : OP_RISE_STEP (write, LOCAL)
 * A failed step ends the rise where it is (soft-rise limit kept); the timer
 * reports the error.
 ******************************************************************************/
OP_DECLARE_WRITE(OWN_TPS55289, OP_RISE_STEP) {
  if (c->rise == RISE_IDLE) {
    sys_device_timer_stop(&c->timer);
    return NULL;
  }
  err_h err = tps_rise_step(c);
  if (err != NULL) {
    tps_rise_cancel(c);
  }
  return err;
}

/*******************************************************************************
 * OWN_TPS55289 : OP_PROTECTION (write)
 * Stores protection settings and writes them now if powered, else on next enable.
 ******************************************************************************/
OP_DECLARE_WRITE(OWN_TPS55289, OP_PROTECTION) {
  SYS_DEV_CHECK_IN_RANGE(c->id, args->ocp_delay, 0, TPS55289_OCP_DELAY_MAX);
  SYS_DEV_CHECK_IN_RANGE(c->id, args->fault_mask, 0, TPS55289_FAULT_ALL);
  ERR_CHECK(args->hiccup <= 1 && args->discharge <= 1, ERR_CMD_BAD_ARG);
  c->prot = *args;
  return tps_powered(c) ? chip_set_protection(c) : NULL;
}

/*******************************************************************************
 * OWN_TPS55289 : OP_PROTECTION (read)
 * Returns the current stored protection settings.
 ******************************************************************************/
OP_DECLARE_READ(OWN_TPS55289, OP_PROTECTION) {
  return cmd_reply(out, &c->prot, sizeof(c->prot));
}

/*******************************************************************************
 * OWN_TPS55289 : OP_INT (event, LOCAL | ALWAYS | TAIL)
 * Publishes each fault of the status register when FB/INT falls. OCP during a
 * rise is the inrush.
 ******************************************************************************/
OP_DECLARE_EVENT(OWN_TPS55289, OP_INT) {
  if (!tps_powered(c)) {
    return NULL;  // EN low or suspended: nothing to read, no fault possible
  }
  uint8_t status;
  ERR_TRY(sys_i2c_reg_read(&c->i2c, REG_STATUS, &status, 1));  // clears latched fault bits
  err_h err = NULL;
  if (status & STATUS_OVP) {
    ERR_KEEP_FIRST(err, SYS_EVENT_PUBLISH(VREG_EV_OVP, c->id, 0, .mV = c->last_voltage_mV));
  }
  if ((status & STATUS_OCP) && c->rise == RISE_IDLE) {
    ERR_KEEP_FIRST(err, SYS_EVENT_PUBLISH(VREG_EV_OCP, c->id, 0, .mA = c->last_current_limit_mA));
  }
  if (status & STATUS_SCP) {
    ERR_KEEP_FIRST(err, SYS_EVENT_PUBLISH(VREG_EV_SCP, c->id, 0));
  }
  return err;
}

/* ---- Lifecycle ---- */

/*******************************************************************************
 * CONTRACT_SYS_DEVICE : OP_RESET (write)
 * Back to the create defaults: rail off (EN low), 5 V / minimum current /
 * default protection wanted. Without an EN pin the chip stays powered, so the
 * protection is written now.
 ******************************************************************************/
OP_DECLARE_WRITE(CONTRACT_SYS_DEVICE, OP_RESET) {
  c->last_voltage_mV = TPS_DEFAULT_VOLTAGE_MV;
  c->last_current_limit_mA = TPS55289_MIN_CURRENT_MA;
  c->prot = k_default_protection;
  ERR_TRY(tps_set_enable(c, false));
  return c->en.locked ? NULL : chip_set_protection(c);
}

/*******************************************************************************
 * TPS55289 : suspend (lifecycle)
 * Fault safe state (and REMOVE's last state): turns the rail off while keeping
 * `last_enable_state` for resume.
 ******************************************************************************/
static err_h suspend(tps_t* c) {
  const bool was_powered = !c->en.locked || c->last_enable_state;
  return tps_rail_off(c, was_powered, false);
}

/*******************************************************************************
 * TPS55289 : resume (lifecycle)
 * Restores an enabled rail (EN high and settings re-applied with soft rise);
 * a failing resume leaves the device suspended with the rail off.
 ******************************************************************************/
static err_h resume(tps_t* c) {
  if (c->last_enable_state || !c->en.locked) {
    err_h err = tps_set_en(c, true);
    if (err == NULL) {
      err = tps_apply_config(c);
    }
    if (err != NULL) {
      ERR_KEEP_FIRST(err, tps_rail_off(c, true, false));
      return err;
    }
  }
  return NULL;
}

/*******************************************************************************
 * TPS55289 : create (init)
 * Validates I2C address, sets defaults (5 V / min current / default prot),
 * claims EN (temporarily high for I2C probe/config), attaches I2C, arms INT
 * falling edge, then drops EN low (kept on REMOVE).
 ******************************************************************************/
static err_h create(uint8_t id, tps_t* c, const tps55289_cfg_t* cfg) {
  SYS_DEV_CHECK_IN_RANGE(id, cfg->i2c_addr, TPS55289_I2C_ADDR_74, TPS55289_I2C_ADDR_75);
  c->id = id;
  c->last_voltage_mV = TPS_DEFAULT_VOLTAGE_MV;
  c->last_current_limit_mA = TPS55289_MIN_CURRENT_MA;
  c->prot = k_default_protection;
  ERR_TRY(sys_device_timer_init(id, &c->timer, OWN_TPS55289_OP_RISE_STEP));

  // EN must be driven high before the I2C probe below - the TPS55289's I2C interface is unavailable while EN is
  // low/floating, so probing first would always fail with ERR_I2C_DEV_NOT_FOUND on a fresh boot.
  ERR_TRY(sys_io_claim_output(id, &c->en, cfg->en_pin, false));
  ERR_TRY(tps_set_en(c, true));
  err_h err = sys_i2c_dev_attach(id, &c->i2c, cfg->i2c_bus, cfg->i2c_addr, I2C_SPEED_HZ);
  if (err == NULL) {
    err = sys_io_claim_input(id, &c->intr, cfg->intr_pin, IO_EV_FALLING, OWN_TPS55289_OP_INT,
                             SYS_IO_INTR_MODE_FALLING_EDGE);
  }
  if (err == NULL) {
    err = tps_apply_config(c);
  }
  // Leave EN low (rail stays off until enabled); if anything above failed, keep is still false so rollback resets EN.
  ERR_KEEP_FIRST(err, tps_set_en(c, false));
  if (err != NULL) {
    return err;
  }
  c->en.keep = true;  // on REMOVE (after suspend drove EN low), release leaves it an output driven low
  return NULL;
}

/*******************************************************************************
 * TPS55289 : type registration (k_ops & SYS_DEVICE_TYPE_DEFINE)
 * Exposes lifecycle reset, VREG contract (enable/voltage/current) and
 * TPS55289 own ops (rise step, protection R/W, INT event).
 ******************************************************************************/

static const sys_device_op_t k_ops[] = {
    OP_ENTRY_W(CONTRACT_SYS_DEVICE, OP_RESET),
    OP_ENTRY_W(CONTRACT_VREG, OP_ENABLE),  OP_ENTRY_W(CONTRACT_VREG, OP_VOLTAGE),  OP_ENTRY_W(CONTRACT_VREG, OP_CURRENT),
    OP_ENTRY_W(OWN_TPS55289, OP_RISE_STEP),
    OP_ENTRY(OWN_TPS55289, OP_PROTECTION),
    OP_ENTRY_W(OWN_TPS55289, OP_INT),
};

SYS_DEVICE_TYPE_DEFINE(TPS55289, tps55289_cfg_t, create, suspend, resume, k_ops)
