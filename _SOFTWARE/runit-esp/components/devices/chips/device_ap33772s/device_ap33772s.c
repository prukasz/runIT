/*
 * AP33772S: USB-C Power Delivery sink over I2C. The whole device is this file: nothing includes it, the rest of the
 * firmware reaches it by its ID and three contracts: POWER_USB_PD (PD: the charger's offers, the profile asked for),
 * POWER_VREG (VREG: the sink's output switch, a wanted voltage and current) and POWER_MONITOR (PMON: the output, one
 * channel). A request takes the charger's PPS profile when it fits, else the highest fixed profile at or below it.
 *
 * The output is the board's own supply, so reset, suspend and resume leave it as it is: switching it off in a fault
 * would cut the power of the ESP.
 */
#include "sys_device_driver.h"
#include "sys_i2c.h"
#include "sys_io_driver.h"
#include "sys_power_monitor.h"
#include "sys_power_usb_pd.h"
#include "sys_power_vreg.h"
#include "utils.h"

SYS_DEVICE_TYPE_DECLARE(AP33772S, 0x0045)

#define AP33772S_I2C_ADDR 0x52u  // fixed 7-bit I2C address

/** @brief CREATE's struct (wire format). The interrupt pin not wired is SYS_IO_PIN_NONE. */
typedef struct __attribute__((packed)) {
  uint8_t i2c_bus;        /**< The bus's device ID (an I2C contract device) */
  uint8_t i2c_addr;       /**< 7-bit I2C address (0x52) */
  sys_io_pin_t intr_pin;  /**< Active-low interrupt pin (optional) */
} ap33772s_cfg_t;

#define I2C_SPEED_HZ       400000u
#define PDO_COUNT          SYS_POWER_USB_PD_OPTIONS_MAX  // slots 1-7 SPR, 8-13 EPR
#define MAX_VOLTAGE_MV     22000u  // software limit, whatever the charger offers
#define MAX_CURRENT_MA     5000u
#define PPS_MIN_MV         3300u
#define PDO_WAIT_MS        100u    // the chip has the source PDOs this long after power-up
#define DEFAULT_VOLTAGE_MV 5000u
#define DEFAULT_CURRENT_MA 500u
#define VSAFE_5V_MV        5000u   // the 5 V every USB-PD source offers

#define CMD_SYSTEM    0x06u
#define CMD_VOLTAGE   0x11u
#define CMD_CURRENT   0x12u
#define CMD_SRCPDO    0x20u
#define CMD_PD_REQMSG 0x31u

#define OUTPUT_ON  0x12u
#define OUTPUT_OFF 0x11u

#define VOLTAGE_MV_PER_LSB 80
#define CURRENT_MA_PER_LSB 24

/* A source PDO as the chip reports it (2 bytes per slot, little endian): bits 0-7 voltage max (100 mV SPR, 200 mV
 * EPR), 8-9 peak current (fixed) / voltage-min flag (PPS, AVS), 10-13 current max, 14 type (0 fixed, 1 PPS in slots
 * 1-7 / AVS in slots 8-13). */
static inline uint8_t pdo_vmax(uint16_t p) { return (uint8_t)(p & 0xFFu); }
static inline bool pdo_has_vmin(uint16_t p) { return ((p >> 8) & 0x3u) > 0; }
static inline uint8_t pdo_imax_code(uint16_t p) { return (uint8_t)((p >> 10) & 0xFu); }
static inline bool pdo_augmented(uint16_t p) { return ((p >> 14) & 1u) != 0; }

typedef struct {
  uint8_t id;
  sys_i2c_dev_t i2c;
  sys_io_claim_t intr;
  uint16_t pdo[PDO_COUNT];   // slot - 1
  uint32_t last_voltage_mV;  // the last request the charger took
  uint32_t last_current_mA;
} ap_t;

#define SYS_DEVICE_CTX ap_t

/* ---- PDOs ---- */

// Code of a PDO current range: 0 = below 1.25 A, n = 1.00 + 0.25n A up to the next code, 15 = the top range.
static unsigned current_code(uint32_t mA) {
  const unsigned code = mA < 1250 ? 0u : ((mA - 1250) / 250) + 1;
  return code > 15u ? 15u : code;
}

// The most a request may ask of a PDO with current code `code`: the top of its range (15: up to MAX_CURRENT_MA).
static uint16_t pdo_max_mA(unsigned code) {
  return code >= 15 ? (uint16_t)MAX_CURRENT_MA : (uint16_t)(code * 250 + 1249);
}

static uint32_t pdo_mV(uint16_t pdo, uint8_t slot) {
  return pdo_vmax(pdo) * (slot >= 8 ? 200u : 100u);
}

// Read before each request and each read of the offers, so a charger attached after power-up is seen.
static err_h read_pdos(ap_t* c) {
  uint8_t raw[2 * PDO_COUNT];
  ERR_TRY(sys_i2c_reg_read(&c->i2c, CMD_SRCPDO, raw, sizeof(raw)));
  for (int i = 0; i < PDO_COUNT; i++) {
    c->pdo[i] = (uint16_t)(raw[2 * i] | (raw[2 * i + 1] << 8));
  }
  return NULL;
}

