#include <stdlib.h>
#include <string.h>
#include "device_drv8962.h"
#include "driver_drv8962.h"
#include "esp_log.h"
#include "status.h"
#include "status_codes.h"
#include "sys_device.h"
#include "sys_h_bridge.h"
#include "sys_io.h"
#include "sys_power.h"

static const char* TAG = "adapter_drv8962";

#undef OWNER
#define OWNER OWNER_DEVICE_DRV8962

typedef struct {
  void* adapter_ctx;
  uint8_t channel;
} drv8962_adc_ctx_t;

typedef struct {
  sys_device_adapter_base_t base;

  drv8962_config_t config;
  drv8962_driver_t driver;

  // Double buffering (for frozen mode)
  uint16_t frozen_in_duty[4];
  bool frozen_en_state[4];
  bool frozen_outputs_dirty;

  // Callbacks for power monitor contract
  void (*monitor_callbacks[4])(uint8_t device_id, sys_power_events_e triggered_by);
  drv8962_adc_ctx_t adc_callback_ctx[4];
} drv8962_adapter_ctx_t;

// Forward declaration of VTable
static const sys_h_bridge_contract_t h_bridge_contract;
static const sys_power_monitor_contract monitor_contract;

static void drv8962_fault_isr_callback(const sys_io_intr_event_t* event) {
  drv8962_adapter_ctx_t* ctx = (drv8962_adapter_ctx_t*)event->user_arg;
  if (!ctx) return;

  ESP_LOGE(TAG, "DRV8962 FAULT PIN TRIGGERED (active low) on Device ID: %u", ctx->base.device_id);
  status_rep_t err = STA_C(ERR_HARDWARE_FAULT, OWNER, ctx->base.device_id, STATUS_PAYLOAD_DEVICE);

  sys_device_t* dev = sys_device_get_by_id(ctx->base.device_id);
  if (dev && dev->error_handler) {
    dev->error_handler(ctx, &err);
  }
}

static void drv8962_adc_threshold_callback(const sys_io_intr_event_t* event) {
  drv8962_adc_ctx_t* actx = (drv8962_adc_ctx_t*)event->user_arg;
  if (!actx || !actx->adapter_ctx) return;

  drv8962_adapter_ctx_t* ctx = (drv8962_adapter_ctx_t*)actx->adapter_ctx;
  uint8_t chan = actx->channel;

  ESP_LOGW(TAG, "DRV8962 Current Alert on channel %u of Device ID: %u", chan, ctx->base.device_id);
  if (ctx->monitor_callbacks[chan]) {
    ctx->monitor_callbacks[chan](ctx->base.device_id, SYS_PWR_EVENT_OCP_CRITICAL);
  }
}

static status_rep_t write_output_channel(drv8962_adapter_ctx_t* ctx, uint8_t chan, uint16_t duty, bool en) {
  uint8_t in_dev = ctx->config.in_devices[chan];
  sys_io_pin_num_t in_pin = ctx->config.in_pins[chan];

  uint8_t en_dev = ctx->config.en_devices[chan];
  sys_io_pin_num_t en_pin = ctx->config.en_pins[chan];

  // Set enable level
  IF_PIN(en_pin) {
    WITH_PIN_UNLOCKED(en_dev, en_pin) { STA_R_ON_ERR(sys_io_set_level(en_dev, en_pin, en)); }
  }

  // Set input PWM or level
  IF_PIN(in_pin) {
    WITH_PIN_UNLOCKED(in_dev, in_pin) {
      if (duty == 0) {
        STA_R_ON_ERR(sys_io_set_mode(in_dev, in_pin, SYS_IO_MODE_OUTPUT_PUSH_PULL));
        STA_R_ON_ERR(sys_io_set_level(in_dev, in_pin, false));
      } else if (duty == 65535) {
        STA_R_ON_ERR(sys_io_set_mode(in_dev, in_pin, SYS_IO_MODE_OUTPUT_PUSH_PULL));
        STA_R_ON_ERR(sys_io_set_level(in_dev, in_pin, true));
      } else {
        // Attempt PWM configuration
        status_rep_t r = sys_io_set_mode(in_dev, in_pin, SYS_IO_MODE_PWM);
        if (STA_IS_OK(r)) {
          sys_io_set_pwm_frequency(in_dev, in_pin, 20000);  // 20kHz default
          STA_R_ON_ERR(sys_io_set_pwm_duty(in_dev, in_pin, duty));
        } else {
          // Fallback to simple logic levels
          STA_R_ON_ERR(sys_io_set_mode(in_dev, in_pin, SYS_IO_MODE_OUTPUT_PUSH_PULL));
          STA_R_ON_ERR(sys_io_set_level(in_dev, in_pin, duty > 32768));
        }
      }
    }
  }

  return STA_OK;
}

