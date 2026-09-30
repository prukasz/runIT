#include <math.h>
#include <stdlib.h>
#include "device_pca9685.h"
#include "esp_rom_sys.h"
#include "sys_device.h"
#include "sys_error.h"
#include "sys_i2c.h"
#include "sys_io.h"

#undef OWNER
#define OWNER OWNER_DEVICE_PCA9685
#define PINS_MASK 0xFFFF
#define PCA9685_I2C_DEFAULT_FREQUENCY 100000

// Instance state: the whole device in one struct (chip state; the create cfg is not kept).
typedef struct pca_ctx_t {
  sys_device_adapter_base_t base;  // must be first
  sys_io_pin_ref_t oe_pin;         // the OE pin of the create cfg, converted once at install
  sys_i2c_dev_t i2c;
  uint16_t duty[PCA9685_CHANNEL_ALL];  // last written duty (read-back for get_level)
} pca_ctx_t;

// Install steps, recorded so teardown rolls back only what was actually built
enum { PCA_STEP_I2C_ADDED = 0, PCA_STEP_OE_READY = 1 };

#define REG_MODE1 0x00
#define REG_LED_START 0x06
#define REG_PRE_SCALE 0xFE
#define REG_LED_N(ch) (REG_LED_START + (ch) * 4)

#define MODE1_SLEEP_BIT (1 << 4)
#define MODE1_AI (1 << 5)
#define LED_FULL_ON_OFF (1 << 4)

#define PCA9685_INTERNAL_FREQ 25000000UL
#define WAKEUP_DELAY_US 500
#define MIN_PRESCALER 0x03

/* ---- Chip access: err_h, the dispatcher above adds the device id ---- */

static SE_MUST_USE err_h chip_sleep(pca_ctx_t* c, bool sleep) {
  SE_TRY(sys_i2c_reg_update(&c->i2c, REG_MODE1, MODE1_SLEEP_BIT, sleep ? MODE1_SLEEP_BIT : 0));
  if (!sleep) esp_rom_delay_us(WAKEUP_DELAY_US);
  return NULL;
}

static SE_MUST_USE err_h chip_enable_auto_increment(pca_ctx_t* c) {
  return sys_i2c_reg_update(&c->i2c, REG_MODE1, MODE1_AI, MODE1_AI);
}

static SE_MUST_USE err_h chip_set_duty(pca_ctx_t* c, uint8_t channel, uint16_t value) {
  bool full_on = (value >= PCA9685_MAX_PWM_VALUE);
  bool full_off = (value == 0);
  uint16_t raw = full_on ? 4095 : value;

  uint8_t data[4] = {0, full_on ? LED_FULL_ON_OFF : 0, raw & 0xFF, full_off ? LED_FULL_ON_OFF : (raw >> 8)};
  SE_TRY(sys_i2c_reg_write(&c->i2c, REG_LED_N(channel), data, sizeof(data)));
  c->duty[channel] = value;  // state follows the chip, not the request
  return NULL;
}

static SE_MUST_USE err_h chip_set_frequency(pca_ctx_t* c, uint16_t freq) {
  // prescale = round(25 MHz / (4096 * f)) - 1, clamped to 3..255 (~1526 Hz .. ~24 Hz).
  long pre = lround((double)PCA9685_INTERNAL_FREQ / (4096.0 * freq)) - 1;
  if (pre < MIN_PRESCALER) pre = MIN_PRESCALER;
  if (pre > 0xFF) pre = 0xFF;

  // The prescaler can only be written while the chip sleeps
  SE_TRY(chip_sleep(c, true));
  uint8_t prescaler = (uint8_t)pre;
  SE_TRY(sys_i2c_reg_write(&c->i2c, REG_PRE_SCALE, &prescaler, 1));
  return chip_sleep(c, false);
}

/* ---- IO contract ---- */

static SE_MUST_USE err_h io_set_pwm_duty(void* handle, sys_io_pin_num_t pin, uint32_t duty) {
  SYS_DEV_CTX_FROM(pca_ctx_t, c, handle);
  VERIFY_PIN(SYS_DEV_GET_ID(c), pin, PINS_MASK);
  if (duty > PCA9685_MAX_PWM_VALUE) duty = PCA9685_MAX_PWM_VALUE;
  SE_TRY(chip_set_duty(c, pin, (uint16_t)duty));
  return NULL;
}

/* One frequency for all 16 channels: the pin is not used. */
static SE_MUST_USE err_h io_set_pwm_frequency(void* handle, sys_io_pin_num_t pin, uint32_t frequency_Hz) {
  SYS_DEV_CTX_FROM(pca_ctx_t, c, handle);
  (void)pin;
  SE_CHECK_IN_RANGE(frequency_Hz, PCA9685_MIN_FREQUENCY_HZ, PCA9685_MAX_FREQUENCY_HZ);
  SE_TRY(chip_set_frequency(c, (uint16_t)frequency_Hz));
  return NULL;
}

static SE_MUST_USE err_h io_get_level(void* handle, sys_io_pin_num_t pin, bool* level) {
  SYS_DEV_CTX_FROM(pca_ctx_t, c, handle);
  SE_CHECK_HANDLE(level);
  VERIFY_PIN(SYS_DEV_GET_ID(c), pin, PINS_MASK);
  *level = (c->duty[pin] >= (PCA9685_MAX_PWM_VALUE / 2));
  return NULL;
}

