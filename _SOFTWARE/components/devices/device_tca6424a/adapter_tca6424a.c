#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "device_tca6424a.h"
#include "driver_tca6424a.h"
#include "esp_log.h"
#include "status.h"
#include "sys_device.h"
#include "sys_i2c.h"
#include "sys_io.h"

static const char* TAG = __FILE_NAME__;

#define PINS_COUNT 24
#define PINS_MASK ((1UL << PINS_COUNT) - 1)
#undef OWNER
#define OWNER OWNER_DEVICE_TCA6424A

typedef struct tca_adapter_ctx_t {
  sys_device_adapter_base_t base;

  uint8_t reset_gpio_device_id;
  sys_io_pin_num_t reset_pin_num;
  uint8_t intr_gpio_device_id;
  sys_io_pin_num_t intr_pin_num;

  uint32_t cached_inputs;
  uint32_t frozen_outputs_mask;
  uint32_t frozen_outputs_state;
  uint32_t configured_pins;  // 24-bit bitmask tracking pin usage

  uint16_t route_masks[24];
  sys_io_intr_mode_e intr_modes[24];
  own_funct_t own_funcs[24];
} tca_adapter_ctx_t;

static status_rep_t device_event_handler(void* handle, cb_event_t* event) {
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
  return STA_OK;
}

status_rep_t contract_io_tca6424a_set_mode(void* handle, sys_io_pin_num_t pin, sys_io_mode_e mode) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tca_adapter_ctx_t, tca6424a_handle_t, ctx, hw, handle);
  VERIFY_PIN_R(pin, PINS_MASK);

  uint32_t tca_cfg_state = 0;

  switch (mode) {
    case SYS_IO_MODE_OUTPUT_PUSH_PULL:
      tca_cfg_state = 0x00000000;
      break;
    case SYS_IO_MODE_INPUT:
      tca_cfg_state = 0xFFFFFFFF;
      break;
    default:
      return STA_C(ERR_SYS_IO_MODE_UNAVAILABLE, OWNER, SYS_IO_MAKE_INFO(ctx->base.device_id, pin, mode), STATUS_PAYLOAD_DEV_IO_ERR);
  }

  SYS_DEV_CHECK_DRIVER_CALL(tca_preset_cfg(hw, 1UL << pin, tca_cfg_state), ctx);
  ctx->configured_pins |= (1UL << pin);

  return STA_OK;
}

status_rep_t contract_io_tca6424a_set_level(void* handle, sys_io_pin_num_t pin, bool level) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tca_adapter_ctx_t, tca6424a_handle_t, ctx, hw, handle);
  VERIFY_PIN_R(pin, PINS_MASK);

  uint32_t pin_mask = (1UL << pin);
  uint32_t state_mask = level ? pin_mask : 0;

  IF_SYS_DEV_FROZEN(ctx) {
    ctx->frozen_outputs_mask |= pin_mask;
    ctx->frozen_outputs_state = (ctx->frozen_outputs_state & ~pin_mask) | state_mask;
    return STA_OK;
  }

  SYS_DEV_CHECK_DRIVER_CALL(tca_set_pins(hw, pin_mask, state_mask), ctx);
  return STA_OK;
}

status_rep_t contract_io_tca6424a_get_level(void* handle, sys_io_pin_num_t pin, bool* level) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tca_adapter_ctx_t, tca6424a_handle_t, ctx, hw, handle);
  CHECK_HANDLE_R(level);
  VERIFY_PIN_R(pin, PINS_MASK);

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
  return STA_OK;
}

status_rep_t contract_io_tca6424a_toggle(void* handle, sys_io_pin_num_t pin) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tca_adapter_ctx_t, tca6424a_handle_t, ctx, hw, handle);
  VERIFY_PIN_R(pin, PINS_MASK);

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

status_rep_t contract_io_tca6424a_reset_pin(void* handle, sys_io_pin_num_t pin) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tca_adapter_ctx_t, tca6424a_handle_t, ctx, hw, handle);
  VERIFY_PIN_R(pin, PINS_MASK);

  ctx->route_masks[pin] = 0;
  ctx->intr_modes[pin] = SYS_IO_INTR_DISABLE;
  memset(&ctx->own_funcs[pin], 0, sizeof(own_funct_t));
  ctx->configured_pins &= ~(1UL << pin);

  SYS_DEV_CHECK_DRIVER_CALL(tca_set_pins(hw, 1UL << pin, 0), ctx);
  SYS_DEV_CHECK_DRIVER_CALL(tca_preset_cfg(hw, 1UL << pin, 1UL << pin), ctx);

  return STA_OK;
}