// --- VREG/H-Bridge Contract Implementations ---

static status_rep_t contract_h_bridge_forward(void* device_handle, sys_h_bridge_mode_e mode, uint16_t duty, uint8_t h_id) {
  SYS_DEV_GET_ADAPTER_CONTEXT(drv8962_adapter_ctx_t, void*, ctx, hw, device_handle);

  if (mode == SYS_H_BRIDGE_MODE_NORMAL) {
    CHECK_ARG_R(h_id, 0, 1, h_id);
    uint8_t a = h_id * 2;
    uint8_t b = h_id * 2 + 1;

    if (ctx->base.is_frozen) {
      ctx->frozen_in_duty[a] = 65535;
      ctx->frozen_en_state[a] = true;
      ctx->frozen_in_duty[b] = duty;
      ctx->frozen_en_state[b] = true;
      ctx->frozen_outputs_dirty = true;
    } else {
      ctx->driver.in_duty[a] = 65535;
      ctx->driver.en_state[a] = true;
      ctx->driver.in_duty[b] = duty;
      ctx->driver.en_state[b] = true;
      STA_R_ON_ERR(write_output_channel(ctx, a, 65535, true));
      STA_R_ON_ERR(write_output_channel(ctx, b, duty, true));
    }
  } else {
    CHECK_ARG_R(h_id, 0, 3, h_id);
    if (ctx->base.is_frozen) {
      ctx->frozen_in_duty[h_id] = duty;
      ctx->frozen_en_state[h_id] = true;
      ctx->frozen_outputs_dirty = true;
    } else {
      ctx->driver.in_duty[h_id] = duty;
      ctx->driver.en_state[h_id] = true;
      STA_R_ON_ERR(write_output_channel(ctx, h_id, duty, true));
    }
  }
  return STA_OK;
}

static status_rep_t contract_h_bridge_backwards(void* device_handle, sys_h_bridge_mode_e mode, uint16_t duty, uint8_t h_id) {
  SYS_DEV_GET_ADAPTER_CONTEXT(drv8962_adapter_ctx_t, void*, ctx, hw, device_handle);

  if (mode == SYS_H_BRIDGE_MODE_NORMAL) {
    CHECK_ARG_R(h_id, 0, 1, h_id);
    uint8_t a = h_id * 2;
    uint8_t b = h_id * 2 + 1;

    if (ctx->base.is_frozen) {
      ctx->frozen_in_duty[a] = duty;
      ctx->frozen_en_state[a] = true;
      ctx->frozen_in_duty[b] = 65535;
      ctx->frozen_en_state[b] = true;
      ctx->frozen_outputs_dirty = true;
    } else {
      ctx->driver.in_duty[a] = duty;
      ctx->driver.en_state[a] = true;
      ctx->driver.in_duty[b] = 65535;
      ctx->driver.en_state[b] = true;
      STA_R_ON_ERR(write_output_channel(ctx, a, duty, true));
      STA_R_ON_ERR(write_output_channel(ctx, b, 65535, true));
    }
  } else {
    // For half H-Bridge, backwards behaves exactly like forward (direction does not matter)
    CHECK_ARG_R(h_id, 0, 3, h_id);
    if (ctx->base.is_frozen) {
      ctx->frozen_in_duty[h_id] = duty;
      ctx->frozen_en_state[h_id] = true;
      ctx->frozen_outputs_dirty = true;
    } else {
      ctx->driver.in_duty[h_id] = duty;
      ctx->driver.en_state[h_id] = true;
      STA_R_ON_ERR(write_output_channel(ctx, h_id, duty, true));
    }
  }
  return STA_OK;
}

