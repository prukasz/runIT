#include "device_ina3221.h"
#include "sys_event.h"
#include "sys_i2c.h"
#include "sys_pins.h"
#include "utils.h"

#define I2C_SPEED_HZ 400000u

#define REG_CONFIG     0x00u
#define REG_SHUNT_1    0x01u  // + 2 per channel
#define REG_BUS_1      0x02u    // + 2 per channel
#define REG_CRITICAL_1 0x07u
#define REG_WARNING_1  0x08u
#define REG_MASK       0x0Fu

#define CONFIG_DEFAULT   0x7127u
#define CONFIG_RESET     0x8000u
#define CONFIG_MODE_BITS 0x0007u  // ESHT | EBUS | MODE: continuous shunt + bus measurement
#define MASK_DEFAULT     0x0002u
#define MASK_WRITABLE    0x7C00u
#define MASK_CEN         (1u << 10)  // critical alert latch
#define MASK_WEN         (1u << 11)  // warning alert latch
#define MASK_CF_SHIFT    7      // critical flags CF1..CF3 = bits 9..7 (channel 0 = bit 9)
#define MASK_WF_SHIFT    3      // warning flags WF1..WF3 = bits 5..3

#define ALERT_HYSTERESIS_PCT 10u  // a reversed-shunt alert re-arms once the current is this far back under its threshold

typedef struct {
  sys_device_base_t base;  // first: id, suspended
  sys_i2c_dev_t i2c;
  sys_pins_owned_t pin[2];  // [0] critical, [1] warning
  int8_t inverted_alert[2];  // reversed-shunt channel armed on that pin, -1 = none
  int32_t inverted_threshold_mA[2];  // its threshold
  bool inverted_over[2];             // over it: the pin waits (falling edge) for the current back under the hysteresis
  uint8_t inverted_mask;
  uint16_t shunt_mohm;  // every channel's shunt, mOhm
  uint16_t config;  // as last written
  uint16_t mask;    // as last written (latch bits)
} ina_t;

/* ---- Chip access ---- */

static err_h chip_read(ina_t* c, uint8_t reg, uint16_t* val) {
  return sys_i2c_reg_read_u16(&c->i2c, reg, val, SYS_I2C_BE);
}

static err_h chip_write(ina_t* c, uint8_t reg, uint16_t val) {
  return sys_i2c_reg_write_u16(&c->i2c, reg, val, SYS_I2C_BE);
}

// Continuous measurement of bus and shunt, or power-down.
static err_h chip_set_measuring(ina_t* c, bool on) {
  c->config = on ? (uint16_t)(c->config | CONFIG_MODE_BITS) : (uint16_t)(c->config & ~CONFIG_MODE_BITS);
  return chip_write(c, REG_CONFIG, c->config);
}

static err_h chip_set_latch(ina_t* c, bool warning, bool critical) {
  c->mask = (uint16_t)((c->mask & ~(MASK_WEN | MASK_CEN)) | (warning ? MASK_WEN : 0u) | (critical ? MASK_CEN : 0u));
  return chip_write(c, REG_MASK, c->mask & MASK_WRITABLE);
}

static err_h chip_reset(ina_t* c) {
  c->mask = MASK_DEFAULT;
  c->config = CONFIG_DEFAULT;
  return chip_write(c, REG_CONFIG, CONFIG_DEFAULT | CONFIG_RESET);
}

static err_h chip_read_bus_mV(ina_t* c, uint8_t ch, int32_t* mV) {
  uint16_t raw;
  ERR_TRY(chip_read(c, (uint8_t)(REG_BUS_1 + ch * 2u), &raw));
  *mV = ((int16_t)raw >> 3) * 8;  // 8 mV per LSB
  return NULL;
}

// Shunt current as the board sees it: negated for a reversed shunt.
static err_h chip_read_current_mA(ina_t* c, uint8_t ch, int32_t* mA) {
  uint16_t raw;
  ERR_TRY(chip_read(c, (uint8_t)(REG_SHUNT_1 + ch * 2u), &raw));
  const int32_t uV = ((int16_t)raw >> 3) * 40;  // 40 uV per LSB
  *mA = uV / c->shunt_mohm;                      // uV / mOhm = mA
  if (c->inverted_mask & (1u << ch)) {
    *mA = -*mA;
  }
  return NULL;
}

static err_h chip_set_alert(ina_t* c, uint8_t ch, int32_t mA, bool critical) {
  const int32_t raw = (mA * c->shunt_mohm) / 40;  // mA * mOhm = uV; 40 uV per LSB
  return chip_write(c, (uint8_t)((critical ? REG_CRITICAL_1 : REG_WARNING_1) + ch * 2u),
                    (uint16_t)((int16_t)raw << 3));
}

