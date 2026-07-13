/**
 * @file adapter_example.c
 * @brief User Guide & Template for implementing IO device adapters in the sys_device manager.
 * 
 * This file serves as a reference template for writing custom hardware device adapters.
 * It demonstrates standard naming styles, error handling macros, static scoping,
 * context retrieval, and the unified freeze/sync (double-buffering) mechanism.
 */

#include <stdlib.h>
#include "driver_example.h"
#include "esp_log.h"
#include "status.h"
#include "sys_device.h"
#include "sys_i2c.h"
#include "sys_io.h"

static const char* TAG = __FILE_NAME__;

/* Every submodule must define its OWNER for error tracing */
#undef OWNER
#define OWNER OWNER_ADAPTER_EXAMPLE

#define EXAMPLE_PINS_COUNT 8
#define EXAMPLE_PINS_MASK ((1UL << EXAMPLE_PINS_COUNT) - 1)

/**
 * @brief The Encapsulated Adapter Context
 * 
 * This struct holds the state of the adapter.
 * It MUST start with `sys_device_adapter_base_t base` as its first member.
 */
typedef struct {
  sys_device_adapter_base_t base; // Required: contains device_id, is_frozen, hw_handle

  // Hardware configuration pins
  uint8_t reset_gpio_device_id;
  sys_io_pin_num_t reset_pin_num;
  uint8_t intr_gpio_device_id;
  sys_io_pin_num_t intr_pin_num;

  // Cached state for freeze/sync (double buffering)
  uint32_t cached_inputs;
  uint32_t frozen_outputs_mask;
  uint32_t frozen_outputs_state;

  // Configured pin tracks
  uint32_t configured_pins;
} example_adapter_ctx_t;

/* Callback trigger when hardware interrupt occurs */
static void example_on_change_handler(void* handle, uint32_t rising_edges, uint32_t falling_edges) {
  example_adapter_ctx_t* ctx = (example_adapter_ctx_t*)handle;
  if (!ctx) return;

  // Implement hardware state changes mapping to sys_io callbacks here
  ESP_LOGI(TAG, "Interrupt triggered: rising 0x%02lx, falling 0x%02lx", rising_edges, falling_edges);
}

static void example_adapter_isr(void* arg) {
  example_adapter_ctx_t* ctx = (example_adapter_ctx_t*)arg;
  if (ctx && ctx->base.hw_handle) {
    // Call the driver ISR handler
    example_driver_isr_handler(ctx->base.hw_handle);
  }
}

// =============================================================================
// --- VTABLE Implementations (IO Contract) ---
// All contract functions MUST:
// 1. Be declared static.
// 2. Start with 'contract_io_<driver>_'.
// 3. Retrieve context using `SYS_DEV_GET_ADAPTER_CONTEXT`.
// 4. Verify pin constraints using `VERIFY_PIN_R`.
// 5. Wrap driver calls using `SYS_DEV_CHECK_DRIVER_CALL`.
// =============================================================================

static status_rep_t contract_io_example_set_mode(void* handle, sys_io_pin_num_t pin, sys_io_mode_e mode) {
  SYS_DEV_GET_ADAPTER_CONTEXT(example_adapter_ctx_t, example_handle_t, ctx, hw, handle);
  VERIFY_PIN_R(pin, EXAMPLE_PINS_MASK);

  uint8_t config = (mode == SYS_IO_MODE_INPUT) ? 0xFF : 0x00;
  
  // Set configuration in driver
  SYS_DEV_CHECK_DRIVER_CALL(example_driver_set_config(hw, 1 << pin, config), ctx);
  ctx->configured_pins |= (1 << pin);

  return STA_OK;
}

static status_rep_t contract_io_example_set_level(void* handle, sys_io_pin_num_t pin, bool level) {
  SYS_DEV_GET_ADAPTER_CONTEXT(example_adapter_ctx_t, example_handle_t, ctx, hw, handle);
  VERIFY_PIN_R(pin, EXAMPLE_PINS_MASK);

  uint32_t pin_mask = (1UL << pin);
  uint32_t state_mask = level ? pin_mask : 0;

  /* If the device is frozen, cache changes instead of writing to registers */
  IF_SYS_DEV_FROZEN(ctx) {
    ctx->frozen_outputs_mask |= pin_mask;
    ctx->frozen_outputs_state = (ctx->frozen_outputs_state & ~pin_mask) | state_mask;
    return STA_OK;
  }

  SYS_DEV_CHECK_DRIVER_CALL(example_driver_write_pins(hw, pin_mask, state_mask), ctx);
  return STA_OK;
}

