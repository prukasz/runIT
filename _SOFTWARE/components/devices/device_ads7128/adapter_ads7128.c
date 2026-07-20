#include <stdlib.h>
#include "device_ads7128.h"
#include "driver_ads7128.h"
#include "esp_log.h"
#include "sys_device.h"
#include "sys_io.h"


static const char* TAG = __FILE_NAME__;

#undef OWNER
#define OWNER OWNER_DEVICE_ADS7128

typedef struct device_ads_t device_ads_t;

// Context structure for routing callbacks back from driver task to adapter callbacks
typedef struct {
  device_ads_t* ctx;
  uint8_t pin;
} ads_pin_ctx_t;

// --- 1. The Encapsulated Adapter Context ---
struct device_ads_t {
  sys_device_adapter_base_t base;
  uint32_t vref_mv;
  float ratio;
  uint8_t intr_gpio_device_id;
  sys_io_pin_num_t intr_pin_num;
  uint16_t cached_analog_values[8];
  uint16_t route_masks[8];
  sys_io_intr_mode_e intr_modes[8];
  ads_pin_ctx_t pin_contexts[8];
};

#define get_hw_handle(ctx) ((ads_handle_t)((ctx)->base.hw_handle))

// --- Helper Functions ---
static inline uint16_t clamp_uint16(uint16_t val, uint16_t min, uint16_t max) {
  if (val < min) return min;
  if (val > max) return max;
  return val;
}

static inline uint8_t clamp_uint8(uint8_t val, uint8_t min, uint8_t max) {
  if (val < min) return min;
  if (val > max) return max;
  return val;
}

// --- Callback Dispatcher (routes from driver to sys_io user callback) ---
static void adapter_on_alert_cb(void* arg) {
  ads_pin_ctx_t* pctx = (ads_pin_ctx_t*)arg;
  if (pctx && pctx->ctx) {
    device_ads_t* ctx = pctx->ctx;
    uint8_t pin = pctx->pin;
    sys_io_intr_mode_e mode = ctx->intr_modes[pin];
    if (mode != SYS_IO_INTR_DISABLE) {
      SYS_IO_CB(ctx, pin, mode, 0, ctx->route_masks[pin]);
    }
  }
}

// --- 2. VTable Implementations ---

static status_rep_t p_adc_expander_read_voltage(void* handle, sys_io_pin_num_t pin, uint32_t* out_mv) {
  SYS_DEV_GET_ADAPTER_CONTEXT(device_ads_t, ads_handle_t, ctx, hw, handle);
  CHECK_HANDLE_R(out_mv);

  VERIFY_PIN_R(pin, 0xFF);

  uint16_t raw;
  IF_SYS_DEV_FROZEN(ctx) {
    raw = ctx->cached_analog_values[pin];
  }
  else {
    // Read and update raw data using driver function
    uint8_t channel_mask = (1 << pin);
    SYS_DEV_CHECK_DRIVER_CALL(ads_analog_ch_read(hw, channel_mask, true), ctx);
    raw = hw->recent_analog_values[pin];
  }

  *out_mv = (uint32_t)(raw * ctx->ratio);
  return STA_OK;
}

static status_rep_t p_adc_expander_configure_intr(void* handle, sys_io_pin_num_t pin, const sys_io_intr_config_t* config) {
  SYS_DEV_GET_ADAPTER_CONTEXT(device_ads_t, ads_handle_t, ctx, hw, handle);
  CHECK_NOT_NULL_R(config);

  VERIFY_PIN_R(pin, 0xFF);

  if (config->mode == SYS_IO_INTR_DISABLE) {
    ctx->route_masks[pin] = 0;
    ctx->intr_modes[pin] = SYS_IO_INTR_DISABLE;
    SYS_DEV_CHECK_DRIVER_CALL(ads_set_alert_cfg(hw, pin + 1, 0, 0, 0, false, true), ctx);
    SYS_DEV_CHECK_DRIVER_CALL(ads_register_alert_callback(hw, 1 << pin, NULL, NULL), ctx);
    return STA_OK;
  }

  // Store user callback locally in the adapter
  ctx->route_masks[pin] = config->route_mask;
  ctx->intr_modes[pin] = config->mode;

  // Setup the pin routing context
  ctx->pin_contexts[pin].ctx = ctx;
  ctx->pin_contexts[pin].pin = pin;

  uint8_t channel = pin + 1;  // Assuming ADS7128 driver expects channels 1-8

  uint16_t h_thres = clamp_uint16((uint16_t)(config->adc.adc_threshold_up_mV / ctx->ratio), 0, 4095);
  uint16_t l_thres = clamp_uint16((uint16_t)(config->adc.adc_threshold_down_mV / ctx->ratio), 0, 4095);
  uint8_t hist = clamp_uint8((uint8_t)(config->adc.adc_threshold_hysteresis_mV / (ctx->ratio * 8)), 0, 15);
  uint8_t event_cnt = clamp_uint8((uint8_t)(config->adc.adc_event_counter_threshold - 1), 0, 15);

  h_thres = h_thres | (hist << 4);
  l_thres = l_thres | (event_cnt << 4);

  uint8_t window_mode = (config->mode == SYS_IO_INTR_ADC_WINDOW_INSIDE) ? 1 : 0;

  SYS_DEV_CHECK_DRIVER_CALL(ads_set_alert_cfg(hw, channel, h_thres, l_thres, window_mode, true, true), ctx);
  SYS_DEV_CHECK_DRIVER_CALL(ads_register_alert_callback(hw, 1 << pin, adapter_on_alert_cb, &ctx->pin_contexts[pin]), ctx);

  return STA_OK;
}

