#include <stdlib.h>
#include "device_dac53202.h"
#include "sys_device.h"
#include "sys_error.h"
#include "sys_i2c.h"
#include "sys_io.h"

#define OWNER OWNER_DEVICE

#define DAC53202_I2C_FREQUENCY 400000
#define DAC53202_CHANNELS 2
#define DAC53202_CHANNEL_MASK 0x03

/* Full scale: both channels use VDD as reference at gain 1x (3.3 V board supply). */
#define DAC53202_VREF_MV 3300

/* Register map: DAC53202 datasheet (SLASF47) §7.6. Channel 0 and 1 registers
   are not in address order. */
#define REG_DAC_1_VOUT_CMP_CONFIG 0x03
#define REG_DAC_0_VOUT_CMP_CONFIG 0x15
#define REG_DAC_1_DATA 0x19
#define REG_DAC_0_DATA 0x1C
#define REG_COMMON_CONFIG 0x1F

/* COMMON-CONFIG: reset 0x0FFF (both channels powered down, Hi-Z). Per channel
   VOUT-PDN (2 bits) + IOUT-PDN (1 bit): channel 0 at [11:9], channel 1 at [2:0].
   The don't-care bits [8:3] are written with their reset value. */
#define COMMON_DONT_CARE 0x01F8
#define PDN_CH0 0x0E00
#define PDN_CH1 0x0007
#define VOUT_ON_CH0 0x0200  // VOUT powered up, IOUT still down
#define VOUT_ON_CH1 0x0001

/* VOUT-GAIN [12:10] = 001: gain 1x, VDD as reference (full scale = VDD). */
#define VOUT_GAIN_VDD (1u << 10)

// Instance state: the whole device in one struct (chip state, bus; the create cfg is not kept).
typedef struct dac_ctx_t {
  sys_device_base_t base;  // must be first
  sys_i2c_dev_t i2c;

  uint8_t power_on_mask;  // bit per channel: VOUT powered up
  uint16_t raw[DAC53202_CHANNELS];  // last code written (read-back for get_voltage)
  uint8_t suspended_power_mask;
} dac_ctx_t;

// Install steps, recorded so teardown rolls back only what was actually built
enum { DAC53202_STEP_I2C_ADDED = 0 };

/* ---- Chip access: err_h, the dispatcher above adds the device id ---- */

static SE_MUST_USE err_h chip_write_reg(dac_ctx_t* c, uint8_t reg, uint16_t data) {
  const uint8_t b[2] = {(uint8_t)(data >> 8), (uint8_t)data};
  return sys_i2c_reg_write(&c->i2c, reg, b, sizeof(b));
}

/* Power the VOUT of each channel in the mask up; the others down (Hi-Z). */
static SE_MUST_USE err_h chip_set_power(dac_ctx_t* c, uint8_t power_on_mask) {
  uint16_t reg = COMMON_DONT_CARE;
  reg |= (power_on_mask & 0x01) ? VOUT_ON_CH0 : PDN_CH0;
  reg |= (power_on_mask & 0x02) ? VOUT_ON_CH1 : PDN_CH1;
  SE_TRY(chip_write_reg(c, REG_COMMON_CONFIG, reg));
  c->power_on_mask = power_on_mask & DAC53202_CHANNEL_MASK;
  return NULL;
}

/* Select the VDD reference on both channels and power them down (Hi-Z). */
static SE_MUST_USE err_h chip_start(dac_ctx_t* c) {
  SE_TRY(chip_write_reg(c, REG_DAC_0_VOUT_CMP_CONFIG, VOUT_GAIN_VDD));
  SE_TRY(chip_write_reg(c, REG_DAC_1_VOUT_CMP_CONFIG, VOUT_GAIN_VDD));
  return chip_set_power(c, 0x00);
}

/* Writes the code of one channel and powers the channel up if it was down. */
static SE_MUST_USE err_h chip_set_voltage_mV(dac_ctx_t* c, uint8_t channel, uint16_t voltage_mV) {
  // Data is MSB-aligned in [15:4] (DAC63202 12 bit; DAC53202 uses the top 10).
  const uint16_t code = (uint16_t)((((uint32_t)voltage_mV * 4095) / DAC53202_VREF_MV) << 4);
  SE_TRY(chip_write_reg(c, channel ? REG_DAC_1_DATA : REG_DAC_0_DATA, code));
  c->raw[channel] = code;

  const uint8_t next = c->power_on_mask | (1u << channel);
  return next == c->power_on_mask ? NULL : chip_set_power(c, next);
}

/* ---- IO contract ---- */