static status_rep_t contract_h_bridge_brake(void* device_handle, uint16_t duty, uint8_t h_id) {
  SYS_DEV_GET_ADAPTER_CONTEXT(drv8962_adapter_ctx_t, void*, ctx, hw, device_handle);
  CHECK_ARG_R(h_id, 0, 1, h_id);

  uint8_t a = h_id * 2;
  uint8_t b = h_id * 2 + 1;

  if (ctx->base.is_frozen) {
    ctx->frozen_in_duty[a] = 65535;
    ctx->frozen_en_state[a] = true;
    ctx->frozen_in_duty[b] = 65535;
    ctx->frozen_en_state[b] = true;
    ctx->frozen_outputs_dirty = true;
  } else {
    ctx->driver.in_duty[a] = 65535;
    ctx->driver.en_state[a] = true;
    ctx->driver.in_duty[b] = 65535;
    ctx->driver.en_state[b] = true;
    STA_R_ON_ERR(write_output_channel(ctx, a, 65535, true));
    STA_R_ON_ERR(write_output_channel(ctx, b, 65535, true));
  }
  return STA_OK;
}

static status_rep_t contract_h_bridge_coast(void* device_handle, uint16_t duty, uint8_t h_id) {
  SYS_DEV_GET_ADAPTER_CONTEXT(drv8962_adapter_ctx_t, void*, ctx, hw, device_handle);
  CHECK_ARG_R(h_id, 0, 1, h_id);

  uint8_t a = h_id * 2;
  uint8_t b = h_id * 2 + 1;

  if (ctx->base.is_frozen) {
    ctx->frozen_in_duty[a] = 0;
    ctx->frozen_en_state[a] = false;
    ctx->frozen_in_duty[b] = 0;
    ctx->frozen_en_state[b] = false;
    ctx->frozen_outputs_dirty = true;
  } else {
    ctx->driver.in_duty[a] = 0;
    ctx->driver.en_state[a] = false;
    ctx->driver.in_duty[b] = 0;
    ctx->driver.en_state[b] = false;
    STA_R_ON_ERR(write_output_channel(ctx, a, 0, false));
    STA_R_ON_ERR(write_output_channel(ctx, b, 0, false));
  }
  return STA_OK;
}

static const sys_h_bridge_contract_t h_bridge_contract = {.forward = contract_h_bridge_forward, .backwards = contract_h_bridge_backwards, .brake = contract_h_bridge_brake, .coast = contract_h_bridge_coast};

// --- sys_power_monitor_contract Implementations ---

static status_rep_t contract_monitor_get_voltage(void* device_handle, uint8_t channel, int32_t* out_mV) {
  SYS_DEV_GET_ADAPTER_CONTEXT(drv8962_adapter_ctx_t, void*, ctx, hw, device_handle);
  CHECK_ARG_R(channel, 0, 3, channel);
  CHECK_NOT_NULL_R(out_mV);

  uint8_t dev = ctx->config.ipropi_devices[channel];
  sys_io_pin_num_t pin = ctx->config.ipropi_pins[channel];

  uint32_t val_mv = 0;
  IF_PIN(pin) {
    STA_R_ON_ERR(sys_io_get_voltage(dev, pin, &val_mv));
    *out_mV = (int32_t)val_mv;
  }
  else {
    *out_mV = 0;
  }
  return STA_OK;
}

