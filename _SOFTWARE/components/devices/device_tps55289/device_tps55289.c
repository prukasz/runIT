#include <stdlib.h>
#include "device_tps55289.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_device.h"
#include "sys_error.h"
#include "sys_i2c.h"
#include "sys_io.h"
#include "sys_power.h"

#define OWNER OWNER_DEVICE

#define TPS55289_I2C_FREQUENCY 400000
#define TPS55289_I2C_ADDR_74 0x74
#define TPS55289_I2C_ADDR_75 0x75
#define TPS55289_SHUNT_MOHM 10  // current sense resistor of the board

#define REG_REF_LSB 0x00
#define REG_IOUT_LIMIT 0x02
#define REG_VOUT_SR 0x03
#define REG_VOUT_FS 0x04
#define REG_CDC 0x05
#define REG_MODE 0x06
#define REG_STATUS 0x07

#define MODE_OE 0x80  // output enable
#define STATUS_SCP 0x80
#define STATUS_OCP 0x40
#define STATUS_OVP 0x20

/* VOUT_SR[1:0]: slew rate of output voltage changes (reset default 2.5 mV/us). */
#define SLEW_1_25_MV_US 0

// Instance state: the whole device in one struct (the create cfg is not kept).
typedef struct tps_ctx_t {
  sys_device_base_t base;  // must be first
  sys_i2c_dev_t i2c;
  sys_io_pin_ref_t intr_pin;  // the pins of the create cfg, converted once at install
  sys_io_pin_ref_t en_pin;

  uint8_t status;  // STATUS register of the last read

  /* Wanted settings. The hardware EN pin has priority: while the rail is off EN
     is low, and the TPS55289 forgets every register, so these are written
     again each time EN goes high (tps_apply_config). */
  uint16_t last_voltage_mV;
  uint16_t last_current_limit_mA;
  bool last_enable_state;
  bool is_current_limit_enabled;
  /* True while an output rise charges the capacitors (tps_soft_rise): the
     chip's OCP flag is expected then and not reported. */
  volatile bool rising;

  uint8_t intr_sub; /* sys_event subscription on intr_pin */
} tps_ctx_t;

#define TPS_DEFAULT_VOLTAGE_MV 5000
/* Raising the output charges its capacitors (VOUT_SR slew, set to the slowest 1.25 mV/us;
   3.6 ms soft start after OE). With a high current limit that inrush comes
   straight from the input and can pull a USB source down, so every rise runs
   at the soft-rise current limit (TPS_SOFT_RISE_MA) and the wanted limit is applied after this
   settle time. Note: the limit is VISP - VISN in 0.5 mV steps over the sense
   resistor, specified only at 30 / 50 mV (datasheet 6.5), so limits below ~1 A
   (a few mV) are coarse: on the PCB 250 / 350 mA settings held ~150 / ~240 mA. */
#define TPS_RAMP_SETTLE_MS 100
/* Current limit while the output rises. The limit is VISP - VISN over the
   10 mOhm sense resistor, so 200 mA is 2 mV - within the chip's offset: a rail
   then flags OCP just charging its capacitors (seen on rail A). 500 mA = 5 mV. */
#define TPS_SOFT_RISE_MA 500

/* Lowest limit written to the chip. Below ~5 mV sense the offset dominates: on
   the PCB rail A's chip sat in current limit with no load at a 200 mA (2 mV)
   setting and flagged OCP right after enable. Lower requests are raised to it. */
#define TPS_LIMIT_FLOOR_MA TPS_SOFT_RISE_MA

/* TEMP bring-up (short test): cap every current limit written to the chip,
   below the floor if needed. 0 = no cap. */
#define TPS_TEST_CURRENT_CAP_MA 350
static inline uint16_t tps_limit(uint16_t mA) {
  if (mA < TPS_LIMIT_FLOOR_MA) mA = TPS_LIMIT_FLOOR_MA;
  return (TPS_TEST_CURRENT_CAP_MA && mA > TPS_TEST_CURRENT_CAP_MA) ? TPS_TEST_CURRENT_CAP_MA : mA;
}

// Install steps, recorded so teardown rolls back only what was actually built
enum { TPS_STEP_I2C_ADDED = 0, TPS_STEP_EN_READY = 1, TPS_STEP_INTR_READY = 2, TPS_STEP_INTR_SUB = 3 };

