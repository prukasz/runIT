#include <stdint.h>
#include <stdlib.h>
#include "device_pca9685.h"
#include "driver_pca9685.h"
#include "esp_err.h"
#include "esp_log.h"
#include "sys_device.h"
#include "sys_error.h"
#include "sys_error_codes.h"
#include "sys_i2c.h"
#include "sys_io.h"

static const char* TAG = __FILE_NAME__;
#undef OWNER
#define OWNER OWNER_DEVICE_PCA9685
#define PINS_MASK 0xFFFF

// Install steps, recorded so teardown rolls back only what was actually built
enum { PCA_STEP_I2C_ADDED = 0, PCA_STEP_OE_READY = 1 };

typedef struct pca_adapter_ctx_t {
  sys_device_adapter_base_t base;  // must be first
  d_pca9685_cfg_t cfg;             // value copy; the only cfg the adapter reads

  uint16_t frozen_duty[PCA9685_CHANNEL_ALL];
  uint16_t frozen_outputs_mask;
  uint16_t frozen_freq;
  bool frozen_freq_dirty;
} pca_adapter_ctx_t;

// --- VTABLE Implementations (IO Contract) ---
static err_h contract_io_pca9685_set_pwm_duty(void* handle, sys_io_pin_num_t pin, uint32_t duty) {
  SYS_DEV_GET_ADAPTER_CONTEXT(pca_adapter_ctx_t, pca9685_handle_t, ctx, hw, handle);
  VERIFY_PIN(SYS_DEV_GET_ID(ctx), pin, PINS_MASK);

  if (duty > PCA9685_MAX_PWM_VALUE) {
    duty = PCA9685_MAX_PWM_VALUE;
  }

  IF_SYS_DEV_FROZEN(ctx) {
    ctx->frozen_duty[pin] = (uint16_t)duty;
    ctx->frozen_outputs_mask |= (1 << pin);
    return NULL;
  }
  SYS_DEV_CHECK_DRIVER_CALL(pca9685_set_pwm_value(hw, pin, (uint16_t)duty), ctx);
  return NULL;
}

static err_h contract_io_pca9685_set_pwm_frequency(void* handle, sys_io_pin_num_t pin, uint32_t frequency_HZ) {
  SYS_DEV_GET_ADAPTER_CONTEXT(pca_adapter_ctx_t, pca9685_handle_t, ctx, hw, handle);

  IF_SYS_DEV_FROZEN(ctx) {
    ctx->frozen_freq = (uint16_t)frequency_HZ;
    ctx->frozen_freq_dirty = true;
    return NULL;
  }
  SYS_DEV_CHECK_DRIVER_CALL(pca9685_set_pwm_frequency(hw, (uint16_t)frequency_HZ), ctx);
  return NULL;
}

static err_h contract_io_pca9685_set_level(void* handle, sys_io_pin_num_t pin, bool level) {
  uint32_t target_pwm = level ? PCA9685_MAX_PWM_VALUE : 0;
  return contract_io_pca9685_set_pwm_duty(handle, pin, target_pwm);
}

static err_h contract_io_pca9685_get_level(void* handle, sys_io_pin_num_t pin, bool* level) {
  SYS_DEV_GET_ADAPTER_CONTEXT(pca_adapter_ctx_t, pca9685_handle_t, ctx, hw, handle);
  SE_CHECK_HANDLE(level);
  VERIFY_PIN(SYS_DEV_GET_ID(ctx), pin, PINS_MASK);

  uint16_t current_val;
  if (ctx->base.is_frozen && (ctx->frozen_outputs_mask & (1 << pin))) {
    current_val = ctx->frozen_duty[pin];
  } else {
    current_val = hw->channel_pwm_value[pin];
  }
  *level = (current_val >= (PCA9685_MAX_PWM_VALUE / 2));

  return NULL;
}

