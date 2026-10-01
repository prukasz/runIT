#include <stdint.h>
#include <stdlib.h>
#include "device_ina3221.h"
#include "sys_device.h"
#include "sys_i2c.h"
#include "sys_io.h"
#include "sys_power.h"

#define OWNER OWNER_DEVICE

#define INA3221_I2C_FREQUENCY 400000
#define INA3221_I2C_ADDR_GND 0x40  // A0 to GND
#define INA3221_I2C_ADDR_SCL 0x43  // A0 to SCL
#define INA3221_CHANNELS 3
#define INA3221_SHUNT_MOHM 10      // all three shunts on the board

#define REG_CONFIG 0x00
#define REG_SHUNTVOLTAGE_1 0x01
#define REG_BUSVOLTAGE_1 0x02
#define REG_CRITICAL_ALERT_1 0x07
#define REG_WARNING_ALERT_1 0x08
#define REG_MASK 0x0F

#define DEFAULT_CONFIG 0x7127
#define DEFAULT_MASK 0x0002
#define MASK_WRITABLE 0x7C00  // alert latches and sum-channel enables

typedef union {
  struct {
    uint16_t esht : 1;  // shunt measurement enable (LSB)
    uint16_t ebus : 1;  // bus measurement enable
    uint16_t mode : 1;  // continuous (1) / power-down (0)
    uint16_t vsht : 3;  // shunt conversion time
    uint16_t vbus : 3;  // bus conversion time
    uint16_t avg : 3;   // averaging
    uint16_t ch3 : 1;
    uint16_t ch2 : 1;
    uint16_t ch1 : 1;
    uint16_t rst : 1;   // reset (MSB)
  };
  uint16_t reg;
} ina_config_t;

typedef union {
  struct {
    uint16_t cvrf : 1;  // conversion ready (LSB)
    uint16_t tcf : 1;   // timing control
    uint16_t pvf : 1;   // power valid
    uint16_t wf : 3;    // warning alert flags (read to clear)
    uint16_t sf : 1;    // sum alert flag
    uint16_t cf : 3;    // critical alert flags (read to clear)
    uint16_t cen : 1;   // critical alert latch
    uint16_t wen : 1;   // warning alert latch
    uint16_t scc3 : 1;
    uint16_t scc2 : 1;
    uint16_t scc1 : 1;
    uint16_t : 1;       // reserved (MSB)
  };
  uint16_t reg;
} ina_mask_t;

// Instance state: the whole device in one struct (chip registers, pins, bus; the create cfg is not kept).
typedef struct ina_ctx_t {
  sys_device_base_t base;  // must be first
  sys_i2c_dev_t i2c;
  sys_io_pin_ref_t crit_pin;  // the alert pins of the create cfg, converted once at install
  sys_io_pin_ref_t warn_pin;
  uint8_t inverted_mask;      // bit N set: channel N's shunt is wired reversed

  ina_config_t config;  // registers as last written
  ina_mask_t mask;      // also holds the alert flags of the last status read

  uint8_t crit_sub;  // sys_event subscriptions on the alert pins
  uint8_t warn_sub;

  // Reversed-shunt channel armed on the critical [0] / warning [1] pin, -1 = none (set_inverted_alert).
  int8_t inverted_alert[2];
} ina_ctx_t;

// Install steps, recorded so teardown rolls back only what was actually built
enum { INA_STEP_I2C_ADDED = 0, INA_STEP_CRIT_READY = 1, INA_STEP_WARN_READY = 2, INA_STEP_CRIT_SUB = 3, INA_STEP_WARN_SUB = 4 };

#define INA_FEATURE_SET_ALERT 2 /* index in sys_power_monitor_feature_names */

static SE_MUST_USE err_h device_event_handler(const sys_event_t* event, void* handle);

/* ---- Chip access: err_h, the dispatcher above adds the device id ---- */

static SE_MUST_USE err_h chip_read(ina_ctx_t* c, uint8_t reg, uint16_t* val) {
  uint8_t b[2];
  SE_TRY(sys_i2c_reg_read(&c->i2c, reg, b, sizeof(b)));
  *val = (uint16_t)((b[0] << 8) | b[1]);
  return NULL;
}