/* ---- Alert pins ---- */

// The edge of an alert pin; the pin stays locked to this device.
static err_h set_pin_edge(ina_t* c, int i, sys_io_intr_mode_e mode) {
  ERR_TRY(sys_pins_unlock(c->pin[i].pin));
  const sys_io_intr_cfg_t cfg = {.mode = mode};
  err_h err = sys_io_configure_intr(c->pin[i].pin, &cfg);
  ERR_KEEP_FIRST(err, sys_pins_lock(c->pin[i].pin));
  return err;
}

/* A reversed shunt reads negative and the chip compares signed, so the limit is -threshold: the alert is active
   while the current is below the threshold and releases above it. It is transparent (not latched), and its pin
   takes this one channel only - an active channel holds the shared pin and would hide the others. The pin's
   release (rising edge) is the alert. A current around the threshold would release and re-assert the pin on every
   conversion: after the event the limit moves down by the hysteresis and the pin waits for its falling edge (the
   current back under it), then the threshold is armed again. One event per excursion. */
static err_h set_inverted_alert(ina_t* c, uint8_t ch, bool critical, int32_t threshold_mA) {
  const int i = critical ? 0 : 1;
  const uint8_t alert = critical ? SYS_POWER_ALERT_OCP_CRITICAL : SYS_POWER_ALERT_OCP_WARNING;
  ERR_CHECK(threshold_mA >= 1, ERR_DEV_RANGE, .id = c->base.id, .value = threshold_mA, .min = 1, .max = INT32_MAX);
  // No pin wired, or the pin already watches another channel (one reversed channel per alert pin).
  ERR_CHECK(sys_io_pin_valid(c->pin[i].pin) && (c->inverted_alert[i] < 0 || c->inverted_alert[i] == (int8_t)ch),
            ERR_POWER_ALERT_UNSUPPORTED, .id = c->base.id, .channel = ch, .alert = alert);
  // Rising edge first: arming pulls the pin low (its normal state now), which must not count.
  if (c->inverted_alert[i] < 0 || c->inverted_over[i]) {
    ERR_TRY(set_pin_edge(c, i, SYS_IO_INTR_MODE_RISING_EDGE));
  }
  ERR_TRY(chip_set_latch(c, critical ? (c->mask & MASK_WEN) != 0 : false, critical ? false : (c->mask & MASK_CEN) != 0));
  ERR_TRY(chip_set_alert(c, ch, -threshold_mA, critical));
  c->inverted_alert[i] = (int8_t)ch;
  c->inverted_threshold_mA[i] = threshold_mA;
  c->inverted_over[i] = false;
  return NULL;
}

// After a chip reset (limits back at their defaults) the reversed-shunt alerts are gone: pins back to the falling edge.
static err_h disarm_inverted_alerts(ina_t* c) {
  err_h err = NULL;
  for (int i = 0; i < 2; i++) {
    if (c->inverted_alert[i] < 0) {
      continue;
    }
    c->inverted_alert[i] = -1;
    c->inverted_over[i] = false;
    ERR_KEEP_FIRST(err, set_pin_edge(c, i, SYS_IO_INTR_MODE_FALLING_EDGE));
  }
  return err;
}

/* ---- POWER_MONITOR contract ---- */

static err_h check_channel(const ina_t* c, uint8_t ch) {
  ERR_CHECK(ch < INA3221_CHANNELS, ERR_POWER_BAD_CHANNEL, .id = c->base.id, .channel = ch);
  return NULL;
}

PMON_READ(VOLTAGE) {
  ina_t* c = ctx;
  ERR_TRY(check_channel(c, args->channel));
  int32_t mV;
  ERR_TRY(chip_read_bus_mV(c, args->channel, &mV));
  return cmd_reply(out, out_cap, out_len, &mV, sizeof(mV));
}

PMON_READ(CURRENT) {
  ina_t* c = ctx;
  ERR_TRY(check_channel(c, args->channel));
  int32_t mA;
  ERR_TRY(chip_read_current_mA(c, args->channel, &mA));
  return cmd_reply(out, out_cap, out_len, &mA, sizeof(mA));
}

PMON_READ(POWER) {
  ina_t* c = ctx;
  ERR_TRY(check_channel(c, args->channel));
  int32_t mV, mA;
  ERR_TRY(chip_read_bus_mV(c, args->channel, &mV));
  ERR_TRY(chip_read_current_mA(c, args->channel, &mA));
  const int32_t mW = sys_power_mW(mV, mA);
  return cmd_reply(out, out_cap, out_len, &mW, sizeof(mW));
}

