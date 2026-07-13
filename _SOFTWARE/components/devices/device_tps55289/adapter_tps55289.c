#include <stdlib.h>
#include "device_tps55289.h"
#include "driver_tps55289.h"
#include "esp_log.h"
#include "status.h"
#include "sys_device.h"
#include "sys_i2c.h"
#include "sys_io.h"
#include "sys_power.h"

static const char* TAG = __FILE_NAME__;
#undef OWNER
#define OWNER OWNER_DEVICE_TPS55289

typedef struct {
  sys_device_adapter_base_t base;

  uint8_t intr_gpio_device_id;
  sys_io_pin_num_t intr_gpio_pin_num;
  uint8_t en_gpio_device_id;
  sys_io_pin_num_t en_gpio_pin_num;

  uint16_t last_voltage_mv;
  uint16_t last_current_limit_ma;
  bool last_enable_state;
  bool is_current_limit_enabled;

  void (*power_callback_ovp)(uint8_t device_id, sys_power_events_e triggered_by);
  void (*power_callback_ocp)(uint8_t device_id, sys_power_events_e triggered_by);
  void (*power_callback_scp)(uint8_t device_id, sys_power_events_e triggered_by);
} tps_adapter_ctx_t;

static void tps55289_on_fault_handler(void* arg, bool ovp, bool ocp, bool scp) {
  tps_adapter_ctx_t* ctx = (tps_adapter_ctx_t*)arg;
  if (!ctx) return;
  if (ovp && ctx->power_callback_ovp) {
    ctx->power_callback_ovp(ctx->base.device_id, SYS_PWR_EVENT_OVP);
  }
  if (ocp && ctx->power_callback_ocp) {
    ctx->power_callback_ocp(ctx->base.device_id, SYS_PWR_EVENT_OCP_CRITICAL);
  }
  if (scp && ctx->power_callback_scp) {
    ctx->power_callback_scp(ctx->base.device_id, SYS_PWR_EVENT_SPC);
  }
}

static void tps55289_adapter_isr(void* arg) {
  tps_adapter_ctx_t* ctx = (tps_adapter_ctx_t*)arg;
  if (ctx && ctx->base.hw_handle) {
    tps55289_isr_handler(ctx->base.hw_handle);
  }
}

// --- VREG Contract Implementations ---
static status_rep_t contract_vreg_tps55289_set_enable(void* device_handle, bool state) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tps_adapter_ctx_t, tps55289_handle_t, ctx, hw, device_handle);
  ctx->last_enable_state = state;

  if (state) {
    IF_PIN(ctx->en_gpio_pin_num) {
      WITH_PIN_UNLOCKED(ctx->en_gpio_device_id, ctx->en_gpio_pin_num) {
        STA_R_ON_ERR(sys_io_set_level(ctx->en_gpio_device_id, ctx->en_gpio_pin_num, state));
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_output_enable(hw, state), ctx);
  } else {
    SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_output_enable(hw, state), ctx);
    IF_PIN(ctx->en_gpio_pin_num) {
      WITH_PIN_UNLOCKED(ctx->en_gpio_device_id, ctx->en_gpio_pin_num) {
        STA_R_ON_ERR(sys_io_set_level(ctx->en_gpio_device_id, ctx->en_gpio_pin_num, state));
      }
    }
  }

  return STA_OK;
}

static status_rep_t contract_vreg_tps55289_set_voltage(void* device_handle, uint32_t voltage_mV) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tps_adapter_ctx_t, tps55289_handle_t, ctx, hw, device_handle);
  CHECK_ARG_R(voltage_mV, DEVICE_TPS55289_MIN_VOLTAGE_MV, DEVICE_TPS55289_MAX_VOLTAGE_MV, voltage_mV);
  ctx->last_voltage_mv = voltage_mV;
  SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_voltage(hw, voltage_mV), ctx);
  return STA_OK;
}

static status_rep_t contract_vreg_tps55289_set_current(void* device_handle, uint32_t current_mA) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tps_adapter_ctx_t, tps55289_handle_t, ctx, hw, device_handle);
  CHECK_ARG_R(current_mA, DEVICE_TPS55289_MIN_CURRENT_MA, DEVICE_TPS55289_MAX_CURRENT_MA, current_mA);
  ctx->last_current_limit_ma = current_mA;
  SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_current_limit(hw, ctx->is_current_limit_enabled, current_mA), ctx);
  return STA_OK;
}

