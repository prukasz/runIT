#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "device_tca6424a.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_device.h"
#include "sys_error.h"
#include "sys_i2c.h"
#include "sys_io.h"

#define OWNER OWNER_DEVICE

#define PINS_COUNT 24
#define PINS_MASK ((1UL << PINS_COUNT) - 1)
#define TCA6424A_I2C_FREQUENCY 400000

#define REG_INPUT_PORT0 0x00
#define REG_OUTPUT_PORT0 0x04
#define REG_POLARITY_PORT0 0x08
#define REG_CONFIG_PORT0 0x0C
#define REG_AUTO_INCREMENT 0x80

// Instance state: the whole device in one struct (chip registers, pins, bus; the create cfg is not kept).
typedef struct tca_ctx_t {
  sys_device_base_t base;  // must be first
  sys_i2c_dev_t i2c;
  sys_io_pin_ref_t intr_pin;  // the pins of the create cfg, converted once at install
  sys_io_pin_ref_t rst_pin;

  // Chip registers as last written (the chip has no read-back of its outputs/config worth the bus time)
  uint8_t output[3];
  uint8_t config[3];  // 1 = input
  uint8_t polarity[3];

  uint32_t cached_inputs;
  sys_io_intr_mode_e intr_modes[PINS_COUNT];
  uint8_t intr_sub;  // sys_event subscription on intr_pin
} tca_ctx_t;

// Install steps, recorded so teardown rolls back only what was actually built
enum { TCA_STEP_I2C_ADDED = 0, TCA_STEP_RST_READY = 1, TCA_STEP_INTR_READY = 2, TCA_STEP_INTR_SUB = 3 };

/* ---- Chip access: err_h, the dispatcher above adds the device id ---- */

static SE_MUST_USE err_h chip_write_regs(tca_ctx_t* c, uint8_t reg0, const uint8_t regs[3]) {
  return sys_i2c_reg_write(&c->i2c, reg0 | REG_AUTO_INCREMENT, regs, 3);
}

/* Applies `state` to the bits selected by `mask` of a 24-bit value kept as three port bytes. */
static void port_update(uint8_t port[3], uint32_t mask, uint32_t state) {
  for (uint8_t i = 0; i < 3; i++) {
    uint8_t m = (mask >> (8 * i)) & 0xFF;
    uint8_t s = (state >> (8 * i)) & 0xFF;
    port[i] = (port[i] & ~m) | (s & m);
  }
}

static SE_MUST_USE err_h chip_set_pins(tca_ctx_t* c, uint32_t mask, uint32_t state) {
  port_update(c->output, mask, state);
  return chip_write_regs(c, REG_OUTPUT_PORT0, c->output);
}

static SE_MUST_USE err_h chip_set_config(tca_ctx_t* c, uint32_t mask, uint32_t state) {
  port_update(c->config, mask, state);
  return chip_write_regs(c, REG_CONFIG_PORT0, c->config);
}

static SE_MUST_USE err_h chip_get_pins(tca_ctx_t* c, uint32_t* levels) {
  uint8_t in[3];
  SE_TRY(sys_i2c_reg_read(&c->i2c, REG_INPUT_PORT0 | REG_AUTO_INCREMENT, in, sizeof(in)));
  *levels = ((uint32_t)in[2] << 16) | ((uint32_t)in[1] << 8) | in[0];
  return NULL;
}

static uint32_t chip_output(const tca_ctx_t* c) {
  return ((uint32_t)c->output[2] << 16) | ((uint32_t)c->output[1] << 8) | c->output[0];
}

/* Writes the cached registers back: after a reset, a resume, or at install (the chip keeps its registers across an ESP reset). */
static SE_MUST_USE err_h chip_restore(tca_ctx_t* c) {
  SE_TRY(chip_write_regs(c, REG_CONFIG_PORT0, c->config));
  SE_TRY(chip_write_regs(c, REG_OUTPUT_PORT0, c->output));
  return chip_write_regs(c, REG_POLARITY_PORT0, c->polarity);
}

