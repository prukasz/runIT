#include "driver_drv8962.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "rom/ets_sys.h"
#include "sys_device.h"
#include "sys_io.h"
#include "utils.h"

#define OWNER OWNER_DEVICE_DRV8962

#define DRV8962_AIPROPI_GAIN    0.000212f
#define DRV8962_MAX_PWM_COUNTS  4096.0f
#define DRV8962_DEFAULT_FREQ_HZ 20000

#define DRV_CHANNEL_COUNT(dev) (((dev)->cfg.topology == DRV8962_TOPOLOGY_2_FULL_BRIDGES) ? 2 : 4)

typedef struct drv8962_channel_state_t {
  sys_hbridge_mode_e mode;
  float current_drive;
  int32_t last_current_mA;
  bool is_fault;
  sys_hbridge_fault_reason_e fault_reason;
} drv8962_channel_state_t;

typedef struct drv8962_dev_t {
  d_drv8962_cfg_t cfg;
  drv8962_channel_state_t channels[4];
  SemaphoreHandle_t mutex;
  StaticSemaphore_t mutex_buf;
  bool is_started;
  bool nfault_subscribed;
  uint8_t nfault_sub; /* sys_event subscription on nfault_pin */
} drv8962_dev_t;

static uint32_t drive_to_duty(float magnitude) {
  if (magnitude < 0.0f) magnitude = 0.0f;
  if (magnitude > 1.0f) magnitude = 1.0f;
  return (uint32_t)lroundf(magnitude * DRV8962_MAX_PWM_COUNTS);
}

static SE_MUST_USE err_h sample_pin_current_mA(sys_io_pin_ref_t pin, uint32_t ripropi_ohms, int32_t* out_mA) {
  *out_mA = 0;
  if (pin.pin == SYS_GPIO_NONE) return NULL;
  int32_t adc_mV = 0;
  SE_TRY(sys_io_get_voltage(pin, &adc_mV));
  float denominator = DRV8962_AIPROPI_GAIN * (float)ripropi_ohms;
  if (denominator <= 0.00001f) return NULL;
  *out_mA = (int32_t)lroundf(((float)adc_mV) / denominator);
  return NULL;
}

/* A limit of 0 disables the check; a negative reading is never over a limit. */
static bool current_at_or_over(int32_t mA, uint32_t limit_mA) {
  return limit_mA > 0 && mA >= 0 && (uint32_t)mA >= limit_mA;
}

/* Channel current without the OCP guard (full bridge: the larger of its two
   half-bridge IPROPI readings). */
static SE_MUST_USE err_h read_channel_current_mA(drv8962_dev_t* dev, uint8_t channel, int32_t* out_mA) {
  if (dev->cfg.topology == DRV8962_TOPOLOGY_2_FULL_BRIDGES) {
    uint8_t p1 = (channel == 0) ? 0 : 2;
    uint8_t p2 = (channel == 0) ? 1 : 3;
    int32_t ma1 = 0;
    int32_t ma2 = 0;
    SE_TRY(sample_pin_current_mA(dev->cfg.current_adc_pins[p1], dev->cfg.ripropi_ohms[p1], &ma1));
    SE_TRY(sample_pin_current_mA(dev->cfg.current_adc_pins[p2], dev->cfg.ripropi_ohms[p2], &ma2));
    *out_mA = (ma1 > ma2) ? ma1 : ma2;
  } else {
    SE_TRY(sample_pin_current_mA(dev->cfg.current_adc_pins[channel], dev->cfg.ripropi_ohms[channel], out_mA));
  }
  dev->channels[channel].last_current_mA = *out_mA;
  return NULL;
}

/* Latch the fault and brake the channel (turn_off_at_ocp). The caller
   publishes it after releasing the driver mutex. */
static SE_MUST_USE err_h mark_channel_fault(drv8962_dev_t* dev, uint8_t channel, sys_hbridge_fault_reason_e reason) {
  drv8962_channel_state_t* st = &dev->channels[channel];
  st->is_fault = true;
  st->fault_reason = reason;
  return dev->cfg.turn_off_at_ocp ? drv8962_brake(dev, channel) : NULL;
}

static SE_MUST_USE err_h publish_channel_fault(drv8962_dev_t* dev, uint8_t channel, uint8_t hops) {
  const drv8962_channel_state_t* st = &dev->channels[channel];
  return sys_hbridge_publish(dev->cfg.device_id, channel, st->fault_reason, st->last_current_mA, hops);
}