static err_h contract_io_pca9685_toggle(void* handle, sys_io_pin_num_t pin) {
  SYS_DEV_GET_ADAPTER_CONTEXT(pca_adapter_ctx_t, pca9685_handle_t, ctx, hw, handle);
  VERIFY_PIN(SYS_DEV_GET_ID(ctx), pin, PINS_MASK);

  uint16_t current_val;
  if (ctx->base.is_frozen && (ctx->frozen_outputs_mask & (1 << pin))) {
    current_val = ctx->frozen_duty[pin];
  } else {
    current_val = hw->channel_pwm_value[pin];
  }
  uint16_t new_val = (current_val >= (PCA9685_MAX_PWM_VALUE / 2)) ? 0 : PCA9685_MAX_PWM_VALUE;

  return contract_io_pca9685_set_pwm_duty(handle, pin, new_val);
}

static err_h contract_io_pca9685_reset_pin(void* handle, sys_io_pin_num_t pin) {
  return contract_io_pca9685_set_pwm_duty(handle, pin, 0);
}

// Instantiate the static VTable
// Instantiate the static VTable
static sys_io_vtable_t io_pca_vtable = {.io_set_pwm_duty = contract_io_pca9685_set_pwm_duty,
    .io_set_pwm_frequency = contract_io_pca9685_set_pwm_frequency,
    .io_set_level = contract_io_pca9685_set_level,
    .io_get_level = contract_io_pca9685_get_level,
    .io_toggle = contract_io_pca9685_toggle,
    .io_reset = contract_io_pca9685_reset_pin,
    .io_configure_intr = NULL,
    .io_set_mode = NULL,
    .io_get_voltage = NULL,
    .io_set_voltage = NULL,
    .protected_pins = 0};

// --- sys_device VTable Implementations ---
// Doubles as the install rollback path: each step is gated on having actually
// run, and no step may early-return - teardown must always free everything.
static err_h device_uninstall(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(pca_adapter_ctx_t, pca9685_handle_t, ctx, hw, handle);
  err_h err = NULL;

  if (hw) {
    pca9685_sleep(hw, true);
  }

  // Disable outputs if OE pin was configured (set HIGH)
  IF_SYS_DEV_STEP_DONE(ctx, PCA_STEP_OE_READY) {
    SYS_IO_REF_UNLOCK(ctx->cfg.oe_pin);
    SYS_DEV_TEARDOWN_STEP(err, SYS_IO_REF_HIGH(ctx->cfg.oe_pin));
    SYS_DEV_TEARDOWN_STEP(err, SYS_IO_REF_RESET(ctx->cfg.oe_pin));
  }

  if (ctx->base.hw_handle) {
    IF_SYS_DEV_STEP_DONE(ctx, PCA_STEP_I2C_ADDED) {
      SYS_DEV_TEARDOWN_STEP(err, sys_i2c_remove_driver(ctx->base.hw_handle));
    }
    free(hw);
  }
  free(ctx);
  return err;
}

static err_h device_reset(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(pca_adapter_ctx_t, pca9685_handle_t, ctx, hw, handle);

  for (uint8_t i = 0; i < PCA9685_CHANNEL_ALL; i++) {
    SYS_DEV_CHECK_DRIVER_CALL(pca9685_set_pwm_value(hw, i, 0), ctx);
  }

  SYS_DEV_CHECK_DRIVER_CALL(pca9685_enable_auto_increment(hw), ctx);

  return NULL;
}

static err_h device_suspend(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(pca_adapter_ctx_t, pca9685_handle_t, ctx, hw, handle);

  IF_PIN_REF(ctx->cfg.oe_pin) {
    WITH_REF_UNLOCKED(ctx->cfg.oe_pin) {
      SYS_IO_REF_HIGH(ctx->cfg.oe_pin);
    }
  }

  SYS_DEV_CHECK_DRIVER_CALL(pca9685_sleep(hw, true), ctx);
  ctx->base.is_frozen = true;

  return NULL;
}

static err_h device_resume(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(pca_adapter_ctx_t, pca9685_handle_t, ctx, hw, handle);

  pca9685_sleep(hw, false);

  IF_PIN_REF(ctx->cfg.oe_pin) {
    WITH_REF_UNLOCKED(ctx->cfg.oe_pin) {
      SYS_IO_REF_LOW(ctx->cfg.oe_pin);
    }
  }

  ctx->base.is_frozen = false;
  return NULL;
}