/* Reads the inputs and publishes each armed pin's edge. */
static SE_MUST_USE err_h handle_inputs(const sys_event_t* event, tca_ctx_t* c) {
  err_h err = NULL;

  uint32_t current_state = 0;
  SE_TRY(chip_get_pins(c, &current_state));

  uint32_t changed_bits = current_state ^ c->cached_inputs;
  uint32_t rising_edges = changed_bits & current_state;
  uint32_t falling_edges = changed_bits & ~current_state;

  for (uint8_t i = 0; i < PINS_COUNT; i++) {
    if (!(changed_bits & (1UL << i))) continue;

    sys_io_intr_mode_e mode = c->intr_modes[i];
    if (mode == SYS_IO_INTR_DISABLE) continue;

    bool trigger = false;
    if (mode == SYS_IO_INTR_MODE_RISING_EDGE && (rising_edges & (1UL << i))) {
      trigger = true;
    } else if (mode == SYS_IO_INTR_MODE_FALLING_EDGE && (falling_edges & (1UL << i))) {
      trigger = true;
    } else if (mode == SYS_IO_INTR_MODE_BOTH_EDGES) {
      trigger = true;
    }

    if (trigger) {
      bool level = (rising_edges & (1UL << i)) != 0;
      sys_io_intr_mode_e edge = level ? SYS_IO_INTR_MODE_RISING_EDGE : SYS_IO_INTR_MODE_FALLING_EDGE;
      SYS_DEV_TEARDOWN_STEP(err, sys_io_publish(SYS_DEV_GET_ID(c), i, edge, level, SYS_EVENT_CAUSED_BY(event)));
    }
  }

  c->cached_inputs = current_state;
  return err;
}

/* Inline listener of intr_pin. No dispatcher wraps a listener's error, so it carries the device id here. */
static SE_MUST_USE err_h device_event_handler(const sys_event_t* event, void* handle) {
  SYS_DEV_CTX_FROM(tca_ctx_t, c, handle);
  err_h err = handle_inputs(event, c);
  return SYS_DEV_WRAP(err, SYS_DEV_GET_ID(c));
}

/* ---- IO contract ---- */

static SE_MUST_USE err_h io_set_mode(void* handle, sys_io_pin_num_t pin, sys_io_mode_e mode) {
  SYS_DEV_CTX_FROM(tca_ctx_t, c, handle);
  VERIFY_PIN(SYS_DEV_GET_ID(c), pin, PINS_MASK);

  uint32_t config_state;
  switch (mode) {
    case SYS_IO_MODE_OUTPUT_PUSH_PULL:
      config_state = 0x00000000;
      break;
    case SYS_IO_MODE_INPUT:
      config_state = 0xFFFFFFFF;
      break;
    default:
      SE_FAIL(ERR_IO_PIN_MODE_UNSUPPORTED, SYS_DEV_GET_ID(c), pin, mode);
  }
  return chip_set_config(c, 1UL << pin, config_state);
}

static SE_MUST_USE err_h io_set_level(void* handle, sys_io_pin_num_t pin, bool level) {
  SYS_DEV_CTX_FROM(tca_ctx_t, c, handle);
  VERIFY_PIN(SYS_DEV_GET_ID(c), pin, PINS_MASK);
  return chip_set_pins(c, 1UL << pin, level ? (1UL << pin) : 0);
}

static SE_MUST_USE err_h io_get_level(void* handle, sys_io_pin_num_t pin, bool* level) {
  SYS_DEV_CTX_FROM(tca_ctx_t, c, handle);
  SE_CHECK_HANDLE(level);
  VERIFY_PIN(SYS_DEV_GET_ID(c), pin, PINS_MASK);

  uint32_t levels = 0;
  SE_TRY(chip_get_pins(c, &levels));
  c->cached_inputs = levels;
  *level = (levels & (1UL << pin)) != 0;
  return NULL;
}