/* Inline listener of the nFAULT pin: find the faulted channels, then publish them. */
static SE_MUST_USE err_h drv8962_fault_handler(const sys_event_t* event, void* handle) {
  drv8962_dev_t* dev = (drv8962_dev_t*)handle;
  if (!dev) return NULL;

  if (xSemaphoreTake(dev->mutex, portMAX_DELAY) != pdTRUE) {
    return NULL;
  }

  uint8_t num_channels = DRV_CHANNEL_COUNT(dev);
  uint8_t active_count = 0;
  int last_active_ch = -1;
  uint8_t oc_count = 0;
  uint8_t faulted = 0; /* bit per channel */
  err_h err = NULL;

  /* Sample current on all channels (a failed read counts as 0 mA). No OCP
     guard here: the classification below dispatches each fault once. */
  for (uint8_t ch = 0; ch < num_channels; ch++) {
    int32_t ma = 0;
    SYS_DEV_TEARDOWN_STEP(err, read_channel_current_mA(dev, ch, &ma));

    if (fabsf(dev->channels[ch].current_drive) > 0.005f) {
      active_count++;
      last_active_ch = ch;
    }

    if (current_at_or_over(ma, dev->cfg.current_limit_mA[ch])) {
      oc_count++;
    }
  }

  if (oc_count > 0) {
    /* Direct overcurrent detected */
    for (uint8_t ch = 0; ch < num_channels; ch++) {
      if (current_at_or_over(dev->channels[ch].last_current_mA, dev->cfg.current_limit_mA[ch])) {
        SYS_DEV_TEARDOWN_STEP(err, mark_channel_fault(dev, ch, SYS_HBRIDGE_FAULT_OVERCURRENT));
        faulted |= (uint8_t)(1u << ch);
      }
    }
  } else if (active_count == 1 && last_active_ch >= 0) {
    /* Exactly one channel was driving when fault occurred -> culprit channel */
    SYS_DEV_TEARDOWN_STEP(err, mark_channel_fault(dev, (uint8_t)last_active_ch, SYS_HBRIDGE_FAULT_OVERCURRENT));
    faulted |= (uint8_t)(1u << last_active_ch);
  } else {
    /* Chip-wide Thermal Shutdown (OTSD) or supply rail UVLO */
    for (uint8_t ch = 0; ch < num_channels; ch++) {
      SYS_DEV_TEARDOWN_STEP(err, mark_channel_fault(dev, ch, SYS_HBRIDGE_FAULT_THERMAL_OR_UVLO));
      faulted |= (uint8_t)(1u << ch);
    }
  }

  xSemaphoreGive(dev->mutex);

  /* Outside the mutex: an inline listener may call back into this driver. */
  for (uint8_t ch = 0; ch < num_channels; ch++) {
    if (faulted & (1u << ch)) SYS_DEV_TEARDOWN_STEP(err, publish_channel_fault(dev, ch, SYS_EVENT_CAUSED_BY(event)));
  }
  return err;
}

