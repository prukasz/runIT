#include <stdlib.h>
#include "device_tps55289.h"
#include "driver_tps55289.h"
#include "sys_device.h"
#include "sys_error.h"
#include "sys_i2c.h"
#include "sys_io.h"
#include "sys_power.h"

#undef OWNER
#define OWNER OWNER_DEVICE_TPS55289

typedef struct {
  sys_device_adapter_base_t base;

  d_tps55289_cfg_t cfg;

  uint16_t last_voltage_mV;
  uint16_t last_current_limit_mA;
  bool last_enable_state;
  bool is_current_limit_enabled;

  uint8_t intr_sub; /* sys_event subscription on intr_pin */
} tps_adapter_ctx_t;

enum { TPS_STEP_I2C_ADDED = 0, TPS_STEP_EN_READY = 1, TPS_STEP_INTR_READY = 2, TPS_STEP_INTR_SUB = 3 };

/* Inline listener of intr_pin: publish each fault in the status register. */
static SE_MUST_USE err_h device_event_handler(const sys_event_t* event, void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tps_adapter_ctx_t, tps55289_handle_t, ctx, hw, handle);
  SYS_DEV_CHECK_DRIVER_CALL(tps55289_get_status(hw), ctx);

  err_h err = NULL;
  uint8_t hops = SYS_EVENT_CAUSED_BY(event);
  if (hw->last_status.ovp) {
    SYS_DEV_TEARDOWN_STEP(err, sys_power_publish(SYS_DEV_GET_ID(ctx), 0, SYS_PWR_EVENT_OVP, ctx->last_voltage_mV, hops));
  }
  if (hw->last_status.ocp) {
    SYS_DEV_TEARDOWN_STEP(err, sys_power_publish(SYS_DEV_GET_ID(ctx), 0, SYS_PWR_EVENT_OCP_CRITICAL, ctx->last_current_limit_mA, hops));
  }
  if (hw->last_status.scp) {
    SYS_DEV_TEARDOWN_STEP(err, sys_power_publish(SYS_DEV_GET_ID(ctx), 0, SYS_PWR_EVENT_SPC, 0, hops));
  }
  return err;
}

// --- VREG Contract Implementations ---
static SE_MUST_USE err_h contract_vreg_tps55289_set_enable(void* device_handle, bool state) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tps_adapter_ctx_t, tps55289_handle_t, ctx, hw, device_handle);
  ctx->last_enable_state = state;

  if (state) {
    if (sys_io_pin_is_valid(ctx->cfg.en_pin)) {
      SYS_DEV_TRY(sys_io_set_locked_level(ctx->cfg.en_pin, state), ctx);
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_output_enable(hw, state), ctx);
  } else {
    SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_output_enable(hw, state), ctx);
    if (sys_io_pin_is_valid(ctx->cfg.en_pin)) {
      SYS_DEV_TRY(sys_io_set_locked_level(ctx->cfg.en_pin, state), ctx);
    }
  }

  return NULL;
}

static SE_MUST_USE err_h contract_vreg_tps55289_set_voltage(void* device_handle, uint32_t voltage_mV) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tps_adapter_ctx_t, tps55289_handle_t, ctx, hw, device_handle);
  SE_CHECK_IN_RANGE(voltage_mV, DEVICE_TPS55289_MIN_VOLTAGE_MV, DEVICE_TPS55289_MAX_VOLTAGE_MV);
  ctx->last_voltage_mV = voltage_mV;
  SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_voltage(hw, voltage_mV), ctx);
  return NULL;
}

static SE_MUST_USE err_h contract_vreg_tps55289_set_current(void* device_handle, uint32_t current_mA) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tps_adapter_ctx_t, tps55289_handle_t, ctx, hw, device_handle);
  SE_CHECK_IN_RANGE(current_mA, DEVICE_TPS55289_MIN_CURRENT_MA, DEVICE_TPS55289_MAX_CURRENT_MA);
  ctx->last_current_limit_mA = current_mA;
  SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_current_limit(hw, ctx->is_current_limit_enabled, current_mA), ctx);
  return NULL;
}