static SE_MUST_USE err_h chip_write(ina_ctx_t* c, uint8_t reg, uint16_t val) {
  const uint8_t b[2] = {(uint8_t)(val >> 8), (uint8_t)val};
  return sys_i2c_reg_write(&c->i2c, reg, b, sizeof(b));
}

static SE_MUST_USE err_h chip_write_config(ina_ctx_t* c) { return chip_write(c, REG_CONFIG, c->config.reg); }

static SE_MUST_USE err_h chip_write_mask(ina_ctx_t* c) { return chip_write(c, REG_MASK, c->mask.reg & MASK_WRITABLE); }

/* Reads the mask register: the alert flags in it are cleared by the read. */
static SE_MUST_USE err_h chip_get_status(ina_ctx_t* c) { return chip_read(c, REG_MASK, &c->mask.reg); }

static SE_MUST_USE err_h chip_set_options(ina_ctx_t* c, bool bus, bool mode, bool shunt) {
  c->config.mode = mode;
  c->config.ebus = bus;
  c->config.esht = shunt;
  return chip_write_config(c);
}

static SE_MUST_USE err_h chip_enable_latch(ina_ctx_t* c, bool warning, bool critical) {
  c->mask.wen = warning;
  c->mask.cen = critical;
  return chip_write_mask(c);
}

static SE_MUST_USE err_h chip_reset(ina_ctx_t* c) {
  c->config.reg = DEFAULT_CONFIG;
  c->mask.reg = DEFAULT_MASK;
  c->config.rst = 1;
  return chip_write_config(c);
}

static SE_MUST_USE err_h chip_read_bus_voltage(ina_ctx_t* c, uint8_t channel, int32_t* out_mV) {
  uint16_t raw;
  SE_TRY(chip_read(c, REG_BUSVOLTAGE_1 + channel * 2, &raw));
  *out_mV = ((int16_t)raw >> 3) * 8;  // 8 mV per LSB
  return NULL;
}

/* Current as the board sees it: flipped for a reversed shunt (inverted_mask). */
static SE_MUST_USE err_h chip_read_current(ina_ctx_t* c, uint8_t channel, int32_t* out_mA) {
  uint16_t raw;
  SE_TRY(chip_read(c, REG_SHUNTVOLTAGE_1 + channel * 2, &raw));
  float mV = ((int16_t)raw >> 3) * 0.04f;  // 40 uV per LSB
  *out_mA = (int32_t)(mV * 1000.0f) / INA3221_SHUNT_MOHM;
  if (c->inverted_mask & (1u << channel)) *out_mA = -*out_mA;
  return NULL;
}

static SE_MUST_USE err_h chip_set_alert(ina_ctx_t* c, uint8_t channel, int32_t current_mA, bool critical) {
  float limit_mV = ((float)current_mA * INA3221_SHUNT_MOHM) / 1000.0f;
  int16_t raw = (int16_t)(limit_mV / 0.04f);  // 40 uV per LSB
  return chip_write(c, (critical ? REG_CRITICAL_ALERT_1 : REG_WARNING_ALERT_1) + channel * 2, (uint16_t)raw << 3);
}

/* ---- Power monitor contract ---- */

static SE_MUST_USE err_h monitor_get_voltage(void* handle, uint8_t channel, int32_t* out_mV) {
  SYS_DEV_CTX_FROM(ina_ctx_t, c, handle);
  SE_CHECK_HANDLE(out_mV);
  SE_CHECK_IN_RANGE(channel, 0, INA3221_CHANNELS - 1);
  return chip_read_bus_voltage(c, channel, out_mV);
}

static SE_MUST_USE err_h monitor_get_current(void* handle, uint8_t channel, int32_t* out_mA) {
  SYS_DEV_CTX_FROM(ina_ctx_t, c, handle);
  SE_CHECK_HANDLE(out_mA);
  SE_CHECK_IN_RANGE(channel, 0, INA3221_CHANNELS - 1);
  return chip_read_current(c, channel, out_mA);
}

/* An alert pin's interrupt edge; the pin stays locked to this device. */
static SE_MUST_USE err_h set_pin_edge(sys_io_pin_ref_t pin, sys_io_intr_mode_e mode) {
  SE_TRY(sys_io_unlock_pin(pin));
  sys_io_intr_config_t intr_cfg = {.mode = mode};
  err_h err = sys_io_configure_intr(pin, &intr_cfg);
  SYS_DEV_TEARDOWN_STEP(err, sys_io_lock_pin(pin));
  return err;
}