static const sys_io_vtable_t s_adc_vtable = {
    .io_get_voltage = p_adc_expander_read_voltage, .io_configure_intr = p_adc_expander_configure_intr, .io_reset = NULL, .io_set_mode = NULL, .io_set_level = NULL, .io_get_level = NULL, .io_toggle = NULL, .io_set_voltage = NULL, .io_set_pwm_frequency = NULL, .io_set_pwm_duty = NULL};

// --- 3. System Device Manager Callback Implementations ---

static status_rep_t adapter_uninstall_device(void* driver_handle) {
  device_ads_t* ctx = (device_ads_t*)driver_handle;
  CHECK_HANDLE_R(ctx);
  ads_handle_t hw = get_hw_handle(ctx);
  if (hw) {
    IF_PIN(ctx->intr_pin_num) {
      sys_io_reset(ctx->intr_gpio_device_id, ctx->intr_pin_num);
    }
    sys_io_unregister_driver(ctx->base.device_id);
    sys_i2c_remove_driver(ctx->base.hw_handle);
    ads_delete(hw);
  }
  free(ctx);
  return STA_OK;
}

static status_rep_t adapter_reset_device(void* driver_handle) {
  device_ads_t* ctx = (device_ads_t*)driver_handle;
  CHECK_HANDLE_R(ctx);
  ads_handle_t hw = get_hw_handle(ctx);
  CHECK_HANDLE_R(hw);

  for (uint8_t ch = 0; ch < 8; ch++) {
    hw->alert_configs[ch].h_thres_msb = 0;
    hw->alert_configs[ch].histeresis_config.h_thres_lsb = 0;
    hw->alert_configs[ch].histeresis_config.hist = 0;
    hw->alert_configs[ch].l_thres_msb = 0;
    hw->alert_configs[ch].event_count_config.l_thres_lsb = 0;
    hw->alert_configs[ch].event_count_config.event_cnt = 0;
    hw->alert_configs[ch].mode = 0;
    hw->alert_configs[ch].route_to_alert_pin = false;

    SYS_DEV_CHECK_DRIVER_CALL(ads_set_alert_cfg(hw, ch + 1, 0, 0, 0, 0, true), ctx);

    ctx->route_masks[ch] = 0;
    ctx->intr_modes[ch] = SYS_IO_INTR_DISABLE;
  }

  hw->alert_triggered = false;
  return STA_OK;
}

static status_rep_t adapter_error_handler(void* driver_handle, status_rep_t* error) {
  ESP_LOGE(TAG, "Device error: code=%lu, owner=%lu", error->e_code, error->e_owner);
  (void)adapter_reset_device(driver_handle);
  return STA_OK;
}

static status_rep_t adapter_suspend_device(void* driver_handle) {
  return STA_OK;
}

static status_rep_t adapter_resume_device(void* driver_handle) {
  return STA_OK;
}

static status_rep_t adapter_freeze_device(void* driver_handle) {
  device_ads_t* ctx = (device_ads_t*)driver_handle;
  CHECK_HANDLE_R(ctx);
  ads_handle_t hw = get_hw_handle(ctx);
  CHECK_HANDLE_R(hw);

  ctx->base.is_frozen = true;
  // Load and cache all 8 channels from driver
  CHECK_ESP_CALL_R(ads_analog_ch_read(hw, 0xFF, true));
  for (int i = 0; i < 8; i++) {
    ctx->cached_analog_values[i] = hw->recent_analog_values[i];
  }
  return STA_OK;
}

static status_rep_t adapter_sync_device(void* driver_handle) {
  device_ads_t* ctx = (device_ads_t*)driver_handle;
  CHECK_HANDLE_R(ctx);
  ctx->base.is_frozen = false;
  return STA_OK;
}

static void adapter_install_fallback(device_ads_t* ctx) {
  if (ctx) {
    IF_PIN(ctx->intr_pin_num) {
      sys_io_reset(ctx->intr_gpio_device_id, ctx->intr_pin_num);
    }
    if (ctx->base.hw_handle) {
      sys_i2c_remove_driver(ctx->base.hw_handle);
      ads_delete(get_hw_handle(ctx));
    }
    free(ctx);
  }
}

