#include <stdint.h>
#include <stdlib.h>
#include "device_pca9685.h"
#include "driver_pca9685.h"
#include "esp_err.h"
#include "esp_log.h"
#include "status.h"
#include "status_codes.h"
#include "sys_device.h"
#include "sys_i2c.h"
#include "sys_io.h"

static const char* TAG = __FILE_NAME__;
#undef OWNER
#define OWNER OWNER_DEVICE_PCA9685
#define PINS_MASK 0xFFFF

typedef struct pca_adapter_ctx_t {
  sys_device_adapter_base_t base;

  uint16_t frozen_duty[PCA9685_CHANNEL_ALL];
  uint16_t frozen_outputs_mask;
  uint16_t frozen_freq;
  bool frozen_freq_dirty;

  // OE pin config
  uint8_t oe_device_id;
  sys_io_pin_num_t oe_pin_num;
} pca_adapter_ctx_t;

// --- VTABLE Implementations (IO Contract) ---
static status_rep_t contract_io_pca9685_set_pwm_duty(void* handle, sys_io_pin_num_t pin, uint32_t duty) {
  SYS_DEV_GET_ADAPTER_CONTEXT(pca_adapter_ctx_t, pca9685_handle_t, ctx, hw, handle);
  VERIFY_PIN_R(pin, PINS_MASK);

  if (duty > PCA9685_MAX_PWM_VALUE) {
    duty = PCA9685_MAX_PWM_VALUE;
  }

  IF_SYS_DEV_FROZEN(ctx) {
    ctx->frozen_duty[pin] = (uint16_t)duty;
    ctx->frozen_outputs_mask |= (1 << pin);
    return STA_OK;
  }
  SYS_DEV_CHECK_DRIVER_CALL(pca9685_set_pwm_value(hw, pin, (uint16_t)duty), ctx);
  return STA_OK;
}

static status_rep_t contract_io_pca9685_set_pwm_frequency(void* handle, sys_io_pin_num_t pin, uint32_t frequency_HZ) {
  SYS_DEV_GET_ADAPTER_CONTEXT(pca_adapter_ctx_t, pca9685_handle_t, ctx, hw, handle);

  IF_SYS_DEV_FROZEN(ctx) {
    ctx->frozen_freq = (uint16_t)frequency_HZ;
    ctx->frozen_freq_dirty = true;
    return STA_OK;
  }
  SYS_DEV_CHECK_DRIVER_CALL(pca9685_set_pwm_frequency(hw, (uint16_t)frequency_HZ), ctx);
  return STA_OK;
}

static status_rep_t contract_io_pca9685_set_level(void* handle, sys_io_pin_num_t pin, bool level) {
  uint32_t target_pwm = level ? PCA9685_MAX_PWM_VALUE : 0;
  return contract_io_pca9685_set_pwm_duty(handle, pin, target_pwm);
}

static status_rep_t contract_io_pca9685_get_level(void* handle, sys_io_pin_num_t pin, bool* level) {
  SYS_DEV_GET_ADAPTER_CONTEXT(pca_adapter_ctx_t, pca9685_handle_t, ctx, hw, handle);
  CHECK_HANDLE_R(level);
  VERIFY_PIN_R(pin, PINS_MASK);

  uint16_t current_val;
  if (ctx->base.is_frozen && (ctx->frozen_outputs_mask & (1 << pin))) {
    current_val = ctx->frozen_duty[pin];
  } else {
    current_val = hw->channel_pwm_value[pin];
  }
  *level = (current_val >= (PCA9685_MAX_PWM_VALUE / 2));

  return STA_OK;
}

static status_rep_t contract_io_pca9685_toggle(void* handle, sys_io_pin_num_t pin) {
  SYS_DEV_GET_ADAPTER_CONTEXT(pca_adapter_ctx_t, pca9685_handle_t, ctx, hw, handle);
  VERIFY_PIN_R(pin, PINS_MASK);

  uint16_t current_val;
  if (ctx->base.is_frozen && (ctx->frozen_outputs_mask & (1 << pin))) {
    current_val = ctx->frozen_duty[pin];
  } else {
    current_val = hw->channel_pwm_value[pin];
  }
  uint16_t new_val = (current_val >= (PCA9685_MAX_PWM_VALUE / 2)) ? 0 : PCA9685_MAX_PWM_VALUE;

  return contract_io_pca9685_set_pwm_duty(handle, pin, new_val);
}

