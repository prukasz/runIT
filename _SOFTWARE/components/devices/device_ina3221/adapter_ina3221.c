// INA3221 device adapter implementation
#include "device_ina3221.h"
#include "driver_ina3221.h"
#include "esp_log.h"
#include "sys_device.h"
#include "sys_i2c.h"
#include "sys_io.h"
#include "sys_power.h"

static const char* TAG = __FILE_NAME__;
#undef OWNER
#define OWNER OWNER_DEVICE_INA3221

typedef struct {
  sys_device_adapter_base_t base;

  d_ina3221_cfg_t cfg;

  // Cached readings when frozen
  int32_t cached_voltage[3];
  int32_t cached_current[3];

  uint16_t route_masks_crit[3];
  uint16_t route_masks_warn[3];
} ina_adapter_ctx_t;

enum { INA_STEP_I2C_ADDED = 0, INA_STEP_CRIT_READY = 1, INA_STEP_WARN_READY = 2 };

static err_h device_event_handler(void* handle, cb_event_t* event);

static err_h contract_monitor_ina3221_get_voltage(void* device_handle, uint8_t channel, int32_t* out_mV) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ina_adapter_ctx_t, ina3221_handle_t, ctx, hw, device_handle);
  SE_CHECK_HANDLE(out_mV);
  SE_CHECK_IN_RANGE(channel, 0, 2);

  IF_SYS_DEV_FROZEN(ctx) {
    *out_mV = ctx->cached_voltage[channel];
    return NULL;
  }
  SYS_DEV_CHECK_DRIVER_CALL(ina3221_read_bus_voltage(hw, channel, out_mV), ctx);
  return NULL;
}

static err_h contract_monitor_ina3221_get_current(void* device_handle, uint8_t channel, int32_t* out_mA) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ina_adapter_ctx_t, ina3221_handle_t, ctx, hw, device_handle);
  SE_CHECK_HANDLE(out_mA);
  SE_CHECK_IN_RANGE(channel, 0, 2);

  IF_SYS_DEV_FROZEN(ctx) {
    *out_mA = ctx->cached_current[channel];
    return NULL;
  }
  SYS_DEV_CHECK_DRIVER_CALL(ina3221_read_shunt_current(hw, channel, out_mA), ctx);
  return NULL;
}

static err_h contract_monitor_ina3221_add_callback(void* device_handle, uint8_t channel, int32_t trigger_value, sys_power_events_e on_event, uint16_t route_mask) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ina_adapter_ctx_t, ina3221_handle_t, ctx, hw, device_handle);
  SE_CHECK_IN_RANGE(channel, 0, 2);

  if (on_event == SYS_PWR_EVENT_OCP_CRITICAL) {
    ctx->route_masks_crit[channel] = route_mask;
    SYS_DEV_CHECK_DRIVER_CALL(ina3221_set_alert(hw, channel, trigger_value, true), ctx);
  } else if (on_event == SYS_PWR_EVENT_OCP_WARNING) {
    ctx->route_masks_warn[channel] = route_mask;
    SYS_DEV_CHECK_DRIVER_CALL(ina3221_set_alert(hw, channel, trigger_value, false), ctx);
  } else {
    SE_RET_ERR(ERR_DEV_FEATURE_UNAVAILABLE, SYS_DEV_GET_ID(ctx), 0, on_event);
  }

  return NULL;
}

static const sys_power_monitor_contract s_ina_monitor_contract = {.get_voltage = contract_monitor_ina3221_get_voltage, .get_current = contract_monitor_ina3221_get_current, .add_callback = contract_monitor_ina3221_add_callback};

// --- sys_device_t VTable Implementations ---
static err_h device_uninstall(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ina_adapter_ctx_t, ina3221_handle_t, ctx, hw, handle);
  err_h err = NULL;

  IF_SYS_DEV_STEP_DONE(ctx, INA_STEP_CRIT_READY) {
    SYS_IO_REF_UNLOCK(ctx->cfg.crit_pin);
    SYS_DEV_TEARDOWN_STEP(err, SYS_IO_REF_RESET(ctx->cfg.crit_pin));
  }
  IF_SYS_DEV_STEP_DONE(ctx, INA_STEP_WARN_READY) {
    SYS_IO_REF_UNLOCK(ctx->cfg.warn_pin);
    SYS_DEV_TEARDOWN_STEP(err, SYS_IO_REF_RESET(ctx->cfg.warn_pin));
  }
  if (ctx->base.hw_handle) {
    IF_SYS_DEV_STEP_DONE(ctx, INA_STEP_I2C_ADDED) {
      SYS_DEV_TEARDOWN_STEP(err, sys_i2c_remove_driver(ctx->base.hw_handle));
    }
    ina3221_delete(hw);
  }
  free(ctx);
  return err;
}