drv8962_handle_t drv8962_new(const d_drv8962_cfg_t* cfg) {
  if (!cfg) return NULL;

  drv8962_dev_t* dev = (drv8962_dev_t*)malloc(sizeof(drv8962_dev_t));
  if (!dev) return NULL;

  memset(dev, 0, sizeof(drv8962_dev_t));
  dev->cfg = *cfg;
  dev->mutex = xSemaphoreCreateMutexStatic(&dev->mutex_buf);

  if (dev->cfg.pwm_freq_Hz == 0) {
    dev->cfg.pwm_freq_Hz = DRV8962_DEFAULT_FREQ_HZ;
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
    if (sys_io_pin_is_valid(dev->cfg.in_pins[i])) {
      SE_TRY(sys_io_set_mode(dev->cfg.in_pins[i]));
      SE_TRY(sys_io_set_pwm_frequency(dev->cfg.in_pins[i], dev->cfg.pwm_freq_Hz));
      SE_TRY(sys_io_set_pwm_duty(dev->cfg.in_pins[i], 0));
    }
  }

  /* 2. Configure EN pins as Digital Output */
  for (int i = 0; i < 4; i++) {
    if (sys_io_pin_is_valid(dev->cfg.en_pins[i])) {
      SE_TRY(sys_io_set_mode(dev->cfg.en_pins[i]));
      SE_TRY(sys_io_set_level(dev->cfg.en_pins[i], true));
    }
  }

  /* 3. Configure nSLEEP pin: drive HIGH to wake up */
  if (sys_io_pin_is_valid(dev->cfg.nsleep_pin)) {
    SE_TRY(sys_io_set_mode(dev->cfg.nsleep_pin));
    SE_TRY(sys_io_set_level(dev->cfg.nsleep_pin, true));
  }

  /* 4. Configure nFAULT pin interrupt */
  if (sys_io_pin_is_valid(dev->cfg.nfault_pin)) {
    SE_TRY(sys_io_set_mode(dev->cfg.nfault_pin));
    SE_TRY(sys_io_subscribe_pin(dev->cfg.nfault_pin, drv8962_fault_handler, dev, &dev->nfault_sub));
    dev->nfault_subscribed = true;
    sys_io_intr_config_t intr_cfg = {.mode = SYS_IO_INTR_MODE_FALLING_EDGE};
    SE_TRY(sys_io_configure_intr(dev->cfg.nfault_pin, &intr_cfg));
  }

  /* 5. Configure IPROPI ADC pins */
  for (int i = 0; i < 4; i++) {
    if (sys_io_pin_is_valid(dev->cfg.current_adc_pins[i])) {
      SE_TRY(sys_io_set_mode(dev->cfg.current_adc_pins[i]));
    }
  }

  /* 6. Configure VREF DAC pin */
  if (sys_io_pin_is_valid(dev->cfg.vref_dac_pin)) {
    SE_TRY(sys_io_set_mode(dev->cfg.vref_dac_pin));
  }

  /* Brake all channels by default */
  for (uint8_t i = 0; i < DRV_CHANNEL_COUNT(dev); i++) {
    SE_TRY(drv8962_brake(dev, i));
  }

  dev->is_started = true;
  return NULL;
}

err_h drv8962_stop(drv8962_handle_t handle) {
  SE_CHECK_NOT_NULL(handle);
  drv8962_dev_t* dev = (drv8962_dev_t*)handle;

  err_h err = NULL;

  /* Disable nFAULT interrupt */
  if (sys_io_pin_is_valid(dev->cfg.nfault_pin)) {
    sys_io_intr_config_t disable_intr = {.mode = SYS_IO_INTR_DISABLE};
    SYS_DEV_TEARDOWN_STEP(err, sys_io_configure_intr(dev->cfg.nfault_pin, &disable_intr));
  }
  if (dev->nfault_subscribed) {
    SYS_DEV_TEARDOWN_STEP(err, sys_event_unsubscribe(dev->nfault_sub, false));
    dev->nfault_subscribed = false;
  }

  /* Coast outputs (every channel, even if one fails) */
  for (uint8_t i = 0; i < DRV_CHANNEL_COUNT(dev); i++) {
    SYS_DEV_TEARDOWN_STEP(err, drv8962_coast(dev, i));
  }

  dev->is_started = false;
  return err;
}

err_h drv8962_set_mode(drv8962_handle_t handle, uint8_t channel, sys_hbridge_mode_e mode) {
  SE_CHECK_NOT_NULL(handle);
  drv8962_dev_t* dev = (drv8962_dev_t*)handle;
  if (channel >= 4) SE_FAIL(ERR_BASE_INVALID_STATE, 0);

  dev->channels[channel].mode = mode;
  return NULL;
}

