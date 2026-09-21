#include "driver_drv8962.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "rom/ets_sys.h"
#include "sys_io.h"
#include "utils.h"

#define TAG "DRV8962_HW"
#define OWNER OWNER_SYS_ERRORS_BASE

#define DRV8962_AIPROPI_GAIN    0.000212f
#define DRV8962_MAX_PWM_COUNTS  4096.0f
#define DRV8962_DEFAULT_FREQ_HZ 20000

typedef struct drv8962_channel_state_t {
  sys_hbridge_mode_e mode;
  float current_drive;
  uint32_t last_current_ma;
  bool is_fault;
  sys_hbridge_fault_reason_e fault_reason;
  sys_hbridge_fault_config_t fault_config;
} drv8962_channel_state_t;

typedef struct drv8962_dev_t {
  d_drv8962_cfg_t cfg;
  drv8962_channel_state_t channels[4];
  SemaphoreHandle_t mutex;
  StaticSemaphore_t mutex_buf;
  bool is_started;
} drv8962_dev_t;

static uint32_t drive_to_duty(float magnitude) {
  if (magnitude < 0.0f) magnitude = 0.0f;
  if (magnitude > 1.0f) magnitude = 1.0f;
  return (uint32_t)lroundf(magnitude * DRV8962_MAX_PWM_COUNTS);
}

static uint32_t sample_pin_current_ma(sys_io_pin_ref_t pin, uint32_t ripropi_ohms) {
  if (pin.pin == SYS_GPIO_NONE) return 0;
  uint32_t adc_mv = 0;
  if (sys_io_get_voltage(pin.device_id, pin.pin, &adc_mv) != NULL) {
    return 0;
  }
  float denominator = DRV8962_AIPROPI_GAIN * (float)ripropi_ohms;
  if (denominator <= 0.00001f) return 0;
  return (uint32_t)lroundf(((float)adc_mv) / denominator);
}

static void dispatch_channel_fault(drv8962_dev_t* dev, uint8_t channel, sys_hbridge_fault_reason_e reason) {
  drv8962_channel_state_t* st = &dev->channels[channel];
  st->is_fault = true;
  st->fault_reason = reason;

  if (dev->cfg.turn_off_at_ocp) {
    drv8962_brake(dev, channel);
  }

  /* Direct C callback */
  if (st->fault_config.own_func.own_func) {
    st->fault_config.own_func.own_func(st->fault_config.own_func.device_handle, NULL);
  }

  /* Event routing to VM / actions */
  if (st->fault_config.route_mask != 0 || st->fault_config.static_action_id != 0 || st->fault_config.dynamic_action_id != 0) {
    cb_event_t ev = {
        .head = {
            .callback_type = CALLBACK_IO,
            .route_mask = st->fault_config.route_mask,
            .static_action_id = st->fault_config.static_action_id,
            .dynamic_action_id = st->fault_config.dynamic_action_id,
        },
        .event = {
            .io = {
                .device_id = dev->cfg.device_id,
                .pin_id = channel,
                .trigger_event = (uint16_t)reason,
                .trigger_value = (int32_t)st->last_current_ma,
            },
        },
    };
    sys_callback_trigger(&ev);
  }
}