/* ---- Chip access: err_h, the dispatcher above adds the device id ---- */

/* Read-modify-write: a failed read is returned instead of writing a register built from 0
   (a lost VOUT_FS read alone would turn a 10 V request into ~21 V). */
static SE_MUST_USE err_h chip_set_output_enable(tps_ctx_t* c, bool enable) {
  return sys_i2c_reg_update(&c->i2c, REG_MODE, MODE_OE, enable ? MODE_OE : 0);
}

static SE_MUST_USE err_h chip_set_current_limit(tps_ctx_t* c, bool enable, uint16_t limit_mA) {
  float v_ilim_mV = (limit_mA * TPS55289_SHUNT_MOHM) / 1000.0f;
  if (v_ilim_mV > 63.5f) v_ilim_mV = 63.5f;
  uint8_t reg_val = (uint8_t)(v_ilim_mV / 0.5f);
  if (reg_val > 127) reg_val = 127;
  uint8_t data = (enable ? 0x80 : 0x00) | (reg_val & 0x7F);
  return sys_i2c_reg_write(&c->i2c, REG_IOUT_LIMIT, &data, 1);
}

static SE_MUST_USE err_h chip_set_voltage(tps_ctx_t* c, uint16_t voltage_mV) {
  uint8_t vout_fs = 0;
  SE_TRY(sys_i2c_reg_read(&c->i2c, REG_VOUT_FS, &vout_fs, 1));
  if (vout_fs & 0x80) {
    /* External feedback: VOUT depends on the board's divider, which the device doesn't
       know (datasheet 7.3.12). Only internal feedback is supported. */
    SE_FAIL(ERR_BASE_NOT_SUPPORTED, 0);
  }
  // INTFB[1:0]: reference step and lowest output voltage
  static const struct {
    float step_mV;
    uint16_t min_mV;
  } k_intfb[4] = {{2.5f, 200}, {5.0f, 400}, {7.5f, 600}, {10.0f, 800}};
  const float step_mV = k_intfb[vout_fs & 0x03].step_mV;
  const uint16_t min_mV = k_intfb[vout_fs & 0x03].min_mV;
  if (voltage_mV < min_mV) voltage_mV = min_mV;
  uint16_t ref = (uint16_t)((voltage_mV - min_mV) / step_mV);
  if (ref > 0x07FF) ref = 0x07FF;
  const uint8_t data[2] = {(uint8_t)(ref & 0xFF), (uint8_t)(ref >> 8)};
  return sys_i2c_reg_write(&c->i2c, REG_REF_LSB, data, sizeof(data));
}

static SE_MUST_USE err_h chip_set_slew_rate(tps_ctx_t* c, uint8_t rate) {
  return sys_i2c_reg_update(&c->i2c, REG_VOUT_SR, 0x03, rate);  // OCP_DELAY [5:4] kept
}

/* CDC bits 7-5 (SC_MASK / OCP_MASK / OVP_MASK): 1 = the fault is indicated on
   the FB/INT pin (reset default), 0 = not indicated (datasheet 7.6.5). */
static SE_MUST_USE err_h chip_set_fault_reporting(tps_ctx_t* c, bool scp, bool ocp, bool ovp) {
  return sys_i2c_reg_update(&c->i2c, REG_CDC, 0xE0, (scp ? 0x80 : 0) | (ocp ? 0x40 : 0) | (ovp ? 0x20 : 0));
}

static SE_MUST_USE err_h chip_get_status(tps_ctx_t* c) { return sys_i2c_reg_read(&c->i2c, REG_STATUS, &c->status, 1); }

/* ---- Rail control ---- */

/* True while the chip is powered and answers I2C: always without an EN pin,
   else only while the rail is enabled (EN high). */
static inline bool tps_powered(const tps_ctx_t* c) {
  return !sys_io_pin_is_valid(c->en_pin) || c->last_enable_state;
}

/* Drive EN; without an EN pin only the MODE register's output-enable bit switches the rail. */
static SE_MUST_USE err_h tps_set_en(tps_ctx_t* c, bool high) {
  if (sys_io_pin_is_valid(c->en_pin)) {
    SYS_DEV_TRY(sys_io_set_locked_level(c->en_pin, high), c);
    if (high) vTaskDelay(pdMS_TO_TICKS(10));  // I2C is up ~10 ms after EN rises
  }
  return NULL;
}

