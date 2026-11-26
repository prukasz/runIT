/*
 * INA3221: three-channel shunt and bus voltage monitor over I2C. The whole device is this file: nothing includes it,
 * the rest of the firmware reaches it by its ID and the POWER_MONITOR contract (PMON): VOLTAGE, CURRENT, POWER and
 * ALERT, whose crossings are PMON_EV_OCP_WARNING / PMON_EV_OCP_CRITICAL from the chip's two alert pins.
 *
 * A reversed shunt (`inverted_mask`) reads negative and the chip compares signed, so its alert is armed at -threshold:
 * the pin is low (the alert active) while the current is under the threshold and released over it. Such an alert is
 * transparent and takes its pin for that one channel. The pin watches both edges: its rise is the event, then the limit
 * moves down by the hysteresis and its fall (the current back under it) arms the threshold again. One event per
 * excursion.
 */
#include "sys_device_driver.h"
#include "sys_device_event_contract.h"
#include "sys_event.h"
#include "sys_i2c_contract.h"
#include "sys_io_driver.h"
#include "sys_power_monitor_contract.h"

SYS_DEVICE_TYPE_DECLARE(INA3221, 0x0044)

/*******************************************************************************
 * OWN_INA3221 : own operations
 *   OP_ALERT_PIN - an edge of an alert pin (LOCAL | ALWAYS | TAIL)
 ******************************************************************************/
#define OWN_INA3221_OPERATIONS(X, P) \
  X(P, OP_ALERT_PIN, 0x00, cmd_none_t, sys_event_t, SYS_DEVICE_OP_LOCAL | SYS_DEVICE_OP_ALWAYS | SYS_DEVICE_OP_TAIL)

SYS_DEVICE_OWN_DECLARE(OWN_INA3221)

#define INA3221_CHANNELS     3u
#define INA3221_I2C_ADDR_MIN 0x40u  // the A0 strap (GND / Vs+ / SDA / SCL) selects 0x40..0x43
#define INA3221_I2C_ADDR_MAX 0x43u

/** @brief CREATE's struct (wire format). An alert pin not wired is SYS_IO_PIN_NONE. */
typedef struct __attribute__((packed)) {
  uint8_t i2c_bus;        /**< The bus's device ID (an I2C contract device) */
  uint8_t i2c_addr;       /**< 7-bit I2C address (0x40 - 0x43) */
  sys_io_pin_t crit_pin;  /**< Critical alert active-low open-drain pin */
  sys_io_pin_t warn_pin;  /**< Warning alert active-low open-drain pin */
  uint8_t inverted_mask;  /**< Bitmask of channels with inverted shunt wiring (0..7) */
  uint16_t shunt_mohm;    /**< Resistance of each channel's shunt in mOhm */
} ina3221_cfg_t;

#define I2C_SPEED_HZ 400000u

#define REG_CONFIG     0x00u
#define REG_SHUNT_1    0x01u  // + 2 per channel
#define REG_BUS_1      0x02u  // + 2 per channel
#define REG_CRITICAL_1 0x07u  // + 2 per channel
#define REG_WARNING_1  0x08u  // + 2 per channel
#define REG_MASK       0x0Fu

#define CONFIG_DEFAULT   0x7127u
#define CONFIG_RESET     0x8000u
#define CONFIG_MODE_BITS 0x0007u  // continuous shunt + bus measurement
#define MASK_CEN         (1u << 10)  // critical alert latch
#define MASK_WEN         (1u << 11)  // warning alert latch
#define MASK_CF_SHIFT    7u          // critical flags CF1..CF3 = bits 9..7 (channel 0 = bit 9)
#define MASK_WF_SHIFT    3u          // warning flags WF1..WF3 = bits 5..3

#define UV_PER_LSB           40   // shunt voltage register: 40 uV per LSB
#define MV_PER_LSB           8    // bus voltage register: 8 mV per LSB
#define ALERT_HYSTERESIS_PCT 10   // a reversed-shunt alert re-arms this far back under its threshold

enum { PIN_CRIT = 0, PIN_WARN = 1 };

typedef struct {
  int8_t channel;  // reversed-shunt channel armed on the pin, -1 = none
  bool over;       // that channel is over its threshold: the pin waits for its fall
  int32_t threshold_mA;
} inverted_t;

