#include <stdlib.h>
#include "device_ina3221.h"
#include "driver_ina3221.h"
#include "esp_log.h"
#include "status.h"
#include "sys_device.h"
#include "sys_i2c.h"
#include "sys_io.h"
#include "sys_power.h"

#undef OWNER
#define OWNER OWNER_DEVICE_INA3221

typedef struct {
  sys_device_adapter_base_t base;

  uint8_t crit_gpio_device_id;
  sys_io_pin_num_t crit_gpio_pin_num;
  uint8_t warn_gpio_device_id;
  sys_io_pin_num_t warn_gpio_pin_num;

  // Cached readings when frozen
  int32_t cached_voltage[3];
  int32_t cached_current[3];

  void (*power_callback_crit[3])(uint8_t device_id, sys_power_events_e triggered_by);
  void (*power_callback_warn[3])(uint8_t device_id, sys_power_events_e triggered_by);
} ina_adapter_ctx_t;

static void ina3221_adapter_isr(void* arg) {
  ina_adapter_ctx_t* ctx = (ina_adapter_ctx_t*)arg;
  if (!ctx) return;
  ina3221_handle_t hw = (ina3221_handle_t)(ctx->base.hw_handle);
  if (!hw) return;

  if (ina3221_get_status(hw) != ESP_OK) return;

  ina3221_mask_t mask = hw->mask;

  for (int ch = 0; ch < 3; ch++) {
    bool has_crit = false;
    bool has_warn = false;
    if (ch == 0) {
      has_crit = (mask.cf & 0x04) != 0;
      has_warn = (mask.wf & 0x04) != 0;
    } else if (ch == 1) {
      has_crit = (mask.cf & 0x02) != 0;
      has_warn = (mask.wf & 0x02) != 0;
    } else {
      has_crit = (mask.cf & 0x01) != 0;
      has_warn = (mask.wf & 0x01) != 0;
    }

    if (has_crit && ctx->power_callback_crit[ch]) {
      ctx->power_callback_crit[ch](ctx->base.device_id, SYS_PWR_EVENT_OCP_CRITICAL);
    }
    if (has_warn && ctx->power_callback_warn[ch]) {
      ctx->power_callback_warn[ch](ctx->base.device_id, SYS_PWR_EVENT_OCP_WARNING);
    }
  }
}

// --- sys_power_monitor_contract Implementations ---
static status_rep_t contract_monitor_ina3221_get_voltage(void* device_handle, uint8_t channel, int32_t* out_mV) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ina_adapter_ctx_t, ina3221_handle_t, ctx, hw, device_handle);
  CHECK_HANDLE_R(out_mV);
  if (channel >= 3) return STA_C(ERR_INVALID_ARG, OWNER, channel, STATUS_PAYLOAD_DEVICE);

  IF_SYS_DEV_FROZEN(ctx) {
    *out_mV = ctx->cached_voltage[channel];
    return STA_OK;
  }

  float mv_val = 0;
  SYS_DEV_CHECK_DRIVER_CALL(ina3221_read_bus_voltage(hw, channel, &mv_val), ctx);
  *out_mV = (int32_t)mv_val;
  return STA_OK;
}

static status_rep_t contract_monitor_ina3221_get_current(void* device_handle, uint8_t channel, int32_t* out_mA) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ina_adapter_ctx_t, ina3221_handle_t, ctx, hw, device_handle);
  CHECK_HANDLE_R(out_mA);
  if (channel >= 3) return STA_C(ERR_INVALID_ARG, OWNER, channel, STATUS_PAYLOAD_DEVICE);

  IF_SYS_DEV_FROZEN(ctx) {
    *out_mA = ctx->cached_current[channel];
    return STA_OK;
  }

  float ma_val = 0;
  SYS_DEV_CHECK_DRIVER_CALL(ina3221_read_shunt_current(hw, channel, &ma_val), ctx);
  *out_mA = (int32_t)ma_val;
  return STA_OK;
}

static status_rep_t contract_monitor_ina3221_add_callback(void* device_handle, uint8_t channel, int32_t trigger_value, sys_power_events_e on_event, void (*callback)(uint8_t device_id, sys_power_events_e triggered_by)) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ina_adapter_ctx_t, ina3221_handle_t, ctx, hw, device_handle);
  if (channel >= 3) return STA_C(ERR_INVALID_ARG, OWNER, channel, STATUS_PAYLOAD_DEVICE);

  if (on_event == SYS_PWR_EVENT_OCP_CRITICAL) {
    ctx->power_callback_crit[channel] = callback;
    SYS_DEV_CHECK_DRIVER_CALL(ina3221_set_alert(hw, channel, trigger_value, true), ctx);
  } else if (on_event == SYS_PWR_EVENT_OCP_WARNING) {
    ctx->power_callback_warn[channel] = callback;
    SYS_DEV_CHECK_DRIVER_CALL(ina3221_set_alert(hw, channel, trigger_value, false), ctx);
  } else {
    return STA_C(ERR_SYS_IO_FEATURE_UNAVAILABLE, OWNER, on_event, STATUS_PAYLOAD_DEVICE);
  }

  return STA_OK;
}