/* A reversed shunt reads negative and the chip compares signed, so the limit
   is -threshold: the alert is active while the current is below the threshold
   and releases above it (checked on the PCB). It is transparent (not latched),
   and its pin takes this one channel only - an active channel holds the shared
   pin and would hide the others. The pin's release (rising edge) is the alert. */
static SE_MUST_USE err_h set_inverted_alert(ina_ctx_t* c, uint8_t channel, bool critical, int32_t threshold_mA) {
  SE_CHECK_IN_RANGE(threshold_mA, 1, INT32_MAX);
  sys_io_pin_ref_t pin = critical ? c->crit_pin : c->warn_pin;
  if (!sys_io_pin_is_valid(pin)) SE_FAIL(ERR_DEV_FEATURE_UNAVAILABLE, SYS_DEV_GET_ID(c), SYS_DEVICE_CONTRACT_POWER_MONITOR, INA_FEATURE_SET_ALERT);
  int8_t* armed = &c->inverted_alert[critical ? 0 : 1];
  if (*armed >= 0 && *armed != channel) SE_FAIL(ERR_IO_PIN_ALREADY_IN_USE, pin.device_id, pin.pin, pin.mode);
  // Rising edge first: arming pulls the pin low (its normal state now), which must not count.
  if (*armed < 0) SE_TRY(set_pin_edge(pin, SYS_IO_INTR_MODE_RISING_EDGE));
  SE_TRY(chip_enable_latch(c, critical ? c->mask.wen : false, critical ? false : c->mask.cen));
  SE_TRY(chip_set_alert(c, channel, -threshold_mA, critical));
  *armed = (int8_t)channel;
  return NULL;
}

/* After a chip reset (limits at their defaults): the armed reversed-shunt alerts are gone, their pins back to the falling edge. */
static SE_MUST_USE err_h disarm_inverted_alerts(ina_ctx_t* c) {
  err_h err = NULL;
  const sys_io_pin_ref_t pins[2] = {c->crit_pin, c->warn_pin};
  for (int i = 0; i < 2; i++) {
    if (c->inverted_alert[i] < 0) continue;
    c->inverted_alert[i] = -1;
    SYS_DEV_TEARDOWN_STEP(err, set_pin_edge(pins[i], SYS_IO_INTR_MODE_FALLING_EDGE));
  }
  return err;
}

static SE_MUST_USE err_h monitor_set_alert(void* handle, uint8_t channel, sys_power_events_e alert, int32_t threshold_mA) {
  SYS_DEV_CTX_FROM(ina_ctx_t, c, handle);
  SE_CHECK_IN_RANGE(channel, 0, INA3221_CHANNELS - 1);
  bool critical = alert == SYS_PWR_EVENT_OCP_CRITICAL;
  if (!critical && alert != SYS_PWR_EVENT_OCP_WARNING) SE_FAIL(ERR_DEV_FEATURE_UNAVAILABLE, SYS_DEV_GET_ID(c), 0, alert);
  if (c->inverted_mask & (1u << channel)) return set_inverted_alert(c, channel, critical, threshold_mA);
  return chip_set_alert(c, channel, threshold_mA, critical);
}

static const sys_power_monitor_contract_t s_ina3221_monitor_contract = {
    .get_voltage = monitor_get_voltage, .get_current = monitor_get_current, .set_alert = monitor_set_alert};

/* ---- Lifecycle ---- */

// Doubles as the install rollback path: each step is gated on having actually
// run, and no step may early-return - teardown must always free everything.
static SE_MUST_USE err_h device_uninstall(void* handle) {
  SYS_DEV_CTX_FROM(ina_ctx_t, c, handle);
  err_h err = NULL;

  IF_SYS_DEV_STEP_DONE(c, INA_STEP_CRIT_SUB) {
    SYS_DEV_TEARDOWN_STEP(err, sys_event_unsubscribe(c->crit_sub, false));
  }
  IF_SYS_DEV_STEP_DONE(c, INA_STEP_WARN_SUB) {
    SYS_DEV_TEARDOWN_STEP(err, sys_event_unsubscribe(c->warn_sub, false));
  }
  IF_SYS_DEV_STEP_DONE(c, INA_STEP_CRIT_READY) {
    SYS_DEV_TEARDOWN_STEP(err, sys_io_unlock_pin(c->crit_pin));
    SYS_DEV_TEARDOWN_STEP(err, sys_io_reset(c->crit_pin));
  }
  IF_SYS_DEV_STEP_DONE(c, INA_STEP_WARN_READY) {
    SYS_DEV_TEARDOWN_STEP(err, sys_io_unlock_pin(c->warn_pin));
    SYS_DEV_TEARDOWN_STEP(err, sys_io_reset(c->warn_pin));
  }
  IF_SYS_DEV_STEP_DONE(c, INA_STEP_I2C_ADDED) {
    SYS_DEV_TEARDOWN_STEP(err, sys_i2c_dev_remove(&c->i2c));
  }

  free(c);
  return err;
}

