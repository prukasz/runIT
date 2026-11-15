/*
 * PCA9685: 16-channel 12-bit PWM over I2C, type 0x0041. The whole device is this file: nothing includes it, the rest
 * of the firmware reaches it by its ID and the IO contract's ops.
 */
#include <math.h>

#include "board_time.h"
#include "sys_device_driver.h"
#include "sys_i2c.h"
#include "sys_io_driver.h"

SYS_DEVICE_TYPE_DECLARE(PCA9685, 0x0041)

#define PCA9685_CHANNELS         16u
#define PCA9685_MIN_FREQUENCY_HZ 24u    // prescaler 255 at the 25 MHz oscillator
#define PCA9685_MAX_FREQUENCY_HZ 1526u  // prescaler 3

/** @brief CREATE's struct (wire format). No OE pin control: `.oe_pin = SYS_IO_PIN_NONE`. */
typedef struct __attribute__((packed)) {
  uint8_t i2c_bus;      /**< The bus's device ID (an I2C contract device) */
  uint8_t i2c_addr;     /**< 7-bit I2C address (0x40 - 0x77) */
  sys_io_pin_t oe_pin;  /**< Optional active-low output enable pin */
} pca9685_cfg_t;

#define I2C_SPEED_HZ    100000u
#define WAKEUP_DELAY_US 500u

#define REG_MODE1     0x00u
#define REG_LED_START 0x06u
#define REG_PRE_SCALE 0xFEu
#define REG_LED_N(ch) (REG_LED_START + (ch) * 4u)

#define MODE1_SLEEP (1u << 4)
#define MODE1_AI    (1u << 5)
#define LED_FULL    (1u << 4)  // full on / full off bit of the LED_ON_H / LED_OFF_H registers

#define OSC_HZ       25000000.0
#define MIN_PRESCALE 0x03

typedef struct {
  uint8_t id;
  sys_io_pins_t pins;               // which channels exist, which are locked
  sys_io_claim_t oe;                // output enable (active low): locked on its device, driven by us
  sys_i2c_dev_t i2c;
  uint16_t duty[PCA9685_CHANNELS];  // last written duty (the LEVEL read answers from it)
} pca_t;

#define SYS_DEVICE_CTX pca_t