static status_rep_t contract_io_example_get_level(void* handle, sys_io_pin_num_t pin, bool* level) {
  SYS_DEV_GET_ADAPTER_CONTEXT(example_adapter_ctx_t, example_handle_t, ctx, hw, handle);
  CHECK_HANDLE_R(level);
  VERIFY_PIN_R(pin, EXAMPLE_PINS_MASK);

  uint32_t pin_mask = (1UL << pin);
  uint32_t all_levels = 0;

  /* Retrieve cached readings if frozen, otherwise query the driver directly */
  IF_SYS_DEV_FROZEN(ctx) {
    all_levels = ctx->cached_inputs;
  } else {
    SYS_DEV_CHECK_DRIVER_CALL(example_driver_read_pins(hw, &all_levels), ctx);
    ctx->cached_inputs = all_levels;
  }

  *level = (all_levels & pin_mask) ? true : false;
  return STA_OK;
}

static status_rep_t contract_io_example_toggle(void* handle, sys_io_pin_num_t pin) {
  SYS_DEV_GET_ADAPTER_CONTEXT(example_adapter_ctx_t, example_handle_t, ctx, hw, handle);
  VERIFY_PIN_R(pin, EXAMPLE_PINS_MASK);

  uint32_t pin_mask = (1UL << pin);
  bool is_high = false;

  if (ctx->base.is_frozen && (ctx->frozen_outputs_mask & pin_mask)) {
    is_high = (ctx->frozen_outputs_state & pin_mask) != 0;
  } else {
    uint32_t current_outputs = 0;
    SYS_DEV_CHECK_DRIVER_CALL(example_driver_read_pins(hw, &current_outputs), ctx);
    is_high = (current_outputs & pin_mask) != 0;
  }

  return contract_io_example_set_level(handle, pin, !is_high);
}

static status_rep_t contract_io_example_reset_pin(void* handle, sys_io_pin_num_t pin) {
  SYS_DEV_GET_ADAPTER_CONTEXT(example_adapter_ctx_t, example_handle_t, ctx, hw, handle);
  VERIFY_PIN_R(pin, EXAMPLE_PINS_MASK);

  ctx->configured_pins &= ~(1UL << pin);
  SYS_DEV_CHECK_DRIVER_CALL(example_driver_write_pins(hw, 1UL << pin, 0), ctx);

  return STA_OK;
}

// Instantiate the static VTable to map adapter functions to sys_io contracts
static sys_io_vtable_t io_example_vtable = {
    .io_reset = contract_io_example_reset_pin,
    .io_set_mode = contract_io_example_set_mode,
    .io_set_level = contract_io_example_set_level,
    .io_get_level = contract_io_example_get_level,
    .io_toggle = contract_io_example_toggle,
    .io_configure_intr = NULL,
    .io_get_voltage = NULL,
    .io_set_voltage = NULL,
    .io_set_pwm_frequency = NULL,
    .io_set_pwm_duty = NULL,
    .protected_pins = 0
};

// =============================================================================
// --- sys_device VTable Implementations ---
// Standard lifecycle callbacks for device suspension, resume, freeze, reset.
// =============================================================================