/* The output is rising (enable, or a higher voltage): hold the soft-rise current
   limit (TPS_SOFT_RISE_MA) while it settles, then apply the wanted one. */
static SE_MUST_USE err_h tps_soft_rise(tps_ctx_t* c) {
  vTaskDelay(pdMS_TO_TICKS(TPS_RAMP_SETTLE_MS));
  SE_TRY(chip_set_current_limit(c, c->is_current_limit_enabled, tps_limit(c->last_current_limit_mA)));
  // Clear the OCP the rise may have latched; if the overload persists the
  // chip sets it again right away and the fault is reported normally.
  SE_TRY(chip_get_status(c));
  c->rising = false;
  return NULL;
}

/* Write every wanted setting; called right after EN goes high (the chip lost them).
   An enabled output starts at the soft-rise limit (tps_soft_rise). */
static SE_MUST_USE err_h tps_apply_config(tps_ctx_t* c) {
  if (sys_io_pin_is_valid(c->intr_pin)) {
    // Every fault raises the alert; listeners decide what matters.
    SE_TRY(chip_set_fault_reporting(c, true, true, true));
  }
  // Slowest output slew: voltage changes charge the output capacitors gently.
  SE_TRY(chip_set_slew_rate(c, SLEW_1_25_MV_US));
  uint16_t start_mA = c->last_enable_state ? TPS_SOFT_RISE_MA : c->last_current_limit_mA;
  c->rising = c->last_enable_state;
  SE_TRY(chip_set_current_limit(c, c->is_current_limit_enabled, tps_limit(start_mA)));
  SE_TRY(chip_set_voltage(c, c->last_voltage_mV));
  SE_TRY(chip_set_output_enable(c, c->last_enable_state));
  if (c->last_enable_state) SE_TRY(tps_soft_rise(c));
  return NULL;
}

/* Publishes each fault in the status register. */
static SE_MUST_USE err_h handle_faults(const sys_event_t* event, tps_ctx_t* c) {
  if (!tps_powered(c)) return NULL;  // EN low: nothing to read, no fault possible
  SE_TRY(chip_get_status(c));

  err_h err = NULL;
  uint8_t hops = SYS_EVENT_CAUSED_BY(event);
  if (c->status & STATUS_OVP) {
    SYS_DEV_TEARDOWN_STEP(err, sys_power_publish(SYS_DEV_GET_ID(c), 0, SYS_PWR_EVENT_OVP, c->last_voltage_mV, hops));
  }
  if ((c->status & STATUS_OCP) && !c->rising) {
    SYS_DEV_TEARDOWN_STEP(err, sys_power_publish(SYS_DEV_GET_ID(c), 0, SYS_PWR_EVENT_OCP_CRITICAL, c->last_current_limit_mA, hops));
  }
  if (c->status & STATUS_SCP) {
    SYS_DEV_TEARDOWN_STEP(err, sys_power_publish(SYS_DEV_GET_ID(c), 0, SYS_PWR_EVENT_SPC, 0, hops));
  }
  return err;
}

/* Inline listener of intr_pin. No dispatcher wraps a listener's error, so it carries the device id here. */
static SE_MUST_USE err_h device_event_handler(const sys_event_t* event, void* handle) {
  SYS_DEV_CTX_FROM(tps_ctx_t, c, handle);
  err_h err = handle_faults(event, c);
  return SYS_DEV_WRAP(err, SYS_DEV_GET_ID(c));
}

/* ---- Voltage regulator contract ---- */

/* On: EN high, then every setting again (EN low cleared them). Off: output
   disabled, then EN low - the hardware switch has the last word. */
static SE_MUST_USE err_h vreg_set_enable(void* handle, bool state) {
  SYS_DEV_CTX_FROM(tps_ctx_t, c, handle);
  bool was_powered = tps_powered(c);
  c->last_enable_state = state;
  c->rising = false;

  if (state) {
    SE_TRY(tps_set_en(c, true));
    SE_TRY(tps_apply_config(c));
    return NULL;
  }
  err_h err = NULL;
  if (was_powered) SYS_DEV_TEARDOWN_STEP(err, chip_set_output_enable(c, false));
  SYS_DEV_TEARDOWN_STEP(err, tps_set_en(c, false));
  return err;
}