/* nFAULT falling-edge interrupt handler executed in task context */
static err_h drv8962_fault_trampoline(void* handle, struct cb_event_t* event) {
  (void)event;
  drv8962_dev_t* dev = (drv8962_dev_t*)handle;
  if (!dev) return NULL;

  if (xSemaphoreTake(dev->mutex, portMAX_DELAY) != pdTRUE) {
    return NULL;
  }

  uint8_t num_channels = (dev->cfg.topology == DRV8962_TOPOLOGY_2_FULL_BRIDGES) ? 2 : 4;
  uint8_t active_count = 0;
  int last_active_ch = -1;
  uint8_t oc_count = 0;

  /* Sample current on all channels */
  for (uint8_t ch = 0; ch < num_channels; ch++) {
    uint32_t ma = 0;
    drv8962_get_current_ma(dev, ch, &ma);
    dev->channels[ch].last_current_ma = ma;

    if (fabsf(dev->channels[ch].current_drive) > 0.005f) {
      active_count++;
      last_active_ch = ch;
    }

    uint32_t limit = dev->cfg.current_limit_ma[ch];
    if (limit > 0 && ma >= limit) {
      oc_count++;
    }
  }

  if (oc_count > 0) {
    /* Direct overcurrent detected */
    for (uint8_t ch = 0; ch < num_channels; ch++) {
      uint32_t limit = dev->cfg.current_limit_ma[ch];
      if (limit > 0 && dev->channels[ch].last_current_ma >= limit) {
        dispatch_channel_fault(dev, ch, SYS_HBRIDGE_FAULT_OVERCURRENT);
      }
    }
  } else if (active_count == 1 && last_active_ch >= 0) {
    /* Exactly one channel was driving when fault occurred -> culprit channel */
    dispatch_channel_fault(dev, (uint8_t)last_active_ch, SYS_HBRIDGE_FAULT_OVERCURRENT);
  } else {
    /* Chip-wide Thermal Shutdown (OTSD) or supply rail UVLO */
    for (uint8_t ch = 0; ch < num_channels; ch++) {
      dispatch_channel_fault(dev, ch, SYS_HBRIDGE_FAULT_THERMAL_OR_UVLO);
    }
  }

  xSemaphoreGive(dev->mutex);
  return NULL;
}

drv8962_handle_t drv8962_new(const d_drv8962_cfg_t* cfg) {
  if (!cfg) return NULL;

  drv8962_dev_t* dev = (drv8962_dev_t*)malloc(sizeof(drv8962_dev_t));
  if (!dev) return NULL;

  memset(dev, 0, sizeof(drv8962_dev_t));
  dev->cfg = *cfg;
  dev->mutex = xSemaphoreCreateMutexStatic(&dev->mutex_buf);

  if (dev->cfg.pwm_freq_hz == 0) {
    dev->cfg.pwm_freq_hz = DRV8962_DEFAULT_FREQ_HZ;
  }
  for (int i = 0; i < 4; i++) {
    if (dev->cfg.ripropi_ohms[i] == 0) {
      dev->cfg.ripropi_ohms[i] = 3090;
    }
  }

  return dev;
}

