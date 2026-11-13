/*
 * TCA6424A: 24-bit I2C I/O expander, type 0x0042. The whole device is this file: nothing includes it, the rest of
 * the firmware reaches it by its ID and the IO contract's ops.
 */
#include <string.h>

#include "sys_device_driver.h"
#include "sys_device_event.h"
#include "sys_event.h"
#include "sys_i2c.h"
#include "sys_io.h"
#include "sys_io_claim.h"
#include "sys_io_driver.h"
#include "utils.h"

SYS_DEVICE_TYPE_DECLARE(TCA6424A, 0x0042)

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

/* Own op: INT receives edges of `intr_pin` via sys_io_claim_input(). LOCAL (not a command), ALWAYS (runs while
 * suspended so the handler skips hardware access cleanly via sys_device_self_suspended()), TAIL (carries sys_event_t). */
#define TCA6424A_OPERATIONS(X, P) \
  X(P, INT, 0x00, cmd_none_t, sys_event_t, SYS_DEVICE_OP_LOCAL | SYS_DEVICE_OP_ALWAYS | SYS_DEVICE_OP_TAIL)

SYS_DEVICE_OWN_DECLARE(TCA6424A)

#define I2C_SPEED_HZ   400000u
#define RESET_PULSE_MS 10u  // RST low, and the wait after it is released

#define REG_INPUT_PORT0    0x00u
#define REG_OUTPUT_PORT0   0x04u
#define REG_POLARITY_PORT0 0x08u
#define REG_CONFIG_PORT0   0x0Cu
#define REG_AUTO_INCREMENT 0x80u

#define PINS_MASK ((1UL << TCA6424A_PINS) - 1u)