static SE_MUST_USE err_h vreg_set_voltage(void* handle, uint32_t voltage_mV) {
  SYS_DEV_CTX_FROM(tps_ctx_t, c, handle);
  SE_CHECK_IN_RANGE(voltage_mV, DEVICE_TPS55289_MIN_VOLTAGE_MV, DEVICE_TPS55289_MAX_VOLTAGE_MV);
  bool rising = voltage_mV > c->last_voltage_mV;
  c->last_voltage_mV = voltage_mV;
  if (!tps_powered(c)) return NULL;  // applied by the next enable
  if (rising) {
    c->rising = true;
    SE_TRY(chip_set_current_limit(c, c->is_current_limit_enabled, tps_limit(TPS_SOFT_RISE_MA)));
  }
  SE_TRY(chip_set_voltage(c, voltage_mV));
  if (rising) SE_TRY(tps_soft_rise(c));
  return NULL;
}

static SE_MUST_USE err_h vreg_set_current(void* handle, uint32_t current_mA) {
  SYS_DEV_CTX_FROM(tps_ctx_t, c, handle);
  SE_CHECK_IN_RANGE(current_mA, DEVICE_TPS55289_MIN_CURRENT_MA, DEVICE_TPS55289_MAX_CURRENT_MA);
  c->last_current_limit_mA = current_mA;
  if (!tps_powered(c)) return NULL;  // applied by the next enable
  return chip_set_current_limit(c, c->is_current_limit_enabled, tps_limit(current_mA));
}

static const sys_power_vreg_contract_t s_tps55289_vreg_contract = {
    .set_enable = vreg_set_enable, .set_voltage = vreg_set_voltage, .set_current = vreg_set_current};

/* ---- Lifecycle ---- */

// Doubles as the install rollback path: each step is gated on having actually
// run, and no step may early-return - teardown must always free everything.
static SE_MUST_USE err_h device_uninstall(void* handle) {
  SYS_DEV_CTX_FROM(tps_ctx_t, c, handle);
  err_h err = NULL;

  IF_SYS_DEV_STEP_DONE(c, TPS_STEP_EN_READY) {
    SYS_DEV_TEARDOWN_STEP(err, sys_io_unlock_pin(c->en_pin));
    SYS_DEV_TEARDOWN_STEP(err, sys_io_set_level(c->en_pin, false));
    SYS_DEV_TEARDOWN_STEP(err, sys_io_reset(c->en_pin));
  }
  IF_SYS_DEV_STEP_DONE(c, TPS_STEP_INTR_SUB) {
    SYS_DEV_TEARDOWN_STEP(err, sys_event_unsubscribe(c->intr_sub, false));
  }
  IF_SYS_DEV_STEP_DONE(c, TPS_STEP_INTR_READY) {
    SYS_DEV_TEARDOWN_STEP(err, sys_io_unlock_pin(c->intr_pin));
    SYS_DEV_TEARDOWN_STEP(err, sys_io_reset(c->intr_pin));
  }
  IF_SYS_DEV_STEP_DONE(c, TPS_STEP_I2C_ADDED) {
    SYS_DEV_TEARDOWN_STEP(err, sys_i2c_dev_remove(&c->i2c));
  }

  free(c);
  return err;
}

/* Back to the install defaults: rail off (EN low), 5 V / minimum current wanted. */
static SE_MUST_USE err_h device_reset(void* handle) {
  SYS_DEV_CTX_FROM(tps_ctx_t, c, handle);
  c->last_voltage_mV = TPS_DEFAULT_VOLTAGE_MV;
  c->last_current_limit_mA = DEVICE_TPS55289_MIN_CURRENT_MA;
  c->is_current_limit_enabled = true;
  return vreg_set_enable(handle, false);
}

// Fault safe state: EN is driven low even if the I2C disable fails. The wanted
// state (last_enable_state) is kept for resume.
static SE_MUST_USE err_h device_suspend(void* handle) {
  SYS_DEV_CTX_FROM(tps_ctx_t, c, handle);
  err_h err = NULL;
  c->rising = false;

  if (tps_powered(c)) SYS_DEV_TEARDOWN_STEP(err, chip_set_output_enable(c, false));
  SYS_DEV_TEARDOWN_STEP(err, tps_set_en(c, false));
  return err;
}

// Back to the wanted state: an enabled rail gets EN high and every setting again.
static SE_MUST_USE err_h device_resume(void* handle) {
  SYS_DEV_CTX_FROM(tps_ctx_t, c, handle);
  if (!tps_powered(c)) return NULL;
  SE_TRY(tps_set_en(c, true));
  return tps_apply_config(c);
}

