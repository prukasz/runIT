#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "device_tca6424a.h"
#include "driver_tca6424a.h"
#include "esp_log.h"
#include "sys_device.h"
#include "sys_error.h"
#include "sys_i2c.h"
#include "sys_io.h"

static const char* TAG = __FILE_NAME__;

#define PINS_COUNT 24
#define PINS_MASK ((1UL << PINS_COUNT) - 1)
#undef OWNER
#define OWNER OWNER_DEVICE_TCA6424A

typedef struct tca_adapter_ctx_t {
  sys_device_adapter_base_t base;

  d_tca6424a_cfg_t cfg;

  uint32_t cached_inputs;
  uint32_t frozen_outputs_mask;
  uint32_t frozen_outputs_state;
  uint32_t configured_pins;  // 24-bit bitmask tracking pin usage

  uint16_t route_masks[24];
  sys_io_intr_mode_e intr_modes[24];
  own_funct_t own_funcs[24];
} tca_adapter_ctx_t;

enum { TCA_STEP_I2C_ADDED = 0, TCA_STEP_RST_READY = 1, TCA_STEP_INTR_READY = 2 };

static err_h device_event_handler(void* handle, cb_event_t* event) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tca_adapter_ctx_t, tca6424a_handle_t, ctx, hw, handle);

  uint32_t current_state = 0;
  SYS_DEV_CHECK_DRIVER_CALL(tca_get_pins(hw, &current_state), ctx);

  uint32_t previous_state = ctx->cached_inputs;
  uint32_t changed_bits = current_state ^ previous_state;
  uint32_t rising_edges = changed_bits & current_state;
  uint32_t falling_edges = changed_bits & ~current_state;

  for (uint8_t i = 0; i < PINS_COUNT; i++) {
    if (!(changed_bits & (1UL << i))) continue;

    sys_io_intr_mode_e mode = ctx->intr_modes[i];
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
      if (ctx->own_funcs[i].own_func) {
        SYS_CB_OWN(ctx->own_funcs[i]);
      } else {
        bool level = (rising_edges & (1UL << i)) != 0;
        SYS_IO_CB(ctx, i, mode, level, ctx->route_masks[i]);
      }
    }
  }

  ctx->cached_inputs = current_state;
  return NULL;
}

err_h contract_io_tca6424a_set_mode(void* handle, sys_io_pin_num_t pin, sys_io_mode_e mode) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tca_adapter_ctx_t, tca6424a_handle_t, ctx, hw, handle);
  VERIFY_PIN(SYS_DEV_GET_ID(ctx), pin, PINS_MASK);

  uint32_t tca_cfg_state = 0;

  switch (mode) {
    case SYS_IO_MODE_OUTPUT_PUSH_PULL:
      tca_cfg_state = 0x00000000;
      break;
    case SYS_IO_MODE_INPUT:
      tca_cfg_state = 0xFFFFFFFF;
      break;
    default:
      SE_RET_ERR(ERR_IO_PIN_MODE_UNSUPPORTED, SYS_DEV_GET_ID(ctx), pin, mode);
  }

  SYS_DEV_CHECK_DRIVER_CALL(tca_preset_cfg(hw, 1UL << pin, tca_cfg_state), ctx);
  ctx->configured_pins |= (1UL << pin);

  return NULL;
}

err_h contract_io_tca6424a_set_level(void* handle, sys_io_pin_num_t pin, bool level) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tca_adapter_ctx_t, tca6424a_handle_t, ctx, hw, handle);
  VERIFY_PIN(SYS_DEV_GET_ID(ctx), pin, PINS_MASK);

  uint32_t pin_mask = (1UL << pin);
  uint32_t state_mask = level ? pin_mask : 0;

  IF_SYS_DEV_FROZEN(ctx) {
    ctx->frozen_outputs_mask |= pin_mask;
    ctx->frozen_outputs_state = (ctx->frozen_outputs_state & ~pin_mask) | state_mask;
    return NULL;
  }

  SYS_DEV_CHECK_DRIVER_CALL(tca_set_pins(hw, pin_mask, state_mask), ctx);
  return NULL;
}

