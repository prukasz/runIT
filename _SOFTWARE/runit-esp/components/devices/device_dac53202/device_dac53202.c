#include "device_dac53202.h"

#include "sys_i2c.h"

#define I2C_SPEED_HZ 400000u

/* Register map: DAC53202 datasheet (SLASF47) 7.6. Channel 0 and 1 registers are not in address order. */
#define REG_DAC_1_VOUT_CMP_CONFIG 0x03u
#define REG_DAC_0_VOUT_CMP_CONFIG 0x15u
#define REG_DAC_1_DATA            0x19u
#define REG_DAC_0_DATA            0x1Cu
#define REG_COMMON_CONFIG         0x1Fu

/* COMMON-CONFIG: reset 0x0FFF (both channels powered down, Hi-Z). Per channel VOUT-PDN (2 bits) + IOUT-PDN (1 bit):
   channel 0 at [11:9], channel 1 at [2:0]. The don't-care bits [8:3] are written with their reset value. */
#define COMMON_DONT_CARE 0x01F8u
#define PDN_CH0          0x0E00u
#define PDN_CH1          0x0007u
#define VOUT_ON_CH0      0x0200u  // VOUT powered up, IOUT still down
#define VOUT_ON_CH1      0x0001u

/* VOUT-GAIN [12:10] = 001: gain 1x, VDD as reference (full scale = VDD). */
#define VOUT_GAIN_VDD (1u << 10)

/* The data is a 10-bit straight-binary code, MSB left-aligned in the 16-bit register (datasheet 7.6.8: 0x155 is
   written 0x5540). */
#define CODE_MAX   1023u
#define CODE_SHIFT 6u

#define ALL_CHANNELS 0x03u

typedef struct {
  sys_device_base_t base;  // first: id, suspended
  sys_i2c_dev_t i2c;
  uint8_t power_on_mask;      // bit per channel: VOUT powered up
  uint8_t resume_mask;        // the channels that were on when SUSPEND turned them off
  uint16_t code[DAC53202_CHANNELS];  // last code written (GET_VOLTAGE reads it back)
} dac_t;

static err_h chip_write_reg(dac_t* c, uint8_t reg, uint16_t data) {
  const uint8_t b[2] = {(uint8_t)(data >> 8), (uint8_t)data};
  return sys_i2c_reg_write(&c->i2c, reg, b, sizeof(b));
}

// Powers the VOUT of each channel in the mask up and the others down (Hi-Z).
static err_h chip_set_power(dac_t* c, uint8_t power_on_mask) {
  uint16_t reg = COMMON_DONT_CARE;
  reg |= (power_on_mask & 0x01u) ? VOUT_ON_CH0 : PDN_CH0;
  reg |= (power_on_mask & 0x02u) ? VOUT_ON_CH1 : PDN_CH1;
  ERR_TRY(chip_write_reg(c, REG_COMMON_CONFIG, reg));
  c->power_on_mask = power_on_mask & ALL_CHANNELS;
  return NULL;
}

// Selects the VDD reference on both channels and powers them down (Hi-Z).
static err_h chip_start(dac_t* c) {
  ERR_TRY(chip_write_reg(c, REG_DAC_0_VOUT_CMP_CONFIG, VOUT_GAIN_VDD));
  ERR_TRY(chip_write_reg(c, REG_DAC_1_VOUT_CMP_CONFIG, VOUT_GAIN_VDD));
  return chip_set_power(c, 0);
}

// Writes the code, then powers the channel up if it was down (so it starts at the new voltage, not at the old code).
static err_h chip_set_voltage_mV(dac_t* c, uint8_t channel, uint32_t voltage_mV) {
  const uint16_t code = (uint16_t)((voltage_mV * CODE_MAX + DAC53202_VREF_MV / 2) / DAC53202_VREF_MV);
  ERR_TRY(chip_write_reg(c, channel ? REG_DAC_1_DATA : REG_DAC_0_DATA, (uint16_t)(code << CODE_SHIFT)));
  c->code[channel] = code;
  const uint8_t next = c->power_on_mask | (1u << channel);
  return next == c->power_on_mask ? NULL : chip_set_power(c, next);
}