status_rep_t d_tca6424a_driver_reset(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tca_adapter_ctx_t, tca6424a_handle_t, ctx, hw, handle);
  for (uint8_t i = 0; i < PINS_COUNT; i++) {
    STA_R_ON_ERR(contract_io_tca6424a_reset_pin(handle, i));
  }
  return STA_OK;
}

status_rep_t contract_io_tca6424a_configure_intr(void* handle, sys_io_pin_num_t pin, const sys_io_intr_config_t* config) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tca_adapter_ctx_t, tca6424a_handle_t, ctx, hw, handle);
  VERIFY_PIN_R(pin, PINS_MASK);

  if (config->mode == SYS_IO_INTR_DISABLE) {
    ctx->route_masks[pin] = 0;
    ctx->intr_modes[pin] = SYS_IO_INTR_DISABLE;
    memset(&ctx->own_funcs[pin], 0, sizeof(own_funct_t));
    return STA_OK;
  }

  ctx->route_masks[pin] = config->route_mask;
  ctx->intr_modes[pin] = config->mode;
  ctx->own_funcs[pin] = config->own_func;

  return STA_OK;
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

static status_rep_t device_freeze(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tca_adapter_ctx_t, tca6424a_handle_t, ctx, hw, handle);
  IF_SYS_DEV_FROZEN(ctx) {
    return STA_OK;
  }
  SYS_DEV_CTX_FREEZE(ctx);
  SYS_DEV_CHECK_DRIVER_CALL(tca_get_pins(hw, &ctx->cached_inputs), ctx);
  ctx->frozen_outputs_mask = 0;
  ctx->frozen_outputs_state = 0;
  return STA_OK;
}

static status_rep_t device_sync(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tca_adapter_ctx_t, tca6424a_handle_t, ctx, hw, handle);
  SYS_DEV_CTX_UNFREEZE(ctx);
  SYS_DEV_CHECK_DRIVER_CALL(tca_get_pins(hw, &ctx->cached_inputs), ctx);
  if (ctx->frozen_outputs_mask != 0) {
    SYS_DEV_CHECK_DRIVER_CALL(tca_set_pins(hw, ctx->frozen_outputs_mask, ctx->frozen_outputs_state), ctx);
    ctx->frozen_outputs_mask = 0;
  }
  return STA_OK;
}

static status_rep_t device_uninstall(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tca_adapter_ctx_t, tca6424a_handle_t, ctx, hw, handle);

  IF_PIN(ctx->reset_pin_num) {
    SYS_IO_UNLOCK_PIN(ctx->reset_gpio_device_id, ctx->reset_pin_num);
    STA_R_ON_ERR(sys_io_reset(ctx->reset_gpio_device_id, ctx->reset_pin_num));
  }
  IF_PIN(ctx->intr_pin_num) {
    SYS_IO_UNLOCK_PIN(ctx->intr_gpio_device_id, ctx->intr_pin_num);
    STA_R_ON_ERR(sys_io_reset(ctx->intr_gpio_device_id, ctx->intr_pin_num));
  }

  STA_R_ON_ERR(sys_i2c_remove_driver(hw));
  d_tca6424a_delete(hw);
  free(ctx);
  return STA_OK;
}

