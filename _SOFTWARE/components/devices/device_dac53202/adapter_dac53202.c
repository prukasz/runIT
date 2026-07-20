#include <stdlib.h>
#include "device_dac53202.h"
#include "driver_dac53202.h"
#include "esp_log.h"
#include "status.h"
#include "sys_device.h"
#include "sys_i2c.h"
#include "sys_io.h"

static const char* TAG = __FILE_NAME__;
#undef OWNER
#define OWNER OWNER_DEVICE_DAC53202

// --- 1. The Encapsulated Adapter Context ---
typedef struct dac_adapter_ctx_t {
  sys_device_adapter_base_t base;

  // Caching mechanism for freeze/sync
  uint32_t cached_voltage_mv[2];
  bool cached_voltage_dirty[2];
  uint8_t cached_power_mask;
  bool cached_power_dirty;
} dac_adapter_ctx_t;

// --- VTABLE Implementations (IO Contract) ---
static status_rep_t contract_io_dac53202_reset_pin(void* handle, sys_io_pin_num_t pin) {
  SYS_DEV_GET_ADAPTER_CONTEXT(dac_adapter_ctx_t, dac53202_handle_t, ctx, hw, handle);
  VERIFY_PIN_R(pin, 0x03);

  IF_SYS_DEV_FROZEN(ctx) {
    if (ctx->cached_power_dirty) {
      ctx->cached_power_mask &= ~(1 << pin);
    } else {
      ctx->cached_power_mask = (hw->common_config & 0xFF) & ~(1 << pin);
      ctx->cached_power_dirty = true;
    }
    return STA_OK;
  }

  uint8_t current_power_on = hw->common_config & 0xFF;
  uint8_t next_power_on = current_power_on & ~(1 << pin);
  SYS_DEV_CHECK_DRIVER_CALL(dac53202_preset_cfg(hw, 0x03, next_power_on), ctx);
  return STA_OK;
}

static status_rep_t contract_io_dac53202_set_voltage(void* handle, sys_io_pin_num_t pin, uint32_t voltage_mV) {
  SYS_DEV_GET_ADAPTER_CONTEXT(dac_adapter_ctx_t, dac53202_handle_t, ctx, hw, handle);
  VERIFY_PIN_R(pin, 0x03);

  IF_SYS_DEV_FROZEN(ctx) {
    ctx->cached_voltage_mv[pin] = voltage_mV;
    ctx->cached_voltage_dirty[pin] = true;
    return STA_OK;
  }

  SYS_DEV_CHECK_DRIVER_CALL(dac53202_set_voltage_mv(hw, 1 << pin, (uint16_t)voltage_mV), ctx);
  return STA_OK;
}