static err_h chip_sleep(pca_t* c, bool sleep) {
  ERR_TRY(sys_i2c_reg_update(&c->i2c, REG_MODE1, MODE1_SLEEP, sleep ? MODE1_SLEEP : 0));
  if (!sleep) {
    board_time_delay_us(WAKEUP_DELAY_US);
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

/*******************************************************************************
 * CONTRACT_IO : OP_LEVEL (write)
 * Drives 0% or 100% PWM duty for the pin.
 ******************************************************************************/
OP_DECLARE_WRITE(CONTRACT_IO, OP_LEVEL) {
  ERR_CHECK(args->level <= 1, ERR_CMD_BAD_ARG);
  ERR_TRY(sys_io_pins_check_free(&c->pins, c->id, args->pin));
  return chip_set_duty(c, args->pin, args->level ? IO_PWM_DUTY_MAX : 0);
}

/*******************************************************************************
 * CONTRACT_IO : OP_LEVEL (read)
 * Returns true when duty >= 50% (allowed while suspended).
 ******************************************************************************/
OP_DECLARE_READ(CONTRACT_IO, OP_LEVEL) {
  ERR_TRY(sys_io_pins_check(&c->pins, c->id, args->pin));
  return CMD_REPLY(CONTRACT_IO, OP_LEVEL, .pin = args->pin, .level = c->duty[args->pin] >= IO_PWM_DUTY_MAX / 2);
}

/*******************************************************************************
 * CONTRACT_IO : OP_TOGGLE (write)
 * Flips between 0% and 100% PWM duty based on current half-scale threshold.
 ******************************************************************************/
OP_DECLARE_WRITE(CONTRACT_IO, OP_TOGGLE) {
  ERR_TRY(sys_io_pins_check_free(&c->pins, c->id, args->pin));
  return chip_set_duty(c, args->pin, c->duty[args->pin] >= IO_PWM_DUTY_MAX / 2 ? 0 : IO_PWM_DUTY_MAX);
}

/*******************************************************************************
 * CONTRACT_IO : OP_PWM_DUTY (write)
 * Sets per-channel PWM duty (0..IO_PWM_DUTY_MAX).
 ******************************************************************************/
OP_DECLARE_WRITE(CONTRACT_IO, OP_PWM_DUTY) {
  ERR_TRY(sys_io_pins_check_free(&c->pins, c->id, args->pin));
  SYS_DEV_CHECK_IN_RANGE(c->id, args->duty, 0, IO_PWM_DUTY_MAX);
  return chip_set_duty(c, args->pin, (uint16_t)args->duty);
}

/*******************************************************************************
 * CONTRACT_IO : OP_PWM_FREQ (write)
 * Shared PWM frequency for all 16 channels (`pin` is ignored).
 ******************************************************************************/
OP_DECLARE_WRITE(CONTRACT_IO, OP_PWM_FREQ) {
  SYS_DEV_CHECK_IN_RANGE(c->id, args->frequency_Hz, PCA9685_MIN_FREQUENCY_HZ, PCA9685_MAX_FREQUENCY_HZ);
  return chip_set_frequency(c, (uint16_t)args->frequency_Hz);
}

/*******************************************************************************
 * CONTRACT_IO : OP_AVAILABLE / OP_LOCKED / OP_LOCK / OP_UNLOCK
 * Shared pin availability and ownership handlers; channel owners (e.g. a bridge
 * on CH 8-15) drive locked channels via the IO ops above.
 ******************************************************************************/
SYS_IO_PINS_HANDLERS

/* ---- Lifecycle ---- */

/*******************************************************************************
 * CONTRACT_SYS_DEVICE : OP_RESET (write)
 * Zeros all 16 PWM channel duties and ensures auto-increment mode (MODE1_AI).
 ******************************************************************************/
OP_DECLARE_WRITE(CONTRACT_SYS_DEVICE, OP_RESET) {
  for (uint8_t ch = 0; ch < PCA9685_CHANNELS; ch++) {
    ERR_TRY(chip_set_duty(c, ch, 0));
  }
  return sys_i2c_reg_update(&c->i2c, REG_MODE1, MODE1_AI, MODE1_AI);
}

/*******************************************************************************
 * PCA9685 : suspend (lifecycle)
 * Drives OE high and puts the chip to sleep (also the last state before REMOVE).
 * Runs every step so a half-safe device stays suspended on failure.
 ******************************************************************************/
static err_h suspend(pca_t* c) {
  err_h err = NULL;
  ERR_KEEP_FIRST(err, sys_io_claim_drive(&c->oe, true));
  ERR_KEEP_FIRST(err, chip_sleep(c, true));
  return err;
}

/*******************************************************************************
 * PCA9685 : resume (lifecycle)
 * Wakes the chip and drives OE low again; a failing resume leaves it suspended.
 ******************************************************************************/
static err_h resume(pca_t* c) {
  ERR_TRY(chip_sleep(c, false));
  return sys_io_claim_drive(&c->oe, false);
}

/* ---- Create ---- */

/*******************************************************************************
 * PCA9685 : create (init)
 * Initializes 16 channels, attaches I2C, wakes chip with MODE1_AI enabled,
 * and claims active-low OE output (kept driven on REMOVE via keep).
 ******************************************************************************/
static err_h create(uint8_t id, pca_t* c, const pca9685_cfg_t* cfg) {
  c->id = id;
  sys_io_pins_init(&c->pins, PCA9685_CHANNELS);
  ERR_TRY(sys_i2c_dev_attach(id, &c->i2c, cfg->i2c_bus, cfg->i2c_addr, I2C_SPEED_HZ));  // declares the bus, probes
  ERR_TRY(chip_sleep(c, false));
  ERR_TRY(sys_i2c_reg_update(&c->i2c, REG_MODE1, MODE1_AI, MODE1_AI));
  // Declares the use of OE's device, locks the pin, drives it low (active low: outputs on). SYS_IO_PIN_NONE: nothing.
  ERR_TRY(sys_io_claim_output(id, &c->oe, cfg->oe_pin, false));
  c->oe.keep = true;  // release leaves it an output at its outputs-off level
  return NULL;
}

/*******************************************************************************
 * PCA9685 : type registration (k_ops & SYS_DEVICE_TYPE_DEFINE)
 * Exposes lifecycle reset and IO PWM/level contract + shared pin handlers.
 ******************************************************************************/

static const sys_device_op_t k_ops[] = {
    OP_ENTRY_W(CONTRACT_SYS_DEVICE, OP_RESET),
    OP_ENTRY(CONTRACT_IO, OP_LEVEL),       OP_ENTRY_W(CONTRACT_IO, OP_TOGGLE),
    OP_ENTRY_W(CONTRACT_IO, OP_PWM_DUTY),  OP_ENTRY_W(CONTRACT_IO, OP_PWM_FREQ),
    SYS_IO_PINS_ENTRIES,
};

SYS_DEVICE_TYPE_DEFINE(PCA9685, pca9685_cfg_t, create, suspend, resume, k_ops)