static status_rep_t contract_monitor_get_current(void* device_handle, uint8_t channel, int32_t* out_mA) {
  SYS_DEV_GET_ADAPTER_CONTEXT(drv8962_adapter_ctx_t, void*, ctx, hw, device_handle);
  CHECK_ARG_R(channel, 0, 3, channel);
  CHECK_NOT_NULL_R(out_mA);

  uint8_t dev = ctx->config.ipropi_devices[channel];
  sys_io_pin_num_t pin = ctx->config.ipropi_pins[channel];
  uint32_t r_ohms = ctx->config.r_ipropi_ohms[channel];

  if (r_ohms == 0) {
    *out_mA = 0;
    return STA_OK;
  }

  uint32_t val_mv = 0;
  IF_PIN(pin) {
    STA_R_ON_ERR(sys_io_get_voltage(dev, pin, &val_mv));
    // IHS_mA = (V_mV * 1,000,000) / (212 * R_ohms)
    uint64_t numerator = (uint64_t)val_mv * 1000000ULL;
    uint64_t denominator = (uint64_t)212 * r_ohms;
    *out_mA = (int32_t)(numerator / denominator);
  }
  else {
    *out_mA = 0;
  }
  return STA_OK;
}

static status_rep_t contract_monitor_add_callback(void* device_handle, uint8_t channel, int32_t trigger_value, sys_power_events_e on_event, void (*callback)(uint8_t device_id, sys_power_events_e triggered_by)) {
  SYS_DEV_GET_ADAPTER_CONTEXT(drv8962_adapter_ctx_t, void*, ctx, hw, device_handle);
  CHECK_ARG_R(channel, 0, 3, channel);
  CHECK_NOT_NULL_R(callback);

  uint8_t dev = ctx->config.ipropi_devices[channel];
  sys_io_pin_num_t pin = ctx->config.ipropi_pins[channel];
  uint32_t r_ohms = ctx->config.r_ipropi_ohms[channel];

  if (r_ohms == 0) {
    return STA_C(ERR_SYS_IO_FEATURE_UNAVAILABLE, OWNER, channel, STATUS_PAYLOAD_DEVICE);
  }

  ctx->monitor_callbacks[channel] = callback;
  ctx->adc_callback_ctx[channel].adapter_ctx = ctx;
  ctx->adc_callback_ctx[channel].channel = channel;

  // V_trigger_mV = (I_trigger_mA * 212 * R_ohms) / 1,000,000
  uint64_t product = (uint64_t)trigger_value * 212 * r_ohms;
  uint16_t v_trigger_mv = (uint16_t)(product / 1000000ULL);

  sys_io_intr_config_t config = {
      .mode = SYS_IO_INTR_ADC_WINDOW_OUTSIDE, .callback = drv8962_adc_threshold_callback, .user_ctx = &ctx->adc_callback_ctx[channel], .adc = {.adc_threshold_up_mV = v_trigger_mv, .adc_threshold_down_mV = 0, .adc_threshold_hysteresis_mV = 100, .adc_event_counter_threshold = 1}};

  IF_PIN(pin) { STA_R_ON_ERR(sys_io_configure_intr(dev, pin, &config)); }

  return STA_OK;
}

static const sys_power_monitor_contract monitor_contract = {.get_voltage = contract_monitor_get_voltage, .get_current = contract_monitor_get_current, .add_callback = contract_monitor_add_callback};

// --- sys_device_t Implementations ---