err_h drv8962_start(drv8962_handle_t handle) {
  SE_CHECK_NOT_NULL(handle);
  drv8962_dev_t* dev = (drv8962_dev_t*)handle;

  /* 1. Configure IN pins as PWM */
  for (int i = 0; i < 4; i++) {
    IF_PIN_REF(dev->cfg.in_pins[i]) {
      SE_RET_IF_ERR(sys_io_set_mode(dev->cfg.in_pins[i].device_id, dev->cfg.in_pins[i].pin, SYS_IO_MODE_PWM));
      SE_RET_IF_ERR(sys_io_set_pwm_frequency(dev->cfg.in_pins[i].device_id, dev->cfg.in_pins[i].pin, dev->cfg.pwm_freq_hz));
      SE_RET_IF_ERR(sys_io_set_pwm_duty(dev->cfg.in_pins[i].device_id, dev->cfg.in_pins[i].pin, 0));
    }
  }

  /* 2. Configure EN pins as Digital Output */
  for (int i = 0; i < 4; i++) {
    IF_PIN_REF(dev->cfg.en_pins[i]) {
      SE_RET_IF_ERR(SYS_IO_REF_SET_MODE(dev->cfg.en_pins[i]));
      SYS_IO_REF_HIGH(dev->cfg.en_pins[i]);
    }
  }

  /* 3. Configure nSLEEP pin: drive HIGH to wake up */
  IF_PIN_REF(dev->cfg.nsleep_pin) {
    SE_RET_IF_ERR(SYS_IO_REF_SET_MODE(dev->cfg.nsleep_pin));
    SYS_IO_REF_HIGH(dev->cfg.nsleep_pin);
  }

  /* 4. Configure nFAULT pin interrupt */
  IF_PIN_REF(dev->cfg.nfault_pin) {
    SE_RET_IF_ERR(sys_io_set_mode(dev->cfg.nfault_pin.device_id, dev->cfg.nfault_pin.pin, SYS_IO_MODE_INPUT_PULLUP));

    sys_io_intr_config_t intr_cfg = {
        .mode = SYS_IO_INTR_MODE_FALLING_EDGE,
        .route_mask = 0,
        .static_action_id = 0,
        .dynamic_action_id = 0,
        .own_func = {
            .own_func = drv8962_fault_trampoline,
            .device_handle = (void*)dev,
        },
    };
    SE_RET_IF_ERR(sys_io_configure_intr(dev->cfg.nfault_pin.device_id, dev->cfg.nfault_pin.pin, &intr_cfg));
  }

  /* 5. Configure IPROPI ADC pins */
  for (int i = 0; i < 4; i++) {
    IF_PIN_REF(dev->cfg.current_adc_pins[i]) {
      SE_RET_IF_ERR(sys_io_set_mode(dev->cfg.current_adc_pins[i].device_id, dev->cfg.current_adc_pins[i].pin, SYS_IO_MODE_ADC));
    }
  }

  /* 6. Configure VREF DAC pin */
  IF_PIN_REF(dev->cfg.vref_dac_pin) {
    SE_RET_IF_ERR(sys_io_set_mode(dev->cfg.vref_dac_pin.device_id, dev->cfg.vref_dac_pin.pin, SYS_IO_MODE_DAC));
  }

  /* Brake all channels by default */
  uint8_t channels = (dev->cfg.topology == DRV8962_TOPOLOGY_2_FULL_BRIDGES) ? 2 : 4;
  for (uint8_t i = 0; i < channels; i++) {
    drv8962_brake(dev, i);
  }

  dev->is_started = true;
  return NULL;
}

err_h drv8962_stop(drv8962_handle_t handle) {
  SE_CHECK_NOT_NULL(handle);
  drv8962_dev_t* dev = (drv8962_dev_t*)handle;

  /* Disable nFAULT interrupt */
  IF_PIN_REF(dev->cfg.nfault_pin) {
    sys_io_intr_config_t disable_intr = {.mode = SYS_IO_INTR_DISABLE};
    sys_io_configure_intr(dev->cfg.nfault_pin.device_id, dev->cfg.nfault_pin.pin, &disable_intr);
  }

  /* Coast outputs */
  uint8_t channels = (dev->cfg.topology == DRV8962_TOPOLOGY_2_FULL_BRIDGES) ? 2 : 4;
  for (uint8_t i = 0; i < channels; i++) {
    drv8962_coast(dev, i);
  }

  dev->is_started = false;
  return NULL;
}

err_h drv8962_set_mode(drv8962_handle_t handle, uint8_t channel, sys_hbridge_mode_e mode) {
  SE_CHECK_NOT_NULL(handle);
  drv8962_dev_t* dev = (drv8962_dev_t*)handle;
  if (channel >= 4) SE_RET_ERR(ERR_BASE_INVALID_STATE, 0);

  dev->channels[channel].mode = mode;
  return NULL;
}