typedef struct {
  uint8_t id;
  sys_i2c_dev_t i2c;
  sys_io_claim_t pin[2];  // PIN_CRIT, PIN_WARN: both edges run OP_ALERT_PIN
  inverted_t inv[2];
  uint8_t inverted_mask;
  uint16_t shunt_mohm;
  uint16_t mask;          // MASK_WEN / MASK_CEN as written
} ina_t;

#define SYS_DEVICE_CTX ina_t

/* ---- Chip access ---- */

static err_h chip_read(ina_t* c, uint8_t reg, uint16_t* val) {
  return sys_i2c_reg_read_u16(&c->i2c, reg, val, SYS_I2C_BE);
}

static err_h chip_write(ina_t* c, uint8_t reg, uint16_t val) {
  return sys_i2c_reg_write_u16(&c->i2c, reg, val, SYS_I2C_BE);
}

static err_h chip_set_measuring(ina_t* c, bool on) {
  return chip_write(c, REG_CONFIG, on ? CONFIG_DEFAULT : (uint16_t)(CONFIG_DEFAULT & ~CONFIG_MODE_BITS));
}

// A latched alert (normal polarity) holds its pin until MASK is read; a reversed-shunt alert is transparent.
static err_h chip_set_latches(ina_t* c) {
  c->mask = (uint16_t)((c->inv[PIN_WARN].channel < 0 ? MASK_WEN : 0) | (c->inv[PIN_CRIT].channel < 0 ? MASK_CEN : 0));
  return chip_write(c, REG_MASK, c->mask);
}

static err_h chip_read_bus_mV(ina_t* c, uint8_t ch, int32_t* mV) {
  uint16_t raw;
  ERR_TRY(chip_read(c, (uint8_t)(REG_BUS_1 + ch * 2u), &raw));
  *mV = ((int16_t)raw >> 3) * MV_PER_LSB;
  return NULL;
}

// The current as the board sees it: negated for a reversed shunt.
static err_h chip_read_current_mA(ina_t* c, uint8_t ch, int32_t* mA) {
  uint16_t raw;
  ERR_TRY(chip_read(c, (uint8_t)(REG_SHUNT_1 + ch * 2u), &raw));
  *mA = ((int16_t)raw >> 3) * UV_PER_LSB / c->shunt_mohm;  // uV / mOhm = mA
  if (c->inverted_mask & (1u << ch)) {
    *mA = -*mA;
  }
  return NULL;
}

// The limit register of an alert, in the chip's terms (signed, before the reversal).
static err_h chip_set_limit(ina_t* c, uint8_t ch, int pin, int32_t mA) {
  const int32_t raw = mA * c->shunt_mohm / UV_PER_LSB;
  return chip_write(c, (uint8_t)((pin == PIN_CRIT ? REG_CRITICAL_1 : REG_WARNING_1) + ch * 2u),
                    (uint16_t)((int16_t)raw << 3));
}

static err_h chip_reset(ina_t* c) {
  c->inv[0] = c->inv[1] = (inverted_t){.channel = -1};
  ERR_TRY(chip_write(c, REG_CONFIG, CONFIG_DEFAULT | CONFIG_RESET));
  ERR_TRY(chip_set_latches(c));
  return chip_set_measuring(c, true);
}

static err_h publish(ina_t* c, int pin, uint8_t ch, int32_t mA) {
  return pin == PIN_CRIT ? SYS_EVENT_PUBLISH(PMON_EV_OCP_CRITICAL, c->id, ch, .mA = mA)
                         : SYS_EVENT_PUBLISH(PMON_EV_OCP_WARNING, c->id, ch, .mA = mA);
}

/* ---- POWER_MONITOR contract ---- */

static err_h check_channel(const ina_t* c, uint8_t ch) {
  ERR_CHECK(ch < INA3221_CHANNELS, ERR_POWER_BAD_CHANNEL, .id = c->id, .channel = ch);
  return NULL;
}

OP_DECLARE_READ(CONTRACT_PMON, OP_VOLTAGE) {
  ERR_TRY(check_channel(c, args->channel));
  int32_t mV;
  ERR_TRY(chip_read_bus_mV(c, args->channel, &mV));
  return CMD_REPLY(CONTRACT_PMON, OP_VOLTAGE, mV);
}