err_h contract_io_tca6424a_get_level(void* handle, sys_io_pin_num_t pin, bool* level) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tca_adapter_ctx_t, tca6424a_handle_t, ctx, hw, handle);
  SE_CHECK_HANDLE(level);
  VERIFY_PIN(SYS_DEV_GET_ID(ctx), pin, PINS_MASK);

  uint32_t pin_mask = (1UL << pin);
  uint32_t all_levels = 0;

  IF_SYS_DEV_FROZEN(ctx) {
    all_levels = ctx->cached_inputs;
  }
  else {
    SYS_DEV_CHECK_DRIVER_CALL(tca_get_pins(hw, &all_levels), ctx);
    ctx->cached_inputs = all_levels;
  }

  *level = (all_levels & pin_mask) ? true : false;
  return NULL;
}

err_h contract_io_tca6424a_toggle(void* handle, sys_io_pin_num_t pin) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tca_adapter_ctx_t, tca6424a_handle_t, ctx, hw, handle);
  VERIFY_PIN(SYS_DEV_GET_ID(ctx), pin, PINS_MASK);

  uint32_t pin_mask = (1UL << pin);
  bool is_high;

  if (ctx->base.is_frozen && (ctx->frozen_outputs_mask & pin_mask)) {
    is_high = (ctx->frozen_outputs_state & pin_mask) != 0;
  } else {
    uint32_t current_outputs = tca_get_pin_output(hw);
    is_high = (current_outputs & pin_mask) != 0;
  }

  return contract_io_tca6424a_set_level(handle, pin, !is_high);
}

err_h contract_io_tca6424a_reset_pin(void* handle, sys_io_pin_num_t pin) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tca_adapter_ctx_t, tca6424a_handle_t, ctx, hw, handle);
  VERIFY_PIN(SYS_DEV_GET_ID(ctx), pin, PINS_MASK);

  ctx->route_masks[pin] = 0;
  ctx->intr_modes[pin] = SYS_IO_INTR_DISABLE;
  memset(&ctx->own_funcs[pin], 0, sizeof(own_funct_t));
  ctx->configured_pins &= ~(1UL << pin);

  SYS_DEV_CHECK_DRIVER_CALL(tca_set_pins(hw, 1UL << pin, 0), ctx);
  SYS_DEV_CHECK_DRIVER_CALL(tca_preset_cfg(hw, 1UL << pin, 1UL << pin), ctx);

  return NULL;
}

err_h d_tca6424a_driver_reset(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tca_adapter_ctx_t, tca6424a_handle_t, ctx, hw, handle);
  for (uint8_t i = 0; i < PINS_COUNT; i++) {
    SE_RET_IF_ERR(contract_io_tca6424a_reset_pin(handle, i));
  }
  return NULL;
}

err_h contract_io_tca6424a_configure_intr(void* handle, sys_io_pin_num_t pin, const sys_io_intr_config_t* config) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tca_adapter_ctx_t, tca6424a_handle_t, ctx, hw, handle);
  VERIFY_PIN(SYS_DEV_GET_ID(ctx), pin, PINS_MASK);

  if (config->mode == SYS_IO_INTR_DISABLE) {
    ctx->route_masks[pin] = 0;
    ctx->intr_modes[pin] = SYS_IO_INTR_DISABLE;
    memset(&ctx->own_funcs[pin], 0, sizeof(own_funct_t));
    return NULL;
  }

  ctx->route_masks[pin] = config->route_mask;
  ctx->intr_modes[pin] = config->mode;
  ctx->own_funcs[pin] = config->own_func;

  return NULL;
}

static sys_io_vtable_t io_tca_vtable = {.io_reset = contract_io_tca6424a_reset_pin,
    .io_set_mode = contract_io_tca6424a_set_mode,
    .io_configure_intr = contract_io_tca6424a_configure_intr,
    .io_set_level = contract_io_tca6424a_set_level,
    .io_get_level = contract_io_tca6424a_get_level,
    .io_toggle = contract_io_tca6424a_toggle,
    .io_get_voltage = NULL,
    .io_set_voltage = NULL,
    .io_set_pwm_frequency = NULL,
    .io_set_pwm_duty = NULL,
    .protected_pins = 0};