err_h drv8962_set_drive(drv8962_handle_t handle, uint8_t channel, float magnitude) {
  SE_CHECK_NOT_NULL(handle);
  drv8962_dev_t* dev = (drv8962_dev_t*)handle;

  if (dev->cfg.topology == DRV8962_TOPOLOGY_2_FULL_BRIDGES) {
    if (channel >= 2) SE_RET_ERR(ERR_BASE_INVALID_STATE, 0);

    /* Clamp [-1.0f, +1.0f] */
    if (magnitude > 1.0f) magnitude = 1.0f;
    if (magnitude < -1.0f) magnitude = -1.0f;

    if (fabsf(magnitude) < 0.005f) {
      return drv8962_brake(handle, channel);
    }

    uint8_t in1_idx = (channel == 0) ? 0 : 2;
    uint8_t in2_idx = (channel == 0) ? 1 : 3;

    /* Ensure EN pins are HIGH */
    IF_PIN_REF(dev->cfg.en_pins[in1_idx]) SYS_IO_REF_HIGH(dev->cfg.en_pins[in1_idx]);
    IF_PIN_REF(dev->cfg.en_pins[in2_idx]) SYS_IO_REF_HIGH(dev->cfg.en_pins[in2_idx]);

    float mag = fabsf(magnitude);
    uint32_t duty = drive_to_duty(mag);
    uint32_t max_duty = (uint32_t)DRV8962_MAX_PWM_COUNTS;
    uint32_t comp_duty = max_duty - duty;

    if (magnitude > 0.0f) {
      /* Forward (Slow Decay): IN1 = 100%, IN2 = PWM comp */
      SE_RET_IF_ERR(sys_io_set_pwm_duty(dev->cfg.in_pins[in1_idx].device_id, dev->cfg.in_pins[in1_idx].pin, max_duty));
      SE_RET_IF_ERR(sys_io_set_pwm_duty(dev->cfg.in_pins[in2_idx].device_id, dev->cfg.in_pins[in2_idx].pin, comp_duty));
    } else {
      /* Reverse (Slow Decay): IN1 = PWM comp, IN2 = 100% */
      SE_RET_IF_ERR(sys_io_set_pwm_duty(dev->cfg.in_pins[in1_idx].device_id, dev->cfg.in_pins[in1_idx].pin, comp_duty));
      SE_RET_IF_ERR(sys_io_set_pwm_duty(dev->cfg.in_pins[in2_idx].device_id, dev->cfg.in_pins[in2_idx].pin, max_duty));
    }

    dev->channels[channel].current_drive = magnitude;
    return NULL;
  } else {
    /* 4 Half-Bridges Topology */
    if (channel >= 4) SE_RET_ERR(ERR_BASE_INVALID_STATE, 0);
    if (magnitude < 0.0f) magnitude = 0.0f;
    if (magnitude > 1.0f) magnitude = 1.0f;

    IF_PIN_REF(dev->cfg.en_pins[channel]) SYS_IO_REF_HIGH(dev->cfg.en_pins[channel]);

    uint32_t duty = drive_to_duty(magnitude);
    SE_RET_IF_ERR(sys_io_set_pwm_duty(dev->cfg.in_pins[channel].device_id, dev->cfg.in_pins[channel].pin, duty));
    dev->channels[channel].current_drive = magnitude;
    return NULL;
  }
}

err_h drv8962_brake(drv8962_handle_t handle, uint8_t channel) {
  SE_CHECK_NOT_NULL(handle);
  drv8962_dev_t* dev = (drv8962_dev_t*)handle;

  if (dev->cfg.topology == DRV8962_TOPOLOGY_2_FULL_BRIDGES) {
    if (channel >= 2) SE_RET_ERR(ERR_BASE_INVALID_STATE, 0);
    uint8_t in1_idx = (channel == 0) ? 0 : 2;
    uint8_t in2_idx = (channel == 0) ? 1 : 3;

    IF_PIN_REF(dev->cfg.en_pins[in1_idx]) SYS_IO_REF_HIGH(dev->cfg.en_pins[in1_idx]);
    IF_PIN_REF(dev->cfg.en_pins[in2_idx]) SYS_IO_REF_HIGH(dev->cfg.en_pins[in2_idx]);

    IF_PIN_REF(dev->cfg.in_pins[in1_idx]) {
      SE_RET_IF_ERR(sys_io_set_pwm_duty(dev->cfg.in_pins[in1_idx].device_id, dev->cfg.in_pins[in1_idx].pin, 0));
    }
    IF_PIN_REF(dev->cfg.in_pins[in2_idx]) {
      SE_RET_IF_ERR(sys_io_set_pwm_duty(dev->cfg.in_pins[in2_idx].device_id, dev->cfg.in_pins[in2_idx].pin, 0));
    }
    dev->channels[channel].current_drive = 0.0f;
    return NULL;
  } else {
    if (channel >= 4) SE_RET_ERR(ERR_BASE_INVALID_STATE, 0);
    IF_PIN_REF(dev->cfg.en_pins[channel]) SYS_IO_REF_HIGH(dev->cfg.en_pins[channel]);
    IF_PIN_REF(dev->cfg.in_pins[channel]) {
      SE_RET_IF_ERR(sys_io_set_pwm_duty(dev->cfg.in_pins[channel].device_id, dev->cfg.in_pins[channel].pin, 0));
    }
    dev->channels[channel].current_drive = 0.0f;
    return NULL;
  }
}

