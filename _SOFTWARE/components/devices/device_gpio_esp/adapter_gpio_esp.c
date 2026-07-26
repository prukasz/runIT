#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "device_gpio_esp.h"
#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_continuous.h"
#include "esp_adc_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "shared_io_types.h"
#include "sys_device.h"
#include "sys_error.h"
#include "sys_error_codes.h"
#include "sys_error_io.h"
#include "sys_io.h"
#include "utils.h"

#undef OWNER
#define OWNER OWNER_DEVICE_GPIO_ESP
static const char* TAG = __FILE_NAME__;

static const uint64_t pin_bitmask = SOC_GPIO_VALID_GPIO_MASK;

#define CONFIG_ESP_GPIO_DEBOUNCE_TIME_US 50000

esp_pin_obj_t pin_pool[GPIO_NUM_MAX] = {0};
uint64_t configured_pins = 0;
gpio_esp_ctx_t gpio_esp_ctx = {0};
static gpio_esp_ctx_t* const ctx = &gpio_esp_ctx;
R_MUTEX_DEFINE(gpio_mutex);

// GPIO mode -> ESP-IDF gpio_config_t fields. GPIO_MODE_DISABLE (0) marks a
// mode this table doesn't support, so validity is a single lookup.
static const struct {
  gpio_mode_t mode;
  gpio_pullup_t pull_up;
  gpio_pulldown_t pull_down;
} k_gpio_mode_map[] = {
    [SYS_IO_MODE_INPUT] = {GPIO_MODE_INPUT, GPIO_PULLUP_DISABLE, GPIO_PULLDOWN_DISABLE},
    [SYS_IO_MODE_INPUT_PULLUP] = {GPIO_MODE_INPUT, GPIO_PULLUP_ENABLE, GPIO_PULLDOWN_DISABLE},
    [SYS_IO_MODE_INPUT_PULLDOWN] = {GPIO_MODE_INPUT, GPIO_PULLUP_DISABLE, GPIO_PULLDOWN_ENABLE},
    [SYS_IO_MODE_OUTPUT_PUSH_PULL] = {GPIO_MODE_INPUT_OUTPUT, GPIO_PULLUP_DISABLE, GPIO_PULLDOWN_DISABLE},
    [SYS_IO_MODE_OUTPUT_OPEN_DRAIN] = {GPIO_MODE_INPUT_OUTPUT_OD, GPIO_PULLUP_DISABLE, GPIO_PULLDOWN_DISABLE},
};

// sys_io interrupt mode -> ESP-IDF interrupt type. GPIO_INTR_DISABLE (0)
// marks a mode this table doesn't support.
static const gpio_int_type_t k_gpio_intr_map[] = {
    [SYS_IO_INTR_MODE_RISING_EDGE] = GPIO_INTR_POSEDGE,
    [SYS_IO_INTR_MODE_FALLING_EDGE] = GPIO_INTR_NEGEDGE,
    [SYS_IO_INTR_MODE_BOTH_EDGES] = GPIO_INTR_ANYEDGE,
};

// Ultra-fast ISR Trampoline triggering callback directly
static void IRAM_ATTR _gpio_pin_isr_trampoline(void* arg) {
  esp_pin_obj_t* pin = (esp_pin_obj_t*)arg;
  if (!pin) return;

  uint64_t current_time = esp_timer_get_time();
  if ((current_time - pin->last_isr_time) < CONFIG_ESP_GPIO_DEBOUNCE_TIME_US) {
    return;
  }
  pin->last_isr_time = current_time;

  int level = gpio_get_level((gpio_num_t)pin->io_num);
  if (pin->intr_config.own_func.own_func) {
    SYS_CB_OWN(pin->intr_config.own_func);
  } else {
    SYS_IO_CB(ctx, pin->io_num, pin->intr_config.mode, level, pin->intr_config.route_mask);
  }
}

// --- VTABLE Implementations (IO Contract) ---

