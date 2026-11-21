/*
 * TCA6424A: 24-bit I2C I/O expander, type 0x0042. The whole device is this file: nothing includes it, the rest of
 * the firmware reaches it by its ID and the IO contract's ops.
 */
#include <string.h>

#include "sys_device_driver.h"
#include "sys_device_event.h"
#include "sys_event.h"
#include "sys_i2c.h"
#include "sys_io_driver.h"
#include "utils.h"

SYS_DEVICE_TYPE_DECLARE(TCA6424A, 0x0042)

/*******************************************************************************
 * OWN_TCA6424A : own operations (0xFFxx)
 * OP_INT receives edges of `intr_pin` via sys_io_claim_input()
 * (LOCAL | ALWAYS | TAIL with sys_event_t).
 ******************************************************************************/
#define OWN_TCA6424A_OPERATIONS(X, P) \
  X(P, OP_INT, 0x00, cmd_none_t, sys_event_t, SYS_DEVICE_OP_LOCAL | SYS_DEVICE_OP_ALWAYS | SYS_DEVICE_OP_TAIL)

SYS_DEVICE_OWN_DECLARE(OWN_TCA6424A)

/*******************************************************************************
 * TCA6424A : config & registers
 ******************************************************************************/

#define TCA6424A_PINS          24u
#define TCA6424A_I2C_ADDR_LOW  0x22u  // ADDR low
#define TCA6424A_I2C_ADDR_HIGH 0x23u  // ADDR high

/** @brief CREATE's struct (wire format). If a pin is not wired, set it to SYS_IO_PIN_NONE. */
typedef struct __attribute__((packed)) {
  uint8_t i2c_bus;        /**< The bus's device ID (an I2C contract device) */
  uint8_t i2c_addr;       /**< 7-bit I2C address (0x22 or 0x23) */
  sys_io_pin_t intr_pin;  /**< Optional active-low open-drain interrupt pin */
  sys_io_pin_t rst_pin;   /**< Optional active-low reset pin */
} tca6424a_cfg_t;

#define I2C_SPEED_HZ   400000u
#define RESET_PULSE_MS 10u  // RST low, and the wait after it is released

#define REG_INPUT_PORT0    0x00u
#define REG_OUTPUT_PORT0   0x04u
#define REG_POLARITY_PORT0 0x08u
#define REG_CONFIG_PORT0   0x0Cu
#define REG_AUTO_INCREMENT 0x80u

#define PINS_MASK ((1UL << TCA6424A_PINS) - 1u)

typedef struct {
  uint8_t id;
  sys_io_pins_t pins;
  sys_io_claim_t intr;  // INT: its falling edge runs our INT op
  sys_io_claim_t rst;   // RST: low holds the chip in reset
  sys_i2c_dev_t i2c;

  // Registers as last written (the chip has no read-back of its outputs / config worth the bus time)
  uint8_t output[3];
  uint8_t config[3];  // 1 = input
  uint8_t polarity[3];

  uint32_t cached_inputs;             // the input port at the last read: the base of edge detection
  uint8_t intr_mode[TCA6424A_PINS];   // sys_io_intr_mode_e per pin
} tca_t;

#define SYS_DEVICE_CTX tca_t

/* ---- Chip access ---- */

static err_h chip_write_regs(tca_t* c, uint8_t reg0, const uint8_t regs[3]) {
  return sys_i2c_reg_write(&c->i2c, reg0 | REG_AUTO_INCREMENT, regs, 3);
}

// Applies `state` to the bits selected by `mask` of a 24-bit value kept as three port bytes.
static void port_update(uint8_t port[3], uint32_t mask, uint32_t state) {
  for (uint8_t i = 0; i < 3; i++) {
    const uint8_t m = (mask >> (8 * i)) & 0xFFu;
    const uint8_t s = (state >> (8 * i)) & 0xFFu;
    port[i] = (port[i] & ~m) | (s & m);
  }
}

static err_h chip_set_pins(tca_t* c, uint32_t mask, uint32_t state) {
  port_update(c->output, mask, state);
  return chip_write_regs(c, REG_OUTPUT_PORT0, c->output);
}