static status_rep_t device_reset(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tca_adapter_ctx_t, tca6424a_handle_t, ctx, hw, handle);

  IF_PIN(ctx->reset_pin_num) {
    WITH_PIN_UNLOCKED(ctx->reset_gpio_device_id, ctx->reset_pin_num) {
      STA_R_ON_ERR(SYS_IO_LOW(ctx->reset_gpio_device_id, ctx->reset_pin_num));
      vTaskDelay(pdMS_TO_TICKS(10));
      STA_R_ON_ERR(SYS_IO_HIGH(ctx->reset_gpio_device_id, ctx->reset_pin_num));
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
  ctx->cached_inputs = 0;
  ctx->configured_pins = 0;
  ctx->frozen_outputs_mask = 0;
  ctx->frozen_outputs_state = 0;
  return d_tca6424a_driver_reset(handle);
}

static status_rep_t device_error_handler(void* handle, status_rep_t* error) {
  if (!error) return STA_OK;

  tca_adapter_ctx_t* ctx = (tca_adapter_ctx_t*)handle;
  if (!ctx) {
    ESP_LOGE(TAG, "Missing context handle");
    return STA_C(ERR_DEV_MISSING_HANDLE, OWNER, 0, STATUS_PAYLOAD_DEV_SOLO);
  }

  uint32_t e_code = error->e_code;
  uint64_t payload = error->payload;
  switch (e_code) {
    case ERR_DEV_DEP_ERR:
    case ERR_DEV_DRIVER_ERR: {
      if (e_code == ERR_DEV_DEP_ERR) {
        ESP_LOGE(TAG, "Encountered dependency error on device %u: %s, suspending device ID: %u", DEV_ERR_GET_DEP(payload), status_error_to_name(DEV_ERR_GET_CODE(payload)), ctx->base.device_id);
      } else {
        ESP_LOGE(TAG, "Encountered driver error: %s, suspending device ID: %u", esp_err_to_name(DEV_ERR_GET_CODE(payload)), ctx->base.device_id);
      }

      status_suspend();
      sys_device_suspend(ctx->base.device_id);
      status_resume();
      return STA_OK;
    }
    case ERR_SYS_IO_PIN_DOES_NOT_EXIST: {
      uint32_t pin = SYS_IO_UNPACK_PIN(payload);
      ESP_LOGW(TAG, "Configuration Warning (device ID: %u): Pin %lu does not exist. Available IO: 0..23.", ctx->base.device_id, pin);
      return STA_OK;
    }
    case ERR_SYS_IO_MODE_UNAVAILABLE: {
      uint32_t pin = SYS_IO_UNPACK_PIN(payload);
      uint32_t mode = SYS_IO_UNPACK_EXTRA(payload);
      const char* mode_str = (mode < 9) ? sys_io_mode_e_to_string[mode] : "UNKNOWN";
      ESP_LOGW(TAG, "Pin %lu (device ID: %u) can be configured only as SYS_IO_MODE_OUTPUT_PUSH_PULL or SYS_IO_MODE_INPUT, %s not supported.", pin, ctx->base.device_id, mode_str);
      return STA_OK;
    }
    case ERR_NOT_SUPPORTED:
      ESP_LOGW(TAG, "Available functions for device ID %u: set_mode, configure_intr, set_level, get_level, toggle", ctx->base.device_id);
      return STA_OK;
    default:
      break;
  }
  return *error;
}

static status_rep_t device_suspend(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tca_adapter_ctx_t, tca6424a_handle_t, ctx, hw, handle);
  IF_PIN(ctx->reset_pin_num) {
    WITH_PIN_UNLOCKED(ctx->reset_gpio_device_id, ctx->reset_pin_num) {
      STA_R_ON_ERR(SYS_IO_LOW(ctx->reset_gpio_device_id, ctx->reset_pin_num));
    }
  }
  return STA_OK;
}

