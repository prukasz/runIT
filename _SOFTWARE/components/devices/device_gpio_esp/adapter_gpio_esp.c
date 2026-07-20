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
#include "status.h"
#include "status_codes.h"
#include "sys_device.h"
#include "sys_io.h"
#include "utils.h"

static const char* TAG = __FILE_NAME__;

static uint64_t pin_bitmask = SOC_GPIO_VALID_GPIO_MASK;

#undef OWNER
#define OWNER OWNER_DEVICE_GPIO_ESP

#define GET_PIN_OBJ_R(pin, pin_obj)                                                                                              \
  esp_pin_obj_t* pin_obj = pin_registry[pin];                                                                                    \
  if ((pin_obj) == NULL) {                                                                                                       \
    return STA_C(ERR_SYS_IO_FEATURE_UNAVAILABLE, OWNER, SYS_IO_MAKE_INFO(ctx->base.device_id, (pin), 0), STATUS_PAYLOAD_DEV_SOLO); \
  }

esp_pin_obj_t* pin_registry[GPIO_NUM_MAX] = {0};
gpio_esp_ctx_t gpio_esp_ctx = {0};
static gpio_esp_ctx_t* const ctx = &gpio_esp_ctx;
R_MUTEX_DEFINE(gpio_mutex);

#define CONFIG_MAX_PENDING_GPIO_ESP_INTR 32
#define CONFIG_ESP_GPIO_DEBOUNCE_TIME_US 50000
R_QUEUE_DEFINE(gpio_evt_queue, CONFIG_MAX_PENDING_GPIO_ESP_INTR, sizeof(uint8_t));
R_TASK_DEFINE(gpio_dispatcher_task, 4096);

// Background task that executes the user's callbacks safely
static void gpio_dispatcher_task_function(void* arg) {
  uint8_t pin_num;
  while (1) {
    if (xQueueReceive(gpio_evt_queue, &pin_num, portMAX_DELAY)) {
      sys_io_isr_callback_t callback = NULL;
      sys_io_intr_event_t event;

      if (R_MUTEX_LOCK(gpio_mutex, portMAX_DELAY) == pdTRUE) {
        esp_pin_obj_t* pin_obj = pin_registry[pin_num];
        if (pin_obj && pin_obj->intr_config.callback) {
          callback = pin_obj->intr_config.callback;
          event = (sys_io_intr_event_t){.device_id = ctx->base.device_id, .pin_num = pin_obj->io_num, .triggered_by = pin_obj->intr_config.mode, .user_arg = pin_obj->intr_config.user_ctx};
        }
        R_MUTEX_UNLOCK(gpio_mutex);
      }

      if (callback != NULL) {
        callback(&event);
      }
    }
  }
}

// Ultra-fast ISR Trampoline
static void IRAM_ATTR _gpio_pin_isr_trampoline(void* arg) {
  esp_pin_obj_t* pin = (esp_pin_obj_t*)arg;

  uint64_t current_time = esp_timer_get_time();
  if ((current_time - pin->last_isr_time) < CONFIG_ESP_GPIO_DEBOUNCE_TIME_US) {
    return;
  }
  pin->last_isr_time = current_time;

  uint8_t pin_num = (uint8_t)pin->io_num;
  BaseType_t high_task_wakeup = pdFALSE;
  xQueueSendFromISR(gpio_evt_queue, &pin_num, &high_task_wakeup);

  if (high_task_wakeup == pdTRUE) {
    portYIELD_FROM_ISR();
  }
}

// --- VTABLE Implementations (IO Contract) ---