static status_rep_t device_uninstall(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(example_adapter_ctx_t, example_handle_t, ctx, hw, handle);
  status_rep_t status = STA_OK;

  // Release configuration pins
  IF_PIN(ctx->reset_pin_num) {
    SYS_IO_UNLOCK_PIN(ctx->reset_gpio_device_id, ctx->reset_pin_num);
    status_rep_t r = sys_io_reset(ctx->reset_gpio_device_id, ctx->reset_pin_num);
    if (STA_IS_ERR(r)) status = r;
  }
  IF_PIN(ctx->intr_pin_num) {
    SYS_IO_UNLOCK_PIN(ctx->intr_gpio_device_id, ctx->intr_pin_num);
    status_rep_t r = sys_io_reset(ctx->intr_gpio_device_id, ctx->intr_pin_num);
    if (STA_IS_ERR(r)) status = r;
  }

  // Unregister driver references safely
  status_rep_t r = sys_io_unregister_driver(ctx->base.device_id);
  if (STA_IS_ERR(r)) status = r;
  r = sys_i2c_remove_driver(ctx->base.hw_handle);
  if (STA_IS_ERR(r)) status = r;

  // Delete driver context and free adapter ctx
  example_driver_delete(hw);
  free(ctx);
  return status;
}

static status_rep_t device_reset(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(example_adapter_ctx_t, example_handle_t, ctx, hw, handle);

  // Perform physical reset using GPIO pin if available
  IF_PIN(ctx->reset_pin_num) {
    WITH_PIN_UNLOCKED(ctx->reset_gpio_device_id, ctx->reset_pin_num) {
      STA_R_ON_ERR(SYS_IO_LOW(ctx->reset_gpio_device_id, ctx->reset_pin_num));
      vTaskDelay(pdMS_TO_TICKS(10));
      STA_R_ON_ERR(SYS_IO_HIGH(ctx->reset_gpio_device_id, ctx->reset_pin_num));
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }

  SYS_DEV_CHECK_DRIVER_CALL(example_driver_reset(hw), ctx);
  return STA_OK;
}

static status_rep_t device_suspend(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(example_adapter_ctx_t, example_handle_t, ctx, hw, handle);
  
  // Power down device
  SYS_DEV_CHECK_DRIVER_CALL(example_driver_set_power_mode(hw, false), ctx);
  return STA_OK;
}

static status_rep_t device_resume(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(example_adapter_ctx_t, example_handle_t, ctx, hw, handle);
  
  // Power up device
  SYS_DEV_CHECK_DRIVER_CALL(example_driver_set_power_mode(hw, true), ctx);
  return STA_OK;
}

static status_rep_t device_freeze(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(example_adapter_ctx_t, example_handle_t, ctx, hw, handle);
  IF_SYS_DEV_FROZEN(ctx) { return STA_OK; }
  SYS_DEV_CTX_FREEZE(ctx);

  // Cache current inputs
  SYS_DEV_CHECK_DRIVER_CALL(example_driver_read_pins(hw, &ctx->cached_inputs), ctx);
  ctx->frozen_outputs_mask = 0;
  ctx->frozen_outputs_state = 0;
  return STA_OK;
}

static status_rep_t device_sync(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(example_adapter_ctx_t, example_handle_t, ctx, hw, handle);
  SYS_DEV_CTX_UNFREEZE(ctx);

  // Write all deferred output levels accumulated during freeze
  if (ctx->frozen_outputs_mask != 0) {
    SYS_DEV_CHECK_DRIVER_CALL(example_driver_write_pins(hw, ctx->frozen_outputs_mask, ctx->frozen_outputs_state), ctx);
    ctx->frozen_outputs_mask = 0;
  }
  return STA_OK;
}

static status_rep_t device_error_handler(void* handle, status_rep_t* error) {
  ESP_LOGE(TAG, "Device error caught: owner=%d, code=%d", error->e_owner, error->e_code);
  return device_reset(handle);
}

// =============================================================================
// --- The Installation Function ---
// Unpacks creation arguments, allocates context, registers driver, and applies defaults.
// =============================================================================

static void* device_install(void** args) {
  SYS_DEV_ARG_UNPACK(uint8_t, device_id, args, 0);
  SYS_DEV_ARG_UNPACK(bool, i2c_bus, args, 1);
  SYS_DEV_ARG_UNPACK(uint8_t, i2c_addr, args, 2);
  SYS_DEV_ARG_UNPACK(uint8_t, intr_io_device, args, 3);
  SYS_DEV_ARG_UNPACK(sys_io_pin_num_t, intr_io_num, args, 4);
  SYS_DEV_ARG_UNPACK(sys_io_mode_e, intr_io_mode, args, 5);
  SYS_DEV_ARG_UNPACK(uint8_t, rst_io_device, args, 6);
  SYS_DEV_ARG_UNPACK(sys_io_pin_num_t, rst_io_num, args, 7);
  SYS_DEV_ARG_UNPACK(sys_io_mode_e, rst_io_mode, args, 8);

  example_adapter_ctx_t* ctx = sys_device_allocate_ctx(sizeof(example_adapter_ctx_t), args);
  if (!ctx) return NULL;

  ctx->intr_gpio_device_id = intr_io_device;
  ctx->intr_pin_num = intr_io_num;
  ctx->reset_gpio_device_id = rst_io_device;
  ctx->reset_pin_num = rst_io_num;

  ctx->base.hw_handle = example_driver_new(i2c_addr, i2c_bus);
  if (!ctx->base.hw_handle) {
    free(ctx);
    return NULL;
  }

  // Bind register fault callbacks
  example_handle_t hw = (example_handle_t)(ctx->base.hw_handle);
  example_driver_register_change_callback(hw, example_on_change_handler, ctx);

  if (STA_IS_ERR(sys_i2c_add_driver(ctx->base.hw_handle))) {
    goto fail;
  }

  // Setup HW reset pin
  IF_PIN(rst_io_num) {
    if (STA_IS_ERR(sys_io_set_mode(rst_io_device, rst_io_num, rst_io_mode))) {
      goto fail;
    }
    SYS_IO_HIGH(rst_io_device, rst_io_num);
    vTaskDelay(pdMS_TO_TICKS(10));
    SYS_IO_LOCK_PIN(rst_io_device, rst_io_num);
  }

  // Setup HW interrupt pin
  IF_PIN(intr_io_num) {
    if (STA_IS_ERR(sys_io_set_mode(intr_io_device, intr_io_num, intr_io_mode))) {
      goto fail;
    }
    sys_io_intr_config_t config = {
        .mode = SYS_IO_INTR_MODE_FALLING_EDGE,
        .callback = (sys_io_isr_callback_t)(void*)example_adapter_isr,
        .user_ctx = ctx
    };
    if (STA_IS_ERR(sys_io_configure_intr(intr_io_device, intr_io_num, &config))) {
      goto fail;
    }
    SYS_IO_LOCK_PIN(intr_io_device, intr_io_num);
  }

  // Register device to the central sys_io manager
  if (STA_IS_ERR(sys_io_register_driver(device_id, ctx, &io_example_vtable))) {
    goto fail;
  }

  return ctx;

fail:
  device_uninstall(ctx);
  return NULL;
}

// =============================================================================
// --- Exposed Creation API ---
// Packs args and installs device into the central sys_device manager.
// =============================================================================

status_rep_t d_example_create(uint8_t device_id, bool i2c_bus, uint8_t i2c_addr,
                              uint8_t intr_io_device, sys_io_pin_num_t intr_io_num, sys_io_mode_e intr_io_mode,
                              uint8_t rst_io_device, sys_io_pin_num_t rst_io_num, sys_io_mode_e rst_io_mode) {
  void* args[] = {
      SYS_DEV_ARG_PACK(device_id),
      SYS_DEV_ARG_PACK(i2c_bus),
      SYS_DEV_ARG_PACK(i2c_addr),
      SYS_DEV_ARG_PACK(intr_io_device),
      SYS_DEV_ARG_PACK(intr_io_num),
      SYS_DEV_ARG_PACK(intr_io_mode),
      SYS_DEV_ARG_PACK(rst_io_device),
      SYS_DEV_ARG_PACK(rst_io_num),
      SYS_DEV_ARG_PACK(rst_io_mode)
  };

  sys_device_t dev = {
      .device_id = device_id,
      .role = SYS_DEV_ROLE_IO,
      .name = "EXAMPLE_GPIO_EXPANDER",
      .install_args = args,
      .install_device = device_install,
      .uninstall_device = device_uninstall,
      .reset_device = device_reset,
      .error_handler = device_error_handler,
      .suspend_device = device_suspend,
      .resume_device = device_resume,
      .freeze_device = device_freeze,
      .sync_device = device_sync
  };

  return sys_device_install(&dev);
}