static status_rep_t device_uninstall(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(drv8962_adapter_ctx_t, void*, ctx, hw, handle);
  status_rep_t status = STA_OK;
  status_rep_t r;

  // Unlock and reset IN pins
  for (int i = 0; i < 4; i++) {
    IF_PIN(ctx->config.in_pins[i]) {
      SYS_IO_UNLOCK_PIN(ctx->config.in_devices[i], ctx->config.in_pins[i]);
      r = sys_io_reset(ctx->config.in_devices[i], ctx->config.in_pins[i]);
      if (STA_IS_ERR(r)) status = r;
    }
    IF_PIN(ctx->config.en_pins[i]) {
      SYS_IO_UNLOCK_PIN(ctx->config.en_devices[i], ctx->config.en_pins[i]);
      r = sys_io_reset(ctx->config.en_devices[i], ctx->config.en_pins[i]);
      if (STA_IS_ERR(r)) status = r;
    }
    IF_PIN(ctx->config.ipropi_pins[i]) {
      SYS_IO_UNLOCK_PIN(ctx->config.ipropi_devices[i], ctx->config.ipropi_pins[i]);
      r = sys_io_reset(ctx->config.ipropi_devices[i], ctx->config.ipropi_pins[i]);
      if (STA_IS_ERR(r)) status = r;
    }
  }

  // Unlock and reset control pins
  IF_PIN(ctx->config.nsleep_pin) {
    SYS_IO_UNLOCK_PIN(ctx->config.nsleep_device, ctx->config.nsleep_pin);
    r = sys_io_reset(ctx->config.nsleep_device, ctx->config.nsleep_pin);
    if (STA_IS_ERR(r)) status = r;
  }
  IF_PIN(ctx->config.nfault_pin) {
    SYS_IO_UNLOCK_PIN(ctx->config.nfault_device, ctx->config.nfault_pin);
    r = sys_io_reset(ctx->config.nfault_device, ctx->config.nfault_pin);
    if (STA_IS_ERR(r)) status = r;
  }
  IF_PIN(ctx->config.mode_pin) {
    SYS_IO_UNLOCK_PIN(ctx->config.mode_device, ctx->config.mode_pin);
    r = sys_io_reset(ctx->config.mode_device, ctx->config.mode_pin);
    if (STA_IS_ERR(r)) status = r;
  }
  IF_PIN(ctx->config.ocpm_pin) {
    SYS_IO_UNLOCK_PIN(ctx->config.ocpm_device, ctx->config.ocpm_pin);
    r = sys_io_reset(ctx->config.ocpm_device, ctx->config.ocpm_pin);
    if (STA_IS_ERR(r)) status = r;
  }

  r = sys_h_bridge_unregister(ctx->base.device_id);
  if (STA_IS_ERR(r)) status = r;

  r = sys_power_unregister(ctx->base.device_id);
  if (STA_IS_ERR(r)) status = r;

  free(ctx);
  return status;
}

static status_rep_t device_reset(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(drv8962_adapter_ctx_t, void*, ctx, hw, handle);

  // Coast motor outputs
  for (int i = 0; i < 4; i++) {
    ctx->driver.in_duty[i] = 0;
    ctx->driver.en_state[i] = false;
    STA_R_ON_ERR(write_output_channel(ctx, i, 0, false));
  }

  // Clear latch-off fault if occurred by issuing a 30us nSLEEP low pulse
  IF_PIN(ctx->config.nsleep_pin) {
    WITH_PIN_UNLOCKED(ctx->config.nsleep_device, ctx->config.nsleep_pin) {
      sys_io_set_level(ctx->config.nsleep_device, ctx->config.nsleep_pin, false);
      vTaskDelay(pdMS_TO_TICKS(1));  // Min 20us to 40us per datasheet, 1ms is safe
      sys_io_set_level(ctx->config.nsleep_device, ctx->config.nsleep_pin, true);
      vTaskDelay(pdMS_TO_TICKS(2));  // Wait for charge pump wake-up (tWAKE ~ 1.2ms)
    }
  }

  ESP_LOGI(TAG, "DRV8962 Reset complete.");
  return STA_OK;
}

static status_rep_t device_error_handler(void* handle, status_rep_t* error) {
  ESP_LOGE(TAG, "DRV8962 Error triggered: code=%d", error->e_code);
  return device_reset(handle);
}

static status_rep_t device_suspend(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(drv8962_adapter_ctx_t, void*, ctx, hw, handle);

  // Set outputs to coast/disabled
  for (int i = 0; i < 4; i++) {
    write_output_channel(ctx, i, 0, false);
  }

  // Put device into low-power sleep mode (nsleep low)
  IF_PIN(ctx->config.nsleep_pin) {
    WITH_PIN_UNLOCKED(ctx->config.nsleep_device, ctx->config.nsleep_pin) { STA_R_ON_ERR(sys_io_set_level(ctx->config.nsleep_device, ctx->config.nsleep_pin, false)); }
  }

  return STA_OK;
}