static status_rep_t contract_io_gpio_esp_reset_pin(void* handle, sys_io_pin_num_t pin) {
  VERIFY_PIN_R(pin, pin_bitmask);

  esp_pin_obj_t* pin_obj = NULL;
  if (R_MUTEX_LOCK(gpio_mutex, portMAX_DELAY) == pdTRUE) {
    pin_obj = pin_registry[pin];
    pin_registry[pin] = NULL;
    R_MUTEX_UNLOCK(gpio_mutex);
  }

  if (pin_obj == NULL) return STA_OK;

  SYS_DEV_CHECK_DRIVER_CALL(gpio_reset_pin((gpio_num_t)pin), ctx);
  SYS_DEV_CHECK_DRIVER_CALL(gpio_isr_handler_remove((gpio_num_t)pin), ctx);
  SYS_DEV_CHECK_DRIVER_CALL(gpio_set_intr_type((gpio_num_t)pin, GPIO_INTR_DISABLE), ctx);

  if (pin_obj->pin_mode == SYS_IO_MODE_ADC) {
    if (pin_obj->hw.adc_cfg.cali_handle != NULL) {
      SYS_DEV_CHECK_DRIVER_CALL(adc_cali_delete_scheme_curve_fitting(pin_obj->hw.adc_cfg.cali_handle), ctx);
    }
    esp_err_t err = esp_adc_update_active_channels();
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "ADC update failed during reset: %d", err);
    }
  }

  free(pin_obj);
  ESP_LOGI(TAG, "Reset GPIO pin %d", pin);
  return STA_OK;
}

static status_rep_t contract_io_gpio_esp_set_mode(void* handle, sys_io_pin_num_t pin, sys_io_mode_e mode) {
  VERIFY_PIN_R(pin, pin_bitmask);

  bool pin_busy = false;
  if (R_MUTEX_LOCK(gpio_mutex, portMAX_DELAY) == pdTRUE) {
    if (pin_registry[pin] != NULL) {
      pin_busy = true;
    }
    R_MUTEX_UNLOCK(gpio_mutex);
  }

  if (pin_busy) {
    return STA_C(ERR_SYS_IO_PIN_IN_OTHER_USE, OWNER, SYS_IO_MAKE_INFO(ctx->base.device_id, pin, 0), STATUS_PAYLOAD_DEV_SOLO);
  }

  esp_pin_obj_t* new_pin = calloc(1, sizeof(esp_pin_obj_t));
  if (new_pin == NULL) {
    return STA_C(ERR_NO_MEM, OWNER, SYS_IO_MAKE_INFO(ctx->base.device_id, pin, 0), STATUS_PAYLOAD_DEV_SOLO);
  }
  new_pin->io_num = pin;
  new_pin->pin_mode = mode;

  if (mode == SYS_IO_MODE_ADC) {
    adc_channel_t channel = 0;
    adc_unit_t unit = 0;
    esp_err_t err = adc_continuous_io_to_channel(pin, &unit, &channel);
    if (err != ESP_OK || unit != ADC_UNIT_1) {
      free(new_pin);
      return STA_C(ERR_SYS_IO_FEATURE_UNAVAILABLE, OWNER, SYS_IO_MAKE_INFO(ctx->base.device_id, pin, err), STATUS_PAYLOAD_DEV_SOLO);
    }

    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .chan = channel,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = SOC_ADC_DIGI_MAX_BITWIDTH,
    };
    esp_err_t cali_err = adc_cali_create_scheme_curve_fitting(&cali_config, &new_pin->hw.adc_cfg.cali_handle);
    if (cali_err != ESP_OK) {
      free(new_pin);
      return STA_C(ERR_HARDWARE_FAULT, OWNER, SYS_IO_MAKE_INFO(ctx->base.device_id, pin, cali_err), STATUS_PAYLOAD_DEV_SOLO);
    }

    if (R_MUTEX_LOCK(gpio_mutex, portMAX_DELAY) == pdTRUE) {
      pin_registry[pin] = new_pin;
      R_MUTEX_UNLOCK(gpio_mutex);
    }
    esp_err_t update_err = esp_adc_update_active_channels();
    if (update_err != ESP_OK) {
      if (R_MUTEX_LOCK(gpio_mutex, portMAX_DELAY) == pdTRUE) {
        pin_registry[pin] = NULL;
        R_MUTEX_UNLOCK(gpio_mutex);
      }
      adc_cali_delete_scheme_curve_fitting(new_pin->hw.adc_cfg.cali_handle);
      free(new_pin);
      return STA_C(ERR_HARDWARE_FAULT, OWNER, SYS_IO_MAKE_INFO(ctx->base.device_id, pin, update_err), STATUS_PAYLOAD_DEV_SOLO);
    }
    return STA_OK;
  }

  gpio_config_t cfg = {
      .pin_bit_mask = (1ULL << pin),
      .intr_type = GPIO_INTR_DISABLE,
  };

  switch (mode) {
    case SYS_IO_MODE_INPUT:
      cfg.mode = GPIO_MODE_INPUT;
      cfg.pull_up_en = GPIO_PULLUP_DISABLE;
      cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
      break;
    case SYS_IO_MODE_INPUT_PULLUP:
      cfg.mode = GPIO_MODE_INPUT;
      cfg.pull_up_en = GPIO_PULLUP_ENABLE;
      cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
      break;
    case SYS_IO_MODE_INPUT_PULLDOWN:
      cfg.mode = GPIO_MODE_INPUT;
      cfg.pull_up_en = GPIO_PULLUP_DISABLE;
      cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
      break;
    case SYS_IO_MODE_OUTPUT_PUSH_PULL:
      cfg.mode = GPIO_MODE_INPUT_OUTPUT;
      cfg.pull_up_en = GPIO_PULLUP_DISABLE;
      cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
      break;
    case SYS_IO_MODE_OUTPUT_OPEN_DRAIN:
      cfg.mode = GPIO_MODE_INPUT_OUTPUT_OD;
      cfg.pull_up_en = GPIO_PULLUP_DISABLE;
      cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
      break;
    default:
      free(new_pin);
      return STA_C(ERR_NOT_SUPPORTED, OWNER, SYS_IO_MAKE_INFO(ctx->base.device_id, pin, 0), STATUS_PAYLOAD_DEV_SOLO);
  }

  esp_err_t err = gpio_config(&cfg);
  if (err != ESP_OK) {
    free(new_pin);
    return STA_C(ERR_HARDWARE_FAULT, OWNER, SYS_IO_MAKE_INFO(ctx->base.device_id, pin, err), STATUS_PAYLOAD_DEV_SOLO);
  }

  new_pin->hw.gpio_cfg = cfg;
  if (R_MUTEX_LOCK(gpio_mutex, portMAX_DELAY) == pdTRUE) {
    pin_registry[pin] = new_pin;
    R_MUTEX_UNLOCK(gpio_mutex);
  }
  return STA_OK;
}