static err_h device_freeze(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tca_adapter_ctx_t, tca6424a_handle_t, ctx, hw, handle);
  IF_SYS_DEV_FROZEN(ctx) {
    return NULL;
  }
  SYS_DEV_CTX_FREEZE(ctx);
  SYS_DEV_CHECK_DRIVER_CALL(tca_get_pins(hw, &ctx->cached_inputs), ctx);
  ctx->frozen_outputs_mask = 0;
  ctx->frozen_outputs_state = 0;
  return NULL;
}

static err_h device_sync(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tca_adapter_ctx_t, tca6424a_handle_t, ctx, hw, handle);
  SYS_DEV_CTX_UNFREEZE(ctx);
  SYS_DEV_CHECK_DRIVER_CALL(tca_get_pins(hw, &ctx->cached_inputs), ctx);
  if (ctx->frozen_outputs_mask != 0) {
    SYS_DEV_CHECK_DRIVER_CALL(tca_set_pins(hw, ctx->frozen_outputs_mask, ctx->frozen_outputs_state), ctx);
    ctx->frozen_outputs_mask = 0;
  }
  return NULL;
}

// Teardown must never early-return: a failing step would leak the i2c
// registration, the hw handle and ctx. Keep the first error, free everything.
static err_h device_uninstall(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tca_adapter_ctx_t, tca6424a_handle_t, ctx, hw, handle);
  err_h err = NULL;

  IF_SYS_DEV_STEP_DONE(ctx, TCA_STEP_RST_READY) {
    SYS_IO_REF_UNLOCK(ctx->cfg.rst_pin);
    SYS_DEV_TEARDOWN_STEP(err, SYS_IO_REF_RESET(ctx->cfg.rst_pin));
  }
  IF_SYS_DEV_STEP_DONE(ctx, TCA_STEP_INTR_READY) {
    SYS_IO_REF_UNLOCK(ctx->cfg.intr_pin);
    SYS_DEV_TEARDOWN_STEP(err, SYS_IO_REF_RESET(ctx->cfg.intr_pin));
  }

  if (ctx->base.hw_handle) {
    IF_SYS_DEV_STEP_DONE(ctx, TCA_STEP_I2C_ADDED) {
      SYS_DEV_TEARDOWN_STEP(err, sys_i2c_remove_driver(hw));
    }
    d_tca6424a_delete(hw);
  }
  free(ctx);
  return err;
}

static err_h device_reset(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tca_adapter_ctx_t, tca6424a_handle_t, ctx, hw, handle);

  IF_PIN_REF(ctx->cfg.rst_pin) {
    WITH_REF_UNLOCKED(ctx->cfg.rst_pin) {
      RET_IF_DEV_ERR(SYS_IO_REF_LOW(ctx->cfg.rst_pin), ctx);
      vTaskDelay(pdMS_TO_TICKS(10));
      RET_IF_DEV_ERR(SYS_IO_REF_HIGH(ctx->cfg.rst_pin), ctx);
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
  ctx->cached_inputs = 0;
  ctx->configured_pins = 0;
  ctx->frozen_outputs_mask = 0;
  ctx->frozen_outputs_state = 0;
  return d_tca6424a_driver_reset(handle);
}

static err_h device_error_handler(void* handle, err_h error) {
  if (!error) return NULL;
  tca_adapter_ctx_t* ctx = (tca_adapter_ctx_t*)handle;
  SYS_DEV_CHECK_HANDLE(ctx, 0);
  ESP_LOGE(TAG, "TCA6424A Error: owner=%u, tag=%d for device ID %u", (unsigned int)error->owner, (int)error->tag, SYS_DEV_GET_ID(ctx));
  return error;
}

static err_h device_suspend(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tca_adapter_ctx_t, tca6424a_handle_t, ctx, hw, handle);
  IF_PIN_REF(ctx->cfg.rst_pin) {
    WITH_REF_UNLOCKED(ctx->cfg.rst_pin) {
      RET_IF_DEV_ERR(SYS_IO_REF_LOW(ctx->cfg.rst_pin), ctx);
    }
  }
  return NULL;
}

