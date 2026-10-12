#include "device_pca9685.h"

#include "esp_rom_sys.h"
#include "sys_i2c.h"
#include "sys_pins.h"

#define I2C_SPEED_HZ 100000u
#define WAKEUP_DELAY_US 500u

#define REG_MODE1 0x00u
#define REG_LED_START 0x06u
#define REG_PRE_SCALE 0xFEu
#define REG_LED_N(ch) (REG_LED_START + (ch) * 4u)

#define MODE1_SLEEP (1u << 4)
#define MODE1_AI (1u << 5)
#define LED_FULL (1u << 4)  // full on / full off bit of the LED_ON_H / LED_OFF_H registers

#define OSC_HZ 25000000.0
#define MIN_PRESCALE 0x03

typedef struct {
  sys_device_base_t base;  // first: id, suspended
  bool i2c_added;
  bool oe_locked;  // OE is locked on its device and driven by us
  sys_io_pin_t oe;
  sys_pins_state_t pins;
  sys_i2c_dev_t i2c;
  uint16_t duty[PCA9685_CHANNELS];  // last written duty (get_level reads it back)
} pca_t;

/* ---- Chip access: the router and the registry add the context ---- */

static err_h chip_sleep(pca_t* c, bool sleep) {
  ERR_TRY(sys_i2c_reg_update(&c->i2c, REG_MODE1, MODE1_SLEEP, sleep ? MODE1_SLEEP : 0));
  if (!sleep) {
    esp_rom_delay_us(WAKEUP_DELAY_US);
  }
  return NULL;
}

static err_h chip_set_duty(pca_t* c, uint8_t ch, uint16_t duty) {
  const bool full_on = duty >= IO_PWM_DUTY_MAX;
  const bool full_off = duty == 0;
  const uint16_t raw = full_on ? IO_PWM_DUTY_MAX : duty;
  const uint8_t data[4] = {0, full_on ? LED_FULL : 0, raw & 0xFF, full_off ? LED_FULL : (raw >> 8)};
  ERR_TRY(sys_i2c_reg_write(&c->i2c, REG_LED_N(ch), data, sizeof(data)));
  c->duty[ch] = duty;  // state follows the chip, not the request
  return NULL;
}

static err_h chip_set_frequency(pca_t* c, uint16_t hz) {
  // prescale = round(25 MHz / (4096 * f)) - 1; the range check keeps it inside 3..255
  long pre = lround(OSC_HZ / (4096.0 * hz)) - 1;
  if (pre < MIN_PRESCALE) {
    pre = MIN_PRESCALE;
  }
  if (pre > 0xFF) {
    pre = 0xFF;
  }
  // The prescaler can only be written while the chip sleeps.
  ERR_TRY(chip_sleep(c, true));
  const uint8_t prescale = (uint8_t)pre;
  ERR_TRY(sys_i2c_reg_write(&c->i2c, REG_PRE_SCALE, &prescale, 1));
  return chip_sleep(c, false);
}

// For every op that changes an output: not suspended, the channel exists and isn't locked by another device.
static err_h check_writable(const pca_t* c, uint8_t pin) {
  ERR_TRY(sys_device_check_active(&c->base));
  return sys_pins_state_check_free(&c->pins, c->base.id, pin);
}

/* ---- IO contract ---- */

IO_HANDLER(SET_LEVEL) {
  pca_t* c = ctx;
  ERR_CHECK(a->level <= 1, ERR_CMD_BAD_ARG);
  ERR_TRY(check_writable(c, a->pin));
  return chip_set_duty(c, a->pin, a->level ? IO_PWM_DUTY_MAX : 0);
}

IO_HANDLER(TOGGLE) {
  pca_t* c = ctx;
  ERR_TRY(check_writable(c, a->pin));
  return chip_set_duty(c, a->pin, c->duty[a->pin] >= IO_PWM_DUTY_MAX / 2 ? 0 : IO_PWM_DUTY_MAX);
}

// A read: allowed while suspended.
IO_HANDLER(GET_LEVEL) {
  pca_t* c = ctx;
  ERR_TRY(sys_pins_state_check(&c->pins, c->base.id, a->pin));
  const uint8_t level = c->duty[a->pin] >= IO_PWM_DUTY_MAX / 2;
  return cmd_reply(out, out_cap, out_len, &level, sizeof(level));
}

IO_HANDLER(SET_PWM_DUTY) {
  pca_t* c = ctx;
  ERR_TRY(check_writable(c, a->pin));
  ERR_CHECK(a->duty <= IO_PWM_DUTY_MAX, ERR_DEV_RANGE, .id = c->base.id, .value = a->duty, .min = 0,
            .max = IO_PWM_DUTY_MAX);
  return chip_set_duty(c, a->pin, (uint16_t)a->duty);
}

// One frequency for all 16 channels: `pin` is not used.
IO_HANDLER(SET_PWM_FREQ) {
  pca_t* c = ctx;
  ERR_TRY(sys_device_check_active(&c->base));
  ERR_CHECK(a->frequency_Hz >= PCA9685_MIN_FREQUENCY_HZ && a->frequency_Hz <= PCA9685_MAX_FREQUENCY_HZ, ERR_DEV_RANGE,
            .id = c->base.id, .value = a->frequency_Hz, .min = PCA9685_MIN_FREQUENCY_HZ,
            .max = PCA9685_MAX_FREQUENCY_HZ);
  return chip_set_frequency(c, (uint16_t)a->frequency_Hz);
}

/* ---- Pins contract ---- */