static err_h check_channel(const dac_t* c, uint8_t pin) {
  ERR_CHECK(pin < DAC53202_CHANNELS, ERR_DEV_BAD_PIN, .id = c->base.id, .pin = pin);
  return NULL;
}

/* ---- IO contract ---- */

// The channel's output off (Hi-Z), its set voltage forgotten.
OP_WRITE(IO, RESET) {
  dac_t* c = ctx;
  ERR_TRY(check_channel(c, args->pin));
  ERR_TRY(chip_set_power(c, c->power_on_mask & ~(1u << args->pin)));
  c->code[args->pin] = 0;
  return NULL;
}

OP_WRITE(IO, VOLTAGE) {
  dac_t* c = ctx;
  ERR_TRY(check_channel(c, args->pin));
  SYS_DEV_CHECK_IN_RANGE(c->base.id, args->voltage_mV, 0, DAC53202_VREF_MV);
  return chip_set_voltage_mV(c, args->pin, (uint32_t)args->voltage_mV);
}

// The last set voltage (from its code, so the 10-bit rounding shows), 0 for a channel that is off. A read: allowed
// while suspended.
OP_READ(IO, VOLTAGE) {
  dac_t* c = ctx;
  ERR_TRY(check_channel(c, args->pin));
  const int32_t mV = (int32_t)(((uint32_t)c->code[args->pin] * DAC53202_VREF_MV + CODE_MAX / 2) / CODE_MAX);
  return sys_io_reply_voltage(args->pin, mV, out, out_cap, out_len);
}

/* ---- Lifecycle ---- */

// Releases the hardware; REMOVE and the create rollback. The outputs go Hi-Z first.
static err_h uninstall(void* ctx) {
  dac_t* c = ctx;
  err_h err = NULL;
  if (sys_i2c_dev_added(&c->i2c)) {
    ERR_KEEP_FIRST(err, chip_set_power(c, 0));
    ERR_KEEP_FIRST(err, sys_i2c_dev_remove(&c->i2c));
  }
  return err;
}

// Both outputs off (Hi-Z), the set voltages forgotten.
OP_WRITE(SYS_DEVICE, RESET) {
  dac_t* c = ctx;
  ERR_TRY(chip_set_power(c, 0));
  memset(c->code, 0, sizeof(c->code));
  return NULL;
}

/* Both outputs Hi-Z; the channels that were on are remembered for RESUME. The device counts as suspended even if the
   write failed (a half-safe device must not accept writes). */
static err_h suspend_hook(void* ctx) {
  dac_t* c = ctx;
  if (!c->base.suspended) {
    c->resume_mask = c->power_on_mask;
  }
  return chip_set_power(c, 0);
}

// The channels that were on come back with their last code. A failing resume leaves the device suspended.
static err_h resume_hook(void* ctx) {
  dac_t* c = ctx;
  ERR_TRY(chip_set_power(c, c->resume_mask));
  return NULL;
}

/* ---- Create ---- */

static err_h create(void* ctx, const void* cfg_in) {
  dac_t* c = ctx;
  const dac53202_cfg_t* cfg = cfg_in;
  SYS_DEV_CHECK_IN_RANGE(c->base.id, cfg->i2c_bus, 0, SYS_I2C_BUS_COUNT - 1);
  SYS_DEV_CHECK_IN_RANGE(c->base.id, cfg->i2c_addr, 0x48, 0x4B);
  ERR_TRY(sys_i2c_dev_attach(&c->i2c, cfg->i2c_bus, cfg->i2c_addr, I2C_SPEED_HZ));  // probes the chip
  return chip_start(c);  // the outputs stay Hi-Z until a voltage is set
}

/* ---- Type ---- */

static const sys_device_op_t k_ops[] = {
    OP_ENTRY_W(SYS_DEVICE, RESET),
    OP_ENTRY_W(IO, RESET),  OP_ENTRY(IO, VOLTAGE),
};

SYS_DEVICE_TYPE_DEFINE(DAC53202, dac53202_cfg_t, dac_t, k_ops)