static SE_MUST_USE err_h device_reset(void* handle) {
  SYS_DEV_CTX_FROM(ina_ctx_t, c, handle);
  SE_TRY(chip_reset(c));
  SE_TRY(disarm_inverted_alerts(c));
  // Alert latches and measurement options back on (warning and critical alert latch)
  SE_TRY(chip_enable_latch(c, true, true));
  return chip_set_options(c, true, true, true);
}

// Power-down mode (mode bit 0)
static SE_MUST_USE err_h device_suspend(void* handle) {
  SYS_DEV_CTX_FROM(ina_ctx_t, c, handle);
  return chip_set_options(c, false, false, false);
}

// Back to continuous mode
static SE_MUST_USE err_h device_resume(void* handle) {
  SYS_DEV_CTX_FROM(ina_ctx_t, c, handle);
  return chip_set_options(c, true, true, true);
}

static SE_MUST_USE err_h device_install(const void* cfg_blob, void** out_device_handle) {
  const d_ina3221_cfg_t* cfg = (const d_ina3221_cfg_t*)cfg_blob;
  SE_CHECK_IN_RANGE(cfg->i2c_addr, INA3221_I2C_ADDR_GND, INA3221_I2C_ADDR_SCL);
  SE_CHECK_IN_RANGE(cfg->inverted_mask, 0, 0x07);

  // The cfg is read only here: what the device keeps goes into its own state.
  ina_ctx_t* c = (ina_ctx_t*)calloc(1, sizeof(ina_ctx_t));
  SE_CHECK_IF_ALLOCATED(c);
  c->base.device_id = cfg->device_id;
  c->crit_pin = cfg->crit_pin;
  c->warn_pin = cfg->warn_pin;
  c->inverted_mask = cfg->inverted_mask;
  c->inverted_alert[0] = c->inverted_alert[1] = -1;
  c->config.reg = DEFAULT_CONFIG;
  c->mask.reg = DEFAULT_MASK;
  err_h err = NULL;

  sys_i2c_dev_init(&c->i2c, cfg->i2c_bus != 0, cfg->i2c_addr, INA3221_I2C_FREQUENCY);
  SYS_DEV_INSTALL_STEP_BIT(c, INA_STEP_I2C_ADDED, sys_i2c_dev_add(&c->i2c), "i2c add (probes the chip)");

  // Alert pins (critical, then warning): mode, listener, falling edge, lock
  const struct {
    sys_io_pin_ref_t pin;
    uint8_t* sub;
    uint8_t sub_step, ready_step;
  } alert_pins[] = {{c->crit_pin, &c->crit_sub, INA_STEP_CRIT_SUB, INA_STEP_CRIT_READY},
      {c->warn_pin, &c->warn_sub, INA_STEP_WARN_SUB, INA_STEP_WARN_READY}};
  for (size_t i = 0; i < sizeof(alert_pins) / sizeof(alert_pins[0]); i++) {
    if (!sys_io_pin_is_valid(alert_pins[i].pin)) continue;
    sys_io_intr_config_t intr_cfg = {.mode = SYS_IO_INTR_MODE_FALLING_EDGE};
    SYS_DEV_INSTALL_STEP(sys_io_set_mode(alert_pins[i].pin), "alert pin mode");
    SYS_DEV_INSTALL_STEP_BIT(c, alert_pins[i].sub_step, sys_io_subscribe_pin(alert_pins[i].pin, device_event_handler, c, alert_pins[i].sub), "alert pin subscribe");
    SYS_DEV_INSTALL_STEP(sys_io_configure_intr(alert_pins[i].pin, &intr_cfg), "alert pin intr");
    SYS_DEV_INSTALL_STEP_BIT(c, alert_pins[i].ready_step, sys_io_lock_pin(alert_pins[i].pin), "alert pin lock");
  }

  // Chip defaults: reset, alert latches, continuous measurement
  SYS_DEV_INSTALL_STEP(chip_reset(c), "chip reset");
  SYS_DEV_INSTALL_STEP(chip_enable_latch(c, true, true), "chip alert latch");
  SYS_DEV_INSTALL_STEP(chip_set_options(c, true, true, true), "chip options");

  *out_device_handle = c;
  return NULL;

fail:
  SYS_DEV_INSTALL_FAIL(err, out_device_handle, device_uninstall, c);
  return NULL;
}