err_h drv8962_set_drive(drv8962_handle_t handle, uint8_t channel, float magnitude) {
  SE_CHECK_NOT_NULL(handle);
  drv8962_dev_t* dev = (drv8962_dev_t*)handle;

  if (dev->cfg.topology == DRV8962_TOPOLOGY_2_FULL_BRIDGES) {
    if (channel >= 2) SE_FAIL(ERR_BASE_INVALID_STATE, 0);

    /* Clamp [-1.0f, +1.0f] */
    if (magnitude > 1.0f) magnitude = 1.0f;
    if (magnitude < -1.0f) magnitude = -1.0f;

    if (fabsf(magnitude) < 0.005f) {
      return drv8962_brake(handle, channel);
    }

    uint8_t in1_idx = (channel == 0) ? 0 : 2;
    uint8_t in2_idx = (channel == 0) ? 1 : 3;

    /* Ensure EN pins are HIGH */
    if (sys_io_pin_is_valid(dev->cfg.en_pins[in1_idx])) SE_TRY(sys_io_set_level(dev->cfg.en_pins[in1_idx], true));
    if (sys_io_pin_is_valid(dev->cfg.en_pins[in2_idx])) SE_TRY(sys_io_set_level(dev->cfg.en_pins[in2_idx], true));

    float mag = fabsf(magnitude);
    uint32_t duty = drive_to_duty(mag);
    uint32_t max_duty = (uint32_t)DRV8962_MAX_PWM_COUNTS;
    uint32_t comp_duty = max_duty - duty;

    if (magnitude > 0.0f) {
      /* Forward (Slow Decay): IN1 = 100%, IN2 = PWM comp */
      SE_TRY(sys_io_set_pwm_duty(dev->cfg.in_pins[in1_idx], max_duty));
      SE_TRY(sys_io_set_pwm_duty(dev->cfg.in_pins[in2_idx], comp_duty));
    } else {
      /* Reverse (Slow Decay): IN1 = PWM comp, IN2 = 100% */
      SE_TRY(sys_io_set_pwm_duty(dev->cfg.in_pins[in1_idx], comp_duty));
      SE_TRY(sys_io_set_pwm_duty(dev->cfg.in_pins[in2_idx], max_duty));
    }

    dev->channels[channel].current_drive = magnitude;
    return NULL;
  } else {
    /* 4 Half-Bridges Topology */
    if (channel >= 4) SE_FAIL(ERR_BASE_INVALID_STATE, 0);
    if (magnitude < 0.0f) magnitude = 0.0f;
    if (magnitude > 1.0f) magnitude = 1.0f;

    if (sys_io_pin_is_valid(dev->cfg.en_pins[channel])) SE_TRY(sys_io_set_level(dev->cfg.en_pins[channel], true));

    uint32_t duty = drive_to_duty(magnitude);
    SE_TRY(sys_io_set_pwm_duty(dev->cfg.in_pins[channel], duty));
    dev->channels[channel].current_drive = magnitude;
    return NULL;
  }
}

/* Safe-state op: every pin is written even if an earlier write fails; the
   first failure is returned. */
err_h drv8962_brake(drv8962_handle_t handle, uint8_t channel) {
  SE_CHECK_NOT_NULL(handle);
  drv8962_dev_t* dev = (drv8962_dev_t*)handle;
  if (channel >= DRV_CHANNEL_COUNT(dev)) SE_FAIL(ERR_BASE_INVALID_STATE, 0);

  /* Full bridge: channel 0 = half-bridges 0+1, channel 1 = 2+3 */
  uint8_t first = channel;
  uint8_t last = channel;
  if (dev->cfg.topology == DRV8962_TOPOLOGY_2_FULL_BRIDGES) {
    first = (channel == 0) ? 0 : 2;
    last = first + 1;
  }

  err_h err = NULL;
  for (uint8_t i = first; i <= last; i++) {
    if (sys_io_pin_is_valid(dev->cfg.en_pins[i])) SYS_DEV_TEARDOWN_STEP(err, sys_io_set_level(dev->cfg.en_pins[i], true));
    if (sys_io_pin_is_valid(dev->cfg.in_pins[i])) SYS_DEV_TEARDOWN_STEP(err, sys_io_set_pwm_duty(dev->cfg.in_pins[i], 0));
  }
  dev->channels[channel].current_drive = 0.0f;
  return err;
}

/* Safe-state op: every pin is written even if an earlier write fails; the
   first failure is returned. */
err_h drv8962_coast(drv8962_handle_t handle, uint8_t channel) {
  SE_CHECK_NOT_NULL(handle);
  drv8962_dev_t* dev = (drv8962_dev_t*)handle;
  if (channel >= DRV_CHANNEL_COUNT(dev)) SE_FAIL(ERR_BASE_INVALID_STATE, 0);

  /* Full bridge: channel 0 = half-bridges 0+1, channel 1 = 2+3 */
  uint8_t first = channel;
  uint8_t last = channel;
  if (dev->cfg.topology == DRV8962_TOPOLOGY_2_FULL_BRIDGES) {
    first = (channel == 0) ? 0 : 2;
    last = first + 1;
  }

  err_h err = NULL;
  for (uint8_t i = first; i <= last; i++) {
    if (sys_io_pin_is_valid(dev->cfg.en_pins[i])) SYS_DEV_TEARDOWN_STEP(err, sys_io_set_level(dev->cfg.en_pins[i], false));
    if (sys_io_pin_is_valid(dev->cfg.in_pins[i])) SYS_DEV_TEARDOWN_STEP(err, sys_io_set_pwm_duty(dev->cfg.in_pins[i], 0));
  }
  dev->channels[channel].current_drive = 0.0f;
  return err;
}