static status_rep_t contract_io_dac53202_get_voltage(void* handle, sys_io_pin_num_t pin, uint32_t* out_mV) {
  SYS_DEV_GET_ADAPTER_CONTEXT(dac_adapter_ctx_t, dac53202_handle_t, ctx, hw, handle);
  CHECK_HANDLE_R(out_mV);
  VERIFY_PIN_R(pin, 0x03);

  if (ctx->base.is_frozen && ctx->cached_voltage_dirty[pin]) {
    *out_mV = ctx->cached_voltage_mv[pin];
    return STA_OK;
  }

  uint16_t v_mv = 0;
  SYS_DEV_CHECK_DRIVER_CALL(dac53202_get_voltage_mv(hw, pin, &v_mv), ctx);
  *out_mV = (uint32_t)v_mv;
  return STA_OK;
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
static status_rep_t device_uninstall(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(dac_adapter_ctx_t, dac53202_handle_t, ctx, hw, handle);
  status_rep_t status = STA_OK;

  status_rep_t r = sys_io_unregister_driver(ctx->base.device_id);
  if (STA_IS_ERR(r)) status = r;
  r = sys_i2c_remove_driver(ctx->base.hw_handle);
  if (STA_IS_ERR(r)) status = r;

  dac53202_delete(hw);
  free(ctx);
  return status;
}

static status_rep_t device_reset(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(dac_adapter_ctx_t, dac53202_handle_t, ctx, hw, handle);

  SYS_DEV_CHECK_DRIVER_CALL(dac53202_preset_cfg(hw, 0x03, 0x00), ctx);
  return STA_OK;
}

static status_rep_t device_suspend(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(dac_adapter_ctx_t, dac53202_handle_t, ctx, hw, handle);
  SYS_DEV_CHECK_DRIVER_CALL(dac53202_preset_cfg(hw, 0x03, 0x00), ctx);
  return STA_OK;
}

static status_rep_t device_resume(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(dac_adapter_ctx_t, dac53202_handle_t, ctx, hw, handle);
  SYS_DEV_CHECK_DRIVER_CALL(dac53202_preset_cfg(hw, 0x03, 0x03), ctx);
  return STA_OK;
}

static status_rep_t device_freeze(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(dac_adapter_ctx_t, dac53202_handle_t, ctx, hw, handle);
  IF_SYS_DEV_FROZEN(ctx) { return STA_OK; }
  SYS_DEV_CTX_FREEZE(ctx);
  ctx->cached_voltage_dirty[0] = false;
  ctx->cached_voltage_dirty[1] = false;
  ctx->cached_power_dirty = false;
  return STA_OK;
}

static status_rep_t device_sync(void* handle) {
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

  return STA_OK;
}

static status_rep_t device_error_handler(void* handle, status_rep_t* error) {
  ESP_LOGE(TAG, "Device error: code=%d, owner=%d", error->e_code, error->e_owner);
  (void)device_reset(handle);
  return STA_OK;
}

static status_rep_t device_install(void** args, void** out_device_handle) {
  dac_adapter_ctx_t* ctx = sys_device_allocate_ctx(sizeof(dac_adapter_ctx_t), args);
  if (!ctx) return STA_C(ERR_NO_MEM, OWNER, 0, STATUS_PAYLOAD_DEV_SOLO);

  ctx->base.hw_handle = dac53202_new(SYS_DEV_ARG_UNPACK_VAL(uint8_t, args, 2), SYS_DEV_ARG_UNPACK_VAL(bool, args, 1));
  if (!ctx->base.hw_handle) {
    free(ctx);
    return STA_C(ERR_NO_MEM, OWNER, 0, STATUS_PAYLOAD_DEV_SOLO);
  }

  status_rep_t status = sys_i2c_add_driver(ctx->base.hw_handle);
  if (!STA_IS_OK(status)) {
    goto fail;
  }

  status = sys_i2c_device_present(ctx->base.hw_handle);
  if (STA_IS_ERR(status)) {
    status = STA_C(ERR_I2C_DEV_NOT_FOUND, OWNER, DEV_ERR_PACK(ctx->base.device_id, 0, 0), STATUS_PAYLOAD_DEV_SOLO);
    goto fail;
  }

  status = sys_io_register_driver(SYS_DEV_ARG_UNPACK_VAL(uint8_t, args, 0), ctx, &io_dac_vtable);
  if (!STA_IS_OK(status)) {
    goto fail;
  }

  dac53202_handle_t hw = (dac53202_handle_t)(ctx->base.hw_handle);
  status = STA_FROM_ESP(dac53202_preset_cfg(hw, 0x03, 0x03));
  if (!STA_IS_OK(status)) {
    goto fail;
  }

  *out_device_handle = ctx;
  return STA_OK;

fail:
  device_uninstall(ctx);
  *out_device_handle = NULL;
  return status;
}

// --- Exposed Initialization API ---
status_rep_t d_dac53202_create(uint8_t device_id, bool i2c_bus, uint8_t i2c_addr) {
  void* args[] = {SYS_DEV_ARG_PACK(device_id), SYS_DEV_ARG_PACK(i2c_bus), SYS_DEV_ARG_PACK(i2c_addr)};

  sys_device_t dev = {.device_id = device_id,
      .role = SYS_DEV_ROLE_IO,
      .name = "DAC53202",
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
