#include <stdlib.h>
#include "device_tps55289.h"
#include "driver_tps55289.h"
#include "esp_log.h"
#include "sys_device.h"
#include "sys_error.h"
#include "sys_i2c.h"
#include "sys_io.h"
#include "sys_power.h"

static const char* TAG = __FILE_NAME__;
#undef OWNER
#define OWNER OWNER_DEVICE_TPS55289

typedef struct {
  sys_device_adapter_base_t base;

  d_tps55289_cfg_t cfg;

  uint16_t last_voltage_mv;
  uint16_t last_current_limit_ma;
  bool last_enable_state;
  bool is_current_limit_enabled;

  uint16_t route_masks[3];   // For OVP, OCP, SCP
  uint64_t action_masks[3];  // For OVP, OCP, SCP
} tps_adapter_ctx_t;

enum { TPS_STEP_I2C_ADDED = 0, TPS_STEP_EN_READY = 1, TPS_STEP_INTR_READY = 2 };

static err_h device_event_handler(void* handle, cb_event_t* event) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tps_adapter_ctx_t, tps55289_handle_t, ctx, hw, handle);
  SYS_DEV_CHECK_DRIVER_CALL(tps55289_get_status(hw), ctx);

  if (hw->last_status.ovp) {
    SYS_PWR_CB(ctx, 0, SYS_PWR_EVENT_OVP, ctx->last_voltage_mv, ctx->route_masks[0], ctx->action_masks[0]);
  }
  if (hw->last_status.ocp) {
    SYS_PWR_CB(ctx, 0, SYS_PWR_EVENT_OCP_CRITICAL, ctx->last_current_limit_ma, ctx->route_masks[1], ctx->action_masks[1]);
  }
  if (hw->last_status.scp) {
    SYS_PWR_CB(ctx, 0, SYS_PWR_EVENT_SPC, 0, ctx->route_masks[2], ctx->action_masks[2]);
  }
  return NULL;
}

// --- VREG Contract Implementations ---
static err_h contract_vreg_tps55289_set_enable(void* device_handle, bool state) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tps_adapter_ctx_t, tps55289_handle_t, ctx, hw, device_handle);
  ctx->last_enable_state = state;

  if (state) {
    IF_PIN_REF(ctx->cfg.en_pin) {
      WITH_REF_UNLOCKED(ctx->cfg.en_pin) {
        RET_IF_DEV_ERR(sys_io_set_level(ctx->cfg.en_pin.device_id, ctx->cfg.en_pin.pin, state), ctx);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_output_enable(hw, state), ctx);
  } else {
    SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_output_enable(hw, state), ctx);
    IF_PIN_REF(ctx->cfg.en_pin) {
      WITH_REF_UNLOCKED(ctx->cfg.en_pin) {
        RET_IF_DEV_ERR(sys_io_set_level(ctx->cfg.en_pin.device_id, ctx->cfg.en_pin.pin, state), ctx);
      }
    }
  }

  return NULL;
}

static err_h contract_vreg_tps55289_set_voltage(void* device_handle, uint32_t voltage_mV) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tps_adapter_ctx_t, tps55289_handle_t, ctx, hw, device_handle);
  SE_CHECK_IN_RANGE(voltage_mV, DEVICE_TPS55289_MIN_VOLTAGE_MV, DEVICE_TPS55289_MAX_VOLTAGE_MV);
  ctx->last_voltage_mv = voltage_mV;
  SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_voltage(hw, voltage_mV), ctx);
  return NULL;
}

static err_h contract_vreg_tps55289_set_current(void* device_handle, uint32_t current_mA) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tps_adapter_ctx_t, tps55289_handle_t, ctx, hw, device_handle);
  SE_CHECK_IN_RANGE(current_mA, DEVICE_TPS55289_MIN_CURRENT_MA, DEVICE_TPS55289_MAX_CURRENT_MA);
  ctx->last_current_limit_ma = current_mA;
  SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_current_limit(hw, ctx->is_current_limit_enabled, current_mA), ctx);
  return NULL;
}

static err_h contract_vreg_tps55289_add_callback(void* device_handle, sys_power_events_e on_event, uint16_t route_mask, uint64_t action_mask) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tps_adapter_ctx_t, tps55289_handle_t, ctx, hw, device_handle);

  if (on_event == SYS_PWR_EVENT_OVP) {
    ctx->route_masks[0] = route_mask;
    ctx->action_masks[0] = action_mask;
  } else if (on_event == SYS_PWR_EVENT_OCP_CRITICAL) {
    ctx->route_masks[1] = route_mask;
    ctx->action_masks[1] = action_mask;
  } else if (on_event == SYS_PWR_EVENT_SPC) {
    ctx->route_masks[2] = route_mask;
    ctx->action_masks[2] = action_mask;
  }

  tps55289_set_fault_masks(hw, ctx->route_masks[2] == 0, ctx->route_masks[1] == 0, ctx->route_masks[0] == 0);
  return NULL;
}

static const sys_power_vreg_contract s_tps_vreg_contract = {.set_enable = contract_vreg_tps55289_set_enable, .set_voltage = contract_vreg_tps55289_set_voltage, .set_current = contract_vreg_tps55289_set_current, .add_callback = contract_vreg_tps55289_add_callback};