// set_level / toggle / reset are derived by sys_io from these (SYS_IO_PWM_DUTY_FULL is clamped to full on).
static const sys_io_contract_t s_pca9685_io_contract = {
    .set_pwm_duty = io_set_pwm_duty, .set_pwm_frequency = io_set_pwm_frequency, .get_level = io_get_level};

/* ---- Lifecycle ---- */

// Doubles as the install rollback path: each step is gated on having actually
// run, and no step may early-return - teardown must always free everything.
static SE_MUST_USE err_h device_uninstall(void* handle) {
  SYS_DEV_CTX_FROM(pca_ctx_t, c, handle);
  err_h err = NULL;

  IF_SYS_DEV_STEP_DONE(c, PCA_STEP_I2C_ADDED) {
    SYS_DEV_TEARDOWN_STEP(err, chip_sleep(c, true));
    SYS_DEV_TEARDOWN_STEP(err, sys_i2c_dev_remove(&c->i2c));
  }
  IF_SYS_DEV_STEP_DONE(c, PCA_STEP_OE_READY) {
    SYS_DEV_TEARDOWN_STEP(err, sys_io_unlock_pin(c->oe_pin));
    SYS_DEV_TEARDOWN_STEP(err, sys_io_set_level(c->oe_pin, true));  // outputs off
    SYS_DEV_TEARDOWN_STEP(err, sys_io_reset(c->oe_pin));
  }

  free(c);
  return err;
}

static SE_MUST_USE err_h device_reset(void* handle) {
  SYS_DEV_CTX_FROM(pca_ctx_t, c, handle);
  for (uint8_t ch = 0; ch < PCA9685_CHANNEL_ALL; ch++) {
    SE_TRY(chip_set_duty(c, ch, 0));
  }
  SE_TRY(chip_enable_auto_increment(c));
  return NULL;
}

// Fault safe state: every step runs even if an earlier one fails.
static SE_MUST_USE err_h device_suspend(void* handle) {
  SYS_DEV_CTX_FROM(pca_ctx_t, c, handle);
  err_h err = NULL;

  if (sys_io_pin_is_valid(c->oe_pin)) {
    SYS_DEV_TEARDOWN_STEP(err, sys_io_set_locked_level(c->oe_pin, true));
  }
  SYS_DEV_TEARDOWN_STEP(err, chip_sleep(c, true));
  return err;
}

static SE_MUST_USE err_h device_resume(void* handle) {
  SYS_DEV_CTX_FROM(pca_ctx_t, c, handle);
  SE_TRY(chip_sleep(c, false));
  if (sys_io_pin_is_valid(c->oe_pin)) {
    SE_TRY(sys_io_set_locked_level(c->oe_pin, false));
  }
  return NULL;
}

static SE_MUST_USE err_h device_install(const void* cfg_blob, void** out_device_handle) {
  const d_pca9685_cfg_t* cfg = (const d_pca9685_cfg_t*)cfg_blob;

  // The cfg is read only here: what the device keeps (the OE pin) goes into its own state.
  pca_ctx_t* c = (pca_ctx_t*)calloc(1, sizeof(pca_ctx_t));
  SE_CHECK_IF_ALLOCATED(c);
  c->base.device_id = cfg->device_id;
  err_h err = NULL;

  c->oe_pin = pin_ref_from_wire(cfg->oe_pin);
  sys_i2c_dev_init(&c->i2c, cfg->i2c_bus != 0, cfg->i2c_addr, PCA9685_I2C_DEFAULT_FREQUENCY);
  SYS_DEV_INSTALL_STEP(sys_i2c_dev_add(&c->i2c), "i2c add (probes the chip)");
  SYS_DEV_STEP_DONE(c, PCA_STEP_I2C_ADDED);

  SYS_DEV_INSTALL_STEP(chip_sleep(c, false), "chip wake");
  SYS_DEV_INSTALL_STEP(chip_enable_auto_increment(c), "chip auto increment");

  if (sys_io_pin_is_valid(c->oe_pin)) {
    SYS_DEV_INSTALL_STEP(sys_io_set_mode(c->oe_pin), "OE pin mode");
    SYS_DEV_INSTALL_STEP(sys_io_set_level(c->oe_pin, false), "OE pin low");  // active low => outputs enabled
    SYS_DEV_INSTALL_STEP(sys_io_lock_pin(c->oe_pin), "OE pin lock");
    SYS_DEV_STEP_DONE(c, PCA_STEP_OE_READY);
  }

  *out_device_handle = c;
  return NULL;

fail:
  SYS_DEV_INSTALL_FAIL(err, cfg->device_id, out_device_handle, device_uninstall, c);
  return NULL;
}

// No freeze / sync: outputs are written straight to the chip.
static const sys_device_class_t s_pca9685_class = {
    .name = "PCA9685_PWM_EXPANDER",
    .contracts = {[SYS_DEVICE_CONTRACT_IO] = &s_pca9685_io_contract},
    .ops = {.install = device_install,
        .uninstall = device_uninstall,
        .reset = device_reset,
        .suspend = device_suspend,
        .resume = device_resume},
};

err_h d_pca9685_create(const d_pca9685_cfg_t* cfg) {
  SE_CHECK_NOT_NULL(cfg);
  SE_TRY(PIN_REFS_BELOW(cfg->device_id, pin_ref_from_wire(cfg->oe_pin)));
  return SYS_DEVICE_CREATE(&s_pca9685_class, cfg);
}