static SE_MUST_USE err_h io_toggle(void* handle, sys_io_pin_num_t pin) {
  SYS_DEV_CTX_FROM(tca_ctx_t, c, handle);
  VERIFY_PIN(SYS_DEV_GET_ID(c), pin, PINS_MASK);
  return io_set_level(handle, pin, (chip_output(c) & (1UL << pin)) == 0);
}

static SE_MUST_USE err_h io_reset_pin(void* handle, sys_io_pin_num_t pin) {
  SYS_DEV_CTX_FROM(tca_ctx_t, c, handle);
  VERIFY_PIN(SYS_DEV_GET_ID(c), pin, PINS_MASK);

  c->intr_modes[pin] = SYS_IO_INTR_DISABLE;
  SE_TRY(chip_set_pins(c, 1UL << pin, 0));
  return chip_set_config(c, 1UL << pin, 1UL << pin);
}

static SE_MUST_USE err_h io_configure_intr(void* handle, sys_io_pin_num_t pin, const sys_io_intr_config_t* config) {
  SYS_DEV_CTX_FROM(tca_ctx_t, c, handle);
  VERIFY_PIN(SYS_DEV_GET_ID(c), pin, PINS_MASK);
  SE_CHECK_NOT_NULL(config);
  c->intr_modes[pin] = config->mode;
  return NULL;
}

static const sys_io_contract_t s_tca6424a_io_contract = {.reset = io_reset_pin,
    .set_mode = io_set_mode,
    .configure_intr = io_configure_intr,
    .set_level = io_set_level,
    .get_level = io_get_level,
    .toggle = io_toggle};

/* ---- Lifecycle ---- */

// Doubles as the install rollback path: each step is gated on having actually
// run, and no step may early-return - teardown must always free everything.
static SE_MUST_USE err_h device_uninstall(void* handle) {
  SYS_DEV_CTX_FROM(tca_ctx_t, c, handle);
  err_h err = NULL;

  IF_SYS_DEV_STEP_DONE(c, TCA_STEP_RST_READY) {
    SYS_DEV_TEARDOWN_STEP(err, sys_io_unlock_pin(c->rst_pin));
    SYS_DEV_TEARDOWN_STEP(err, sys_io_reset(c->rst_pin));
  }
  IF_SYS_DEV_STEP_DONE(c, TCA_STEP_INTR_SUB) {
    SYS_DEV_TEARDOWN_STEP(err, sys_event_unsubscribe(c->intr_sub, false));
  }
  IF_SYS_DEV_STEP_DONE(c, TCA_STEP_INTR_READY) {
    SYS_DEV_TEARDOWN_STEP(err, sys_io_unlock_pin(c->intr_pin));
    SYS_DEV_TEARDOWN_STEP(err, sys_io_reset(c->intr_pin));
  }
  IF_SYS_DEV_STEP_DONE(c, TCA_STEP_I2C_ADDED) {
    SYS_DEV_TEARDOWN_STEP(err, sys_i2c_dev_remove(&c->i2c));
  }

  free(c);
  return err;
}