OP_DECLARE_READ(CONTRACT_PMON, OP_CURRENT) {
  ERR_TRY(check_channel(c, args->channel));
  int32_t mA;
  ERR_TRY(chip_read_current_mA(c, args->channel, &mA));
  return CMD_REPLY(CONTRACT_PMON, OP_CURRENT, mA);
}

OP_DECLARE_READ(CONTRACT_PMON, OP_POWER) {
  ERR_TRY(check_channel(c, args->channel));
  int32_t mV, mA;
  ERR_TRY(chip_read_bus_mV(c, args->channel, &mV));
  ERR_TRY(chip_read_current_mA(c, args->channel, &mA));
  return CMD_REPLY(CONTRACT_PMON, OP_POWER, sys_power_mW(mV, mA));
}

/*******************************************************************************
 * CONTRACT_PMON : OP_ALERT (write)
 * Normal polarity: the limit register, latched. Reversed shunt: the limit at
 * -threshold on a pin of its own (see the top of the file).
 ******************************************************************************/
OP_DECLARE_WRITE(CONTRACT_PMON, OP_ALERT) {
  const uint8_t ch = args->channel;
  ERR_TRY(check_channel(c, ch));
  ERR_CHECK(args->alert <= SYS_POWER_ALERT_OCP_CRITICAL, ERR_POWER_ALERT_UNSUPPORTED, .id = c->id, .channel = ch,
            .alert = args->alert);
  const int pin = args->alert == SYS_POWER_ALERT_OCP_CRITICAL ? PIN_CRIT : PIN_WARN;
  if (!(c->inverted_mask & (1u << ch))) {
    const int32_t max_mA = 4095 * UV_PER_LSB / c->shunt_mohm;  // the 12-bit signed limit's full scale
    ERR_CHECK_RANGE(args->threshold_mA, -max_mA, max_mA);
    return chip_set_limit(c, ch, pin, args->threshold_mA);
  }
  inverted_t* inv = &c->inv[pin];
  ERR_CHECK_RANGE(args->threshold_mA, 1, INT32_MAX);
  // No pin wired, or the pin watches another reversed channel already.
  ERR_CHECK(sys_io_pin_valid(c->pin[pin].pin) && (inv->channel < 0 || inv->channel == (int8_t)ch),
            ERR_POWER_ALERT_UNSUPPORTED, .id = c->id, .channel = ch, .alert = args->alert);
  *inv = (inverted_t){.channel = (int8_t)ch, .threshold_mA = args->threshold_mA};
  ERR_TRY(chip_set_latches(c));
  return chip_set_limit(c, ch, pin, -args->threshold_mA);
}

/* ---- Alert pins ---- */

// A normal-polarity pin fell: every flagged channel published (the flags clear on this read).
static err_h on_flags(ina_t* c) {
  uint16_t mask;
  ERR_TRY(chip_read(c, REG_MASK, &mask));
  err_h err = NULL;
  for (uint8_t ch = 0; ch < INA3221_CHANNELS; ch++) {
    if (c->inverted_mask & (1u << ch)) {
      continue;  // its flag is set while under the threshold, its normal state
    }
    const bool crit = (mask >> (MASK_CF_SHIFT + 2u - ch)) & 1u;
    const bool warn = (mask >> (MASK_WF_SHIFT + 2u - ch)) & 1u;
    if (!crit && !warn) {
      continue;
    }
    int32_t mA = 0;
    ERR_KEEP_FIRST(err, chip_read_current_mA(c, ch, &mA));  // a failed read still publishes (0)
    if (crit) {
      ERR_KEEP_FIRST(err, publish(c, PIN_CRIT, ch, mA));
    }
    if (warn) {
      ERR_KEEP_FIRST(err, publish(c, PIN_WARN, ch, mA));
    }
  }
  return err;
}