static status_rep_t device_resume(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tca_adapter_ctx_t, tca6424a_handle_t, ctx, hw, handle);
  CHECK_HANDLE_R(ctx);
  IF_PIN(ctx->reset_pin_num) {
    WITH_PIN_UNLOCKED(ctx->reset_gpio_device_id, ctx->reset_pin_num) {
      STA_R_ON_ERR(SYS_IO_HIGH(ctx->reset_gpio_device_id, ctx->reset_pin_num));
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  SYS_DEV_CHECK_DRIVER_CALL(tca_restore_state(hw), ctx);
  return STA_OK;
}

static void adapter_install_fallback(tca_adapter_ctx_t* ctx) {
  if (ctx) {
    IF_PIN(ctx->intr_pin_num) {
      SYS_IO_UNLOCK_PIN(ctx->intr_gpio_device_id, ctx->intr_pin_num);
      sys_io_reset(ctx->intr_gpio_device_id, ctx->intr_pin_num);
    }
    IF_PIN(ctx->reset_pin_num) {
      SYS_IO_UNLOCK_PIN(ctx->reset_gpio_device_id, ctx->reset_pin_num);
      sys_io_reset(ctx->reset_gpio_device_id, ctx->reset_pin_num);
    }
    if (ctx->base.hw_handle) {
      sys_i2c_remove_driver(ctx->base.hw_handle);
      d_tca6424a_delete((tca6424a_handle_t)(ctx->base.hw_handle));
    }
    free(ctx);
  }
}

static status_rep_t device_install(void** args, void** out_device_handle) {
  SYS_DEV_ARG_UNPACK(uint8_t, device_id, args, 0);
  SYS_DEV_ARG_UNPACK(bool, i2c_bus, args, 1);
  SYS_DEV_ARG_UNPACK(uint8_t, i2c_addr, args, 2);
  SYS_DEV_ARG_UNPACK(uint8_t, intr_io_device, args, 3);
  SYS_DEV_ARG_UNPACK(sys_io_pin_num_t, intr_io_num, args, 4);
  SYS_DEV_ARG_UNPACK(sys_io_mode_e, intr_io_mode, args, 5);
  SYS_DEV_ARG_UNPACK(uint8_t, rst_io_device, args, 6);
  SYS_DEV_ARG_UNPACK(sys_io_pin_num_t, rst_io_num, args, 7);
  SYS_DEV_ARG_UNPACK(sys_io_mode_e, rst_io_mode, args, 8);

  tca_adapter_ctx_t* ctx = sys_device_allocate_ctx(sizeof(tca_adapter_ctx_t), args);
  if (!ctx) return STA_C(ERR_NO_MEM, OWNER, 0, STATUS_PAYLOAD_DEV_SOLO);

  ctx->base.hw_handle = d_tca6424a_new(i2c_addr, i2c_bus);
  if (!ctx->base.hw_handle) {
    free(ctx);
    return STA_C(ERR_NO_MEM, OWNER, 0, STATUS_PAYLOAD_DEV_SOLO);
  }

  ctx->intr_gpio_device_id = intr_io_device;
  ctx->intr_pin_num = intr_io_num;
  ctx->reset_gpio_device_id = rst_io_device;
  ctx->reset_pin_num = rst_io_num;

  status_rep_t status = sys_i2c_add_driver(ctx->base.hw_handle);
  if (STA_IS_ERR(status)) {
    goto fail;
  }

  IF_PIN(rst_io_num) {
    status = sys_io_set_mode(rst_io_device, rst_io_num, rst_io_mode);
    if (STA_IS_ERR(status)) {
      goto fail;
    }
    SYS_IO_HIGH(rst_io_device, rst_io_num);
    vTaskDelay(pdMS_TO_TICKS(10));
    SYS_IO_LOCK_PIN(rst_io_device, rst_io_num);
  }

  status = sys_i2c_device_present(ctx->base.hw_handle);
  if (STA_IS_ERR(status)) {
    status = STA_C(ERR_I2C_DEV_NOT_FOUND, OWNER, DEV_ERR_PACK(ctx->base.device_id, 0, 0), STATUS_PAYLOAD_DEV_SOLO);
    goto fail;
  }

  IF_PIN(intr_io_num) {
    status = sys_io_set_mode(intr_io_device, intr_io_num, intr_io_mode);
    if (STA_IS_ERR(status)) {
      goto fail;
    }
    sys_io_intr_config_t config = {
        .mode = SYS_IO_INTR_MODE_FALLING_EDGE,
        .own_func = {.own_func = device_event_handler, .device_handle = ctx},
    };
    status = sys_io_configure_intr(intr_io_device, intr_io_num, &config);
    if (STA_IS_ERR(status)) {
      goto fail;
    }
    SYS_IO_LOCK_PIN(intr_io_device, intr_io_num);
  }

  status = sys_io_register_driver(device_id, ctx, &io_tca_vtable);
  if (STA_IS_ERR(status)) {
    ESP_LOGE(TAG, "Failed to register TCA6424A to IO Manager on device_id %ld", (long)device_id);
    goto fail;
  }

  uint32_t initial_inputs = 0;
  SYS_DEV_CHECK_DRIVER_CALL(tca_get_pins(ctx->base.hw_handle, &initial_inputs), ctx);
  ctx->cached_inputs = initial_inputs;

  ESP_LOGI(TAG, "TCA6424A successfully installed as IO device %ld", (long)device_id);
  *out_device_handle = ctx;
  return STA_OK;

fail:
  adapter_install_fallback(ctx);
  *out_device_handle = NULL;
  return status;
}

status_rep_t d_tca6424a_create(uint8_t device_id, bool i2c_bus, uint8_t i2c_addr, uint8_t intr_io_device, sys_io_pin_num_t intr_io_num, sys_io_mode_e intr_io_mode, uint8_t rst_io_device, sys_io_pin_num_t rst_io_num, sys_io_mode_e rst_io_mode) {
  void* args[] = {SYS_DEV_ARG_PACK(device_id), SYS_DEV_ARG_PACK(i2c_bus), SYS_DEV_ARG_PACK(i2c_addr), SYS_DEV_ARG_PACK(intr_io_device), SYS_DEV_ARG_PACK(intr_io_num), SYS_DEV_ARG_PACK(intr_io_mode), SYS_DEV_ARG_PACK(rst_io_device), SYS_DEV_ARG_PACK(rst_io_num), SYS_DEV_ARG_PACK(rst_io_mode)};

  sys_device_t dev = {.device_id = device_id,
      .role = SYS_DEV_ROLE_IO,
      .name = "TCA6424A_GPIO",
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