// Source PDO `slot` (1-based) as an option; false for an empty slot.
static bool pdo_option(const ap_t* c, uint8_t slot, sys_power_usb_pd_option_t* out) {
  const uint16_t pdo = c->pdo[slot - 1];
  if (pdo == 0) {
    return false;
  }
  out->slot = slot;
  out->max_mA = pdo_max_mA(pdo_imax_code(pdo));
  out->max_mV = (uint16_t)pdo_mV(pdo, slot);
  if (!pdo_augmented(pdo)) {
    out->type = SYS_POWER_USB_PD_OPTION_FIXED;
    out->min_mV = out->max_mV;
  } else if (slot < 8) {
    out->type = SYS_POWER_USB_PD_OPTION_PPS;
    out->min_mV = pdo_has_vmin(pdo) ? PPS_MIN_MV : 0;
  } else {
    out->type = SYS_POWER_USB_PD_OPTION_AVS;
    out->min_mV = pdo_has_vmin(pdo) ? 15000 : 0;
  }
  return true;
}

static err_h send_request(ap_t* c, unsigned voltage_sel, unsigned current_sel, unsigned slot) {
  const uint16_t rdo = (uint16_t)((voltage_sel & 0xFFu) | ((current_sel & 0xFu) << 8) | ((slot & 0xFu) << 12));
  const uint8_t payload[2] = {(uint8_t)rdo, (uint8_t)(rdo >> 8)};
  return sys_i2c_reg_write(&c->i2c, CMD_PD_REQMSG, payload, sizeof(payload));
}

/* Asks the charger for a voltage / current: its PPS profile when the voltage is in its range, else the highest fixed
 * profile at or below the voltage. A current above what the chosen profile offers is refused. */
static err_h negotiate(ap_t* c, uint32_t voltage_mV, uint32_t current_mA) {
  ERR_CHECK_RANGE(voltage_mV, 0, MAX_VOLTAGE_MV);
  ERR_CHECK_RANGE(current_mA, 1, MAX_CURRENT_MA);
  ERR_TRY(read_pdos(c));
  int pps = -1, fixed = -1;
  uint32_t fixed_mV = 0, lowest_mV = 0;
  for (uint8_t slot = 1; slot <= PDO_COUNT; slot++) {
    const uint16_t pdo = c->pdo[slot - 1];
    const uint32_t mV = pdo_mV(pdo, slot);
    if (pdo == 0) {
      continue;
    }
    if (pdo_augmented(pdo)) {
      if (slot < 8 && voltage_mV >= (pdo_has_vmin(pdo) ? PPS_MIN_MV : 0u) && voltage_mV <= mV) {
        pps = slot;
      }
      continue;
    }
    lowest_mV = lowest_mV == 0 || mV < lowest_mV ? mV : lowest_mV;
    if (mV <= voltage_mV && mV >= fixed_mV) {
      fixed = slot;
      fixed_mV = mV;
    }
  }
  const int slot = pps > 0 ? pps : fixed;
  ERR_CHECK(slot > 0, ERR_POWER_NO_PROFILE, .id = c->id, .voltage_mV = voltage_mV,
            .lowest_mV = lowest_mV != 0 ? lowest_mV : VSAFE_5V_MV);
  const unsigned imax = pdo_imax_code(c->pdo[slot - 1]);
  ERR_CHECK_RANGE(current_mA, 1, pdo_max_mA(imax));
  return send_request(c, pps > 0 ? voltage_mV / 100u : 0u, current_code(current_mA), (unsigned)slot);
}

static err_h request(ap_t* c, uint32_t voltage_mV, uint32_t current_mA) {
  ERR_TRY(negotiate(c, voltage_mV, current_mA));
  c->last_voltage_mV = voltage_mV;
  c->last_current_mA = current_mA;
  return NULL;
}

/* ---- POWER_VREG ---- */

OP_DECLARE_WRITE(CONTRACT_VREG, OP_ENABLE) {
  ERR_CHECK(args->enable <= 1, ERR_BAD_ARG);
  const uint8_t flag = args->enable ? OUTPUT_ON : OUTPUT_OFF;
  return sys_i2c_reg_write(&c->i2c, CMD_SYSTEM, &flag, 1);
}

OP_DECLARE_WRITE(CONTRACT_VREG, OP_VOLTAGE) {
  return request(c, args->voltage_mV, c->last_current_mA);
}

OP_DECLARE_WRITE(CONTRACT_VREG, OP_CURRENT) {
  return request(c, c->last_voltage_mV, args->current_mA);
}

/* ---- POWER_USB_PD ---- */

OP_DECLARE_WRITE(CONTRACT_PD, OP_SETTINGS) {
  return request(c, args->voltage_mV, args->current_mA);
}