/* Inline listener of both alert pins: publish every flagged channel. */
static SE_MUST_USE err_h handle_alerts(const sys_event_t* event, ina_ctx_t* c) {
  // Read and clear alert flags from the mask register
  SE_TRY(chip_get_status(c));

  // A reversed-shunt alert's pin released: that channel went over its threshold.
  bool on_crit = event->device_id == c->crit_pin.device_id && event->channel == c->crit_pin.pin;
  bool on_warn = event->device_id == c->warn_pin.device_id && event->channel == c->warn_pin.pin;
  int8_t inverted = on_crit ? c->inverted_alert[0] : on_warn ? c->inverted_alert[1] : -1;
  if (inverted >= 0) {
    int32_t mA = 0;
    err_h alert_err = NULL;
    SYS_DEV_TEARDOWN_STEP(alert_err, chip_read_current(c, (uint8_t)inverted, &mA));
    SYS_DEV_TEARDOWN_STEP(alert_err, sys_power_publish(SYS_DEV_GET_ID(c), (uint8_t)inverted, on_crit ? SYS_PWR_EVENT_OCP_CRITICAL : SYS_PWR_EVENT_OCP_WARNING, mA, SYS_EVENT_CAUSED_BY(event)));
    return alert_err;
  }

  // Every flagged channel is published, even if a current read fails (the
  // value is then 0); the first failure is returned.
  // A reversed-shunt channel's flag is set while it is under its threshold (its normal state): skipped.
  err_h err = NULL;
  const struct {
    uint8_t flags;
    sys_power_events_e event;
  } alerts[] = {{c->mask.cf, SYS_PWR_EVENT_OCP_CRITICAL}, {c->mask.wf, SYS_PWR_EVENT_OCP_WARNING}};
  for (size_t a = 0; a < sizeof(alerts) / sizeof(alerts[0]); a++) {
    for (uint8_t ch = 0; ch < INA3221_CHANNELS; ch++) {
      if (c->inverted_mask & (1u << ch)) continue;
      if (!((alerts[a].flags >> (2 - ch)) & 1)) continue;
      int32_t mA = 0;
      SYS_DEV_TEARDOWN_STEP(err, chip_read_current(c, ch, &mA));
      SYS_DEV_TEARDOWN_STEP(err, sys_power_publish(SYS_DEV_GET_ID(c), ch, alerts[a].event, mA, SYS_EVENT_CAUSED_BY(event)));
    }
  }
  return err;
}

// No dispatcher wraps a listener's error, so it carries the device id here.
static SE_MUST_USE err_h device_event_handler(const sys_event_t* event, void* handle) {
  SYS_DEV_CTX_FROM(ina_ctx_t, c, handle);
  err_h err = handle_alerts(event, c);
  return SYS_DEV_WRAP(err, SYS_DEV_GET_ID(c));
}

static const uint8_t s_pin_refs[] = {offsetof(d_ina3221_cfg_t, crit_pin), offsetof(d_ina3221_cfg_t, warn_pin)};

const sys_device_class_t g_ina3221_class = {
    .type_id = INA3221_TYPE_ID,
    .cfg_size = sizeof(d_ina3221_cfg_t),
    SYS_DEVICE_PINS(s_pin_refs),
    .name = "INA3221_PWR_MONITOR",
    .contracts = {[SYS_DEVICE_CONTRACT_POWER_MONITOR] = &s_ina3221_monitor_contract},
    .ops = {.install = device_install,
        .uninstall = device_uninstall,
        .reset = device_reset,
        .suspend = device_suspend,
        .resume = device_resume},
};