static err_h contract_io_gpio_esp_reset_pin(void* handle, sys_io_pin_num_t pin) {
  VERIFY_PIN(SYS_DEV_GET_ID(ctx), pin, pin_bitmask);

  bool was_adc = false;
  adc_cali_handle_t cali_handle = NULL;
  bool was_configured = false;

  if (R_MUTEX_LOCK(gpio_mutex, portMAX_DELAY) == pdTRUE) {
    esp_pin_obj_t* pin_obj = pin_obj_get(pin);
    if (pin_obj != NULL) {
      was_configured = true;
      was_adc = (pin_obj->pin_mode == SYS_IO_MODE_ADC);
      cali_handle = pin_obj->hw.adc_cfg.cali_handle;
      configured_pins &= ~(1ULL << pin);
      memset(pin_obj, 0, sizeof(*pin_obj));
    }
    R_MUTEX_UNLOCK(gpio_mutex);
  }

  if (!was_configured) return NULL;

  gpio_reset_pin((gpio_num_t)pin);
  gpio_isr_handler_remove((gpio_num_t)pin);
  gpio_set_intr_type((gpio_num_t)pin, GPIO_INTR_DISABLE);

  if (was_adc) {
    if (cali_handle != NULL) {
      adc_cali_delete_scheme_curve_fitting(cali_handle);
    }
    esp_adc_update_active_channels();
  }

  ESP_LOGI(TAG, "Reset GPIO pin %d", pin);
  return NULL;
}

static err_h contract_io_gpio_esp_set_mode(void* handle, sys_io_pin_num_t pin, sys_io_mode_e mode) {
  VERIFY_PIN(SYS_DEV_GET_ID(ctx), pin, pin_bitmask);

  err_h err = NULL;
  esp_pin_obj_t* new_pin = NULL;
  bool needs_adc_update = false;

  if (R_MUTEX_LOCK(gpio_mutex, portMAX_DELAY) != pdTRUE) {
    SE_RET_ERR(ERR_ESP_ERR, 0);
  }

  if (configured_pins & (1ULL << pin)) {
    SE_SET_ERR(err, ERR_IO_PIN_ALREADY_IN_USE, SYS_DEV_GET_ID(ctx), pin, mode);
    goto cleanup;
  }

  new_pin = &pin_pool[pin];
  memset(new_pin, 0, sizeof(*new_pin));
  new_pin->io_num = pin;
  new_pin->pin_mode = mode;

  if (mode == SYS_IO_MODE_ADC) {
    adc_channel_t channel = 0;
    adc_unit_t unit = 0;
    esp_err_t esp_err = adc_continuous_io_to_channel(pin, &unit, &channel);
    if (esp_err != ESP_OK || unit != ADC_UNIT_1) {
      SE_SET_ERR(err, ERR_IO_PIN_FEATURE_UNSUPPORTED, pin, 0);
      goto cleanup;
    }

    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .chan = channel,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = SOC_ADC_DIGI_MAX_BITWIDTH,
    };
    esp_err = adc_cali_create_scheme_curve_fitting(&cali_config, &new_pin->hw.adc_cfg.cali_handle);
    if (esp_err != ESP_OK) {
      SE_SET_ERR(err, ERR_ESP_ERR, esp_err);
      goto cleanup;
    }

    configured_pins |= (1ULL << pin);
    needs_adc_update = true;
  } else {
    if (mode >= (sizeof(k_gpio_mode_map) / sizeof(k_gpio_mode_map[0])) || k_gpio_mode_map[mode].mode == GPIO_MODE_DISABLE) {
      SE_SET_ERR(err, ERR_BASE_NOT_SUPPORTED, 0);
      goto cleanup;
    }

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin),
        .mode = k_gpio_mode_map[mode].mode,
        .pull_up_en = k_gpio_mode_map[mode].pull_up,
        .pull_down_en = k_gpio_mode_map[mode].pull_down,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t esp_err = gpio_config(&cfg);
    if (esp_err != ESP_OK) {
      SE_SET_ERR(err, ERR_ESP_ERR, esp_err);
      goto cleanup;
    }

    new_pin->hw.gpio_cfg = cfg;
    configured_pins |= (1ULL << pin);
  }

cleanup:
  R_MUTEX_UNLOCK(gpio_mutex);
  // Must run outside the lock: it re-enters gpio_mutex via
  // compute_active_channels_mask(), and gpio_mutex is not recursive.
  if (SE_IS_OK(err) && needs_adc_update) {
    esp_err_t esp_err = esp_adc_update_active_channels();
    if (esp_err != ESP_OK) {
      SE_RET_ERR(ERR_ESP_ERR, esp_err);
    }
  }
  return err;
}