static status_rep_t contract_io_pca9685_reset_pin(void* handle, sys_io_pin_num_t pin) {
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
static status_rep_t device_uninstall(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(pca_adapter_ctx_t, pca9685_handle_t, ctx, hw, handle);
  status_rep_t status = STA_OK;
  status_rep_t r;

  // Put chip to sleep
  esp_err_t err = pca9685_sleep(hw, true);
  if (err != ESP_OK) {
    status = STA_C(ERR_DEV_DRIVER_ERR, OWNER, DEV_ERR_PACK(ctx->base.device_id, 0, err), STATUS_PAYLOAD_DEV_ESP);
  }
  status_suspend();
  // Disable outputs if OE pin exists (set HIGH)
  IF_PIN(ctx->oe_pin_num) {
    SYS_IO_UNLOCK_PIN(ctx->oe_device_id, ctx->oe_pin_num);
    r = SYS_IO_HIGH(ctx->oe_device_id, ctx->oe_pin_num);
    if (STA_IS_ERR(r)) status = r;
    r = sys_io_reset(ctx->oe_device_id, ctx->oe_pin_num);
    if (STA_IS_ERR(r)) status = r;
  }

  r = sys_io_unregister_driver(ctx->base.device_id);
  if (STA_IS_ERR(r)) status = r;
  r = sys_i2c_remove_driver(ctx->base.hw_handle);
  if (STA_IS_ERR(r)) status = r;
  status_resume();
  free(hw);
  free(ctx);
  return status;
}

static status_rep_t device_reset(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(pca_adapter_ctx_t, pca9685_handle_t, ctx, hw, handle);

  for (uint8_t i = 0; i < PCA9685_CHANNEL_ALL; i++) {
    SYS_DEV_CHECK_DRIVER_CALL(pca9685_set_pwm_value(hw, i, 0), ctx);
  }

  SYS_DEV_CHECK_DRIVER_CALL(pca9685_enable_auto_increment(hw), ctx);

  ESP_LOGI(TAG, "PWM expander reset: all channels set to 0");
  return STA_OK;
}

static status_rep_t device_suspend(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(pca_adapter_ctx_t, pca9685_handle_t, ctx, hw, handle);

  IF_PIN(ctx->oe_pin_num) {
    WITH_PIN_UNLOCKED(ctx->oe_device_id, ctx->oe_pin_num) {
      SYS_IO_HIGH(ctx->oe_device_id, ctx->oe_pin_num);
    }
  }

  SYS_DEV_CHECK_DRIVER_CALL(pca9685_sleep(hw, true), ctx);
  ctx->base.is_frozen = true;

  return STA_OK;
}

static status_rep_t device_resume(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(pca_adapter_ctx_t, pca9685_handle_t, ctx, hw, handle);

  pca9685_sleep(hw, false);

  IF_PIN(ctx->oe_pin_num) {
    WITH_PIN_UNLOCKED(ctx->oe_device_id, ctx->oe_pin_num) {
      SYS_IO_LOW(ctx->oe_device_id, ctx->oe_pin_num);
    }
  }

  ctx->base.is_frozen = false;
  return STA_OK;
}

static status_rep_t device_freeze(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(pca_adapter_ctx_t, pca9685_handle_t, ctx, hw, handle);
  IF_SYS_DEV_FROZEN(ctx) {
    return STA_OK;
  }
  SYS_DEV_CTX_FREEZE(ctx);
  ctx->frozen_outputs_mask = 0;
  ctx->frozen_freq_dirty = false;
  return STA_OK;
}

static status_rep_t device_sync(void* handle) {
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

  return STA_OK;
}

static status_rep_t device_error_handler(void* handle, status_rep_t* error) {
  return STA_OK;
}

static status_rep_t device_install(void** args, void** out_device_handle) {
  pca_adapter_ctx_t* ctx = sys_device_allocate_ctx(sizeof(pca_adapter_ctx_t), args);
  if (!ctx) return STA_C(ERR_NO_MEM, OWNER, 0, STATUS_PAYLOAD_DEV_SOLO);

  ctx->base.hw_handle = pca9685_new(SYS_DEV_ARG_UNPACK_VAL(uint8_t, args, 2), SYS_DEV_ARG_UNPACK_VAL(bool, args, 1));
  if (!ctx->base.hw_handle) {
    free(ctx);
    return STA_C(ERR_NO_MEM, OWNER, 0, STATUS_PAYLOAD_DEV_SOLO);
  }

  pca9685_handle_t hw = (pca9685_handle_t)(ctx->base.hw_handle);

  status_rep_t status = STA_FROM_ESP(pca9685_start(hw));
  if (STA_IS_ERR(status)) {
    status = STA_C(ERR_DEV_DRIVER_ERR, OWNER, DEV_ERR_PACK(ctx->base.device_id, 0, (esp_err_t)status.payload), STATUS_PAYLOAD_DEV_ESP);
    goto fail;
  }

  status = sys_i2c_add_driver(ctx->base.hw_handle);
  if (STA_IS_ERR(status)) {
    status = STA_C(ERR_DEV_DEP_ERR, OWNER, DEV_ERR_PACK(ctx->base.device_id, 0xFF, status.e_code), STATUS_PAYLOAD_DEV_DEP);
    goto fail;
  }

  status = sys_i2c_device_present(ctx->base.hw_handle);
  if (STA_IS_ERR(status)) {
    status = STA_C(ERR_I2C_DEV_NOT_FOUND, OWNER, DEV_ERR_PACK(ctx->base.device_id, 0, 0), STATUS_PAYLOAD_DEV_SOLO);
    goto fail;
  }

  // Setup OE pin details
  sys_io_pin_num_t oe_io_num = SYS_DEV_ARG_UNPACK_VAL(sys_io_pin_num_t, args, 4);
  ctx->oe_pin_num = oe_io_num;
  IF_PIN(oe_io_num) {
    ctx->oe_device_id = SYS_DEV_ARG_UNPACK_VAL(uint8_t, args, 3);
    status = sys_io_set_mode(ctx->oe_device_id, ctx->oe_pin_num, SYS_DEV_ARG_UNPACK_VAL(sys_io_mode_e, args, 5));
    if (STA_IS_ERR(status)) {
      status = STA_C(ERR_DEV_DEP_ERR, OWNER, DEV_ERR_PACK(ctx->base.device_id, ctx->oe_device_id, status.e_code), STATUS_PAYLOAD_DEV_DEP);
      goto fail;
    }
    SYS_IO_LOW(ctx->oe_device_id, ctx->oe_pin_num);
    SYS_IO_LOCK_PIN(ctx->oe_device_id, ctx->oe_pin_num);
  }

  // Register with IO Manager VFS
  uint8_t device_id = SYS_DEV_ARG_UNPACK_VAL(uint8_t, args, 0);
  status = sys_io_register_driver(device_id, ctx, &io_pca_vtable);
  if (STA_IS_ERR(status)) {
    ESP_LOGE(TAG, "Failed to register PCA9685 to IO Manager on device ID %d", device_id);
    status = STA_C(ERR_DEV_DEP_ERR, OWNER, DEV_ERR_PACK(ctx->base.device_id, device_id, status.e_code), STATUS_PAYLOAD_DEV_DEP);
    goto fail;
  }

  ESP_LOGI(TAG, "PCA9685 successfully installed as Device ID %d", device_id);
  *out_device_handle = ctx;
  return STA_OK;

fail:
  device_uninstall(ctx);
  *out_device_handle = NULL;
  return status;
}

// --- Exposed Initialization API ---
status_rep_t d_pca9685_create(uint8_t device_id, bool i2c_bus, uint8_t i2c_addr, uint8_t oe_io_device, sys_io_pin_num_t oe_io_num, sys_io_mode_e oe_io_mode) {
  void* args[] = {SYS_DEV_ARG_PACK(device_id), SYS_DEV_ARG_PACK(i2c_bus), SYS_DEV_ARG_PACK(i2c_addr), SYS_DEV_ARG_PACK(oe_io_device), SYS_DEV_ARG_PACK(oe_io_num), SYS_DEV_ARG_PACK(oe_io_mode)};

  sys_device_t dev = {.device_id = device_id,
      .role = SYS_DEV_ROLE_IO,
      .name = "PCA9685_PWM_EXPANDER",
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