static status_rep_t device_resume(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(drv8962_adapter_ctx_t, void*, ctx, hw, handle);

  // Wake up device
  IF_PIN(ctx->config.nsleep_pin) {
    WITH_PIN_UNLOCKED(ctx->config.nsleep_device, ctx->config.nsleep_pin) { STA_R_ON_ERR(sys_io_set_level(ctx->config.nsleep_device, ctx->config.nsleep_pin, true)); }
    vTaskDelay(pdMS_TO_TICKS(2));  // Wait for wake-up transition
  }

  // Restore channel outputs
  for (int i = 0; i < 4; i++) {
    STA_R_ON_ERR(write_output_channel(ctx, i, ctx->driver.in_duty[i], ctx->driver.en_state[i]));
  }

  return STA_OK;
}

static status_rep_t device_freeze(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(drv8962_adapter_ctx_t, void*, ctx, hw, handle);
  SYS_DEV_CTX_FREEZE(ctx);

  // Cache current driver outputs to frozen buffers
  for (int i = 0; i < 4; i++) {
    ctx->frozen_in_duty[i] = ctx->driver.in_duty[i];
    ctx->frozen_en_state[i] = ctx->driver.en_state[i];
  }
  ctx->frozen_outputs_dirty = false;
  return STA_OK;
}

static status_rep_t device_sync(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(drv8962_adapter_ctx_t, void*, ctx, hw, handle);
  SYS_DEV_CTX_UNFREEZE(ctx);

  if (ctx->frozen_outputs_dirty) {
    for (int i = 0; i < 4; i++) {
      ctx->driver.in_duty[i] = ctx->frozen_in_duty[i];
      ctx->driver.en_state[i] = ctx->frozen_en_state[i];
      STA_R_ON_ERR(write_output_channel(ctx, i, ctx->driver.in_duty[i], ctx->driver.en_state[i]));
    }
    ctx->frozen_outputs_dirty = false;
  }
  return STA_OK;
}

static void* adapter_install_fallback(drv8962_adapter_ctx_t* ctx) {
  if (ctx) {
    device_uninstall(ctx);
  }
  return NULL;
}