// A reversed-shunt pin: its rise is the excursion (published, the limit moved down by the hysteresis), its fall the
// current back under it (the threshold armed again).
static err_h on_inverted(ina_t* c, int pin, bool rising) {
  inverted_t* inv = &c->inv[pin];
  const uint8_t ch = (uint8_t)inv->channel;
  if (rising == inv->over) {
    return NULL;  // arming pulls the pin low: that fall is no event
  }
  inv->over = rising;
  if (!rising) {
    return chip_set_limit(c, ch, pin, -inv->threshold_mA);
  }
  int32_t hysteresis = inv->threshold_mA * ALERT_HYSTERESIS_PCT / 100;
  hysteresis = hysteresis < 1 ? 1 : hysteresis >= inv->threshold_mA ? inv->threshold_mA - 1 : hysteresis;
  int32_t mA = 0;
  err_h err = chip_read_current_mA(c, ch, &mA);
  ERR_KEEP_FIRST(err, publish(c, pin, ch, mA));
  ERR_KEEP_FIRST(err, chip_set_limit(c, ch, pin, -(inv->threshold_mA - hysteresis)));
  return err;
}

/*******************************************************************************
 * OWN_INA3221 : OP_ALERT_PIN (event, LOCAL | ALWAYS | TAIL)
 * Nothing while suspended: the chip is powered down.
 ******************************************************************************/
OP_DECLARE_EVENT(OWN_INA3221, OP_ALERT_PIN) {
  if (sys_device_self_suspended()) {
    return NULL;
  }
  const bool rising = event->key.tag == IO_EV_RISING;
  for (int pin = 0; pin < 2; pin++) {
    const sys_io_pin_t p = c->pin[pin].pin;
    if (event->key.source != p.device || event->key.channel != p.pin) {
      continue;
    }
    if (c->inv[pin].channel >= 0) {
      return on_inverted(c, pin, rising);
    }
    return rising ? NULL : on_flags(c);
  }
  return NULL;
}

/* ---- Lifecycle ---- */

/*******************************************************************************
 * CONTRACT_SYS_DEVICE : OP_RESET (write)
 * The chip's defaults: every limit off, alerts latched, measuring.
 ******************************************************************************/
OP_DECLARE_WRITE(CONTRACT_SYS_DEVICE, OP_RESET) {
  return chip_reset(c);
}

// Power-down; readings answer the last conversion.
static err_h suspend(ina_t* c) {
  return chip_set_measuring(c, false);
}

static err_h resume(ina_t* c) {
  return chip_set_measuring(c, true);
}

static err_h create(uint8_t id, ina_t* c, const ina3221_cfg_t* cfg) {
  ERR_CHECK_RANGE(cfg->i2c_addr, INA3221_I2C_ADDR_MIN, INA3221_I2C_ADDR_MAX);
  ERR_CHECK_RANGE(cfg->inverted_mask, 0, 0x07);
  ERR_CHECK_RANGE(cfg->shunt_mohm, 1, UINT16_MAX);
  c->id = id;
  c->inverted_mask = cfg->inverted_mask;
  c->shunt_mohm = cfg->shunt_mohm;
  ERR_TRY(sys_i2c_dev_attach(id, &c->i2c, cfg->i2c_bus, cfg->i2c_addr, I2C_SPEED_HZ));
  ERR_TRY(chip_reset(c));
  // Alert pins: inputs, both edges of either to OP_ALERT_PIN (inline: an I2C read), locked.
  const sys_io_pin_t pins[2] = {cfg->crit_pin, cfg->warn_pin};
  for (int pin = 0; pin < 2; pin++) {
    ERR_TRY(sys_io_claim_input(id, &c->pin[pin], pins[pin], EVT_TAG(IO_EV_MODULE, SYS_EVENT_ANY_CODE),
                               OWN_INA3221_OP_ALERT_PIN, SYS_IO_INTR_MODE_BOTH_EDGES));
  }
  return NULL;
}

static const sys_device_op_t k_ops[] = {
    OP_ENTRY_W(CONTRACT_SYS_DEVICE, OP_RESET),
    OP_ENTRY_R(CONTRACT_PMON, OP_VOLTAGE),  OP_ENTRY_R(CONTRACT_PMON, OP_CURRENT),  OP_ENTRY_R(CONTRACT_PMON, OP_POWER),
    OP_ENTRY_W(CONTRACT_PMON, OP_ALERT),
    OP_ENTRY_W(OWN_INA3221, OP_ALERT_PIN),
};

SYS_DEVICE_TYPE_DEFINE(INA3221, ina3221_cfg_t, create, suspend, resume, k_ops)