static err_h device_reset(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ina_adapter_ctx_t, ina3221_handle_t, ctx, hw, handle);

  SYS_DEV_CHECK_DRIVER_CALL(ina3221_reset(hw), ctx);
  // Enable latches & options (Warning & Critical alert latch)
  SYS_DEV_CHECK_DRIVER_CALL(ina3221_enable_latch_pin(hw, true, true), ctx);
  SYS_DEV_CHECK_DRIVER_CALL(ina3221_set_options(hw, true, true, true), ctx);

  for (uint8_t i = 0; i < 3; i++) {
    ctx->route_masks_crit[i] = 0;
    ctx->route_masks_warn[i] = 0;
    ctx->cached_current[i] = 0;
    ctx->cached_voltage[i] = 0;
  }
  return NULL;
}

static err_h device_suspend(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ina_adapter_ctx_t, ina3221_handle_t, ctx, hw, handle);
  // Put INA3221 into power-down mode (mode = 0 in config)
  SYS_DEV_CHECK_DRIVER_CALL(ina3221_set_options(hw, false, false, false), ctx);
  return NULL;
}

static err_h device_resume(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ina_adapter_ctx_t, ina3221_handle_t, ctx, hw, handle);
  // Put INA3221 back into continuous mode (mode = 1)
  SYS_DEV_CHECK_DRIVER_CALL(ina3221_set_options(hw, true, true, true), ctx);
  return NULL;
}

static err_h device_freeze(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ina_adapter_ctx_t, ina3221_handle_t, ctx, hw, handle);
  IF_SYS_DEV_FROZEN(ctx) {
    return NULL;
  }
  SYS_DEV_CTX_FREEZE(ctx);
  for (int i = 0; i < 3; i++) {
    SYS_DEV_CHECK_DRIVER_CALL(ina3221_read_bus_voltage(hw, i, &ctx->cached_voltage[i]), ctx);
    SYS_DEV_CHECK_DRIVER_CALL(ina3221_read_shunt_current(hw, i, &ctx->cached_current[i]), ctx);
  }
  return NULL;
}

static err_h device_sync(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ina_adapter_ctx_t, ina3221_handle_t, ctx, hw, handle);
  SYS_DEV_CTX_UNFREEZE(ctx);
  return NULL;
}

static err_h device_error_handler(void* handle, err_h error) {
  (void)device_reset(handle);
  return NULL;
}