err_h drv8962_coast(drv8962_handle_t handle, uint8_t channel) {
  SE_CHECK_NOT_NULL(handle);
  drv8962_dev_t* dev = (drv8962_dev_t*)handle;

  if (dev->cfg.topology == DRV8962_TOPOLOGY_2_FULL_BRIDGES) {
    if (channel >= 2) SE_RET_ERR(ERR_BASE_INVALID_STATE, 0);
    uint8_t in1_idx = (channel == 0) ? 0 : 2;
    uint8_t in2_idx = (channel == 0) ? 1 : 3;

    IF_PIN_REF(dev->cfg.en_pins[in1_idx]) SYS_IO_REF_LOW(dev->cfg.en_pins[in1_idx]);
    IF_PIN_REF(dev->cfg.en_pins[in2_idx]) SYS_IO_REF_LOW(dev->cfg.en_pins[in2_idx]);

    IF_PIN_REF(dev->cfg.in_pins[in1_idx]) {
      SE_RET_IF_ERR(sys_io_set_pwm_duty(dev->cfg.in_pins[in1_idx].device_id, dev->cfg.in_pins[in1_idx].pin, 0));
    }
    IF_PIN_REF(dev->cfg.in_pins[in2_idx]) {
      SE_RET_IF_ERR(sys_io_set_pwm_duty(dev->cfg.in_pins[in2_idx].device_id, dev->cfg.in_pins[in2_idx].pin, 0));
    }
    dev->channels[channel].current_drive = 0.0f;
    return NULL;
  } else {
    if (channel >= 4) SE_RET_ERR(ERR_BASE_INVALID_STATE, 0);
    IF_PIN_REF(dev->cfg.en_pins[channel]) SYS_IO_REF_LOW(dev->cfg.en_pins[channel]);
    IF_PIN_REF(dev->cfg.in_pins[channel]) {
      SE_RET_IF_ERR(sys_io_set_pwm_duty(dev->cfg.in_pins[channel].device_id, dev->cfg.in_pins[channel].pin, 0));
    }
    dev->channels[channel].current_drive = 0.0f;
    return NULL;
  }
}

err_h drv8962_get_current_ma(drv8962_handle_t handle, uint8_t channel, uint32_t* out_ma) {
  SE_CHECK_NOT_NULL(handle);
  SE_CHECK_NOT_NULL(out_ma);
  *out_ma = 0;
  drv8962_dev_t* dev = (drv8962_dev_t*)handle;

  if (dev->cfg.topology == DRV8962_TOPOLOGY_2_FULL_BRIDGES) {
    if (channel >= 2) SE_RET_ERR(ERR_BASE_INVALID_STATE, 0);
    uint8_t p1 = (channel == 0) ? 0 : 2;
    uint8_t p2 = (channel == 0) ? 1 : 3;

    uint32_t ma1 = sample_pin_current_ma(dev->cfg.current_adc_pins[p1], dev->cfg.ripropi_ohms[p1]);
    uint32_t ma2 = sample_pin_current_ma(dev->cfg.current_adc_pins[p2], dev->cfg.ripropi_ohms[p2]);

    *out_ma = (ma1 > ma2) ? ma1 : ma2;
  } else {
    if (channel >= 4) SE_RET_ERR(ERR_BASE_INVALID_STATE, 0);
    *out_ma = sample_pin_current_ma(dev->cfg.current_adc_pins[channel], dev->cfg.ripropi_ohms[channel]);
  }

  dev->channels[channel].last_current_ma = *out_ma;

  /* Automatic OCP Guard */
  uint32_t limit = dev->cfg.current_limit_ma[channel];
  if (limit > 0 && *out_ma > limit) {
    dispatch_channel_fault(dev, channel, SYS_HBRIDGE_FAULT_OVERCURRENT);
    SE_RET_ERR(ERR_BASE_INVALID_STATE, 0);
  }

  return NULL;
}