static const sys_power_vreg_contract_t s_tps55289_vreg_contract = {.set_enable = contract_vreg_tps55289_set_enable, .set_voltage = contract_vreg_tps55289_set_voltage, .set_current = contract_vreg_tps55289_set_current};

// --- sys_device VTable Implementations ---
static SE_MUST_USE err_h device_uninstall(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tps_adapter_ctx_t, tps55289_handle_t, ctx, hw, handle);
  err_h err = NULL;

  IF_SYS_DEV_STEP_DONE(ctx, TPS_STEP_EN_READY) {
    SYS_DEV_TEARDOWN_STEP(err, sys_io_unlock_pin(ctx->cfg.en_pin));
    SYS_DEV_TEARDOWN_STEP(err, sys_io_set_level(ctx->cfg.en_pin, false));
    SYS_DEV_TEARDOWN_STEP(err, sys_io_reset(ctx->cfg.en_pin));
  }
  IF_SYS_DEV_STEP_DONE(ctx, TPS_STEP_INTR_SUB) {
    SYS_DEV_TEARDOWN_STEP(err, sys_event_unsubscribe(ctx->intr_sub, false));
  }
  IF_SYS_DEV_STEP_DONE(ctx, TPS_STEP_INTR_READY) {
    SYS_DEV_TEARDOWN_STEP(err, sys_io_unlock_pin(ctx->cfg.intr_pin));
    SYS_DEV_TEARDOWN_STEP(err, sys_io_reset(ctx->cfg.intr_pin));
  }
  if (ctx->base.hw_handle) {
    IF_SYS_DEV_STEP_DONE(ctx, TPS_STEP_I2C_ADDED) {
      SYS_DEV_TEARDOWN_STEP(err, sys_i2c_remove_driver(ctx->base.hw_handle));
    }
    tps55289_delete(hw);
  }

  free(ctx);
  return err;
}

static SE_MUST_USE err_h device_reset(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tps_adapter_ctx_t, tps55289_handle_t, ctx, hw, handle);

  SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_output_enable(hw, false), ctx);
  SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_current_limit(hw, true, 100), ctx);
  SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_voltage(hw, 5000), ctx);
  return NULL;
}

// Fault safe state: EN is driven low even if the I2C disable fails.
static SE_MUST_USE err_h device_suspend(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tps_adapter_ctx_t, tps55289_handle_t, ctx, hw, handle);
  err_h err = NULL;

  SYS_DEV_TEARDOWN_DRIVER_STEP(err, tps55289_set_output_enable(hw, false), ctx);
  if (sys_io_pin_is_valid(ctx->cfg.en_pin)) {
    SYS_DEV_TEARDOWN_STEP(err, sys_io_set_locked_level(ctx->cfg.en_pin, false));
  }

  return err;
}

static SE_MUST_USE err_h device_resume(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tps_adapter_ctx_t, tps55289_handle_t, ctx, hw, handle);

  if (sys_io_pin_is_valid(ctx->cfg.en_pin)) {
    SYS_DEV_TRY(sys_io_set_locked_level(ctx->cfg.en_pin, true), ctx);
  }
  SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_output_enable(hw, ctx->last_enable_state), ctx);
  SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_voltage(hw, ctx->last_voltage_mV), ctx);
  SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_current_limit(hw, ctx->is_current_limit_enabled, ctx->last_current_limit_mA), ctx);

  return NULL;
}