static err_h device_resume(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tca_adapter_ctx_t, tca6424a_handle_t, ctx, hw, handle);
  SE_CHECK_HANDLE(ctx);
  IF_PIN_REF(ctx->cfg.rst_pin) {
    WITH_REF_UNLOCKED(ctx->cfg.rst_pin) {
      RET_IF_DEV_ERR(SYS_IO_REF_HIGH(ctx->cfg.rst_pin), ctx);
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  SYS_DEV_CHECK_DRIVER_CALL(tca_restore_state(hw), ctx);
  return NULL;
}

static err_h device_install(const void* cfg_blob, void** out_device_handle) {
  const d_tca6424a_cfg_t* cfg = (const d_tca6424a_cfg_t*)cfg_blob;
  SE_CHECK_NOT_NULL(cfg);
  SE_CHECK_NOT_NULL(out_device_handle);

  SYS_DEV_CTX_NEW(tca_adapter_ctx_t, ctx, cfg);
  err_h err = NULL;

  ctx->base.hw_handle = d_tca6424a_new(ctx->cfg.i2c_addr, ctx->cfg.i2c_bus);
  if (!ctx->base.hw_handle) {
    free(ctx);
    SE_RET_ERR(ERR_DEV_NO_HANDLE, cfg->device_id);
  }

  tca6424a_handle_t hw = (tca6424a_handle_t)ctx->base.hw_handle;

  SYS_DEV_INSTALL_STEP(sys_i2c_add_driver(hw), "i2c add driver");
  SYS_DEV_STEP_DONE(ctx, TCA_STEP_I2C_ADDED);

  SYS_DEV_INSTALL_STEP(sys_i2c_device_present(hw), "probe i2c device");

  IF_PIN_REF(ctx->cfg.rst_pin) {
    SYS_DEV_INSTALL_STEP(SYS_IO_REF_SET_MODE(ctx->cfg.rst_pin), "rst pin mode");
    SYS_DEV_INSTALL_STEP(SYS_IO_REF_HIGH(ctx->cfg.rst_pin), "rst pin high");
    SYS_IO_REF_LOCK(ctx->cfg.rst_pin);
    SYS_DEV_STEP_DONE(ctx, TCA_STEP_RST_READY);
  }

  IF_PIN_REF(ctx->cfg.intr_pin) {
    SYS_DEV_INSTALL_STEP(SYS_IO_REF_SET_MODE(ctx->cfg.intr_pin), "intr pin mode");
    sys_io_intr_config_t intr_cfg = {
        .mode = SYS_IO_INTR_MODE_FALLING_EDGE,
        .own_func = {.own_func = device_event_handler, .device_handle = ctx},
    };
    SYS_DEV_INSTALL_STEP(sys_io_configure_intr(ctx->cfg.intr_pin.device_id, ctx->cfg.intr_pin.pin, &intr_cfg), "intr pin configure");
    SYS_IO_REF_LOCK(ctx->cfg.intr_pin);
    SYS_DEV_STEP_DONE(ctx, TCA_STEP_INTR_READY);
  }

  SYS_DEV_INSTALL_STEP(SE_CONVERT_ESP(tca_get_pins(hw, &ctx->cached_inputs)), "tca get pins");

  *out_device_handle = ctx;
  return NULL;

fail:
  SYS_DEV_INSTALL_FAIL(err, cfg->device_id, out_device_handle, device_uninstall, ctx);
  return NULL;
}

static const sys_device_class_t s_tca6424a_class = {
    .name = "TCA6424A_IO_EXP",
    .roles = SYS_DEV_ROLE_IO,
    .contracts = {[SYS_DEVICE_CONTRACT_IO] = (void*)&io_tca_vtable},
    .ops = {.install = device_install, .uninstall = device_uninstall, .reset = device_reset, .suspend = device_suspend, .resume = device_resume, .freeze = device_freeze, .sync = device_sync, .error_handler = device_error_handler},
};

err_h d_tca6424a_create(const d_tca6424a_cfg_t* cfg) {
  SE_CHECK_NOT_NULL(cfg);
  return SYS_DEVICE_CREATE(&s_tca6424a_class, cfg);
}
// 414