static err_h chip_set_config(tca_t* c, uint32_t mask, uint32_t state) {
  port_update(c->config, mask, state);
  return chip_write_regs(c, REG_CONFIG_PORT0, c->config);
}

static err_h chip_get_pins(tca_t* c, uint32_t* levels) {
  uint8_t in[3];
  ERR_TRY(sys_i2c_reg_read(&c->i2c, REG_INPUT_PORT0 | REG_AUTO_INCREMENT, in, sizeof(in)));
  *levels = ((uint32_t)in[2] << 16) | ((uint32_t)in[1] << 8) | in[0];
  return NULL;
}

/* Writes the cached registers back: at create and after a resume (the chip is at its defaults after a reset).
 * Outputs first, then the direction: a pin that becomes an output never shows the chip's default latch (1). */
static err_h chip_restore(tca_t* c) {
  ERR_TRY(chip_write_regs(c, REG_OUTPUT_PORT0, c->output));
  ERR_TRY(chip_write_regs(c, REG_POLARITY_PORT0, c->polarity));
  return chip_write_regs(c, REG_CONFIG_PORT0, c->config);
}

// Every pin an input, as after a reset. The cache keeps the wanted direction (RESUME writes it back).
static err_h chip_release_all(tca_t* c) {
  const uint8_t inputs[3] = {0xFF, 0xFF, 0xFF};
  return chip_write_regs(c, REG_CONFIG_PORT0, inputs);
}

/* ---- Event: the INT pin fell ---- */

/*******************************************************************************
 * OWN_TCA6424A : OP_INT (event, LOCAL | ALWAYS | TAIL)
 * Reads the inputs (releasing INT) and publishes each armed pin's edge; ignored
 * while suspended because the chip is in reset.
 ******************************************************************************/
OP_DECLARE_EVENT(OWN_TCA6424A, OP_INT) {
  if (sys_device_self_suspended()) {
    return NULL;
  }
  uint32_t now;
  ERR_TRY(chip_get_pins(c, &now));
  const uint32_t changed = now ^ c->cached_inputs;
  c->cached_inputs = now;
  err_h err = NULL;
  for (uint8_t pin = 0; pin < TCA6424A_PINS; pin++) {
    if (!(changed & (1UL << pin))) {
      continue;
    }
    const bool level = (now & (1UL << pin)) != 0;
    const uint8_t mode = c->intr_mode[pin];
    if (mode == SYS_IO_INTR_MODE_BOTH_EDGES || (mode == SYS_IO_INTR_MODE_RISING_EDGE && level) ||
        (mode == SYS_IO_INTR_MODE_FALLING_EDGE && !level)) {
      ERR_KEEP_FIRST(err, level ? SYS_EVENT_PUBLISH(IO_EV_RISING, c->id, pin)
                                : SYS_EVENT_PUBLISH(IO_EV_FALLING, c->id, pin));
    }
  }
  return err;
}

/* ---- IO contract ---- */

/*******************************************************************************
 * CONTRACT_IO : OP_MODE (write)
 * Configures pin direction (input or push-pull output).
 ******************************************************************************/
OP_DECLARE_WRITE(CONTRACT_IO, OP_MODE) {
  ERR_TRY(sys_io_pins_check_free(&c->pins, c->id, args->pin));
  ERR_CHECK(args->mode == SYS_IO_MODE_INPUT || args->mode == SYS_IO_MODE_OUTPUT_PUSH_PULL, ERR_IO_MODE_UNSUPPORTED,
            .id = c->id, .pin = args->pin, .mode = args->mode);
  return chip_set_config(c, 1UL << args->pin, args->mode == SYS_IO_MODE_INPUT ? (1UL << args->pin) : 0);
}

/*******************************************************************************
 * CONTRACT_IO : OP_LEVEL (write)
 * Drives the pin's output latch.
 ******************************************************************************/
