#include <stdlib.h>
#include "device_dac53202.h"
#include "driver_dac53202.h"
#include "esp_log.h"
#include "sys_device.h"
#include "sys_error.h"
#include "sys_i2c.h"
#include "sys_io.h"

static const char* TAG = __FILE_NAME__;
#undef OWNER
#define OWNER OWNER_DEVICE_DAC53202

// --- 1. The Encapsulated Adapter Context ---
typedef struct dac_adapter_ctx_t {
  sys_device_adapter_base_t base;

  d_dac53202_cfg_t cfg;

  // Caching mechanism for freeze/sync
  uint32_t cached_voltage_mv[2];
  bool cached_voltage_dirty[2];
  uint8_t cached_power_mask;
  bool cached_power_dirty;
} dac_adapter_ctx_t;

enum { DAC53202_STEP_I2C_ADDED = 0 };

// --- VTABLE Implementations (IO Contract) ---
static err_h contract_io_dac53202_reset_pin(void* handle, sys_io_pin_num_t pin) {
  SYS_DEV_GET_ADAPTER_CONTEXT(dac_adapter_ctx_t, dac53202_handle_t, ctx, hw, handle);
  VERIFY_PIN(SYS_DEV_GET_ID(ctx), pin, 0x03);

  IF_SYS_DEV_FROZEN(ctx) {
    if (ctx->cached_power_dirty) {
      ctx->cached_power_mask &= ~(1 << pin);
    } else {
      ctx->cached_power_mask = (hw->common_config & 0xFF) & ~(1 << pin);
      ctx->cached_power_dirty = true;
    }
    return NULL;
  }

  uint8_t current_power_on = hw->common_config & 0xFF;
  uint8_t next_power_on = current_power_on & ~(1 << pin);
  SYS_DEV_CHECK_DRIVER_CALL(dac53202_preset_cfg(hw, 0x03, next_power_on), ctx);
  return NULL;
}

static err_h contract_io_dac53202_set_voltage(void* handle, sys_io_pin_num_t pin, uint32_t voltage_mV) {
  SYS_DEV_GET_ADAPTER_CONTEXT(dac_adapter_ctx_t, dac53202_handle_t, ctx, hw, handle);
  VERIFY_PIN(SYS_DEV_GET_ID(ctx), pin, 0x03);

  IF_SYS_DEV_FROZEN(ctx) {
    ctx->cached_voltage_mv[pin] = voltage_mV;
    ctx->cached_voltage_dirty[pin] = true;
    return NULL;
  }

  SYS_DEV_CHECK_DRIVER_CALL(dac53202_set_voltage_mv(hw, 1 << pin, (uint16_t)voltage_mV), ctx);
  return NULL;
}

static err_h contract_io_dac53202_get_voltage(void* handle, sys_io_pin_num_t pin, uint32_t* out_mV) {
  SYS_DEV_GET_ADAPTER_CONTEXT(dac_adapter_ctx_t, dac53202_handle_t, ctx, hw, handle);
  SE_CHECK_HANDLE(out_mV);
  VERIFY_PIN(SYS_DEV_GET_ID(ctx), pin, 0x03);

  if (ctx->base.is_frozen && ctx->cached_voltage_dirty[pin]) {
    *out_mV = ctx->cached_voltage_mv[pin];
    return NULL;
  }

  uint16_t v_mv = 0;
  SYS_DEV_CHECK_DRIVER_CALL(dac53202_get_voltage_mv(hw, pin, &v_mv), ctx);
  *out_mV = (uint32_t)v_mv;
  return NULL;
}

// Instantiate the static VTable
static sys_io_vtable_t io_dac_vtable = {.io_reset = contract_io_dac53202_reset_pin,
    .io_set_voltage = contract_io_dac53202_set_voltage,
    .io_get_voltage = contract_io_dac53202_get_voltage,
    .io_configure_intr = NULL,
    .io_set_mode = NULL,
    .io_set_level = NULL,
    .io_get_level = NULL,
    .io_toggle = NULL,
    .io_set_pwm_frequency = NULL,
    .io_set_pwm_duty = NULL,
    .protected_pins = 0};

// --- sys_device_t VTable Implementations ---
static err_h device_uninstall(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(dac_adapter_ctx_t, dac53202_handle_t, ctx, hw, handle);
  err_h err = NULL;

  if (ctx->base.hw_handle) {
    IF_SYS_DEV_STEP_DONE(ctx, DAC53202_STEP_I2C_ADDED) {
      SYS_DEV_TEARDOWN_STEP(err, sys_i2c_remove_driver(ctx->base.hw_handle));
    }
    dac53202_delete(hw);
  }

  free(ctx);
  return err;
}

static err_h device_reset(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(dac_adapter_ctx_t, dac53202_handle_t, ctx, hw, handle);

  SYS_DEV_CHECK_DRIVER_CALL(dac53202_preset_cfg(hw, 0x03, 0x00), ctx);
  return NULL;
}