PMON_WRITE(ALERT) {
  ina_t* c = ctx;
  ERR_TRY(sys_device_check_active(&c->base));
  ERR_TRY(check_channel(c, args->channel));
  ERR_CHECK(args->alert == SYS_POWER_ALERT_OCP_WARNING || args->alert == SYS_POWER_ALERT_OCP_CRITICAL,
            ERR_POWER_ALERT_UNSUPPORTED, .id = c->base.id, .channel = args->channel, .alert = args->alert);
  const bool critical = args->alert == SYS_POWER_ALERT_OCP_CRITICAL;
  if (c->inverted_mask & (1u << args->channel)) {
    return set_inverted_alert(c, args->channel, critical, args->threshold_mA);
  }
  // 12-bit signed limit register, 40 uV per LSB over the shunt: 4095 steps of it, so the limit's full scale in mA.
  const int32_t max_mA = 4095 * 40 / c->shunt_mohm;
  SYS_DEV_CHECK_IN_RANGE(c->base.id, args->threshold_mA, -max_mA, max_mA);
  return chip_set_alert(c, args->channel, args->threshold_mA, critical);
}

/* ---- EVENT contract: an alert pin changed ---- */

/* A reversed-shunt alert's pin released: that channel went over its threshold. Published once; then the limit moves
   down by the hysteresis and the pin waits for the falling edge (the edge first, so a drop while the limit moves is
   not missed). */
static err_h on_inverted_over(ina_t* c, int i) {
  int32_t mA = 0;
  const uint8_t ch = (uint8_t)c->inverted_alert[i];
  const int32_t threshold = c->inverted_threshold_mA[i];
  int32_t hysteresis = threshold * (int32_t)ALERT_HYSTERESIS_PCT / 100;
  hysteresis = hysteresis < 1 ? 1 : hysteresis;
  hysteresis = hysteresis >= threshold ? threshold - 1 : hysteresis;
  err_h err = NULL;
  ERR_KEEP_FIRST(err, chip_read_current_mA(c, ch, &mA));  // a failed read still publishes (value 0)
  if (i == 0) {
    ERR_KEEP_FIRST(err, SYS_EVENT_PUBLISH(PMON, OCP_CRITICAL, c->base.id, ch, mA));
  } else {
    ERR_KEEP_FIRST(err, SYS_EVENT_PUBLISH(PMON, OCP_WARNING, c->base.id, ch, mA));
  }
  c->inverted_over[i] = true;
  ERR_KEEP_FIRST(err, set_pin_edge(c, i, SYS_IO_INTR_MODE_FALLING_EDGE));
  ERR_KEEP_FIRST(err, chip_set_alert(c, ch, -(threshold - hysteresis), i == 0));
  return err;
}

// The current is back under the hysteresis: the threshold armed again (the rising edge first, as when arming).
static err_h on_inverted_back(ina_t* c, int i) {
  const uint8_t ch = (uint8_t)c->inverted_alert[i];
  ERR_TRY(set_pin_edge(c, i, SYS_IO_INTR_MODE_RISING_EDGE));
  ERR_TRY(chip_set_alert(c, ch, -c->inverted_threshold_mA[i], i == 0));
  c->inverted_over[i] = false;
  return NULL;
}

// Falling edge of a normal-polarity pin: publish every flagged channel (the flags clear on this read).
static err_h on_flags(ina_t* c) {
  uint16_t mask;
  ERR_TRY(chip_read(c, REG_MASK, &mask));
  err_h err = NULL;
  for (uint8_t ch = 0; ch < INA3221_CHANNELS; ch++) {
    if (c->inverted_mask & (1u << ch)) {
      continue;  // its flag is set while under the threshold (its normal state)
    }
    const bool crit = (mask >> (MASK_CF_SHIFT + 2 - ch)) & 1u;
    const bool warn = (mask >> (MASK_WF_SHIFT + 2 - ch)) & 1u;
    if (!crit && !warn) {
      continue;
    }
    int32_t mA = 0;
    ERR_KEEP_FIRST(err, chip_read_current_mA(c, ch, &mA));
    if (crit) {
      ERR_KEEP_FIRST(err, SYS_EVENT_PUBLISH(PMON, OCP_CRITICAL, c->base.id, ch, mA));
    }
    if (warn) {
      ERR_KEEP_FIRST(err, SYS_EVENT_PUBLISH(PMON, OCP_WARNING, c->base.id, ch, mA));
    }
  }
  return err;
}