typedef struct {
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

static uint32_t chip_output(const tca_t* c) {
  return ((uint32_t)c->output[2] << 16) | ((uint32_t)c->output[1] << 8) | c->output[0];
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

/* Reads the inputs and publishes each armed pin's edge. The INT line is released by the read.
 * While suspended the chip is in reset: its INT edge isn't an input change. */
OP_WRITE_EVENT(TCA6424A, INT) {
  if (sys_device_self_suspended()) {
    return NULL;
  }
  uint32_t now;
  ERR_TRY(chip_get_pins(c, &now));
  const uint32_t changed = now ^ c->cached_inputs;
  c->cached_inputs = now;
  const uint8_t self = sys_device_self();
  err_h err = NULL;
  for (uint8_t pin = 0; pin < TCA6424A_PINS; pin++) {
    if (!(changed & (1UL << pin))) {
      continue;
    }
    const bool level = (now & (1UL << pin)) != 0;
    const uint8_t mode = c->intr_mode[pin];
    if (mode == SYS_IO_INTR_MODE_BOTH_EDGES || (mode == SYS_IO_INTR_MODE_RISING_EDGE && level) ||
        (mode == SYS_IO_INTR_MODE_FALLING_EDGE && !level)) {
      ERR_KEEP_FIRST(err, level ? SYS_EVENT_PUBLISH(IO_EV_RISING, self, pin)
                                : SYS_EVENT_PUBLISH(IO_EV_FALLING, self, pin));
    }
  }
  return err;
}

/* ---- IO contract ---- */

OP_WRITE(IO, MODE) {
  const uint8_t self = sys_device_self();
  ERR_TRY(sys_io_pins_check_free(&c->pins, self, args->pin));
  ERR_CHECK(args->mode == SYS_IO_MODE_INPUT || args->mode == SYS_IO_MODE_OUTPUT_PUSH_PULL, ERR_IO_MODE_UNSUPPORTED,
            .id = self, .pin = args->pin, .mode = args->mode);
  return chip_set_config(c, 1UL << args->pin, args->mode == SYS_IO_MODE_INPUT ? (1UL << args->pin) : 0);
}

OP_WRITE(IO, LEVEL) {
  ERR_CHECK(args->level <= 1, ERR_CMD_BAD_ARG);
  ERR_TRY(sys_io_pins_check_free(&c->pins, sys_device_self(), args->pin));
  return chip_set_pins(c, 1UL << args->pin, args->level ? (1UL << args->pin) : 0);
}

OP_WRITE(IO, TOGGLE) {
  ERR_TRY(sys_io_pins_check_free(&c->pins, sys_device_self(), args->pin));
  return chip_set_pins(c, 1UL << args->pin, (chip_output(c) & (1UL << args->pin)) ? 0 : (1UL << args->pin));
}

// Reads the chip; while suspended (the chip may be held in reset) the levels of the last read.
OP_READ(IO, LEVEL) {
  ERR_TRY(sys_io_pins_check(&c->pins, sys_device_self(), args->pin));
  uint32_t levels = c->cached_inputs;
  if (!sys_device_self_suspended()) {
    ERR_TRY(chip_get_pins(c, &levels));
    c->cached_inputs = levels;
  }
  return CMD_REPLY(IO, LEVEL, .pin = args->pin, .level = (levels & (1UL << args->pin)) != 0);
}

// The pin back to its power-on state: input, latch low, interrupt disarmed.
OP_WRITE(IO, RESET) {
  ERR_TRY(sys_io_pins_check_free(&c->pins, sys_device_self(), args->pin));
  c->intr_mode[args->pin] = SYS_IO_INTR_DISABLE;
  ERR_TRY(chip_set_pins(c, 1UL << args->pin, 0));
  return chip_set_config(c, 1UL << args->pin, 1UL << args->pin);
}

// Edges only: the INT line reports changes of the input port, there is no ADC and no filter (`debounce` is ignored).
OP_WRITE(IO, INTR) {
  ERR_CHECK(args->cfg.mode <= SYS_IO_INTR_ADC_WINDOW_INSIDE && args->cfg.debounce <= 1, ERR_CMD_BAD_ARG);
  const uint8_t self = sys_device_self();
  ERR_TRY(sys_io_pins_check_free(&c->pins, self, args->pin));
  ERR_CHECK(args->cfg.mode <= SYS_IO_INTR_MODE_BOTH_EDGES, ERR_IO_FEATURE_UNSUPPORTED, .id = self, .pin = args->pin);
  c->intr_mode[args->pin] = args->cfg.mode;
  return NULL;
}

SYS_IO_PINS_HANDLERS

/* ---- Lifecycle ---- */

/* Unlocked pins back to power-on: inputs, latch low, interrupts disarmed. Locked pins are left to their owners: a
 * pin another device drives (a rail's EN, a bridge's sleep, PCA9685 OE) keeps its level. The chip is not reset. */
OP_WRITE(SYS_DEVICE, RESET) {
  const uint32_t free_pins = ~(uint32_t)c->pins.locked & PINS_MASK;
  memset(c->intr_mode, SYS_IO_INTR_DISABLE, sizeof(c->intr_mode));
  ERR_TRY(chip_set_pins(c, free_pins, 0));
  ERR_TRY(chip_set_config(c, free_pins, free_pins));
  return chip_get_pins(c, &c->cached_inputs);
}

/* Fault safe state: RST low (the chip resets, every pin is an input) or, without RST, every pin an input. Users of
 * the pins are suspended before this device (newest first). */
static err_h suspend(tca_t* c) {
  err_h err = NULL;
  if (c->rst.locked) {
    ERR_KEEP_FIRST(err, sys_io_claim_drive(&c->rst, false));
  } else {
    ERR_KEEP_FIRST(err, chip_release_all(c));
  }
  return err;
}

// RST released, the registers written back, the inputs read again. A failing resume leaves the device suspended.
static err_h resume(tca_t* c) {
  if (c->rst.locked) {
    ERR_TRY(sys_io_claim_drive(&c->rst, true));
    vTaskDelay(MSEC(RESET_PULSE_MS));
  }
  ERR_TRY(chip_restore(c));
  return chip_get_pins(c, &c->cached_inputs);
}

static err_h create(uint8_t id, tca_t* c, const tca6424a_cfg_t* cfg) {
  SYS_DEV_CHECK_IN_RANGE(id, cfg->i2c_addr, TCA6424A_I2C_ADDR_LOW, TCA6424A_I2C_ADDR_HIGH);
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
  ERR_TRY(sys_io_claim_input(id, &c->intr, cfg->intr_pin, IO_EV_FALLING, TCA6424A_OP_INT,
                             SYS_IO_INTR_MODE_FALLING_EDGE));
  return chip_get_pins(c, &c->cached_inputs);
}

/* ---- Type ---- */

static const sys_device_op_t k_ops[] = {
    OP_ENTRY_W(SYS_DEVICE, RESET),
    OP_ENTRY_W(IO, MODE),  OP_ENTRY(IO, LEVEL), OP_ENTRY_W(IO, TOGGLE),
    OP_ENTRY_W(IO, RESET), OP_ENTRY_W(IO, INTR),
    OP_ENTRY_W(TCA6424A, INT),
    SYS_IO_PINS_ENTRIES,
};

SYS_DEVICE_TYPE_DEFINE(TCA6424A, tca6424a_cfg_t, create, suspend, resume, k_ops)