static SE_MUST_USE err_h device_reset(void* handle) {
  SYS_DEV_CTX_FROM(tca_ctx_t, c, handle);

  if (sys_io_pin_is_valid(c->rst_pin)) {
    SYS_DEV_TRY(sys_io_set_locked_level(c->rst_pin, false), c);
    vTaskDelay(pdMS_TO_TICKS(10));
    SYS_DEV_TRY(sys_io_set_locked_level(c->rst_pin, true), c);
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  c->cached_inputs = 0;
  for (uint8_t i = 0; i < PINS_COUNT; i++) {
    SE_TRY(io_reset_pin(handle, i));
  }
  return NULL;
}

static SE_MUST_USE err_h device_suspend(void* handle) {
  SYS_DEV_CTX_FROM(tca_ctx_t, c, handle);
  if (sys_io_pin_is_valid(c->rst_pin)) {
    SYS_DEV_TRY(sys_io_set_locked_level(c->rst_pin, false), c);
  }
  return NULL;
}

static SE_MUST_USE err_h device_resume(void* handle) {
  SYS_DEV_CTX_FROM(tca_ctx_t, c, handle);
  if (sys_io_pin_is_valid(c->rst_pin)) {
    SYS_DEV_TRY(sys_io_set_locked_level(c->rst_pin, true), c);
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  return chip_restore(c);
}

static SE_MUST_USE err_h device_install(const void* cfg_blob, void** out_device_handle) {
  const d_tca6424a_cfg_t* cfg = (const d_tca6424a_cfg_t*)cfg_blob;

  // The cfg is read only here: what the device keeps (the pins) goes into its own state.
  tca_ctx_t* c = (tca_ctx_t*)calloc(1, sizeof(tca_ctx_t));
  SE_CHECK_IF_ALLOCATED(c);
  c->base.device_id = cfg->device_id;
  err_h err = NULL;

  c->intr_pin = cfg->intr_pin;
  c->rst_pin = cfg->rst_pin;
  // Chip power-on defaults: all pins inputs, output latches high, no inversion.
  memset(c->config, 0xFF, sizeof(c->config));
  memset(c->output, 0xFF, sizeof(c->output));

  sys_i2c_dev_init(&c->i2c, cfg->i2c_bus != 0, cfg->i2c_addr, TCA6424A_I2C_FREQUENCY);
  SYS_DEV_INSTALL_STEP_BIT(c, TCA_STEP_I2C_ADDED, sys_i2c_dev_add(&c->i2c), "i2c add (probes the chip)");

  if (sys_io_pin_is_valid(c->rst_pin)) {
    SYS_DEV_INSTALL_STEP(sys_io_set_mode(c->rst_pin), "rst pin mode");
    SYS_DEV_INSTALL_STEP(sys_io_set_level(c->rst_pin, true), "rst pin high");
    SYS_DEV_INSTALL_STEP_BIT(c, TCA_STEP_RST_READY, sys_io_lock_pin(c->rst_pin), "rst pin lock");
  }

  // The chip keeps its registers across an ESP reset when there is no RST pin:
  // write the power-on state the cache starts from.
  SYS_DEV_INSTALL_STEP(chip_restore(c), "write default state");

  if (sys_io_pin_is_valid(c->intr_pin)) {
    SYS_DEV_INSTALL_STEP(sys_io_set_mode(c->intr_pin), "intr pin mode");
    SYS_DEV_INSTALL_STEP_BIT(c, TCA_STEP_INTR_SUB, sys_io_subscribe_pin(c->intr_pin, device_event_handler, c, &c->intr_sub), "intr pin subscribe");
    sys_io_intr_config_t intr_cfg = {.mode = SYS_IO_INTR_MODE_FALLING_EDGE};
    SYS_DEV_INSTALL_STEP(sys_io_configure_intr(c->intr_pin, &intr_cfg), "intr pin configure");
    SYS_DEV_INSTALL_STEP_BIT(c, TCA_STEP_INTR_READY, sys_io_lock_pin(c->intr_pin), "intr pin lock");
  }

  SYS_DEV_INSTALL_STEP(chip_get_pins(c, &c->cached_inputs), "read inputs");

  *out_device_handle = c;
  return NULL;

fail:
  SYS_DEV_INSTALL_FAIL(err, out_device_handle, device_uninstall, c);
  return NULL;
}

static const uint8_t s_pin_refs[] = {offsetof(d_tca6424a_cfg_t, intr_pin), offsetof(d_tca6424a_cfg_t, rst_pin)};

const sys_device_class_t g_tca6424a_class = {
    .type_id = TCA6424A_TYPE_ID,
    .cfg_size = sizeof(d_tca6424a_cfg_t),
    SYS_DEVICE_PINS(s_pin_refs),
    .name = "TCA6424A_IO_EXP",
    .contracts = {[SYS_DEVICE_CONTRACT_IO] = &s_tca6424a_io_contract},
    .ops = {.install = device_install,
        .uninstall = device_uninstall,
        .reset = device_reset,
        .suspend = device_suspend,
        .resume = device_resume},
};
