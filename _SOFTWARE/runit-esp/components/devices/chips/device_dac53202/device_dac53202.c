/*
 * DAC53202: two-channel 10-bit voltage DAC over I2C. The whole device is this file: nothing includes it, the rest of
 * the firmware reaches it by its ID and the IO contract's ops (a channel is a pin: VOLTAGE sets it, RESET puts it
 * Hi-Z). Both channels take VDD (3.3 V) as their reference, gain 1x.
 */
#include "sys_device_driver.h"
#include "sys_i2c_contract.h"
#include "sys_io_driver.h"

SYS_DEVICE_TYPE_DECLARE(DAC53202, 0x0046)

#define DAC53202_CHANNELS 2u
#define DAC53202_VREF_MV  3300

/** @brief CREATE's struct (wire format). */
typedef struct __attribute__((packed)) {
  uint8_t i2c_bus;   /**< The bus's device ID (an I2C contract device) */
  uint8_t i2c_addr;  /**< 7-bit I2C address (0x48 - 0x4B) */
} dac53202_cfg_t;

#define I2C_SPEED_HZ 400000u

/* Register map: datasheet 7.6. Channel 0 and 1 registers are not in address order. */
#define REG_DAC_1_VOUT_CMP_CONFIG 0x03u
#define REG_DAC_0_VOUT_CMP_CONFIG 0x15u
#define REG_DAC_1_DATA            0x19u
#define REG_DAC_0_DATA            0x1Cu
#define REG_COMMON_CONFIG         0x1Fu

/* COMMON-CONFIG: per channel VOUT-PDN (2 bits) + IOUT-PDN (1 bit): channel 0 at [11:9], channel 1 at [2:0]. The
 * don't-care bits [8:3] keep their reset value. */
#define COMMON_DONT_CARE 0x01F8u
#define PDN_CH0          0x0E00u
#define PDN_CH1          0x0007u
#define VOUT_ON_CH0      0x0200u  // VOUT powered up, IOUT still down
#define VOUT_ON_CH1      0x0001u

#define VOUT_GAIN_VDD (1u << 10)  // VOUT-GAIN [12:10] = 001: gain 1x, VDD as reference

/* The data is a 10-bit code, MSB left-aligned in the 16-bit register (datasheet 7.6.8). */
#define CODE_MAX   1023u
#define CODE_SHIFT 6u

typedef struct {
  uint8_t id;
  sys_io_pins_t pins;
  sys_i2c_dev_t i2c;
  uint8_t on_mask;                   // bit per channel: VOUT powered up
  uint16_t code[DAC53202_CHANNELS];  // last code written; 0 for a channel that is off
} dac_t;

#define SYS_DEVICE_CTX dac_t

static err_h chip_write(dac_t* c, uint8_t reg, uint16_t data) {
  return sys_i2c_reg_write_u16(&c->i2c, reg, data, SYS_I2C_BE);
}

// Powers the VOUT of each channel of `mask` up and the others down (Hi-Z).
static err_h chip_set_power(dac_t* c, uint8_t mask) {
  const uint16_t reg = COMMON_DONT_CARE | ((mask & 1u) ? VOUT_ON_CH0 : PDN_CH0) | ((mask & 2u) ? VOUT_ON_CH1 : PDN_CH1);
  return chip_write(c, REG_COMMON_CONFIG, reg);
}

/* ---- IO contract ---- */

/*******************************************************************************
 * CONTRACT_IO : OP_VOLTAGE (write)
 * The code first, then the channel powered up: it starts at the new voltage.
 ******************************************************************************/
