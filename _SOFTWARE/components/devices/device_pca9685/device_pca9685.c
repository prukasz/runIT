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
  sys_dev_pin_t oe;  // output enable (active low): locked on its device, driven by us
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

/* ---- IO contract ---- */

IO_HANDLER(SET_LEVEL) {
  pca_t* c = ctx;
  ERR_CHECK(a->level <= 1, ERR_CMD_BAD_ARG);
  ERR_TRY(sys_pins_check_writable(&c->base, &c->pins, a->pin));
  return chip_set_duty(c, a->pin, a->level ? IO_PWM_DUTY_MAX : 0);
}

IO_HANDLER(TOGGLE) {
  pca_t* c = ctx;
  ERR_TRY(sys_pins_check_writable(&c->base, &c->pins, a->pin));
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
  ERR_TRY(sys_pins_check_writable(&c->base, &c->pins, a->pin));
  DEV_CHECK_RANGE(c->base.id, a->duty, 0, IO_PWM_DUTY_MAX);
  return chip_set_duty(c, a->pin, (uint16_t)a->duty);
}

// One frequency for all 16 channels: `pin` is not used.
IO_HANDLER(SET_PWM_FREQ) {
  pca_t* c = ctx;
  ERR_TRY(sys_device_check_active(&c->base));
  DEV_CHECK_RANGE(c->base.id, a->frequency_Hz, PCA9685_MIN_FREQUENCY_HZ, PCA9685_MAX_FREQUENCY_HZ);
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
  if (sys_i2c_dev_added(&c->i2c)) {
    SYS_DEVICE_KEEP_FIRST(err, chip_sleep(c, true));
    SYS_DEVICE_KEEP_FIRST(err, sys_i2c_dev_remove(&c->i2c));
  }
  SYS_DEVICE_KEEP_FIRST(err, sys_dev_pin_drive(&c->oe, true));  // outputs off, then the pin is let go
  SYS_DEVICE_KEEP_FIRST(err, sys_dev_pin_release(&c->oe));
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
  SYS_DEVICE_KEEP_FIRST(err, sys_dev_pin_drive(&c->oe, true));
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
  ERR_TRY(sys_dev_pin_drive(&c->oe, false));
  c->base.suspended = false;
  return NULL;
}

/* ---- Create ---- */

static err_h create(void* ctx, const void* cfg_in) {
  pca_t* c = ctx;
  const pca9685_cfg_t* cfg = cfg_in;
  DEV_CHECK_RANGE(c->base.id, cfg->i2c_bus, 0, SYS_I2C_BUS_COUNT - 1);
  sys_pins_state_init(&c->pins, PCA9685_CHANNELS);
  ERR_TRY(sys_i2c_dev_attach(&c->i2c, cfg->i2c_bus, cfg->i2c_addr, I2C_SPEED_HZ));  // probes the chip
  ERR_TRY(chip_sleep(c, false));
  ERR_TRY(sys_i2c_reg_update(&c->i2c, REG_MODE1, MODE1_AI, MODE1_AI));
  // Active low: the outputs are enabled. Left as an output on uninstall (its outputs-off level stays).
  ERR_TRY(sys_dev_pin_take_output(&c->base, &c->oe, cfg->oe_pin, false));
  c->oe.keep = true;
  return NULL;
}

/* ---- Type ---- */

static const sys_device_op_t k_ops[] = {
    DEVICE_ENTRY(RESET),          DEVICE_ENTRY(SUSPEND),      DEVICE_ENTRY(RESUME),
    IO_ENTRY(SET_LEVEL),          IO_ENTRY(TOGGLE),           IO_ENTRY(GET_LEVEL),
    IO_ENTRY(SET_PWM_DUTY),       IO_ENTRY(SET_PWM_FREQ),
    SYS_PINS_STATE_ENTRIES,           PINS_ENTRY(SET_LOCKED_LEVEL),
};

SYS_DEVICE_TYPE_DEFINE(.type = SYS_DEVICE_TYPE_PCA9685, .cfg_size = sizeof(pca9685_cfg_t), .ctx_size = sizeof(pca_t),
                       .create = create, .uninstall = uninstall, SYS_DEVICE_OPS(k_ops))
