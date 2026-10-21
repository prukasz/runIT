#include "device_tca6424a.h"

#include "sys_event.h"
#include "sys_i2c.h"
#include "sys_pins.h"
#include "utils.h"

#define I2C_SPEED_HZ   400000u
#define RESET_PULSE_MS 10u  // RST low, and the wait after it is released

#define REG_INPUT_PORT0    0x00u
#define REG_OUTPUT_PORT0   0x04u
#define REG_POLARITY_PORT0 0x08u
#define REG_CONFIG_PORT0   0x0Cu
#define REG_AUTO_INCREMENT 0x80u

#define PINS_MASK ((1UL << TCA6424A_PINS) - 1u)

typedef struct {
  sys_device_base_t base;  // first: id, suspended (chip held in reset, every pin an input)
  sys_pins_owned_t intr;  // INT: its falling edge runs our ON_EVENT
  sys_pins_owned_t rst;   // RST: low holds the chip in reset
  sys_pins_state_t pins;
  sys_i2c_dev_t i2c;

  // Registers as last written (the chip has no read-back of its outputs / config worth the bus time)
  uint8_t output[3];
  uint8_t config[3];  // 1 = input
  uint8_t polarity[3];

  uint32_t cached_inputs;                             // the input port at the last read: the base of edge detection
  uint8_t intr_mode[TCA6424A_PINS];                   // sys_io_intr_mode_e per pin
} tca_t;

/* ---- Chip access: the registry adds the context ---- */

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
   Outputs first, then the direction: a pin that becomes an output never shows the chip's default latch (1). */
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

/* Reads the inputs and publishes each armed pin's edge (value = the new level). The
   INT line is released by the read. While suspended the chip is in reset: its INT edge isn't an input change. */
EVENT_WRITE(ON_EVENT) {
  (void)args;
  tca_t* c = ctx;
  if (c->base.suspended) {
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
      ERR_KEEP_FIRST(err, level ? SYS_EVENT_PUBLISH(IO, RISING, c->base.id, pin, 1)
                                       : SYS_EVENT_PUBLISH(IO, FALLING, c->base.id, pin, 0));
    }
  }
  return err;
}

/* ---- IO contract ---- */

IO_WRITE(MODE) {
  tca_t* c = ctx;
  ERR_TRY(sys_pins_check_writable(&c->base, &c->pins, args->pin));
  ERR_CHECK(args->mode == SYS_IO_MODE_INPUT || args->mode == SYS_IO_MODE_OUTPUT_PUSH_PULL, ERR_IO_MODE_UNSUPPORTED,
            .id = c->base.id, .pin = args->pin, .mode = args->mode);
  return chip_set_config(c, 1UL << args->pin, args->mode == SYS_IO_MODE_INPUT ? (1UL << args->pin) : 0);
}

IO_WRITE(LEVEL) {
  tca_t* c = ctx;
  ERR_CHECK(args->level <= 1, ERR_CMD_BAD_ARG);
  ERR_TRY(sys_pins_check_writable(&c->base, &c->pins, args->pin));
  return chip_set_pins(c, 1UL << args->pin, args->level ? (1UL << args->pin) : 0);
}

IO_WRITE(TOGGLE) {
  tca_t* c = ctx;
  ERR_TRY(sys_pins_check_writable(&c->base, &c->pins, args->pin));
  return chip_set_pins(c, 1UL << args->pin, (chip_output(c) & (1UL << args->pin)) ? 0 : (1UL << args->pin));
}

// Reads the chip: refused while it is in reset.
IO_READ(LEVEL) {
  tca_t* c = ctx;
  ERR_TRY(sys_device_check_active(&c->base));
  ERR_TRY(sys_pins_state_check(&c->pins, c->base.id, args->pin));
  uint32_t levels;
  ERR_TRY(chip_get_pins(c, &levels));
  c->cached_inputs = levels;
  const uint8_t level = (levels & (1UL << args->pin)) != 0;
  return sys_io_reply_level(args->pin, level != 0, out, out_cap, out_len);
}

// The pin back to its power-on state: input, latch low, interrupt disarmed.
IO_WRITE(RESET) {
  tca_t* c = ctx;
  ERR_TRY(sys_pins_check_writable(&c->base, &c->pins, args->pin));
  c->intr_mode[args->pin] = SYS_IO_INTR_DISABLE;
  ERR_TRY(chip_set_pins(c, 1UL << args->pin, 0));
  return chip_set_config(c, 1UL << args->pin, 1UL << args->pin);
}

// Edges only: the INT line reports changes of the input port, there is no ADC and no filter (`debounce` is ignored).
IO_WRITE(INTR) {
  tca_t* c = ctx;
  ERR_CHECK(args->cfg.mode <= SYS_IO_INTR_ADC_WINDOW_INSIDE && args->cfg.debounce <= 1, ERR_CMD_BAD_ARG);
  ERR_TRY(sys_pins_check_writable(&c->base, &c->pins, args->pin));
  ERR_CHECK(args->cfg.mode <= SYS_IO_INTR_MODE_BOTH_EDGES, ERR_IO_FEATURE_UNSUPPORTED, .id = c->base.id, .pin = args->pin);
  c->intr_mode[args->pin] = args->cfg.mode;
  return NULL;
}

/* ---- Pins contract ---- */

SYS_PINS_STATE_HANDLERS(tca_t)

// The owner of a locked pin (a rail's EN, a bridge's sleep, PCA9685 OE) drives it here; the IO writes refuse it.
PINS_WRITE(LOCKED_LEVEL) {
  tca_t* c = ctx;
  ERR_CHECK(args->level <= 1, ERR_CMD_BAD_ARG);
  ERR_TRY(sys_device_check_active(&c->base));
  ERR_TRY(sys_pins_state_check(&c->pins, c->base.id, args->pin));
  return chip_set_pins(c, 1UL << args->pin, args->level ? (1UL << args->pin) : 0);
}