static status_rep_t contract_vreg_tps55289_add_callback(void* device_handle, sys_power_events_e on_event, void (*callback)(uint8_t device_id, sys_power_events_e triggered_by)) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tps_adapter_ctx_t, tps55289_handle_t, ctx, hw, device_handle);

  if (on_event == SYS_PWR_EVENT_OVP) {
    ctx->power_callback_ovp = callback;
  } else if (on_event == SYS_PWR_EVENT_OCP_CRITICAL) {
    ctx->power_callback_ocp = callback;
  } else if (on_event == SYS_PWR_EVENT_SPC) {
    ctx->power_callback_scp = callback;
  }

  tps55289_set_fault_masks(hw, ctx->power_callback_scp == NULL, ctx->power_callback_ocp == NULL, ctx->power_callback_ovp == NULL);
  return STA_OK;
}

static const sys_power_vreg_contract s_tps_vreg_contract = {.set_enable = contract_vreg_tps55289_set_enable, .set_voltage = contract_vreg_tps55289_set_voltage, .set_current = contract_vreg_tps55289_set_current, .add_callback = contract_vreg_tps55289_add_callback};

// --- sys_device VTable Implementations ---
static status_rep_t device_uninstall(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tps_adapter_ctx_t, tps55289_handle_t, ctx, hw, handle);
  status_rep_t status = STA_OK;

  IF_PIN(ctx->en_gpio_pin_num) {
    SYS_IO_UNLOCK_PIN(ctx->en_gpio_device_id, ctx->en_gpio_pin_num);
    status_rep_t r = SYS_IO_LOW(ctx->en_gpio_device_id, ctx->en_gpio_pin_num);
    if (STA_IS_ERR(r)) status = r;
    r = sys_io_reset(ctx->en_gpio_device_id, ctx->en_gpio_pin_num);
    if (STA_IS_ERR(r)) status = r;
  }
  IF_PIN(ctx->intr_gpio_pin_num) {
    SYS_IO_UNLOCK_PIN(ctx->intr_gpio_device_id, ctx->intr_gpio_pin_num);
    status_rep_t r = sys_io_reset(ctx->intr_gpio_device_id, ctx->intr_gpio_pin_num);
    if (STA_IS_ERR(r)) status = r;
  }
  status_rep_t r = sys_power_unregister(ctx->base.device_id);
  if (STA_IS_ERR(r)) status = r;
  r = sys_i2c_remove_driver(ctx->base.hw_handle);
  if (STA_IS_ERR(r)) status = r;

  tps55289_delete(hw);
  free(ctx);
  return status;
}

static status_rep_t device_reset(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tps_adapter_ctx_t, tps55289_handle_t, ctx, hw, handle);

  SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_output_enable(hw, false), ctx);
  SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_current_limit(hw, true, 100), ctx);
  SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_voltage(hw, 5000), ctx);
  return STA_OK;
}

static status_rep_t device_error_handler(void* handle, status_rep_t* error) {
  ESP_LOGE(TAG, "Device error: code=%d, owner=%d", error->e_code, error->e_owner);
  return device_reset(handle);
}

static status_rep_t device_suspend(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tps_adapter_ctx_t, tps55289_handle_t, ctx, hw, handle);

  SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_output_enable(hw, false), ctx);
  IF_PIN(ctx->en_gpio_pin_num) {
    WITH_PIN_UNLOCKED(ctx->en_gpio_device_id, ctx->en_gpio_pin_num) {
      STA_R_ON_ERR(SYS_IO_LOW(ctx->en_gpio_device_id, ctx->en_gpio_pin_num));
    }
  }

  return STA_OK;
}

static status_rep_t device_resume(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tps_adapter_ctx_t, tps55289_handle_t, ctx, hw, handle);

  IF_PIN(ctx->en_gpio_pin_num) {
    WITH_PIN_UNLOCKED(ctx->en_gpio_device_id, ctx->en_gpio_pin_num) {
      STA_R_ON_ERR(SYS_IO_HIGH(ctx->en_gpio_device_id, ctx->en_gpio_pin_num));
    }
  }
  SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_output_enable(hw, ctx->last_enable_state), ctx);
  SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_voltage(hw, ctx->last_voltage_mv), ctx);
  SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_current_limit(hw, ctx->is_current_limit_enabled, ctx->last_current_limit_ma), ctx);

  return STA_OK;
}