SYS_PINS_STATE_HANDLERS(pca_t)

// The owner of a locked channel (a bridge driver on CH 8-15) drives it here; the IO writes refuse it.
PINS_HANDLER(SET_LOCKED_LEVEL) {
  pca_t* c = ctx;
  ERR_TRY(sys_device_check_active(&c->base));
  ERR_TRY(sys_pins_state_check(&c->pins, c->base.id, a->pin));
  ERR_CHECK(a->level <= 1, ERR_CMD_BAD_ARG);
  return chip_set_duty(c, a->pin, a->level ? IO_PWM_DUTY_MAX : 0);
}

/* ---- Lifecycle ---- */

// Releases the hardware; REMOVE and the create rollback. Each step runs only if it was built, none returns early.
static err_h uninstall(void* ctx) {
  pca_t* c = ctx;
  err_h err = NULL;
  if (c->i2c_added) {
    SYS_DEVICE_KEEP_FIRST(err, chip_sleep(c, true));
    SYS_DEVICE_KEEP_FIRST(err, sys_i2c_dev_remove(&c->i2c));
    c->i2c_added = false;
  }
  if (c->oe_locked) {
    SYS_DEVICE_KEEP_FIRST(err, sys_pins_set_locked_level(c->oe, true));  // outputs off
    SYS_DEVICE_KEEP_FIRST(err, sys_pins_unlock(c->oe));
    c->oe_locked = false;
  }
  return err;
}

DEVICE_HANDLER(RESET) {
  pca_t* c = ctx;
  ERR_TRY(sys_device_check_active(&c->base));
  for (uint8_t ch = 0; ch < PCA9685_CHANNELS; ch++) {
    ERR_TRY(chip_set_duty(c, ch, 0));
  }
  return sys_i2c_reg_update(&c->i2c, REG_MODE1, MODE1_AI, MODE1_AI);
}

/* Fault safe state: OE high and the chip asleep. Every step runs, and the
 * device counts as suspended even if one failed (a half-safe device must not
 * accept writes). */
DEVICE_HANDLER(SUSPEND) {
  pca_t* c = ctx;
  err_h err = NULL;
  c->base.suspended = true;
  if (c->oe_locked) {
    SYS_DEVICE_KEEP_FIRST(err, sys_pins_set_locked_level(c->oe, true));
  }
  SYS_DEVICE_KEEP_FIRST(err, chip_sleep(c, true));
  return err;
}

// A failing resume leaves the device suspended.
DEVICE_HANDLER(RESUME) {
  pca_t* c = ctx;
  if (!c->base.suspended) {
    return NULL;
  }
  ERR_TRY(chip_sleep(c, false));
  if (c->oe_locked) {
    ERR_TRY(sys_pins_set_locked_level(c->oe, false));
  }
  c->base.suspended = false;
  return NULL;
}

/* ---- Create ---- */

// The registry allocated `ctx` (id set); on a failure it calls uninstall() and frees it.
static err_h create(void* ctx, const void* cfg_in) {
  pca_t* c = ctx;
  const pca9685_cfg_t* cfg = cfg_in;
  ERR_CHECK(cfg->i2c_bus < SYS_I2C_BUS_COUNT, ERR_DEV_RANGE, .id = c->base.id, .value = cfg->i2c_bus, .min = 0,
            .max = SYS_I2C_BUS_COUNT - 1);
  c->oe = cfg->oe_pin;
  if (sys_io_pin_valid(c->oe)) {
    ERR_TRY(sys_device_use(&c->base, c->oe.device));
  }
  sys_pins_state_init(&c->pins, PCA9685_CHANNELS);
  sys_i2c_dev_init(&c->i2c, cfg->i2c_bus, cfg->i2c_addr, I2C_SPEED_HZ);

  ERR_TRY(sys_i2c_dev_add(&c->i2c));  // probes the chip
  c->i2c_added = true;
  ERR_TRY(chip_sleep(c, false));
  ERR_TRY(sys_i2c_reg_update(&c->i2c, REG_MODE1, MODE1_AI, MODE1_AI));
  if (sys_io_pin_valid(c->oe)) {
    ERR_TRY(sys_io_set_mode(c->oe, SYS_IO_MODE_OUTPUT_PUSH_PULL));
    ERR_TRY(sys_pins_lock(c->oe));
    c->oe_locked = true;
    ERR_TRY(sys_pins_set_locked_level(c->oe, false));  // active low: outputs enabled
  }
  return NULL;
}

/* ---- Type ---- */

// An op not listed (a contract the PCA9685 doesn't have) answers ERR_DEV_UNKNOWN_OP; OPS lists this table.
static const sys_device_op_t k_ops[] = {
    DEVICE_ENTRY(RESET),          DEVICE_ENTRY(SUSPEND),      DEVICE_ENTRY(RESUME),
    IO_ENTRY(SET_LEVEL),          IO_ENTRY(TOGGLE),           IO_ENTRY(GET_LEVEL),
    IO_ENTRY(SET_PWM_DUTY),       IO_ENTRY(SET_PWM_FREQ),
    SYS_PINS_STATE_ENTRIES,           PINS_ENTRY(SET_LOCKED_LEVEL),
};

SYS_DEVICE_TYPE_DEFINE(.type = SYS_DEVICE_TYPE_PCA9685, .cfg_size = sizeof(pca9685_cfg_t), .ctx_size = sizeof(pca_t),
                       .create = create, .uninstall = uninstall, SYS_DEVICE_OPS(k_ops))