OP_DECLARE_WRITE(CONTRACT_IO, OP_VOLTAGE) {
  ERR_TRY(sys_io_pins_check_free(&c->pins, c->id, args->pin));
  ERR_CHECK_RANGE(args->voltage_mV, 0, DAC53202_VREF_MV);
  const uint16_t code = (uint16_t)(((uint32_t)args->voltage_mV * CODE_MAX + DAC53202_VREF_MV / 2) / DAC53202_VREF_MV);
  ERR_TRY(chip_write(c, args->pin ? REG_DAC_1_DATA : REG_DAC_0_DATA, (uint16_t)(code << CODE_SHIFT)));
  c->code[args->pin] = code;
  const uint8_t mask = c->on_mask | (uint8_t)(1u << args->pin);
  if (mask != c->on_mask) {
    ERR_TRY(chip_set_power(c, mask));
    c->on_mask = mask;
  }
  return NULL;
}

/*******************************************************************************
 * CONTRACT_IO : OP_VOLTAGE (read)
 * The voltage last set (from its code: the 10-bit rounding shows), 0 when off.
 ******************************************************************************/
OP_DECLARE_READ(CONTRACT_IO, OP_VOLTAGE) {
  ERR_TRY(sys_io_pins_check(&c->pins, c->id, args->pin));
  const int32_t mV = (int32_t)(((uint32_t)c->code[args->pin] * DAC53202_VREF_MV + CODE_MAX / 2) / CODE_MAX);
  return CMD_REPLY(CONTRACT_IO, OP_VOLTAGE, .pin = args->pin, .voltage_mV = mV);
}

/*******************************************************************************
 * CONTRACT_IO : OP_RESET (write)
 * The channel off (Hi-Z), its voltage forgotten.
 ******************************************************************************/
OP_DECLARE_WRITE(CONTRACT_IO, OP_RESET) {
  ERR_TRY(sys_io_pins_check_free(&c->pins, c->id, args->pin));
  const uint8_t mask = c->on_mask & (uint8_t)~(1u << args->pin);
  ERR_TRY(chip_set_power(c, mask));
  c->on_mask = mask;
  c->code[args->pin] = 0;
  return NULL;
}

SYS_IO_PINS_HANDLERS

/* ---- Lifecycle ---- */

/*******************************************************************************
 * CONTRACT_SYS_DEVICE : OP_RESET (write)
 * Both channels off (Hi-Z), their voltages forgotten.
 ******************************************************************************/
OP_DECLARE_WRITE(CONTRACT_SYS_DEVICE, OP_RESET) {
  ERR_TRY(chip_set_power(c, 0));
  c->on_mask = 0;
  c->code[0] = c->code[1] = 0;
  return NULL;
}

// Both outputs Hi-Z; on_mask is kept for resume.
static err_h suspend(dac_t* c) {
  return chip_set_power(c, 0);
}

// The channels that were on come back at their last code.
static err_h resume(dac_t* c) {
  return chip_set_power(c, c->on_mask);
}

static err_h create(uint8_t id, dac_t* c, const dac53202_cfg_t* cfg) {
  ERR_CHECK_RANGE(cfg->i2c_addr, 0x48, 0x4B);
  c->id = id;
  sys_io_pins_init(&c->pins, DAC53202_CHANNELS);
  ERR_TRY(sys_i2c_dev_attach(id, &c->i2c, cfg->i2c_bus, cfg->i2c_addr, I2C_SPEED_HZ));
  ERR_TRY(chip_write(c, REG_DAC_0_VOUT_CMP_CONFIG, VOUT_GAIN_VDD));
  ERR_TRY(chip_write(c, REG_DAC_1_VOUT_CMP_CONFIG, VOUT_GAIN_VDD));
  return chip_set_power(c, 0);  // Hi-Z until a voltage is set
}

static const sys_device_op_t k_ops[] = {
    OP_ENTRY_W(CONTRACT_SYS_DEVICE, OP_RESET),
    OP_ENTRY(CONTRACT_IO, OP_VOLTAGE),
    OP_ENTRY_W(CONTRACT_IO, OP_RESET),
    SYS_IO_PINS_ENTRIES,
};

SYS_DEVICE_TYPE_DEFINE(DAC53202, dac53202_cfg_t, create, suspend, resume, k_ops)