err_h drv8962_set_current_limit_ma(drv8962_handle_t handle, uint8_t channel, uint32_t limit_ma) {
  SE_CHECK_NOT_NULL(handle);
  drv8962_dev_t* dev = (drv8962_dev_t*)handle;
  if (channel >= 4) SE_RET_ERR(ERR_BASE_INVALID_STATE, 0);

  dev->cfg.current_limit_ma[channel] = limit_ma;

  /* If VREF DAC pin is connected, set analog reference voltage */
  IF_PIN_REF(dev->cfg.vref_dac_pin) {
    float vref_mv = ((float)limit_ma) * DRV8962_AIPROPI_GAIN * ((float)dev->cfg.ripropi_ohms[channel]);
    if (vref_mv > 3300.0f) vref_mv = 3300.0f;
    if (vref_mv < 50.0f) vref_mv = 50.0f;
    SE_RET_IF_ERR(sys_io_set_voltage(dev->cfg.vref_dac_pin.device_id, dev->cfg.vref_dac_pin.pin, (uint32_t)lroundf(vref_mv)));
  }

  return NULL;
}

err_h drv8962_configure_fault(drv8962_handle_t handle, uint8_t channel, const sys_hbridge_fault_config_t* config) {
  SE_CHECK_NOT_NULL(handle);
  drv8962_dev_t* dev = (drv8962_dev_t*)handle;
  if (channel >= 4) SE_RET_ERR(ERR_BASE_INVALID_STATE, 0);

  if (config) {
    dev->channels[channel].fault_config = *config;
  } else {
    memset(&dev->channels[channel].fault_config, 0, sizeof(sys_hbridge_fault_config_t));
  }
  return NULL;
}

err_h drv8962_get_fault(drv8962_handle_t handle, uint8_t channel, bool* out_fault, sys_hbridge_fault_reason_e* out_reason) {
  SE_CHECK_NOT_NULL(handle);
  SE_CHECK_NOT_NULL(out_fault);
  drv8962_dev_t* dev = (drv8962_dev_t*)handle;
  if (channel >= 4) SE_RET_ERR(ERR_BASE_INVALID_STATE, 0);

  *out_fault = dev->channels[channel].is_fault;
  if (out_reason) {
    *out_reason = dev->channels[channel].fault_reason;
  }
  return NULL;
}

err_h drv8962_clear_fault(drv8962_handle_t handle, uint8_t channel) {
  SE_CHECK_NOT_NULL(handle);
  drv8962_dev_t* dev = (drv8962_dev_t*)handle;
  if (channel >= 4) SE_RET_ERR(ERR_BASE_INVALID_STATE, 0);

  /* DRV8962 nSLEEP 30 us reset pulse clears internal fault latches */
  IF_PIN_REF(dev->cfg.nsleep_pin) {
    SYS_IO_REF_LOW(dev->cfg.nsleep_pin);
    ets_delay_us(30);
    SYS_IO_REF_HIGH(dev->cfg.nsleep_pin);
    ets_delay_us(1000); /* 1 ms wake delay */
  }

  dev->channels[channel].is_fault = false;
  dev->channels[channel].fault_reason = SYS_HBRIDGE_FAULT_NONE;
  return drv8962_brake(handle, channel);
}