static const sys_power_monitor_contract s_ina_monitor_contract = {.get_voltage = contract_monitor_ina3221_get_voltage, .get_current = contract_monitor_ina3221_get_current, .add_callback = contract_monitor_ina3221_add_callback};

// --- sys_device_t VTable Implementations ---
static status_rep_t device_uninstall(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ina_adapter_ctx_t, ina3221_handle_t, ctx, hw, handle);
  status_rep_t status = STA_OK;

  IF_PIN(ctx->crit_gpio_pin_num) {
    status_rep_t r = sys_io_reset(ctx->crit_gpio_device_id, ctx->crit_gpio_pin_num);
    if (STA_IS_ERR(r)) status = r;
  }
  IF_PIN(ctx->warn_gpio_pin_num) {
    status_rep_t r = sys_io_reset(ctx->warn_gpio_device_id, ctx->warn_gpio_pin_num);
    if (STA_IS_ERR(r)) status = r;
  }

  status_rep_t r = sys_power_unregister(ctx->base.device_id);
  if (STA_IS_ERR(r)) status = r;
  r = sys_i2c_remove_driver(ctx->base.hw_handle);
  if (STA_IS_ERR(r)) status = r;

  ina3221_delete(hw);
  free(ctx);
  return status;
}

static status_rep_t device_reset(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ina_adapter_ctx_t, ina3221_handle_t, ctx, hw, handle);

  SYS_DEV_CHECK_DRIVER_CALL(ina3221_reset(hw), ctx);
  // Enable latches & options (Warning & Critical alert latch)
  SYS_DEV_CHECK_DRIVER_CALL(ina3221_enable_latch_pin(hw, true, true), ctx);
  SYS_DEV_CHECK_DRIVER_CALL(ina3221_set_options(hw, true, true, true), ctx);

  for (int i = 0; i < 3; i++) {
    ctx->power_callback_crit[i] = NULL;
    ctx->power_callback_warn[i] = NULL;
  }
  return STA_OK;
}

static status_rep_t device_suspend(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ina_adapter_ctx_t, ina3221_handle_t, ctx, hw, handle);
  // Put INA3221 into power-down mode (mode = 0 in config)
  SYS_DEV_CHECK_DRIVER_CALL(ina3221_set_options(hw, false, false, false), ctx);
  return STA_OK;
}

static status_rep_t device_resume(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ina_adapter_ctx_t, ina3221_handle_t, ctx, hw, handle);
  // Put INA3221 back into continuous mode (mode = 1)
  SYS_DEV_CHECK_DRIVER_CALL(ina3221_set_options(hw, true, true, true), ctx);
  return STA_OK;
}

static status_rep_t device_freeze(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ina_adapter_ctx_t, ina3221_handle_t, ctx, hw, handle);
  IF_SYS_DEV_FROZEN(ctx) { return STA_OK; }
  SYS_DEV_CTX_FREEZE(ctx);
  for (int i = 0; i < 3; i++) {
    float mv = 0;
    float ma = 0;
    SYS_DEV_CHECK_DRIVER_CALL(ina3221_read_bus_voltage(hw, i, &mv), ctx);
    SYS_DEV_CHECK_DRIVER_CALL(ina3221_read_shunt_current(hw, i, &ma), ctx);
    ctx->cached_voltage[i] = (int32_t)mv;
    ctx->cached_current[i] = (int32_t)ma;
  }
  return STA_OK;
}

static status_rep_t device_sync(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ina_adapter_ctx_t, ina3221_handle_t, ctx, hw, handle);
  SYS_DEV_CTX_UNFREEZE(ctx);
  return STA_OK;
}

static status_rep_t device_error_handler(void* handle, status_rep_t* error) {
  return device_reset(handle);
}