OP_DECLARE_WRITE(CONTRACT_IO, OP_LEVEL) {
  ERR_CHECK(args->level <= 1, ERR_BAD_ARG);
  ERR_TRY(sys_io_pins_check_free(&c->pins, c->id, args->pin));
  return chip_set_pins(c, 1UL << args->pin, args->level ? (1UL << args->pin) : 0);
}

/*******************************************************************************
 * CONTRACT_IO : OP_LEVEL (read)
 * Samples the chip's pin level (or returns cached inputs while suspended).
 ******************************************************************************/
OP_DECLARE_READ(CONTRACT_IO, OP_LEVEL) {
  ERR_TRY(sys_io_pins_check(&c->pins, c->id, args->pin));
  uint32_t levels = c->cached_inputs;
  if (!sys_device_self_suspended()) {
    ERR_TRY(chip_get_pins(c, &levels));
    c->cached_inputs = levels;
  }
  return CMD_REPLY(CONTRACT_IO, OP_LEVEL, .pin = args->pin, .level = (levels & (1UL << args->pin)) != 0);
}

/*******************************************************************************
 * CONTRACT_IO : OP_TOGGLE (write)
 * Flips the pin's output latch.
 ******************************************************************************/
OP_DECLARE_WRITE(CONTRACT_IO, OP_TOGGLE) {
  ERR_TRY(sys_io_pins_check_free(&c->pins, c->id, args->pin));
  return chip_set_pins(c, 1UL << args->pin,
                       (c->output[args->pin >> 3] & (1u << (args->pin & 7u))) ? 0 : (1UL << args->pin));
}

/*******************************************************************************
 * CONTRACT_IO : OP_RESET (write)
 * Restores a single pin to power-on state: input, latch low, interrupt off.
 ******************************************************************************/
OP_DECLARE_WRITE(CONTRACT_IO, OP_RESET) {
  ERR_TRY(sys_io_pins_check_free(&c->pins, c->id, args->pin));
  c->intr_mode[args->pin] = SYS_IO_INTR_DISABLE;
  ERR_TRY(chip_set_pins(c, 1UL << args->pin, 0));
  return chip_set_config(c, 1UL << args->pin, 1UL << args->pin);
}

/*******************************************************************************
 * CONTRACT_IO : OP_INTR (write)
 * Digital edges only via INT; ADC modes unsupported and `debounce` is ignored.
 ******************************************************************************/
OP_DECLARE_WRITE(CONTRACT_IO, OP_INTR) {
  ERR_CHECK(args->cfg.mode <= SYS_IO_INTR_ADC_WINDOW_INSIDE && args->cfg.debounce <= 1, ERR_BAD_ARG);
  ERR_TRY(sys_io_pins_check_free(&c->pins, c->id, args->pin));
  ERR_CHECK(args->cfg.mode <= SYS_IO_INTR_MODE_BOTH_EDGES, ERR_IO_FEATURE_UNSUPPORTED, .id = c->id, .pin = args->pin);
  c->intr_mode[args->pin] = args->cfg.mode;
  return NULL;
}

/*******************************************************************************
 * CONTRACT_IO : OP_AVAILABLE / OP_LOCKED / OP_LOCK / OP_UNLOCK
 * Shared pin availability and ownership handlers (SYS_IO_PINS_HANDLERS).
 ******************************************************************************/
SYS_IO_PINS_HANDLERS

/* ---- Lifecycle ---- */

/*******************************************************************************
 * CONTRACT_SYS_DEVICE : OP_RESET (write)
 * Resets unlocked pins to input/low/no-intr while preserving owner-locked pins.
 ******************************************************************************/
OP_DECLARE_WRITE(CONTRACT_SYS_DEVICE, OP_RESET) {
  const uint32_t free_pins = ~(uint32_t)c->pins.locked & PINS_MASK;
  memset(c->intr_mode, SYS_IO_INTR_DISABLE, sizeof(c->intr_mode));
  ERR_TRY(chip_set_pins(c, free_pins, 0));
  ERR_TRY(chip_set_config(c, free_pins, free_pins));
  return chip_get_pins(c, &c->cached_inputs);
}