static status_rep_t contract_io_gpio_esp_configure_intr(void* handle, sys_io_pin_num_t pin, const sys_io_intr_config_t* config) {
  CHECK_NOT_NULL_R(config);
  VERIFY_PIN_R(pin, pin_bitmask);

  status_rep_t status = STA_OK;
  bool is_adc = false;

  if (R_MUTEX_LOCK(gpio_mutex, portMAX_DELAY) != pdTRUE) {
    return STA_C(ERR_HARDWARE_FAULT, OWNER, SYS_IO_MAKE_INFO(ctx->base.device_id, pin, 0), STATUS_PAYLOAD_DEV_SOLO);
  }

  esp_pin_obj_t* pin_obj = pin_registry[pin];
  if (pin_obj == NULL) {
    status = STA_C(ERR_SYS_IO_FEATURE_UNAVAILABLE, OWNER, SYS_IO_MAKE_INFO(ctx->base.device_id, pin, 0), STATUS_PAYLOAD_DEV_SOLO);
    goto cleanup;
  }

  if (config->mode == SYS_IO_INTR_DISABLE) {
    gpio_isr_handler_remove((gpio_num_t)pin);
    gpio_set_intr_type((gpio_num_t)pin, GPIO_INTR_DISABLE);
    pin_obj->intr_config.callback = NULL;
    pin_obj->intr_config.user_ctx = NULL;
    pin_obj->intr_config.mode = SYS_IO_INTR_DISABLE;
    goto cleanup;
  }

  if (pin_obj->pin_mode == SYS_IO_MODE_ADC) {
    pin_obj->intr_config = *config;
    is_adc = true;
    goto cleanup;
  }

  gpio_int_type_t intr_type = GPIO_INTR_DISABLE;
  switch (config->mode) {
    case SYS_IO_INTR_MODE_RISING_EDGE:
      intr_type = GPIO_INTR_POSEDGE;
      break;
    case SYS_IO_INTR_MODE_FALLING_EDGE:
      intr_type = GPIO_INTR_NEGEDGE;
      break;
    case SYS_IO_INTR_MODE_BOTH_EDGES:
      intr_type = GPIO_INTR_ANYEDGE;
      break;
    default:
      status = STA_C(ERR_SYS_IO_FEATURE_UNAVAILABLE, OWNER, SYS_IO_MAKE_INFO(ctx->base.device_id, pin, config->mode), STATUS_PAYLOAD_DEV_SOLO);
      goto cleanup;
  }

  pin_obj->intr_config = *config;

  esp_err_t err = gpio_set_intr_type((gpio_num_t)pin, intr_type);
  if (err != ESP_OK) {
    status = STA_C(ERR_HARDWARE_FAULT, OWNER, SYS_IO_MAKE_INFO(ctx->base.device_id, pin, err), STATUS_PAYLOAD_DEV_SOLO);
    goto cleanup;
  }

  err = gpio_isr_handler_add((gpio_num_t)pin, _gpio_pin_isr_trampoline, pin_obj);
  if (err != ESP_OK) {
    status = STA_C(ERR_HARDWARE_FAULT, OWNER, SYS_IO_MAKE_INFO(ctx->base.device_id, pin, err), STATUS_PAYLOAD_DEV_SOLO);
    goto cleanup;
  }

cleanup:
  R_MUTEX_UNLOCK(gpio_mutex);
  if (STA_IS_OK(status) && is_adc) {
    esp_err_t update_err = esp_adc_update_active_channels();
    if (update_err != ESP_OK) {
      return STA_C(ERR_HARDWARE_FAULT, OWNER, SYS_IO_MAKE_INFO(ctx->base.device_id, pin, update_err), STATUS_PAYLOAD_DEV_SOLO);
    }
  }
  return status;
}