// --- sys_device VTable Implementations ---
static err_h device_uninstall(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tps_adapter_ctx_t, tps55289_handle_t, ctx, hw, handle);
  err_h err = NULL;

  IF_SYS_DEV_STEP_DONE(ctx, TPS_STEP_EN_READY) {
    SYS_IO_REF_UNLOCK(ctx->cfg.en_pin);
    SYS_DEV_TEARDOWN_STEP(err, SYS_IO_REF_LOW(ctx->cfg.en_pin));
    SYS_DEV_TEARDOWN_STEP(err, SYS_IO_REF_RESET(ctx->cfg.en_pin));
  }
  IF_SYS_DEV_STEP_DONE(ctx, TPS_STEP_INTR_READY) {
    SYS_IO_REF_UNLOCK(ctx->cfg.intr_pin);
    SYS_DEV_TEARDOWN_STEP(err, SYS_IO_REF_RESET(ctx->cfg.intr_pin));
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

static err_h device_reset(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tps_adapter_ctx_t, tps55289_handle_t, ctx, hw, handle);

  SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_output_enable(hw, false), ctx);
  SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_current_limit(hw, true, 100), ctx);
  SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_voltage(hw, 5000), ctx);
  return NULL;
}

static err_h device_error_handler(void* handle, err_h error) {
  tps_adapter_ctx_t* ctx = (tps_adapter_ctx_t*)handle;
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

static err_h device_suspend(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tps_adapter_ctx_t, tps55289_handle_t, ctx, hw, handle);

  SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_output_enable(hw, false), ctx);
  IF_PIN_REF(ctx->cfg.en_pin) {
    WITH_REF_UNLOCKED(ctx->cfg.en_pin) {
      RET_IF_DEV_ERR(SYS_IO_REF_LOW(ctx->cfg.en_pin), ctx);
    }
  }

  return NULL;
}

static err_h device_resume(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tps_adapter_ctx_t, tps55289_handle_t, ctx, hw, handle);

  IF_PIN_REF(ctx->cfg.en_pin) {
    WITH_REF_UNLOCKED(ctx->cfg.en_pin) {
      RET_IF_DEV_ERR(SYS_IO_REF_HIGH(ctx->cfg.en_pin), ctx);
    }
  }
  SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_output_enable(hw, ctx->last_enable_state), ctx);
  SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_voltage(hw, ctx->last_voltage_mv), ctx);
  SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_current_limit(hw, ctx->is_current_limit_enabled, ctx->last_current_limit_ma), ctx);

  return NULL;
}

static err_h device_install(const void* cfg_blob, void** out_device_handle) {
  const d_tps55289_cfg_t* cfg = (const d_tps55289_cfg_t*)cfg_blob;
  SE_CHECK_NOT_NULL(cfg);
  SE_CHECK_NOT_NULL(out_device_handle);

  SYS_DEV_CTX_NEW(tps_adapter_ctx_t, ctx, cfg);
  err_h err = NULL;

  ctx->base.hw_handle = tps55289_new(ctx->cfg.i2c_addr, ctx->cfg.i2c_bus);
  if (!ctx->base.hw_handle) {
    free(ctx);
    SE_RET_ERR(ERR_BASE_NO_MEM, 0);
  }

  ctx->last_voltage_mv = 5000;
  ctx->last_current_limit_ma = 100;
  ctx->last_enable_state = false;
  ctx->is_current_limit_enabled = true;

  tps55289_handle_t hw = (tps55289_handle_t)(ctx->base.hw_handle);

  SYS_DEV_INSTALL_STEP(sys_i2c_add_driver(hw), "i2c add driver");
  SYS_DEV_STEP_DONE(ctx, TPS_STEP_I2C_ADDED);

  SYS_DEV_INSTALL_STEP(sys_i2c_device_present(hw), "probe i2c device");

  // Configure enable pin
  IF_PIN_REF(ctx->cfg.en_pin) {
    SYS_DEV_INSTALL_STEP(SYS_IO_REF_SET_MODE(ctx->cfg.en_pin), "en pin mode");
    SYS_DEV_INSTALL_STEP(SYS_IO_REF_HIGH(ctx->cfg.en_pin), "en pin high");
    SYS_IO_REF_LOCK(ctx->cfg.en_pin);
    SYS_DEV_STEP_DONE(ctx, TPS_STEP_EN_READY);
  }

  // Configure interrupt pin & callback
  IF_PIN_REF(ctx->cfg.intr_pin) {
    SYS_DEV_INSTALL_STEP(SYS_IO_REF_SET_MODE(ctx->cfg.intr_pin), "intr pin mode");
    sys_io_intr_config_t intr_cfg = {
        .mode = SYS_IO_INTR_MODE_FALLING_EDGE,
        .own_func = {.own_func = device_event_handler, .device_handle = ctx},
    };
    SYS_DEV_INSTALL_STEP(sys_io_configure_intr(ctx->cfg.intr_pin.device_id, ctx->cfg.intr_pin.pin, &intr_cfg), "intr pin configure");
    SYS_IO_REF_LOCK(ctx->cfg.intr_pin);
    SYS_DEV_STEP_DONE(ctx, TPS_STEP_INTR_READY);
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
    .contracts = {[SYS_DEVICE_CONTRACT_POWER_VREG] = (void*)&s_tps_vreg_contract},
    .ops = {
        .install = device_install,
        .uninstall = device_uninstall,
        .reset = device_reset,
        .suspend = device_suspend,
        .resume = device_resume,
        .error_handler = device_error_handler
    },
};

err_h d_tps55289_create(const d_tps55289_cfg_t* cfg) {
  SE_CHECK_NOT_NULL(cfg);
  return SYS_DEVICE_CREATE(&s_tps55289_class, cfg);
}
// 284