/*******************************************************************************
 * TCA6424A : suspend (lifecycle)
 * Holds chip in reset (RST low) or configures every pin as input when no RST
 * pin is wired; dependent devices are suspended first.
 ******************************************************************************/
static err_h suspend(tca_t* c) {
  err_h err = NULL;
  if (c->rst.locked) {
    ERR_KEEP_FIRST(err, sys_io_claim_drive(&c->rst, false));
  } else {
    ERR_KEEP_FIRST(err, chip_release_all(c));
  }
  return err;
}

/*******************************************************************************
 * TCA6424A : resume (lifecycle)
 * Releases RST, restores register state and refreshes cached input levels; a
 * failing resume leaves the device suspended.
 ******************************************************************************/
static err_h resume(tca_t* c) {
  if (c->rst.locked) {
    ERR_TRY(sys_io_claim_drive(&c->rst, true));
    vTaskDelay(MSEC(RESET_PULSE_MS));
  }
  ERR_TRY(chip_restore(c));
  return chip_get_pins(c, &c->cached_inputs);
}

/*******************************************************************************
 * TCA6424A : create (init)
 * Initializes pin state (all inputs, latches low), releases RST before I2C
 * attach/restore, arms INT falling edge and caches initial inputs.
 ******************************************************************************/
static err_h create(uint8_t id, tca_t* c, const tca6424a_cfg_t* cfg) {
  ERR_CHECK_RANGE(cfg->i2c_addr, TCA6424A_I2C_ADDR_LOW, TCA6424A_I2C_ADDR_HIGH);
  c->id = id;
  sys_io_pins_init(&c->pins, TCA6424A_PINS);
  // Start state: every pin an input, latches low (a pin that becomes an output starts low, not at the chip's 1).
  memset(c->config, 0xFF, sizeof(c->config));
  memset(c->output, 0x00, sizeof(c->output));
  memset(c->polarity, 0x00, sizeof(c->polarity));

  /* RST released before probing: after a safe state (suspend holds the chip in reset) an ESP restart finds RST still
   * low, and a chip in reset doesn't answer the probe. */
  ERR_TRY(sys_io_claim_output(id, &c->rst, cfg->rst_pin, true));
  if (c->rst.locked) {
    vTaskDelay(MSEC(RESET_PULSE_MS));
  }
  ERR_TRY(sys_i2c_dev_attach(id, &c->i2c, cfg->i2c_bus, cfg->i2c_addr, I2C_SPEED_HZ));
  // The chip keeps its registers across an ESP reset when there is no RST pin: write the start state.
  ERR_TRY(chip_restore(c));
  // INT: its falling edge reaches our INT op (inline: an I2C read, no delay).
  ERR_TRY(sys_io_claim_input(id, &c->intr, cfg->intr_pin, IO_EV_FALLING, OWN_TCA6424A_OP_INT,
                             SYS_IO_INTR_MODE_FALLING_EDGE));
  return chip_get_pins(c, &c->cached_inputs);
}

/*******************************************************************************
 * TCA6424A : type registration (k_ops & SYS_DEVICE_TYPE_DEFINE)
 * Exposes lifecycle reset, IO contract + shared pin handlers, and own
 * OP_INT edge handler.
 ******************************************************************************/

static const sys_device_op_t k_ops[] = {
    OP_ENTRY_W(CONTRACT_SYS_DEVICE, OP_RESET),
    OP_ENTRY_W(CONTRACT_IO, OP_MODE),  OP_ENTRY(CONTRACT_IO, OP_LEVEL), OP_ENTRY_W(CONTRACT_IO, OP_TOGGLE),
    OP_ENTRY_W(CONTRACT_IO, OP_RESET), OP_ENTRY_W(CONTRACT_IO, OP_INTR),
    OP_ENTRY_W(OWN_TCA6424A, OP_INT),
    SYS_IO_PINS_ENTRIES,
};

SYS_DEVICE_TYPE_DEFINE(TCA6424A, tca6424a_cfg_t, create, suspend, resume, k_ops)