/* ---- Lifecycle ---- */

/* Unlocked pins back to power-on: inputs, latch low, interrupts disarmed. Locked pins are left to their owners: a
   pin another device drives (a rail's EN, an LM73100 enable, PCA9685 OE) keeps its level. The chip is not reset. */
DEVICE_WRITE(RESET) {
  tca_t* c = ctx;
  ERR_TRY(sys_device_check_active(&c->base));
  const uint32_t free_pins = ~(uint32_t)c->pins.locked & PINS_MASK;
  memset(c->intr_mode, SYS_IO_INTR_DISABLE, sizeof(c->intr_mode));
  ERR_TRY(chip_set_pins(c, free_pins, 0));
  ERR_TRY(chip_set_config(c, free_pins, free_pins));
  return chip_get_pins(c, &c->cached_inputs);
}

/* Fault safe state: RST low (the chip resets, every pin is an input) or, without RST, every pin an input. Users of
   the pins are suspended before this device (newest first). The device counts as suspended even if a step failed. */
static err_h suspend_hook(void* ctx) {
  tca_t* c = ctx;
  err_h err = NULL;
  if (c->rst.locked) {
    ERR_KEEP_FIRST(err, sys_pins_drive(&c->rst, false));
  } else {
    ERR_KEEP_FIRST(err, chip_release_all(c));
  }
  return err;
}

// RST released, the registers written back, the inputs read again. A failing resume leaves the device suspended.
static err_h resume_hook(void* ctx) {
  tca_t* c = ctx;
  if (c->rst.locked) {
    ERR_TRY(sys_pins_drive(&c->rst, true));
    vTaskDelay(MSEC(RESET_PULSE_MS));
  }
  ERR_TRY(chip_restore(c));
  ERR_TRY(chip_get_pins(c, &c->cached_inputs));
  return NULL;
}

/* ---- Create / uninstall ---- */

// Releases the hardware; REMOVE and the create rollback. Each step runs only if it was built, none returns early.
static err_h uninstall(void* ctx) {
  tca_t* c = ctx;
  err_h err = NULL;
  if (sys_i2c_dev_added(&c->i2c)) {
    if (!c->base.suspended) {
      ERR_KEEP_FIRST(err, chip_release_all(c));  // nothing stays driven
    }
    ERR_KEEP_FIRST(err, sys_i2c_dev_remove(&c->i2c));
  }
  ERR_KEEP_FIRST(err, sys_pins_release(&c->intr));
  ERR_KEEP_FIRST(err, sys_pins_release(&c->rst));
  return err;
}

static err_h create(void* ctx, const void* cfg_in) {
  tca_t* c = ctx;
  const tca6424a_cfg_t* cfg = cfg_in;
  const uint16_t id = c->base.id;
  SYS_DEV_CHECK_IN_RANGE(id, cfg->i2c_bus, 0, SYS_I2C_BUS_COUNT - 1);
  SYS_DEV_CHECK_IN_RANGE(id, cfg->i2c_addr, TCA6424A_I2C_ADDR_LOW, TCA6424A_I2C_ADDR_HIGH);
  sys_pins_state_init(&c->pins, TCA6424A_PINS);
  // Start state: every pin an input, latches low (a pin that becomes an output starts low, not at the chip's 1).
  memset(c->config, 0xFF, sizeof(c->config));
  memset(c->output, 0x00, sizeof(c->output));
  memset(c->polarity, 0x00, sizeof(c->polarity));
  sys_i2c_dev_init(&c->i2c, cfg->i2c_bus, cfg->i2c_addr, I2C_SPEED_HZ);

  /* RST released before the probe: after a safe state (suspend holds the chip in reset) an ESP restart finds RST
     still low, and a chip in reset doesn't answer the probe. */
  ERR_TRY(sys_pins_take_output(&c->base, &c->rst, cfg->rst_pin, true));
  if (c->rst.locked) {
    vTaskDelay(MSEC(RESET_PULSE_MS));
  }

  ERR_TRY(sys_i2c_dev_add(&c->i2c));  // probes the chip

  // The chip keeps its registers across an ESP reset when there is no RST pin: write the start state.
  ERR_TRY(chip_restore(c));

  // INT: its falling edge reaches our ON_EVENT (inline: an I2C read, no delay).
  ERR_TRY(sys_pins_take_input(&c->base, &c->intr, cfg->intr_pin, IO_EV_FALLING, SYS_IO_INTR_MODE_FALLING_EDGE));

  return chip_get_pins(c, &c->cached_inputs);
}

/* ---- Type ---- */

static const sys_device_op_t k_ops[] = {
    DEVICE_ENTRY_W(RESET),
    IO_ENTRY_W(MODE),       IO_ENTRY(LEVEL),     IO_ENTRY_W(TOGGLE),
      IO_ENTRY_W(RESET),         IO_ENTRY_W(INTR),
    SYS_PINS_STATE_ENTRIES,   PINS_ENTRY_W(LOCKED_LEVEL),
    EVENT_ENTRY_W(ON_EVENT),
};

SYS_DEVICE_TYPE_DEFINE(.type = SYS_DEVICE_TYPE_TCA6424A, .cfg_size = sizeof(tca6424a_cfg_t), .ctx_size = sizeof(tca_t),
                       .create = create, .uninstall = uninstall, .suspend = suspend_hook, .resume = resume_hook,
                       SYS_DEVICE_OPS(k_ops))