EVENT_WRITE(ON_EVENT) {
  ina_t* c = ctx;
  for (int i = 0; i < 2; i++) {
    if (!sys_io_pin_valid(c->pin[i].pin) || args->source != c->pin[i].pin.device || args->channel != c->pin[i].pin.pin) {
      continue;
    }
    if (c->inverted_alert[i] >= 0) {
      if (args->event == IO_EV_RISING && !c->inverted_over[i]) {
        return on_inverted_over(c, i);
      }
      return args->event == IO_EV_FALLING && c->inverted_over[i] ? on_inverted_back(c, i) : NULL;
    }
    return args->event == IO_EV_FALLING ? on_flags(c) : NULL;
  }
  return NULL;
}

/* ---- Lifecycle ---- */

DEVICE_WRITE(RESET) {
  ina_t* c = ctx;
  ERR_TRY(sys_device_check_active(&c->base));
  ERR_TRY(chip_reset(c));
  ERR_TRY(disarm_inverted_alerts(c));
  ERR_TRY(chip_set_latch(c, true, true));
  return chip_set_measuring(c, true);
}

// Power-down. Readings stay allowed (they return the last conversion).
static err_h suspend_hook(void* ctx) {
  ina_t* c = ctx;
  return chip_set_measuring(c, false);
}

static err_h resume_hook(void* ctx) {
  ina_t* c = ctx;
  ERR_TRY(chip_set_measuring(c, true));
  return NULL;
}

// Releases what was built; each step runs only if it was built, none returns early.
static err_h uninstall(void* ctx) {
  ina_t* c = ctx;
  err_h err = NULL;
  for (int i = 0; i < 2; i++) {
    ERR_KEEP_FIRST(err, sys_pins_release(&c->pin[i]));
  }
  if (sys_i2c_dev_added(&c->i2c)) {
    ERR_KEEP_FIRST(err, sys_i2c_dev_remove(&c->i2c));
  }
  return err;
}

static err_h create(void* ctx, const void* cfg_in) {
  ina_t* c = ctx;
  const ina3221_cfg_t* cfg = cfg_in;
  const uint16_t id = c->base.id;
  SYS_DEV_CHECK_IN_RANGE(id, cfg->i2c_bus, 0, SYS_I2C_BUS_COUNT - 1);
  SYS_DEV_CHECK_IN_RANGE(id, cfg->i2c_addr, INA3221_I2C_ADDR_MIN, INA3221_I2C_ADDR_MAX);
  SYS_DEV_CHECK_IN_RANGE(id, cfg->inverted_mask, 0, 0x07);
  SYS_DEV_CHECK_IN_RANGE(id, cfg->shunt_mohm, 1, UINT16_MAX);
  c->inverted_mask = cfg->inverted_mask;
  c->shunt_mohm = cfg->shunt_mohm;
  c->inverted_alert[0] = c->inverted_alert[1] = -1;
  c->config = CONFIG_DEFAULT;
  c->mask = MASK_DEFAULT;
  ERR_TRY(sys_i2c_dev_attach(&c->i2c, cfg->i2c_bus, cfg->i2c_addr, I2C_SPEED_HZ));  // probes the chip

  // Alert pins: input with pull-up, listener on both edges (inline: an I2C read), falling edge armed, locked.
  const sys_io_pin_t alert_pin[2] = {cfg->crit_pin, cfg->warn_pin};
  for (int i = 0; i < 2; i++) {
    ERR_TRY(sys_pins_take_input(&c->base, &c->pin[i], alert_pin[i], (uint16_t)((IO_EV_RISING & 0xFF00u) | SYS_EVENT_ANY_IN),
                                   SYS_IO_INTR_MODE_FALLING_EDGE));
  }

  // Chip defaults: reset, alert latches, continuous measurement.
  ERR_TRY(chip_reset(c));
  ERR_TRY(chip_set_latch(c, true, true));
  return chip_set_measuring(c, true);
}

/* ---- Type ---- */

static const sys_device_op_t k_ops[] = {
    DEVICE_ENTRY_W(RESET),
    PMON_ENTRY_R(VOLTAGE),  PMON_ENTRY_R(CURRENT),  PMON_ENTRY_R(POWER),
    PMON_ENTRY_W(ALERT),    EVENT_ENTRY_W(ON_EVENT),
};

SYS_DEVICE_TYPE_DEFINE(.type = SYS_DEVICE_TYPE_INA3221, .cfg_size = sizeof(ina3221_cfg_t), .ctx_size = sizeof(ina_t),
                       .create = create, .uninstall = uninstall, .suspend = suspend_hook, .resume = resume_hook,
                       SYS_DEVICE_OPS(k_ops))