static void* device_install(void** args) {
  SYS_DEV_ARG_UNPACK(uint8_t, device_id, args, 0);
  SYS_DEV_ARG_UNPACK(bool, i2c_bus, args, 1);
  SYS_DEV_ARG_UNPACK(uint8_t, i2c_addr, args, 2);
  SYS_DEV_ARG_UNPACK(uint8_t, crit_io_device, args, 3);
  SYS_DEV_ARG_UNPACK(sys_io_pin_num_t, crit_io_num, args, 4);
  SYS_DEV_ARG_UNPACK(sys_io_mode_e, crit_io_mode, args, 5);
  SYS_DEV_ARG_UNPACK(uint8_t, warn_io_device, args, 6);
  SYS_DEV_ARG_UNPACK(sys_io_pin_num_t, warn_io_num, args, 7);
  SYS_DEV_ARG_UNPACK(sys_io_mode_e, warn_io_mode, args, 8);

  ina_adapter_ctx_t* ctx = sys_device_allocate_ctx(sizeof(ina_adapter_ctx_t), args);
  if (!ctx) return NULL;

  ctx->crit_gpio_device_id = crit_io_device;
  ctx->crit_gpio_pin_num = crit_io_num;
  ctx->warn_gpio_device_id = warn_io_device;
  ctx->warn_gpio_pin_num = warn_io_num;

  ctx->base.hw_handle = ina3221_new(i2c_addr, i2c_bus);
  if (!ctx->base.hw_handle) {
    free(ctx);
    return NULL;
  }

  if (STA_IS_ERR(sys_i2c_add_driver(ctx->base.hw_handle))) {
    goto fail;
  }

  ina3221_handle_t hw = (ina3221_handle_t)(ctx->base.hw_handle);
  if (ina3221_start(hw) != ESP_OK) {
    goto fail;
  }

  // Configure critical alert interrupt pin
  IF_PIN(crit_io_num) {
    if (STA_IS_ERR(sys_io_set_mode(crit_io_device, crit_io_num, crit_io_mode))) {
      goto fail;
    }
    sys_io_intr_config_t intr_cfg = {.mode = SYS_IO_INTR_MODE_FALLING_EDGE, .callback = (sys_io_isr_callback_t)(void*)ina3221_adapter_isr, .user_ctx = ctx};
    if (STA_IS_ERR(sys_io_configure_intr(crit_io_device, crit_io_num, &intr_cfg))) {
      goto fail;
    }
  }

  // Configure warning alert interrupt pin
  IF_PIN(warn_io_num) {
    if (STA_IS_ERR(sys_io_set_mode(warn_io_device, warn_io_num, warn_io_mode))) {
      goto fail;
    }
    sys_io_intr_config_t intr_cfg = {.mode = SYS_IO_INTR_MODE_FALLING_EDGE, .callback = (sys_io_isr_callback_t)(void*)ina3221_adapter_isr, .user_ctx = ctx};
    if (STA_IS_ERR(sys_io_configure_intr(warn_io_device, warn_io_num, &intr_cfg))) {
      goto fail;
    }
  }

  // Register with sys_power
  if (STA_IS_ERR(sys_power_register_monitor(device_id, ctx, &s_ina_monitor_contract))) {
    goto fail;
  }

  // Initialize INA3221 defaults
  ina3221_reset(hw);
  ina3221_enable_latch_pin(hw, true, true);
  ina3221_set_options(hw, true, true, true);

  return ctx;

fail:
  device_uninstall(ctx);
  return NULL;
}

// --- Exposed Initialization API ---
status_rep_t d_ina3221_create(uint8_t device_id, bool i2c_bus, uint8_t i2c_addr, uint8_t crit_io_device, sys_io_pin_num_t crit_io_num, sys_io_mode_e crit_io_mode, uint8_t warn_io_device, sys_io_pin_num_t warn_io_num, sys_io_mode_e warn_io_mode) {
  void* args[] = {SYS_DEV_ARG_PACK(device_id), SYS_DEV_ARG_PACK(i2c_bus), SYS_DEV_ARG_PACK(i2c_addr), SYS_DEV_ARG_PACK(crit_io_device), SYS_DEV_ARG_PACK(crit_io_num), SYS_DEV_ARG_PACK(crit_io_mode), SYS_DEV_ARG_PACK(warn_io_device), SYS_DEV_ARG_PACK(warn_io_num), SYS_DEV_ARG_PACK(warn_io_mode)};

  sys_device_t dev = {.device_id = device_id,
      .role = SYS_DEV_ROLE_PWR,
      .name = "INA3221_PWR_MONITOR",
      .install_args = args,
      .install_device = device_install,
      .uninstall_device = device_uninstall,
      .reset_device = device_reset,
      .error_handler = device_error_handler,
      .suspend_device = device_suspend,
      .resume_device = device_resume,
      .freeze_device = device_freeze,
      .sync_device = device_sync};

  return sys_device_install(&dev);
}