static err_h contract_io_gpio_esp_configure_intr(void* handle, sys_io_pin_num_t pin, const sys_io_intr_config_t* config) {
  SE_CHECK_NOT_NULL(config);
  VERIFY_PIN(SYS_DEV_GET_ID(ctx), pin, pin_bitmask);

  err_h err = NULL;
  bool is_adc = false;

  if (R_MUTEX_LOCK(gpio_mutex, portMAX_DELAY) != pdTRUE) {
    SE_RET_ERR(ERR_ESP_ERR, 0);
  }

  esp_pin_obj_t* pin_obj = pin_obj_get(pin);
  if (pin_obj == NULL) {
    SE_SET_ERR(err, ERR_IO_PIN_FEATURE_UNSUPPORTED, pin, 0);
    goto cleanup;
  }

  if (config->mode == SYS_IO_INTR_DISABLE) {
    gpio_isr_handler_remove((gpio_num_t)pin);
    gpio_set_intr_type((gpio_num_t)pin, GPIO_INTR_DISABLE);
    pin_obj->intr_config.mode = SYS_IO_INTR_DISABLE;
    goto cleanup;
  }

  if (pin_obj->pin_mode == SYS_IO_MODE_ADC) {
    pin_obj->intr_config = *config;
    is_adc = true;
    goto cleanup;
  }

  if (config->mode >= (sizeof(k_gpio_intr_map) / sizeof(k_gpio_intr_map[0])) || k_gpio_intr_map[config->mode] == GPIO_INTR_DISABLE) {
    SE_SET_ERR(err, ERR_IO_PIN_FEATURE_UNSUPPORTED, pin, 0);
    goto cleanup;
  }

  pin_obj->intr_config = *config;

  esp_err_t esp_err = gpio_set_intr_type((gpio_num_t)pin, k_gpio_intr_map[config->mode]);
  if (esp_err != ESP_OK) {
    SE_SET_ERR(err, ERR_ESP_ERR, esp_err);
    goto cleanup;
  }

  esp_err = gpio_isr_handler_add((gpio_num_t)pin, _gpio_pin_isr_trampoline, pin_obj);
  if (esp_err != ESP_OK) {
    SE_SET_ERR(err, ERR_ESP_ERR, esp_err);
    goto cleanup;
  }

cleanup:
  R_MUTEX_UNLOCK(gpio_mutex);
  if (SE_IS_OK(err) && is_adc) {
    esp_err_t update_err = esp_adc_update_active_channels();
    if (update_err != ESP_OK) {
      SE_RET_ERR(ERR_ESP_ERR, 0);
    }
  }
  return err;
}

static err_h contract_io_gpio_esp_set_level(void* handle, sys_io_pin_num_t pin, bool level) {
  VERIFY_PIN(SYS_DEV_GET_ID(ctx), pin, pin_bitmask);

  err_h err = NULL;
  if (R_MUTEX_LOCK(gpio_mutex, portMAX_DELAY) != pdTRUE) {
    SE_RET_ERR(ERR_ESP_ERR, 0);
  }

  esp_pin_obj_t* pin_obj = pin_obj_get(pin);
  if (pin_obj == NULL) {
    SE_SET_ERR(err, ERR_IO_PIN_FEATURE_UNSUPPORTED, SYS_DEV_GET_ID(ctx), pin);
    goto cleanup;
  }

  if (pin_obj->pin_mode != SYS_IO_MODE_OUTPUT_PUSH_PULL && pin_obj->pin_mode != SYS_IO_MODE_OUTPUT_OPEN_DRAIN) {
    SE_SET_ERR(err, ERR_IO_PIN_ALREADY_IN_USE, SYS_DEV_GET_ID(ctx), pin, pin_obj->pin_mode);
    goto cleanup;
  }

  uint64_t pin_mask = 1ULL << pin;
  IF_SYS_DEV_FROZEN(ctx) {
    ctx->pending_outputs |= pin_mask;
    if (level) {
      ctx->current_outputs |= pin_mask;
    } else {
      ctx->current_outputs &= ~pin_mask;
    }
  }
  else {
    esp_err_t esp_err = gpio_set_level((gpio_num_t)pin, level);
    if (esp_err != ESP_OK) {
      SE_SET_ERR(err, ERR_ESP_ERR, esp_err);
      goto cleanup;
    }
    if (level) {
      ctx->current_outputs |= pin_mask;
    } else {
      ctx->current_outputs &= ~pin_mask;
    }
  }

cleanup:
  R_MUTEX_UNLOCK(gpio_mutex);
  return err;
}