static status_rep_t p_ads7128_install(void** args, void** out_device_handle) {
  device_ads_t* ctx = sys_device_allocate_ctx(sizeof(device_ads_t), &args[2]);
  if (!ctx) return STA_C(ERR_NO_MEM, OWNER, 0, STATUS_PAYLOAD_DEV_SOLO);

  ctx->base.hw_handle = ads_new(SYS_DEV_ARG_UNPACK_VAL(uint8_t, args, 0), SYS_DEV_ARG_UNPACK_VAL(bool, args, 1));
  if (!ctx->base.hw_handle) {
    free(ctx);
    return STA_C(ERR_NO_MEM, OWNER, 0, STATUS_PAYLOAD_DEV_SOLO);
  }

  uint32_t vref_mv = SYS_DEV_ARG_UNPACK_VAL(uint32_t, args, 6);
  ctx->vref_mv = vref_mv;
  ctx->ratio = (float)vref_mv / 4095.0f;

  uint8_t intr_gpio_device_id = SYS_DEV_ARG_UNPACK_VAL(uint8_t, args, 3);
  ctx->intr_gpio_device_id = intr_gpio_device_id;

  sys_io_pin_num_t intr_pin_num = SYS_DEV_ARG_UNPACK_VAL(sys_io_pin_num_t, args, 4);
  ctx->intr_pin_num = intr_pin_num;

  ads_handle_t hw = get_hw_handle(ctx);

  status_rep_t status = sys_i2c_add_driver(ctx->base.hw_handle);
  if (STA_IS_ERR(status)) {
    goto fail;
  }

  status = sys_i2c_device_present(ctx->base.hw_handle);
  if (STA_IS_ERR(status)) {
    status = STA_C(ERR_I2C_DEV_NOT_FOUND, OWNER, DEV_ERR_PACK(ctx->base.device_id, 0, 0), STATUS_PAYLOAD_DEV_SOLO);
    goto fail;
  }

  status = STA_FROM_ESP(ads_start(hw));
  if (STA_IS_ERR(status)) {
    goto fail;
  }

  IF_PIN(intr_pin_num) {
    status = sys_io_set_mode(intr_gpio_device_id, intr_pin_num, SYS_DEV_ARG_UNPACK_VAL(sys_io_mode_e, args, 5));
    if (STA_IS_ERR(status)) {
      goto fail;
    }
    sys_io_intr_config_t config = {.mode = SYS_IO_INTR_MODE_FALLING_EDGE};
    status = sys_io_configure_intr(intr_gpio_device_id, intr_pin_num, &config);
    if (STA_IS_ERR(status)) {
      goto fail;
    }
  }

  uint32_t device_id = SYS_DEV_ARG_UNPACK_VAL(uint32_t, args, 2);
  status = sys_io_register_driver(device_id, ctx, (sys_io_vtable_t*)&s_adc_vtable);
  if (STA_IS_ERR(status)) {
    ESP_LOGE(TAG, "Failed to register ADS7128 to IO Manager on device_id %lu", (unsigned long)device_id);
    goto fail;
  }

  ESP_LOGI(TAG, "ADS7128 successfully installed as IO device %lu", (unsigned long)device_id);
  *out_device_handle = ctx;
  return STA_OK;

fail:
  adapter_install_fallback(ctx);
  *out_device_handle = NULL;
  return status;
}

status_rep_t d_ads7128_create(uint8_t device_id, bool i2c_bus, uint8_t i2c_addr, uint8_t intr_io_device, sys_io_pin_num_t intr_io_num, sys_io_mode_e intr_io_mode, uint32_t vref_mv) {
  void* args[] = {SYS_DEV_ARG_PACK(i2c_addr), SYS_DEV_ARG_PACK(i2c_bus), SYS_DEV_ARG_PACK(device_id), SYS_DEV_ARG_PACK(intr_io_device), SYS_DEV_ARG_PACK(intr_io_num), SYS_DEV_ARG_PACK(intr_io_mode), SYS_DEV_ARG_PACK(vref_mv)};

  sys_device_t dev = {.device_id = device_id,
      .role = SYS_DEV_ROLE_IO,
      .name = "ADS7128_ADC",
      .install_args = args,
      .install_device = p_ads7128_install,
      .uninstall_device = adapter_uninstall_device,
      .reset_device = adapter_reset_device,
      .error_handler = adapter_error_handler,
      .suspend_device = adapter_suspend_device,
      .resume_device = adapter_resume_device,
      .freeze_device = adapter_freeze_device,
      .sync_device = adapter_sync_device};

  return sys_device_install(&dev);
}