static SE_MUST_USE err_h device_install(const void* cfg_blob, void** out_device_handle) {
  const d_tps55289_cfg_t* cfg = (const d_tps55289_cfg_t*)cfg_blob;
  SE_CHECK_IN_RANGE(cfg->i2c_addr, TPS55289_I2C_ADDR_74, TPS55289_I2C_ADDR_75);

  SYS_DEV_CTX_NEW(tps_adapter_ctx_t, ctx, cfg);
  err_h err = NULL;

  ctx->base.hw_handle = tps55289_new(ctx->cfg.i2c_addr, ctx->cfg.i2c_bus);
  if (!ctx->base.hw_handle) {
    free(ctx);
    SE_FAIL(ERR_BASE_NO_MEM, 0);
  }

  ctx->last_voltage_mV = 5000;
  ctx->last_current_limit_mA = 100;
  ctx->last_enable_state = false;
  ctx->is_current_limit_enabled = true;

  tps55289_handle_t hw = (tps55289_handle_t)(ctx->base.hw_handle);

  if (sys_io_pin_is_valid(ctx->cfg.en_pin)) {
    SYS_DEV_INSTALL_STEP(sys_io_set_mode(ctx->cfg.en_pin), "en pin mode");
    SYS_DEV_INSTALL_STEP(sys_io_set_level(ctx->cfg.en_pin, true), "en pin high");
    SYS_DEV_INSTALL_STEP(sys_io_lock_pin(ctx->cfg.en_pin), "en pin lock");
    SYS_DEV_STEP_DONE(ctx, TPS_STEP_EN_READY);
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  SYS_DEV_INSTALL_STEP(sys_i2c_add_driver(hw), "i2c add driver");
  SYS_DEV_STEP_DONE(ctx, TPS_STEP_I2C_ADDED);

  // EN must be driven high before the I2C probe below - the TPS55289's I2C
  // interface is unavailable while EN is low/floating, so probing first
  // would always fail with ERR_I2C_DEV_NOT_FOUND on a fresh boot. The delay
  // matches the settling time contract_vreg_tps55289_set_enable() already
  // waits after driving this same pin high at runtime.

  // Configure interrupt pin & callback
  if (sys_io_pin_is_valid(ctx->cfg.intr_pin)) {
    SYS_DEV_INSTALL_STEP(sys_io_set_mode(ctx->cfg.intr_pin), "intr pin mode");
    SYS_DEV_INSTALL_STEP(sys_io_subscribe_pin(ctx->cfg.intr_pin, device_event_handler, ctx, &ctx->intr_sub), "intr pin subscribe");
    SYS_DEV_STEP_DONE(ctx, TPS_STEP_INTR_SUB);
    sys_io_intr_config_t intr_cfg = {.mode = SYS_IO_INTR_MODE_FALLING_EDGE};
    SYS_DEV_INSTALL_STEP(sys_io_configure_intr(ctx->cfg.intr_pin, &intr_cfg), "intr pin configure");
    SYS_DEV_INSTALL_STEP(sys_io_lock_pin(ctx->cfg.intr_pin), "intr pin lock");
    SYS_DEV_STEP_DONE(ctx, TPS_STEP_INTR_READY);
    // Every fault raises the alert; listeners decide what matters.
    SYS_DEV_INSTALL_STEP(SE_CONVERT_ESP(tps55289_set_fault_masks(hw, false, false, false)), "unmask faults");
  }

  // Apply defaults
  SYS_DEV_INSTALL_STEP(SE_CONVERT_ESP(tps55289_set_output_enable(hw, false)), "tps set output enable");
  SYS_DEV_INSTALL_STEP(SE_CONVERT_ESP(tps55289_set_current_limit(hw, true, 100)), "tps set current limit");
  SYS_DEV_INSTALL_STEP(SE_CONVERT_ESP(tps55289_set_voltage(hw, 5000)), "tps set voltage");

  *out_device_handle = ctx;
  return NULL;

fail:
  SYS_DEV_INSTALL_FAIL(err, cfg->device_id, out_device_handle, device_uninstall, ctx);
  return NULL;
}

static const sys_device_class_t s_tps55289_class = {
    .name = "TPS55289_VREG",
    .contracts = {[SYS_DEVICE_CONTRACT_POWER_VREG] = &s_tps55289_vreg_contract},
    .ops = {.install = device_install, .uninstall = device_uninstall, .reset = device_reset, .suspend = device_suspend, .resume = device_resume},
};

err_h d_tps55289_create(const d_tps55289_cfg_t* cfg) {
  SE_CHECK_NOT_NULL(cfg);
  return SYS_DEVICE_CREATE(&s_tps55289_class, cfg);
}
// 284