static err_h contract_io_gpio_esp_get_level(void* handle, sys_io_pin_num_t pin, bool* level) {
  SE_CHECK_NOT_NULL(level);
  VERIFY_PIN(SYS_DEV_GET_ID(ctx), pin, pin_bitmask);

  err_h err = NULL;
  if (R_MUTEX_LOCK(gpio_mutex, portMAX_DELAY) != pdTRUE) {
    SE_RET_ERR(ERR_ESP_ERR, 0);
  }

  esp_pin_obj_t* pin_obj = pin_obj_get(pin);
  if (pin_obj == NULL) {
    SE_SET_ERR(err, ERR_IO_PIN_FEATURE_UNSUPPORTED, SYS_DEV_GET_ID(ctx), pin);
    goto cleanup;
  }

  if (pin_obj->pin_mode == SYS_IO_MODE_ADC || pin_obj->pin_mode == SYS_IO_MODE_PWM || pin_obj->pin_mode == SYS_IO_MODE_DAC) {
    SE_SET_ERR(err, ERR_IO_PIN_ALREADY_IN_USE, SYS_DEV_GET_ID(ctx), pin, pin_obj->pin_mode);
    goto cleanup;
  }

  uint64_t pin_mask = 1ULL << pin;
  IF_SYS_DEV_FROZEN(ctx) {
    if (pin_obj->pin_mode == SYS_IO_MODE_OUTPUT_PUSH_PULL || pin_obj->pin_mode == SYS_IO_MODE_OUTPUT_OPEN_DRAIN) {
      *level = (ctx->current_outputs & pin_mask) ? true : false;
    } else {
      *level = (ctx->cached_inputs & pin_mask) ? true : false;
    }
  }
  else {
    int val = gpio_get_level((gpio_num_t)pin);
    if (val < 0) {
      SE_SET_ERR(err, ERR_ESP_ERR, val);
      goto cleanup;
    }
    *level = (val > 0);
  }

cleanup:
  R_MUTEX_UNLOCK(gpio_mutex);
  return err;
}

static err_h contract_io_gpio_esp_toggle(void* handle, sys_io_pin_num_t pin) {
  bool current;
  SE_RET_IF_ERR(contract_io_gpio_esp_get_level(handle, pin, &current));
  return contract_io_gpio_esp_set_level(handle, pin, !current);
}

static err_h contract_io_gpio_esp_get_voltage(void* handle, sys_io_pin_num_t pin, uint32_t* out_mV) {
  SE_CHECK_NOT_NULL(out_mV);
  VERIFY_PIN(SYS_DEV_GET_ID(ctx), pin, pin_bitmask);

  // esp_adc_get_mv() already validates the pin range, that it's configured,
  // and that its mode is SYS_IO_MODE_ADC - no need to duplicate that here.
  esp_err_t esp_err = esp_adc_get_mv(pin, out_mV);
  if (esp_err != ESP_OK) {
    SE_RET_ERR(ERR_IO_PIN_FEATURE_UNSUPPORTED, pin, 0);
  }
  return NULL;
}

// Instantiate the static VTable
static sys_io_vtable_t io_gpio_esp_vtable = {.io_reset = contract_io_gpio_esp_reset_pin,
    .io_set_mode = contract_io_gpio_esp_set_mode,
    .io_configure_intr = contract_io_gpio_esp_configure_intr,
    .io_set_level = contract_io_gpio_esp_set_level,
    .io_get_level = contract_io_gpio_esp_get_level,
    .io_toggle = contract_io_gpio_esp_toggle,
    .io_get_voltage = contract_io_gpio_esp_get_voltage,
    .io_set_voltage = NULL,
    .io_set_pwm_frequency = NULL,
    .io_set_pwm_duty = NULL,
    .protected_pins = 0};

// --- sys_device_t Implementations ---

static err_h device_uninstall(void* handle) {
  err_h err = NULL;

  for (int i = 0; i < GPIO_NUM_MAX; i++) {
    if (configured_pins & (1ULL << i)) {
      err_h r = contract_io_gpio_esp_reset_pin(ctx, i);
      if (SE_IS_ERR(r)) err = r;
    }
  }

  memset(&gpio_esp_ctx, 0, sizeof(gpio_esp_ctx_t));
  return err;
}