static void* device_install(void** args) {
  SYS_DEV_ARG_UNPACK(uint8_t, device_id, args, 0);
  SYS_DEV_ARG_UNPACK(drv8962_config_t*, config, args, 1);

  drv8962_adapter_ctx_t* ctx = calloc(1, sizeof(drv8962_adapter_ctx_t));
  if (!ctx) return NULL;

  ctx->base.device_id = device_id;
  ctx->base.hw_handle = (void*)&ctx->driver;
  ctx->config = *config;

  drv8962_driver_init(&ctx->driver, config->mode_val, config->ocpm_val);

  // Configure sleep, mode, and ocpm pins
  IF_PIN(config->nsleep_pin) {
    if (STA_IS_ERR(sys_io_set_mode(config->nsleep_device, config->nsleep_pin, SYS_IO_MODE_OUTPUT_PUSH_PULL))) goto fail;
    WITH_PIN_UNLOCKED(config->nsleep_device, config->nsleep_pin) { sys_io_set_level(config->nsleep_device, config->nsleep_pin, true); }
    SYS_IO_LOCK_PIN(config->nsleep_device, config->nsleep_pin);
  }

  IF_PIN(config->mode_pin) {
    if (STA_IS_ERR(sys_io_set_mode(config->mode_device, config->mode_pin, SYS_IO_MODE_OUTPUT_PUSH_PULL))) goto fail;
    WITH_PIN_UNLOCKED(config->mode_device, config->mode_pin) { sys_io_set_level(config->mode_device, config->mode_pin, config->mode_val); }
    SYS_IO_LOCK_PIN(config->mode_device, config->mode_pin);
  }

  IF_PIN(config->ocpm_pin) {
    if (STA_IS_ERR(sys_io_set_mode(config->ocpm_device, config->ocpm_pin, SYS_IO_MODE_OUTPUT_PUSH_PULL))) goto fail;
    WITH_PIN_UNLOCKED(config->ocpm_device, config->ocpm_pin) { sys_io_set_level(config->ocpm_device, config->ocpm_pin, config->ocpm_val); }
    SYS_IO_LOCK_PIN(config->ocpm_device, config->ocpm_pin);
  }

  // Configure output enable and input pins
  for (int i = 0; i < 4; i++) {
    IF_PIN(config->en_pins[i]) {
      if (STA_IS_ERR(sys_io_set_mode(config->en_devices[i], config->en_pins[i], SYS_IO_MODE_OUTPUT_PUSH_PULL))) goto fail;
      WITH_PIN_UNLOCKED(config->en_devices[i], config->en_pins[i]) { sys_io_set_level(config->en_devices[i], config->en_pins[i], false); }
      SYS_IO_LOCK_PIN(config->en_devices[i], config->en_pins[i]);
    }
    IF_PIN(config->in_pins[i]) {
      if (STA_IS_ERR(sys_io_set_mode(config->in_devices[i], config->in_pins[i], SYS_IO_MODE_OUTPUT_PUSH_PULL))) goto fail;
      WITH_PIN_UNLOCKED(config->in_devices[i], config->in_pins[i]) { sys_io_set_level(config->in_devices[i], config->in_pins[i], false); }
      SYS_IO_LOCK_PIN(config->in_devices[i], config->in_pins[i]);
    }
    IF_PIN(config->ipropi_pins[i]) {
      if (STA_IS_ERR(sys_io_set_mode(config->ipropi_devices[i], config->ipropi_pins[i], SYS_IO_MODE_ADC))) goto fail;
      SYS_IO_LOCK_PIN(config->ipropi_devices[i], config->ipropi_pins[i]);
    }
  }

  // Configure fault interrupt
  IF_PIN(config->nfault_pin) {
    if (STA_IS_ERR(sys_io_set_mode(config->nfault_device, config->nfault_pin, SYS_IO_MODE_INPUT_PULLUP))) goto fail;
    sys_io_intr_config_t fault_intr = {.mode = SYS_IO_INTR_MODE_FALLING_EDGE, .callback = drv8962_fault_isr_callback, .user_ctx = ctx};
    if (STA_IS_ERR(sys_io_configure_intr(config->nfault_device, config->nfault_pin, &fault_intr))) goto fail;
    SYS_IO_LOCK_PIN(config->nfault_device, config->nfault_pin);
  }

  // Register to global power & H-Bridge registries
  if (STA_IS_ERR(sys_power_register_monitor(device_id, ctx, &monitor_contract))) goto fail;
  if (STA_IS_ERR(sys_h_bridge_register(device_id, ctx, &h_bridge_contract))) goto fail;

  return ctx;

fail:
  adapter_install_fallback(ctx);
  return NULL;
}

status_rep_t d_drv8962_create(uint8_t device_id, const drv8962_config_t* config) {
  CHECK_NOT_NULL_RP(config);

  drv8962_config_t* config_copy = malloc(sizeof(drv8962_config_t));
  if (!config_copy) return STA_C(ERR_NO_MEM, OWNER, device_id, STATUS_PAYLOAD_DEVICE);
  *config_copy = *config;

  // Since we package this pointer, we must also free it on uninstall. But wait!
  // It's cleaner to keep the config copied inside the context structure (which is drv8962_adapter_ctx_t)
  // which we already do! The config_copy is only used during device_install.
  // So after sys_device_install completes, we can free config_copy safely!
  void* args[] = {SYS_DEV_ARG_PACK(device_id), SYS_DEV_ARG_PACK(config_copy)};

  sys_device_t dev = {.device_id = device_id,
      .role = SYS_DEV_ROLE_IO,
      .name = "DRV8962_MOTOR_DRIVER",
      .install_args = args,
      .install_device = device_install,
      .uninstall_device = device_uninstall,
      .reset_device = device_reset,
      .error_handler = device_error_handler,
      .suspend_device = device_suspend,
      .resume_device = device_resume,
      .freeze_device = device_freeze,
      .sync_device = device_sync};

  status_rep_t r = sys_device_install(&dev);
  free(config_copy);
  return r;
}