// The last request the charger took (a refused one is not kept).
OP_DECLARE_READ(CONTRACT_PD, OP_SETTINGS) {
  return CMD_REPLY(CONTRACT_PD, OP_SETTINGS, .voltage_mV = c->last_voltage_mV, .current_mA = c->last_current_mA);
}

OP_DECLARE_READ(CONTRACT_PD, OP_OPTIONS) {
  ERR_TRY(read_pdos(c));
  sys_power_usb_pd_option_t options[PDO_COUNT];
  uint32_t count = 0;
  for (uint8_t slot = 1; slot <= PDO_COUNT; slot++) {
    count += pdo_option(c, slot, &options[count]) ? 1u : 0u;
  }
  return cmd_reply(out, options, count * (uint32_t)sizeof(options[0]));
}

// The highest-voltage offer.
OP_DECLARE_READ(CONTRACT_PD, OP_LIMITS) {
  ERR_TRY(read_pdos(c));
  sys_power_usb_pd_limits_t limits = {0};
  for (uint8_t slot = 1; slot <= PDO_COUNT; slot++) {
    sys_power_usb_pd_option_t o;
    if (pdo_option(c, slot, &o) && o.max_mV > limits.voltage_mV) {
      limits = (sys_power_usb_pd_limits_t){.voltage_mV = o.max_mV, .current_mA = o.max_mA};
    }
  }
  return CMD_REPLY(CONTRACT_PD, OP_LIMITS, .voltage_mV = limits.voltage_mV, .current_mA = limits.current_mA);
}

/* ---- POWER_MONITOR: the output, channel 0 ---- */

static err_h read_output(ap_t* c, uint8_t channel, int32_t* mV, int32_t* mA) {
  ERR_CHECK(channel == 0, ERR_POWER_BAD_CHANNEL, .id = c->id, .channel = channel);
  uint8_t v[2], i;
  ERR_TRY(sys_i2c_reg_read(&c->i2c, CMD_VOLTAGE, v, sizeof(v)));
  ERR_TRY(sys_i2c_reg_read(&c->i2c, CMD_CURRENT, &i, 1));
  *mV = ((v[1] << 8) | v[0]) * VOLTAGE_MV_PER_LSB;
  *mA = i * CURRENT_MA_PER_LSB;
  return NULL;
}

OP_DECLARE_READ(CONTRACT_PMON, OP_VOLTAGE) {
  int32_t mV, mA;
  ERR_TRY(read_output(c, args->channel, &mV, &mA));
  return CMD_REPLY(CONTRACT_PMON, OP_VOLTAGE, mV);
}

OP_DECLARE_READ(CONTRACT_PMON, OP_CURRENT) {
  int32_t mV, mA;
  ERR_TRY(read_output(c, args->channel, &mV, &mA));
  return CMD_REPLY(CONTRACT_PMON, OP_CURRENT, mA);
}

OP_DECLARE_READ(CONTRACT_PMON, OP_POWER) {
  int32_t mV, mA;
  ERR_TRY(read_output(c, args->channel, &mV, &mA));
  return CMD_REPLY(CONTRACT_PMON, OP_POWER, sys_power_mW(mV, mA));
}

/* ---- Lifecycle: the board's own supply is left alone ---- */

static err_h suspend(ap_t* c CMD_UNUSED) {
  return NULL;
}

static err_h resume(ap_t* c CMD_UNUSED) {
  return NULL;
}

static err_h create(uint8_t id, ap_t* c, const ap33772s_cfg_t* cfg) {
  ERR_CHECK_RANGE(cfg->i2c_addr, AP33772S_I2C_ADDR, AP33772S_I2C_ADDR);
  c->id = id;
  c->last_voltage_mV = DEFAULT_VOLTAGE_MV;
  c->last_current_mA = DEFAULT_CURRENT_MA;
  ERR_TRY(sys_i2c_dev_attach(id, &c->i2c, cfg->i2c_bus, cfg->i2c_addr, I2C_SPEED_HZ));
  ERR_TRY(sys_io_claim_input(id, &c->intr, cfg->intr_pin, 0, 0, SYS_IO_INTR_DISABLE));  // held, no events
  vTaskDelay(MSEC(PDO_WAIT_MS));
  return read_pdos(c);
}

static const sys_device_op_t k_ops[] = {
    OP_ENTRY_W(CONTRACT_VREG, OP_ENABLE),   OP_ENTRY_W(CONTRACT_VREG, OP_VOLTAGE),  OP_ENTRY_W(CONTRACT_VREG, OP_CURRENT),
    OP_ENTRY(CONTRACT_PD, OP_SETTINGS),     OP_ENTRY_R(CONTRACT_PD, OP_OPTIONS),    OP_ENTRY_R(CONTRACT_PD, OP_LIMITS),
    OP_ENTRY_R(CONTRACT_PMON, OP_VOLTAGE),  OP_ENTRY_R(CONTRACT_PMON, OP_CURRENT),  OP_ENTRY_R(CONTRACT_PMON, OP_POWER),
};

SYS_DEVICE_TYPE_DEFINE(AP33772S, ap33772s_cfg_t, create, suspend, resume, k_ops)