static err_h device_install(const void* cfg_blob, void** out_device_handle) {
  const d_ina3221_cfg_t* cfg = (const d_ina3221_cfg_t*)cfg_blob;
  SE_CHECK_NOT_NULL(cfg);
  SE_CHECK_NOT_NULL(out_device_handle);

  SYS_DEV_CTX_NEW(ina_adapter_ctx_t, ctx, cfg);
  err_h err = NULL;

  ctx->base.hw_handle = ina3221_new(ctx->cfg.i2c_addr, ctx->cfg.i2c_bus);
  if (!ctx->base.hw_handle) {
    free(ctx);
    SE_RET_ERR(ERR_DEV_NO_HANDLE, cfg->device_id);
  }

  ina3221_handle_t hw = (ina3221_handle_t)(ctx->base.hw_handle);

  SYS_DEV_INSTALL_STEP(sys_i2c_add_driver(ctx->base.hw_handle), "i2c add driver");
  SYS_DEV_STEP_DONE(ctx, INA_STEP_I2C_ADDED);

  SYS_DEV_INSTALL_STEP(sys_i2c_device_present(ctx->base.hw_handle), "probe i2c device");
  SYS_DEV_INSTALL_STEP(SE_CONVERT_ESP(ina3221_start(hw)), "start ina3221");

  // Configure critical alert interrupt pin
  IF_PIN_REF(ctx->cfg.crit_pin) {
    SYS_DEV_INSTALL_STEP(SYS_IO_REF_SET_MODE(ctx->cfg.crit_pin), "crit pin mode");
    sys_io_intr_config_t intr_cfg = {
        .mode = SYS_IO_INTR_MODE_FALLING_EDGE,
        .own_func = {.own_func = device_event_handler, .device_handle = ctx},
    };
    SYS_DEV_INSTALL_STEP(sys_io_configure_intr(ctx->cfg.crit_pin.device_id, ctx->cfg.crit_pin.pin, &intr_cfg), "crit pin intr");
    SYS_IO_REF_LOCK(ctx->cfg.crit_pin);
    SYS_DEV_STEP_DONE(ctx, INA_STEP_CRIT_READY);
  }

  // Configure warning alert interrupt pin
  IF_PIN_REF(ctx->cfg.warn_pin) {
    SYS_DEV_INSTALL_STEP(SYS_IO_REF_SET_MODE(ctx->cfg.warn_pin), "warn pin mode");
    sys_io_intr_config_t intr_cfg = {
        .mode = SYS_IO_INTR_MODE_FALLING_EDGE,
        .own_func = {.own_func = device_event_handler, .device_handle = ctx},
    };
    SYS_DEV_INSTALL_STEP(sys_io_configure_intr(ctx->cfg.warn_pin.device_id, ctx->cfg.warn_pin.pin, &intr_cfg), "warn pin intr");
    SYS_IO_REF_LOCK(ctx->cfg.warn_pin);
    SYS_DEV_STEP_DONE(ctx, INA_STEP_WARN_READY);
  }

  // Initialize INA3221 defaults
  SYS_DEV_INSTALL_STEP(SE_CONVERT_ESP(ina3221_reset(hw)), "ina3221 reset");
  SYS_DEV_INSTALL_STEP(SE_CONVERT_ESP(ina3221_enable_latch_pin(hw, true, true)), "ina3221 enable latch");
  SYS_DEV_INSTALL_STEP(SE_CONVERT_ESP(ina3221_set_options(hw, true, true, true)), "ina3221 set options");

  *out_device_handle = ctx;
  return NULL;

fail:
  SYS_DEV_INSTALL_FAIL(err, cfg->device_id, out_device_handle, device_uninstall, ctx);
  return NULL;
}

static err_h device_event_handler(void* handle, cb_event_t* event) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ina_adapter_ctx_t, ina3221_handle_t, ctx, hw, handle);
  // Read and clear alert flags from the mask/status register
  SYS_DEV_CHECK_DRIVER_CALL(ina3221_get_status(hw), ctx);
  // Check critical alert flags
  uint8_t cf = hw->mask.cf;
  for (uint8_t ch = 0; ch < 3; ch++) {
    if (((cf >> (2 - ch)) & 1)) {
      int32_t ma_val = 0;
      ina3221_read_shunt_current(hw, ch, &ma_val);
      SYS_PWR_CB(ctx, ch, SYS_PWR_EVENT_OCP_CRITICAL, ma_val, ctx->route_masks_crit[ch]);
    }
  }
  // Check warning alert flags
  uint8_t wf = hw->mask.wf;
  for (uint8_t ch = 0; ch < 3; ch++) {
    if (((wf >> (2 - ch)) & 1)) {
      int32_t ma_val = 0;
      ina3221_read_shunt_current(hw, ch, &ma_val);
      SYS_PWR_CB(ctx, ch, SYS_PWR_EVENT_OCP_WARNING, ma_val, ctx->route_masks_warn[ch]);
    }
  }
  return NULL;
}

static const sys_device_class_t s_ina3221_class = {
    .name = "INA3221_PWR_MONITOR",
    .contracts = {[SYS_DEVICE_CONTRACT_POWER_MONITOR] = (void*)&s_ina_monitor_contract},
    .ops = {.install = device_install, .uninstall = device_uninstall, .reset = device_reset, .suspend = device_suspend, .resume = device_resume, .freeze = device_freeze, .sync = device_sync, .error_handler = device_error_handler},
};

// --- Exposed Initialization API ---
err_h d_ina3221_create(const d_ina3221_cfg_t* cfg) {
  SE_CHECK_NOT_NULL(cfg);
  return SYS_DEVICE_CREATE(&s_ina3221_class, cfg);
}
// 291