static SE_MUST_USE err_h io_reset_pin(void* handle, sys_io_pin_num_t pin) {
  SYS_DEV_CTX_FROM(dac_ctx_t, c, handle);
  VERIFY_PIN(SYS_DEV_GET_ID(c), pin, DAC53202_CHANNEL_MASK);
  return chip_set_power(c, c->power_on_mask & ~(1u << pin));
}

static SE_MUST_USE err_h io_set_voltage(void* handle, sys_io_pin_num_t pin, uint32_t voltage_mV) {
  SYS_DEV_CTX_FROM(dac_ctx_t, c, handle);
  VERIFY_PIN(SYS_DEV_GET_ID(c), pin, DAC53202_CHANNEL_MASK);
  SE_CHECK_IN_RANGE(voltage_mV, 0, DAC53202_VREF_MV);
  return chip_set_voltage_mV(c, (uint8_t)pin, (uint16_t)voltage_mV);
}

static SE_MUST_USE err_h io_get_voltage(void* handle, sys_io_pin_num_t pin, int32_t* out_mV) {
  SYS_DEV_CTX_FROM(dac_ctx_t, c, handle);
  SE_CHECK_HANDLE(out_mV);
  VERIFY_PIN(SYS_DEV_GET_ID(c), pin, DAC53202_CHANNEL_MASK);
  *out_mV = (int32_t)(((uint32_t)(c->raw[pin] >> 4) * DAC53202_VREF_MV) / 4095);
  return NULL;
}

static const sys_io_contract_t s_dac53202_io_contract = {
    .reset = io_reset_pin, .set_voltage = io_set_voltage, .get_voltage = io_get_voltage};

/* ---- Lifecycle ---- */

// Doubles as the install rollback path: each step is gated on having actually
// run, and no step may early-return - teardown must always free everything.
static SE_MUST_USE err_h device_uninstall(void* handle) {
  SYS_DEV_CTX_FROM(dac_ctx_t, c, handle);
  err_h err = NULL;

  IF_SYS_DEV_STEP_DONE(c, DAC53202_STEP_I2C_ADDED) {
    SYS_DEV_TEARDOWN_STEP(err, sys_i2c_dev_remove(&c->i2c));
  }

  free(c);
  return err;
}

static SE_MUST_USE err_h device_reset(void* handle) {
  SYS_DEV_CTX_FROM(dac_ctx_t, c, handle);
  return chip_set_power(c, 0x00);
}

/* Suspend powers both outputs down (Hi-Z) and remembers which were on;
   resume powers those back up with their last code. */
static SE_MUST_USE err_h device_suspend(void* handle) {
  SYS_DEV_CTX_FROM(dac_ctx_t, c, handle);
  c->suspended_power_mask = c->power_on_mask;
  return chip_set_power(c, 0x00);
}

static SE_MUST_USE err_h device_resume(void* handle) {
  SYS_DEV_CTX_FROM(dac_ctx_t, c, handle);
  return chip_set_power(c, c->suspended_power_mask);
}

static SE_MUST_USE err_h device_install(const void* cfg_blob, void** out_device_handle) {
  const d_dac53202_cfg_t* cfg = (const d_dac53202_cfg_t*)cfg_blob;

  // The cfg is read only here: nothing of it is kept but the bus address.
  dac_ctx_t* c = (dac_ctx_t*)calloc(1, sizeof(dac_ctx_t));
  SE_CHECK_IF_ALLOCATED(c);
  c->base.device_id = cfg->device_id;
  err_h err = NULL;

  sys_i2c_dev_init(&c->i2c, cfg->i2c_bus != 0, cfg->i2c_addr, DAC53202_I2C_FREQUENCY);
  SYS_DEV_INSTALL_STEP_BIT(c, DAC53202_STEP_I2C_ADDED, sys_i2c_dev_add(&c->i2c), "i2c add (probes the chip)");

  // Outputs stay powered down (Hi-Z) until a voltage is set.
  SYS_DEV_INSTALL_STEP(chip_start(c), "reference and power-down");

  *out_device_handle = c;
  return NULL;

fail:
  SYS_DEV_INSTALL_FAIL(err, out_device_handle, device_uninstall, c);
  return NULL;
}

const sys_device_class_t g_dac53202_class = {
    .type_id = DAC53202_TYPE_ID,
    .cfg_size = sizeof(d_dac53202_cfg_t),
    .name = "DAC53202",
    .contracts = {[SYS_DEVICE_CONTRACT_IO] = &s_dac53202_io_contract},
    .ops = {.install = device_install,
        .uninstall = device_uninstall,
        .reset = device_reset,
        .suspend = device_suspend,
        .resume = device_resume},
};