static SE_MUST_USE err_h device_install(const void* cfg_blob, void** out_device_handle) {
  const d_tps55289_cfg_t* cfg = (const d_tps55289_cfg_t*)cfg_blob;
  SE_CHECK_IN_RANGE(cfg->i2c_addr, TPS55289_I2C_ADDR_74, TPS55289_I2C_ADDR_75);

  // The cfg is read only here: what the device keeps (the pins) goes into its own state.
  tps_ctx_t* c = (tps_ctx_t*)calloc(1, sizeof(tps_ctx_t));
  SE_CHECK_IF_ALLOCATED(c);
  c->base.device_id = cfg->device_id;
  c->intr_pin = cfg->intr_pin;
  c->en_pin = cfg->en_pin;
  c->last_voltage_mV = TPS_DEFAULT_VOLTAGE_MV;
  c->last_current_limit_mA = DEVICE_TPS55289_MIN_CURRENT_MA;
  c->last_enable_state = false;
  c->is_current_limit_enabled = true;
  err_h err = NULL;

  // EN must be driven high before the I2C probe below - the TPS55289's I2C
  // interface is unavailable while EN is low/floating, so probing first
  // would always fail with ERR_I2C_DEV_NOT_FOUND on a fresh boot.
  if (sys_io_pin_is_valid(c->en_pin)) {
    SYS_DEV_INSTALL_STEP(sys_io_set_mode(c->en_pin), "en pin mode");
    SYS_DEV_INSTALL_STEP(sys_io_set_level(c->en_pin, true), "en pin high");
    SYS_DEV_INSTALL_STEP_BIT(c, TPS_STEP_EN_READY, sys_io_lock_pin(c->en_pin), "en pin lock");
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  sys_i2c_dev_init(&c->i2c, cfg->i2c_bus != 0, cfg->i2c_addr, TPS55289_I2C_FREQUENCY);
  SYS_DEV_INSTALL_STEP_BIT(c, TPS_STEP_I2C_ADDED, sys_i2c_dev_add(&c->i2c), "i2c add (probes the chip)");

  // Configure interrupt pin & callback
  if (sys_io_pin_is_valid(c->intr_pin)) {
    SYS_DEV_INSTALL_STEP(sys_io_set_mode(c->intr_pin), "intr pin mode");
    SYS_DEV_INSTALL_STEP_BIT(c, TPS_STEP_INTR_SUB, sys_io_subscribe_pin(c->intr_pin, device_event_handler, c, &c->intr_sub), "intr pin subscribe");
    sys_io_intr_config_t intr_cfg = {.mode = SYS_IO_INTR_MODE_FALLING_EDGE};
    SYS_DEV_INSTALL_STEP(sys_io_configure_intr(c->intr_pin, &intr_cfg), "intr pin configure");
    SYS_DEV_INSTALL_STEP_BIT(c, TPS_STEP_INTR_READY, sys_io_lock_pin(c->intr_pin), "intr pin lock");
  }

  // Probe with EN high (above), write the defaults with the output off, then
  // hand over to the hardware switch: the rail stays off (EN low) until enabled.
  SYS_DEV_INSTALL_STEP(tps_apply_config(c), "tps apply defaults");
  SYS_DEV_INSTALL_STEP(tps_set_en(c, false), "en pin low");

  *out_device_handle = c;
  return NULL;

fail:
  SYS_DEV_INSTALL_FAIL(err, out_device_handle, device_uninstall, c);
  return NULL;
}

static const uint8_t s_pin_refs[] = {offsetof(d_tps55289_cfg_t, intr_pin), offsetof(d_tps55289_cfg_t, en_pin)};

const sys_device_class_t g_tps55289_class = {
    .type_id = TPS55289_TYPE_ID,
    .cfg_size = sizeof(d_tps55289_cfg_t),
    SYS_DEVICE_PINS(s_pin_refs),
    .name = "TPS55289_VREG",
    .contracts = {[SYS_DEVICE_CONTRACT_POWER_VREG] = &s_tps55289_vreg_contract},
    .ops = {.install = device_install,
        .uninstall = device_uninstall,
        .reset = device_reset,
        .suspend = device_suspend,
        .resume = device_resume},
};