err_h drv8962_get_current_mA(drv8962_handle_t handle, uint8_t channel, int32_t* out_mA) {
  SE_CHECK_NOT_NULL(handle);
  SE_CHECK_NOT_NULL(out_mA);
  *out_mA = 0;
  drv8962_dev_t* dev = (drv8962_dev_t*)handle;
  if (channel >= DRV_CHANNEL_COUNT(dev)) SE_FAIL(ERR_BASE_INVALID_STATE, 0);

  SE_TRY(read_channel_current_mA(dev, channel, out_mA));

  /* Automatic OCP Guard: a failed fault dispatch is returned instead of the OCP error */
  uint32_t limit = dev->cfg.current_limit_mA[channel];
  if (limit > 0 && *out_mA > 0 && (uint32_t)*out_mA > limit) {
    SE_TRY(mark_channel_fault(dev, channel, SYS_HBRIDGE_FAULT_OVERCURRENT));
    SE_TRY(publish_channel_fault(dev, channel, 0));
    SE_FAIL(ERR_BASE_INVALID_STATE, 0);
  }

  return NULL;
}

err_h drv8962_set_current_limit_mA(drv8962_handle_t handle, uint8_t channel, uint32_t limit_mA) {
  SE_CHECK_NOT_NULL(handle);
  drv8962_dev_t* dev = (drv8962_dev_t*)handle;
  if (channel >= 4) SE_FAIL(ERR_BASE_INVALID_STATE, 0);

  dev->cfg.current_limit_mA[channel] = limit_mA;

  /* If VREF DAC pin is connected, set analog reference voltage */
  if (sys_io_pin_is_valid(dev->cfg.vref_dac_pin)) {
    float vref_mV = ((float)limit_mA) * DRV8962_AIPROPI_GAIN * ((float)dev->cfg.ripropi_ohms[channel]);
    if (vref_mV > 3300.0f) vref_mV = 3300.0f;
    if (vref_mV < 50.0f) vref_mV = 50.0f;
    SE_TRY(sys_io_set_voltage(dev->cfg.vref_dac_pin, (uint32_t)lroundf(vref_mV)));
  }

  return NULL;
}

err_h drv8962_get_fault(drv8962_handle_t handle, uint8_t channel, bool* out_fault, sys_hbridge_fault_reason_e* out_reason) {
  SE_CHECK_NOT_NULL(handle);
  SE_CHECK_NOT_NULL(out_fault);
  drv8962_dev_t* dev = (drv8962_dev_t*)handle;
  if (channel >= 4) SE_FAIL(ERR_BASE_INVALID_STATE, 0);

  *out_fault = dev->channels[channel].is_fault;
  if (out_reason) {
    *out_reason = dev->channels[channel].fault_reason;
  }
  return NULL;
}

err_h drv8962_clear_fault(drv8962_handle_t handle, uint8_t channel) {
  SE_CHECK_NOT_NULL(handle);
  drv8962_dev_t* dev = (drv8962_dev_t*)handle;
  if (channel >= 4) SE_FAIL(ERR_BASE_INVALID_STATE, 0);

  /* DRV8962 nSLEEP 30 us reset pulse clears internal fault latches */
  if (sys_io_pin_is_valid(dev->cfg.nsleep_pin)) {
    SE_TRY(sys_io_set_level(dev->cfg.nsleep_pin, false));
    ets_delay_us(30);
    SE_TRY(sys_io_set_level(dev->cfg.nsleep_pin, true));
    ets_delay_us(1000); /* 1 ms wake delay */
  }

  dev->channels[channel].is_fault = false;
  dev->channels[channel].fault_reason = SYS_HBRIDGE_FAULT_NONE;
  return drv8962_brake(handle, channel);
}