static void* device_install(void** args) {
  SYS_DEV_ARG_UNPACK(uint8_t, device_id, args, 0);
  SYS_DEV_ARG_UNPACK(bool, i2c_bus, args, 1);
  SYS_DEV_ARG_UNPACK(uint8_t, i2c_addr, args, 2);
  SYS_DEV_ARG_UNPACK(uint8_t, intr_io_device, args, 3);
  SYS_DEV_ARG_UNPACK(sys_io_pin_num_t, intr_io_num, args, 4);
  SYS_DEV_ARG_UNPACK(sys_io_mode_e, intr_io_mode, args, 5);
  SYS_DEV_ARG_UNPACK(uint8_t, en_io_device, args, 6);
  SYS_DEV_ARG_UNPACK(sys_io_pin_num_t, en_io_num, args, 7);
  SYS_DEV_ARG_UNPACK(sys_io_mode_e, en_io_mode, args, 8);

  tps_adapter_ctx_t* ctx = sys_device_allocate_ctx(sizeof(tps_adapter_ctx_t), args);
  if (!ctx) return NULL;

  ctx->intr_gpio_device_id = intr_io_device;
  ctx->intr_gpio_pin_num = intr_io_num;
  ctx->en_gpio_device_id = en_io_device;
  ctx->en_gpio_pin_num = en_io_num;

  ctx->base.hw_handle = tps55289_new(i2c_addr, i2c_bus);
  if (!ctx->base.hw_handle) {
    free(ctx);
    return NULL;
  }

  ctx->last_voltage_mv = 5000;
  ctx->last_current_limit_ma = 100;
  ctx->last_enable_state = false;
  ctx->is_current_limit_enabled = true;

  if (STA_IS_ERR(sys_i2c_device_present(ctx->base.hw_handle)) || STA_IS_ERR(sys_i2c_add_driver(ctx->base.hw_handle))) {
    goto fail;
  }

  // Configure enable pin
  IF_PIN(en_io_num) {
    if (STA_IS_ERR(sys_io_set_mode(en_io_device, en_io_num, en_io_mode))) {
      goto fail;
    }
    SYS_IO_HIGH(en_io_device, en_io_num);
    SYS_IO_LOCK_PIN(en_io_device, en_io_num);
  }

  // Configure interrupt pin & callback
  IF_PIN(intr_io_num) {
    if (STA_IS_ERR(sys_io_set_mode(intr_io_device, intr_io_num, intr_io_mode))) {
      goto fail;
    }
    sys_io_intr_config_t config = {.mode = SYS_IO_INTR_MODE_FALLING_EDGE, .callback = (sys_io_isr_callback_t)(void*)tps55289_adapter_isr, .user_ctx = ctx};
    if (STA_IS_ERR(sys_io_configure_intr(intr_io_device, intr_io_num, &config))) {
      goto fail;
    }
  }
  SYS_IO_LOCK_PIN(intr_io_device, intr_io_num);
  // Register faults callbacks
  tps55289_register_on_fault_callback(ctx->base.hw_handle, tps55289_on_fault_handler, ctx);

  // Register to sys_power
  if (STA_IS_ERR(sys_power_register_vreg(device_id, ctx, &s_tps_vreg_contract))) {
    goto fail;
  }

  // Apply defaults
  tps55289_handle_t hw = (tps55289_handle_t)(ctx->base.hw_handle);
  tps55289_set_output_enable(hw, false);
  tps55289_set_current_limit(hw, true, 100);
  tps55289_set_voltage(hw, 5000);

  return ctx;

fail:
  device_uninstall(ctx);
  return NULL;
}

// --- Exposed Initialization API ---
status_rep_t d_tps55289_create(uint8_t device_id, bool i2c_bus, uint8_t i2c_addr, uint8_t intr_io_device, sys_io_pin_num_t intr_io_num, sys_io_mode_e intr_io_mode, uint8_t en_io_device, sys_io_pin_num_t en_io_num, sys_io_mode_e en_io_mode) {
  void* args[] = {SYS_DEV_ARG_PACK(device_id), SYS_DEV_ARG_PACK(i2c_bus), SYS_DEV_ARG_PACK(i2c_addr), SYS_DEV_ARG_PACK(intr_io_device), SYS_DEV_ARG_PACK(intr_io_num), SYS_DEV_ARG_PACK(intr_io_mode), SYS_DEV_ARG_PACK(en_io_device), SYS_DEV_ARG_PACK(en_io_num), SYS_DEV_ARG_PACK(en_io_mode)};

  sys_device_t dev = {.device_id = device_id,
      .role = SYS_DEV_ROLE_PWR,
      .name = "TPS55289_VREG",
      .install_args = args,
      .install_device = device_install,
      .uninstall_device = device_uninstall,
      .reset_device = device_reset,
      .error_handler = device_error_handler,
      .suspend_device = device_suspend,
      .resume_device = device_resume,
      .freeze_device = NULL,
      .sync_device = NULL};

  return sys_device_install(&dev);
}