static err_h device_suspend(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(dac_adapter_ctx_t, dac53202_handle_t, ctx, hw, handle);
  SYS_DEV_CHECK_DRIVER_CALL(dac53202_preset_cfg(hw, 0x03, 0x00), ctx);
  return NULL;
}

static err_h device_resume(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(dac_adapter_ctx_t, dac53202_handle_t, ctx, hw, handle);
  SYS_DEV_CHECK_DRIVER_CALL(dac53202_preset_cfg(hw, 0x03, 0x03), ctx);
  return NULL;
}

static err_h device_freeze(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(dac_adapter_ctx_t, dac53202_handle_t, ctx, hw, handle);
  IF_SYS_DEV_FROZEN(ctx) {
    return NULL;
  }
  SYS_DEV_CTX_FREEZE(ctx);
  ctx->cached_voltage_dirty[0] = false;
  ctx->cached_voltage_dirty[1] = false;
  ctx->cached_power_dirty = false;
  return NULL;
}

static err_h device_sync(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(dac_adapter_ctx_t, dac53202_handle_t, ctx, hw, handle);
  SYS_DEV_CTX_UNFREEZE(ctx);

  if (ctx->cached_power_dirty) {
    SYS_DEV_CHECK_DRIVER_CALL(dac53202_preset_cfg(hw, 0x03, ctx->cached_power_mask), ctx);
    ctx->cached_power_dirty = false;
  }

  for (int i = 0; i < 2; i++) {
    if (ctx->cached_voltage_dirty[i]) {
      SYS_DEV_CHECK_DRIVER_CALL(dac53202_set_voltage_mv(hw, 1 << i, (uint16_t)ctx->cached_voltage_mv[i]), ctx);
      ctx->cached_voltage_dirty[i] = false;
    }
  }

  return NULL;
}

static err_h device_error_handler(void* handle, err_h error) {
  dac_adapter_ctx_t* ctx = (dac_adapter_ctx_t*)handle;
  SYS_DEV_CHECK_HANDLE(ctx, 0);
  sys_device_t* dev = sys_device_get_by_id(SYS_DEV_GET_ID(ctx));
  if (!dev) return NULL;

  if (dev->generate_error_callback) {
    // TODO: report to the VM via the callback system. Payload should carry
    // at least: device_id, and the root cause's tag/owner - walk
    // error->next_cause to the end, since a wrapper like ERR_DEV_DEP_FAILED
    // only carries dev_id, not the underlying failure's tag/owner. Always
    // attach device_id explicitly (the root cause itself may not carry one).
    return NULL;
  }

  if (dev->use_error_handler) {
    // TODO: classify `error` into a sys_device_err_level_e (critical/
    // warning/notice) and sys_actions_invoke(dev->actions[level]).
  }
  return NULL;
}

static err_h device_install(const void* cfg_blob, void** out_device_handle) {
  const d_dac53202_cfg_t* cfg = (const d_dac53202_cfg_t*)cfg_blob;
  SE_CHECK_NOT_NULL(cfg);
  SE_CHECK_NOT_NULL(out_device_handle);

  SYS_DEV_CTX_NEW(dac_adapter_ctx_t, ctx, cfg);
  err_h err = NULL;

  ctx->base.hw_handle = dac53202_new(ctx->cfg.i2c_addr, ctx->cfg.i2c_bus);
  if (!ctx->base.hw_handle) {
    free(ctx);
    SE_RET_ERR(ERR_BASE_NO_MEM, 0);
  }

  dac53202_handle_t hw = (dac53202_handle_t)(ctx->base.hw_handle);

  SYS_DEV_INSTALL_STEP(sys_i2c_add_driver(hw), "i2c add driver");
  SYS_DEV_STEP_DONE(ctx, DAC53202_STEP_I2C_ADDED);

  SYS_DEV_INSTALL_STEP(sys_i2c_device_present(hw), "probe i2c device");

  SYS_DEV_INSTALL_STEP(SE_CONVERT_ESP(dac53202_preset_cfg(hw, 0x03, 0x03)), "dac preset cfg");

  *out_device_handle = ctx;
  return NULL;

fail:
  SYS_DEV_INSTALL_FAIL(err, cfg->device_id, out_device_handle, device_uninstall, ctx);
  return NULL;
}

static const sys_device_class_t s_dac53202_class = {
    .name = "DAC53202",
    .contracts = {[SYS_DEVICE_CONTRACT_IO] = (void*)&io_dac_vtable},
    .ops = {
        .install = device_install,
        .uninstall = device_uninstall,
        .reset = device_reset,
        .suspend = device_suspend,
        .resume = device_resume,
        .freeze = device_freeze,
        .sync = device_sync,
        .error_handler = device_error_handler
    },
};

err_h d_dac53202_create(const d_dac53202_cfg_t* cfg) {
  SE_CHECK_NOT_NULL(cfg);
  return SYS_DEVICE_CREATE(&s_dac53202_class, cfg);
}
// 220