static err_h device_freeze(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(pca_adapter_ctx_t, pca9685_handle_t, ctx, hw, handle);
  IF_SYS_DEV_FROZEN(ctx) {
    return NULL;
  }
  SYS_DEV_CTX_FREEZE(ctx);
  ctx->frozen_outputs_mask = 0;
  ctx->frozen_freq_dirty = false;
  return NULL;
}

static err_h device_sync(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(pca_adapter_ctx_t, pca9685_handle_t, ctx, hw, handle);
  SYS_DEV_CTX_UNFREEZE(ctx);

  // Flush deferred duty cycles
  if (ctx->frozen_outputs_mask != 0) {
    for (uint8_t i = 0; i < PCA9685_CHANNEL_ALL; i++) {
      if (ctx->frozen_outputs_mask & (1 << i)) {
        SYS_DEV_CHECK_DRIVER_CALL(pca9685_set_pwm_value(hw, i, ctx->frozen_duty[i]), ctx);
      }
    }
    ctx->frozen_outputs_mask = 0;
  }

  // Flush deferred frequency
  if (ctx->frozen_freq_dirty) {
    SYS_DEV_CHECK_DRIVER_CALL(pca9685_set_pwm_frequency(hw, ctx->frozen_freq), ctx);
    ctx->frozen_freq_dirty = false;
  }

  return NULL;
}

static err_h device_error_handler(void* handle, err_h error) {
  return NULL;
}

static err_h device_install(const void* cfg_blob, void** out_device_handle) {
  const d_pca9685_cfg_t* cfg = (const d_pca9685_cfg_t*)cfg_blob;
  SE_CHECK_NOT_NULL(cfg);
  SE_CHECK_NOT_NULL(out_device_handle);

  SYS_DEV_CTX_NEW(pca_adapter_ctx_t, ctx, cfg);
  err_h err = NULL;

  ctx->base.hw_handle = pca9685_new(ctx->cfg.i2c_addr, ctx->cfg.i2c_bus);
  if (!ctx->base.hw_handle) {
    free(ctx);
    SE_RET_ERR(ERR_BASE_NO_MEM, 0);
  }

  pca9685_handle_t hw = (pca9685_handle_t)(ctx->base.hw_handle);

  SYS_DEV_INSTALL_STEP(sys_i2c_add_driver(ctx->base.hw_handle), "i2c add driver");
  SYS_DEV_STEP_DONE(ctx, PCA_STEP_I2C_ADDED);

  SYS_DEV_INSTALL_STEP(sys_i2c_device_present(ctx->base.hw_handle), "i2c probe");
  SYS_DEV_INSTALL_STEP(SE_CONVERT_ESP(pca9685_start(hw)), "chip start");

  IF_PIN_REF(ctx->cfg.oe_pin) {
    SYS_DEV_INSTALL_STEP(SYS_IO_REF_SET_MODE(ctx->cfg.oe_pin), "OE pin mode");
    SYS_IO_REF_LOW(ctx->cfg.oe_pin);  // OE is active low => outputs enabled
    SYS_IO_REF_LOCK(ctx->cfg.oe_pin);
    SYS_DEV_STEP_DONE(ctx, PCA_STEP_OE_READY);
  }

  ESP_LOGI(TAG, "PCA9685 successfully installed as Device ID %d", ctx->cfg.device_id);
  *out_device_handle = ctx;
  return NULL;

fail:
  SYS_DEV_INSTALL_FAIL(err, cfg->device_id, out_device_handle, device_uninstall, ctx);
  return NULL;
}

// The IO contract is declared here, not registered imperatively during install.
static const sys_device_class_t s_pca9685_class = {
    .name = "PCA9685_PWM_EXPANDER",
    .contracts = {[SYS_DEVICE_CONTRACT_IO] = (void*)&io_pca_vtable},
    .ops = {.install = device_install,
        .uninstall = device_uninstall,
        .reset = device_reset,
        .suspend = device_suspend,
        .resume = device_resume,
        .freeze = device_freeze,
        .sync = device_sync,
        .error_handler = device_error_handler},
};

// --- Exposed Initialization API ---
err_h d_pca9685_create(const d_pca9685_cfg_t* cfg) {
  SE_CHECK_NOT_NULL(cfg);
  return SYS_DEVICE_CREATE(&s_pca9685_class, cfg);
}