static err_h device_reset(void* handle) {
  for (int i = 0; i < GPIO_NUM_MAX; i++) {
    if (configured_pins & (1ULL << i)) {
      SE_RET_IF_ERR(contract_io_gpio_esp_reset_pin(ctx, i));
    }
  }
  return NULL;
}

// Snapshots every non-ADC input pin's level into ctx->cached_inputs. Shared
// by freeze and suspend, which capture state identically.
static void gpio_esp_snapshot_inputs(void) {
  ctx->cached_inputs = 0;
  if (R_MUTEX_LOCK(gpio_mutex, portMAX_DELAY) == pdTRUE) {
    for (int i = 0; i < GPIO_NUM_MAX; i++) {
      esp_pin_obj_t* pin_obj = pin_obj_get(i);
      if (pin_obj && pin_obj->pin_mode != SYS_IO_MODE_ADC) {
        int level = gpio_get_level(i);
        if (level > 0) {
          ctx->cached_inputs |= (1ULL << i);
        }
      }
    }
    R_MUTEX_UNLOCK(gpio_mutex);
  }
}

// Drains every pending deferred output write. Shared by sync and resume,
// which flush state identically.
static err_h gpio_esp_flush_pending_outputs(void) {
  if (ctx->pending_outputs != 0) {
    for (int i = 0; i < GPIO_NUM_MAX; i++) {
      uint64_t bit = (1ULL << i);
      if (ctx->pending_outputs & bit) {
        bool level = (ctx->current_outputs & bit) ? true : false;
        SYS_DEV_CHECK_DRIVER_CALL(gpio_set_level(i, level), ctx);
      }
    }
    ctx->pending_outputs = 0;
  }
  return NULL;
}

static err_h device_freeze(void* handle) {
  IF_SYS_DEV_FROZEN(ctx) {
    return NULL;
  }
  SYS_DEV_CTX_FREEZE(ctx);
  gpio_esp_snapshot_inputs();
  ctx->pending_outputs = 0;
  return NULL;
}

static err_h device_sync(void* handle) {
  SYS_DEV_CTX_UNFREEZE(ctx);
  return gpio_esp_flush_pending_outputs();
}

// suspend/resume alias freeze/sync: this device has no lower-power state
// beyond deferring output writes and snapshotting inputs.
static err_h device_suspend(void* handle) {
  return device_freeze(handle);
}

static err_h device_resume(void* handle) {
  return device_sync(handle);
}

static err_h device_error_handler(void* handle, err_h error) {
  if (!error) return NULL;
  ESP_LOGE(TAG, "GPIO ESP Error: owner=%u, tag=%d", (unsigned int)error->owner, (int)error->tag);
  return error;
}

static err_h device_install(const void* cfg_blob, void** out_device_handle) {
  const d_gpio_esp_cfg_t* cfg = (const d_gpio_esp_cfg_t*)cfg_blob;
  SE_CHECK_NOT_NULL(cfg);
  SE_CHECK_NOT_NULL(out_device_handle);

  memset(&gpio_esp_ctx, 0, sizeof(gpio_esp_ctx_t));
  gpio_esp_ctx.base.device_id = cfg->device_id;
  gpio_esp_ctx.cfg = *cfg;

  static bool s_isr_service_installed = false;
  if (!s_isr_service_installed) {
    esp_err_t isr_err = gpio_install_isr_service(0);
    if (isr_err == ESP_OK || isr_err == ESP_ERR_INVALID_STATE) {
      s_isr_service_installed = true;
    } else {
      ESP_LOGW(TAG, "gpio_install_isr_service returned %d", isr_err);
    }
  }

  esp_adc_start();

  *out_device_handle = ctx;
  return NULL;
}

static const sys_device_class_t s_gpio_esp_class = {
    .name = "GPIO_ESP_NATIVE",
    .contracts = {[SYS_DEVICE_CONTRACT_IO] = (void*)&io_gpio_esp_vtable},
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

err_h d_gpio_esp_create(const d_gpio_esp_cfg_t* cfg) {
  SE_CHECK_NOT_NULL(cfg);
  return SYS_DEVICE_CREATE(&s_gpio_esp_class, cfg);
}
// 540