static status_rep_t contract_io_gpio_esp_set_level(void* handle, sys_io_pin_num_t pin, bool level) {
  VERIFY_PIN_R(pin, pin_bitmask);

  status_rep_t status = STA_OK;
  if (R_MUTEX_LOCK(gpio_mutex, portMAX_DELAY) != pdTRUE) {
    return STA_C(ERR_HARDWARE_FAULT, OWNER, SYS_IO_MAKE_INFO(ctx->base.device_id, pin, 0), STATUS_PAYLOAD_DEV_SOLO);
  }

  esp_pin_obj_t* pin_obj = pin_registry[pin];
  if (pin_obj == NULL) {
    status = STA_C(ERR_SYS_IO_FEATURE_UNAVAILABLE, OWNER, SYS_IO_MAKE_INFO(ctx->base.device_id, pin, 0), STATUS_PAYLOAD_DEV_SOLO);
    goto cleanup;
  }

  if (pin_obj->pin_mode != SYS_IO_MODE_OUTPUT_PUSH_PULL && pin_obj->pin_mode != SYS_IO_MODE_OUTPUT_OPEN_DRAIN) {
    status = STA_C(ERR_SYS_IO_PIN_IN_OTHER_USE, OWNER, SYS_IO_MAKE_INFO(ctx->base.device_id, pin, 0), STATUS_PAYLOAD_DEV_SOLO);
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
    esp_err_t err = gpio_set_level((gpio_num_t)pin, level);
    if (err != ESP_OK) {
      status = STA_C(ERR_HARDWARE_FAULT, OWNER, SYS_IO_MAKE_INFO(ctx->base.device_id, pin, err), STATUS_PAYLOAD_DEV_SOLO);
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
  return status;
}

static status_rep_t contract_io_gpio_esp_get_level(void* handle, sys_io_pin_num_t pin, bool* level) {
  CHECK_NOT_NULL_R(level);
  VERIFY_PIN_R(pin, pin_bitmask);

  status_rep_t status = STA_OK;
  if (R_MUTEX_LOCK(gpio_mutex, portMAX_DELAY) != pdTRUE) {
    return STA_C(ERR_HARDWARE_FAULT, OWNER, SYS_IO_MAKE_INFO(ctx->base.device_id, pin, 0), STATUS_PAYLOAD_DEV_SOLO);
  }

  esp_pin_obj_t* pin_obj = pin_registry[pin];
  if (pin_obj == NULL) {
    status = STA_C(ERR_SYS_IO_FEATURE_UNAVAILABLE, OWNER, SYS_IO_MAKE_INFO(ctx->base.device_id, pin, 0), STATUS_PAYLOAD_DEV_SOLO);
    goto cleanup;
  }

  if (pin_obj->pin_mode == SYS_IO_MODE_ADC || pin_obj->pin_mode == SYS_IO_MODE_PWM || pin_obj->pin_mode == SYS_IO_MODE_DAC) {
    status = STA_C(ERR_SYS_IO_PIN_IN_OTHER_USE, OWNER, SYS_IO_MAKE_INFO(ctx->base.device_id, pin, 0), STATUS_PAYLOAD_DEV_SOLO);
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
      status = STA_C(ERR_HARDWARE_FAULT, OWNER, SYS_IO_MAKE_INFO(ctx->base.device_id, pin, val), STATUS_PAYLOAD_DEV_SOLO);
      goto cleanup;
    }
    *level = (val > 0);
  }

cleanup:
  R_MUTEX_UNLOCK(gpio_mutex);
  return status;
}

static status_rep_t contract_io_gpio_esp_toggle(void* handle, sys_io_pin_num_t pin) {
  bool current;
  STA_R_ON_ERR(contract_io_gpio_esp_get_level(handle, pin, &current));
  return contract_io_gpio_esp_set_level(handle, pin, !current);
}

static status_rep_t contract_io_gpio_esp_get_voltage(void* handle, sys_io_pin_num_t pin, uint32_t* out_mV) {
  CHECK_NOT_NULL_R(out_mV);
  VERIFY_PIN_R(pin, pin_bitmask);

  status_rep_t status = STA_OK;
  if (R_MUTEX_LOCK(gpio_mutex, portMAX_DELAY) != pdTRUE) {
    return STA_C(ERR_HARDWARE_FAULT, OWNER, SYS_IO_MAKE_INFO(ctx->base.device_id, pin, 0), STATUS_PAYLOAD_DEV_SOLO);
  }

  esp_pin_obj_t* pin_obj = pin_registry[pin];
  if (pin_obj == NULL) {
    status = STA_C(ERR_SYS_IO_FEATURE_UNAVAILABLE, OWNER, SYS_IO_MAKE_INFO(ctx->base.device_id, pin, 0), STATUS_PAYLOAD_DEV_SOLO);
    goto cleanup;
  }

  R_MUTEX_UNLOCK(gpio_mutex);

  esp_err_t err = esp_adc_get_mv(pin, out_mV);
  if (err != ESP_OK) {
    return STA_C(ERR_HARDWARE_FAULT, OWNER, SYS_IO_MAKE_INFO(ctx->base.device_id, pin, err), STATUS_PAYLOAD_DEV_SOLO);
  }
  return STA_OK;

cleanup:
  R_MUTEX_UNLOCK(gpio_mutex);
  return status;
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

static status_rep_t device_uninstall(void* handle) {
  status_rep_t status = STA_OK;

  for (int i = 0; i < GPIO_NUM_MAX; i++) {
    if (pin_registry[i] != NULL) {
      status_rep_t r = contract_io_gpio_esp_reset_pin(ctx, i);
      if (STA_IS_ERR(r)) status = r;
    }
  }

  status_rep_t r = sys_io_unregister_driver(ctx->base.device_id);
  if (STA_IS_ERR(r)) status = r;

  memset(&gpio_esp_ctx, 0, sizeof(gpio_esp_ctx_t));
  return status;
}

static status_rep_t device_reset(void* handle) {
  for (int i = 0; i < GPIO_NUM_MAX; i++) {
    if (pin_registry[i] != NULL) {
      STA_R_ON_ERR(contract_io_gpio_esp_reset_pin(ctx, i));
    }
  }
  return STA_OK;
}

static status_rep_t device_suspend(void* handle) {
  if (!ctx->base.is_frozen) {
    ctx->cached_inputs = 0;
    if (R_MUTEX_LOCK(gpio_mutex, portMAX_DELAY) == pdTRUE) {
      for (int i = 0; i < GPIO_NUM_MAX; i++) {
        esp_pin_obj_t* pin_obj = pin_registry[i];
        if (pin_obj && pin_obj->pin_mode != SYS_IO_MODE_ADC) {
          int level = gpio_get_level(i);
          if (level > 0) {
            ctx->cached_inputs |= (1ULL << i);
          }
        }
      }
      R_MUTEX_UNLOCK(gpio_mutex);
    }
    ctx->pending_outputs = 0;
    ctx->base.is_frozen = true;
  }
  return STA_OK;
}

static status_rep_t device_resume(void* handle) {
  if (ctx->base.is_frozen) {
    ctx->base.is_frozen = false;
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
  }
  return STA_OK;
}

static status_rep_t device_freeze(void* handle) {
  IF_SYS_DEV_FROZEN(ctx) {
    return STA_OK;
  }
  SYS_DEV_CTX_FREEZE(ctx);

  ctx->cached_inputs = 0;
  if (R_MUTEX_LOCK(gpio_mutex, portMAX_DELAY) == pdTRUE) {
    for (int i = 0; i < GPIO_NUM_MAX; i++) {
      esp_pin_obj_t* pin_obj = pin_registry[i];
      if (pin_obj && pin_obj->pin_mode != SYS_IO_MODE_ADC) {
        int level = gpio_get_level(i);
        if (level > 0) {
          ctx->cached_inputs |= (1ULL << i);
        }
      }
    }
    R_MUTEX_UNLOCK(gpio_mutex);
  }
  ctx->pending_outputs = 0;
  return STA_OK;
}

static status_rep_t device_sync(void* handle) {
  SYS_DEV_CTX_UNFREEZE(ctx);

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
  return STA_OK;
}

static status_rep_t device_error_handler(void* handle, status_rep_t* error) {
  ESP_LOGE(TAG, "GPIO ESP Error: owner=%s, code=%s", status_owner_to_name(error->e_owner), status_error_to_name(error->e_code));
  (void)device_reset(handle);
  return STA_OK;
}

static status_rep_t device_install(void** args, void** out_device_handle) {
  SYS_DEV_ARG_UNPACK(uint8_t, device_id, args, 0);

  memset(&gpio_esp_ctx, 0, sizeof(gpio_esp_ctx_t));
  ctx->base.device_id = device_id;

  R_TASK_START_ON_CORE(gpio_dispatcher_task, gpio_dispatcher_task_function, ctx, 10, 0);

  esp_err_t isr_err = gpio_install_isr_service(0);
  if (isr_err != ESP_OK && isr_err != ESP_ERR_INVALID_STATE) {
    ESP_LOGW(TAG, "gpio_install_isr_service returned %d", isr_err);
  }

  esp_adc_start();

  status_rep_t status = sys_io_register_driver(device_id, ctx, &io_gpio_esp_vtable);
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
status_rep_t d_gpio_esp_create(uint8_t device_id) {
  void* args[] = {SYS_DEV_ARG_PACK(device_id)};

  sys_device_t dev = {.device_id = device_id,
      .role = SYS_DEV_ROLE_IO,
      .name = "GPIO_ESP_NATIVE",
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
