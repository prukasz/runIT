#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "device_drv8962.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "rom/ets_sys.h"
#include "sys_device.h"
#include "sys_error.h"
#include "sys_hbridge.h"
#include "sys_io.h"

#define OWNER OWNER_DEVICE

#define DRV8962_AIPROPI_GAIN 0.000212f
#define DRV8962_MAX_PWM_COUNTS 4096.0f
#define DRV8962_DEFAULT_FREQ_HZ 20000
#define DRV8962_DEFAULT_RIPROPI_OHMS 3090
#define DRV8962_DRIVE_DEADBAND 0.005f  // a full-bridge magnitude below this brakes

// Instance state: the whole device in one struct (the create cfg is not kept).
typedef struct drv_channel_t {
  float drive;
  int32_t last_current_mA;
  bool is_fault;
  sys_hbridge_fault_reason_e fault_reason;
} drv_channel_t;

typedef struct drv_ctx_t {
  sys_device_base_t base;  // must be first

  sys_io_pin_ref_t in[4];      // IN1..IN4
  sys_io_pin_ref_t en[4];      // EN1..EN4
  sys_io_pin_ref_t ipropi[4];  // current sense outputs
  sys_io_pin_ref_t nsleep;
  sys_io_pin_ref_t nfault;
  sys_io_pin_ref_t vref;

  uint8_t topology;  // drv8962_topology_e
  uint32_t pwm_freq_Hz;
  uint32_t ripropi_ohms[4];
  uint32_t current_limit_mA[4];
  bool turn_off_at_ocp;

  drv_channel_t channels[4];
  SemaphoreHandle_t mutex;  // the fault handler's view of the channels
  StaticSemaphore_t mutex_buf;
  uint8_t nfault_sub;  // sys_event subscription on nfault
} drv_ctx_t;

// Install steps, recorded so teardown rolls back only what was actually built
enum { DRV_STEP_NFAULT_SUB = 0, DRV_STEP_STARTED = 1 };

#define DRV_CHANNELS(c) (((c)->topology == DRV8962_TOPOLOGY_2_FULL_BRIDGES) ? 2 : 4)

/* Full bridge: channel 0 = half bridges 0+1, channel 1 = 2+3. Half bridges: one each. */
static void channel_bridges(const drv_ctx_t* c, uint8_t channel, uint8_t* first, uint8_t* last) {
  if (c->topology == DRV8962_TOPOLOGY_2_FULL_BRIDGES) {
    *first = (channel == 0) ? 0 : 2;
    *last = *first + 1;
  } else {
    *first = *last = channel;
  }
}

static uint32_t drive_to_duty(float magnitude) {
  if (magnitude < 0.0f) magnitude = 0.0f;
  if (magnitude > 1.0f) magnitude = 1.0f;
  return (uint32_t)lroundf(magnitude * DRV8962_MAX_PWM_COUNTS);
}

/* ---- Chip access: err_h, the dispatcher above adds the device id ---- */

static SE_MUST_USE err_h sample_pin_current_mA(sys_io_pin_ref_t pin, uint32_t ripropi_ohms, int32_t* out_mA) {
  *out_mA = 0;
  if (!sys_io_pin_is_valid(pin)) return NULL;
  int32_t adc_mV = 0;
  SE_TRY(sys_io_get_voltage(pin, &adc_mV));
  float denominator = DRV8962_AIPROPI_GAIN * (float)ripropi_ohms;
  if (denominator <= 0.00001f) return NULL;
  *out_mA = (int32_t)lroundf(((float)adc_mV) / denominator);
  return NULL;
}

/* A limit of 0 disables the check; a negative reading is never over a limit. */
static bool current_at_or_over(int32_t mA, uint32_t limit_mA) { return limit_mA > 0 && mA >= 0 && (uint32_t)mA >= limit_mA; }

/* Channel current without the OCP guard (full bridge: the larger of its two half-bridge IPROPI readings). */
static SE_MUST_USE err_h read_channel_current_mA(drv_ctx_t* c, uint8_t channel, int32_t* out_mA) {
  uint8_t first, last;
  channel_bridges(c, channel, &first, &last);
  SE_TRY(sample_pin_current_mA(c->ipropi[first], c->ripropi_ohms[first], out_mA));
  if (last != first) {
    int32_t other = 0;
    SE_TRY(sample_pin_current_mA(c->ipropi[last], c->ripropi_ohms[last], &other));
    if (other > *out_mA) *out_mA = other;
  }
  c->channels[channel].last_current_mA = *out_mA;
  return NULL;
}

/* Safe-state op: every pin is written even if an earlier write fails; the first failure is returned. */
static SE_MUST_USE err_h chip_set_outputs(drv_ctx_t* c, uint8_t channel, bool enabled) {
  uint8_t first, last;
  channel_bridges(c, channel, &first, &last);

  err_h err = NULL;
  for (uint8_t i = first; i <= last; i++) {
    if (sys_io_pin_is_valid(c->en[i])) SYS_DEV_TEARDOWN_STEP(err, sys_io_set_level(c->en[i], enabled));
    if (sys_io_pin_is_valid(c->in[i])) SYS_DEV_TEARDOWN_STEP(err, sys_io_set_pwm_duty(c->in[i], 0));
  }
  c->channels[channel].drive = 0.0f;
  return err;
}

static SE_MUST_USE err_h chip_brake(drv_ctx_t* c, uint8_t channel) { return chip_set_outputs(c, channel, true); }
static SE_MUST_USE err_h chip_coast(drv_ctx_t* c, uint8_t channel) { return chip_set_outputs(c, channel, false); }

static SE_MUST_USE err_h chip_set_drive(drv_ctx_t* c, uint8_t channel, float magnitude) {
  if (c->topology == DRV8962_TOPOLOGY_2_FULL_BRIDGES) {
    if (magnitude > 1.0f) magnitude = 1.0f;
    if (magnitude < -1.0f) magnitude = -1.0f;
    if (fabsf(magnitude) < DRV8962_DRIVE_DEADBAND) return chip_brake(c, channel);

    uint8_t in1, in2;
    channel_bridges(c, channel, &in1, &in2);
    if (sys_io_pin_is_valid(c->en[in1])) SE_TRY(sys_io_set_level(c->en[in1], true));
    if (sys_io_pin_is_valid(c->en[in2])) SE_TRY(sys_io_set_level(c->en[in2], true));

    const uint32_t max_duty = (uint32_t)DRV8962_MAX_PWM_COUNTS;
    const uint32_t comp_duty = max_duty - drive_to_duty(fabsf(magnitude));
    if (magnitude > 0.0f) {
      /* Forward (slow decay): IN1 = 100 %, IN2 = PWM complement */
      SE_TRY(sys_io_set_pwm_duty(c->in[in1], max_duty));
      SE_TRY(sys_io_set_pwm_duty(c->in[in2], comp_duty));
    } else {
      /* Reverse (slow decay): IN1 = PWM complement, IN2 = 100 % */
      SE_TRY(sys_io_set_pwm_duty(c->in[in1], comp_duty));
      SE_TRY(sys_io_set_pwm_duty(c->in[in2], max_duty));
    }
  } else {
    /* Four half bridges: 0.0 .. 1.0 */
    if (magnitude < 0.0f) magnitude = 0.0f;
    if (magnitude > 1.0f) magnitude = 1.0f;
    if (sys_io_pin_is_valid(c->en[channel])) SE_TRY(sys_io_set_level(c->en[channel], true));
    SE_TRY(sys_io_set_pwm_duty(c->in[channel], drive_to_duty(magnitude)));
  }
  c->channels[channel].drive = magnitude;
  return NULL;
}

/* VREF sets the chopping threshold of every channel of the chip (ITRIP x AIPROPI = VREF / RIPROPI). It
   follows the largest channel limit; 0 everywhere = no regulation (VREF at its 3.3 V maximum). */
static SE_MUST_USE err_h chip_apply_current_limit(drv_ctx_t* c, uint8_t channel) {
  if (!sys_io_pin_is_valid(c->vref)) return NULL;

  uint32_t chip_mA = 0;
  for (int i = 0; i < 4; i++) {
    if (c->current_limit_mA[i] > chip_mA) chip_mA = c->current_limit_mA[i];
  }
  float vref_mV = chip_mA ? ((float)chip_mA) * DRV8962_AIPROPI_GAIN * ((float)c->ripropi_ohms[channel]) : 3300.0f;
  if (vref_mV > 3300.0f) vref_mV = 3300.0f;
  if (vref_mV < 50.0f) vref_mV = 50.0f;
  return sys_io_set_voltage(c->vref, (uint32_t)lroundf(vref_mV));
}

/* Latch the fault and brake the channel (turn_off_at_ocp). The caller publishes it after releasing the mutex. */
static SE_MUST_USE err_h mark_channel_fault(drv_ctx_t* c, uint8_t channel, sys_hbridge_fault_reason_e reason) {
  c->channels[channel].is_fault = true;
  c->channels[channel].fault_reason = reason;
  return c->turn_off_at_ocp ? chip_brake(c, channel) : NULL;
}

static SE_MUST_USE err_h publish_channel_fault(drv_ctx_t* c, uint8_t channel, uint8_t hops) {
  const drv_channel_t* st = &c->channels[channel];
  return sys_hbridge_publish(SYS_DEV_GET_ID(c), channel, st->fault_reason, st->last_current_mA, hops);
}

/* Finds the faulted channels on the nFAULT edge, then publishes them. */
static SE_MUST_USE err_h handle_fault(const sys_event_t* event, drv_ctx_t* c) {
  if (xSemaphoreTake(c->mutex, portMAX_DELAY) != pdTRUE) return NULL;

  const uint8_t num_channels = DRV_CHANNELS(c);
  uint8_t active_count = 0;
  int last_active_ch = -1;
  uint8_t oc_count = 0;
  uint8_t faulted = 0; /* bit per channel */
  err_h err = NULL;

  /* Sample current on all channels (a failed read counts as 0 mA). No OCP guard here: the
     classification below dispatches each fault once. */
  for (uint8_t ch = 0; ch < num_channels; ch++) {
    int32_t ma = 0;
    SYS_DEV_TEARDOWN_STEP(err, read_channel_current_mA(c, ch, &ma));

    if (fabsf(c->channels[ch].drive) > DRV8962_DRIVE_DEADBAND) {
      active_count++;
      last_active_ch = ch;
    }
    if (current_at_or_over(ma, c->current_limit_mA[ch])) oc_count++;
  }

  if (oc_count > 0) {
    /* Direct overcurrent detected */
    for (uint8_t ch = 0; ch < num_channels; ch++) {
      if (current_at_or_over(c->channels[ch].last_current_mA, c->current_limit_mA[ch])) {
        SYS_DEV_TEARDOWN_STEP(err, mark_channel_fault(c, ch, SYS_HBRIDGE_FAULT_OVERCURRENT));
        faulted |= (uint8_t)(1u << ch);
      }
    }
  } else if (active_count == 1 && last_active_ch >= 0) {
    /* Exactly one channel was driving when the fault occurred -> culprit channel */
    SYS_DEV_TEARDOWN_STEP(err, mark_channel_fault(c, (uint8_t)last_active_ch, SYS_HBRIDGE_FAULT_OVERCURRENT));
    faulted |= (uint8_t)(1u << last_active_ch);
  } else {
    /* Chip-wide thermal shutdown (OTSD) or supply rail UVLO */
    for (uint8_t ch = 0; ch < num_channels; ch++) {
      SYS_DEV_TEARDOWN_STEP(err, mark_channel_fault(c, ch, SYS_HBRIDGE_FAULT_THERMAL_OR_UVLO));
      faulted |= (uint8_t)(1u << ch);
    }
  }

  xSemaphoreGive(c->mutex);

  /* Outside the mutex: an inline listener may call back into this device. */
  for (uint8_t ch = 0; ch < num_channels; ch++) {
    if (faulted & (1u << ch)) SYS_DEV_TEARDOWN_STEP(err, publish_channel_fault(c, ch, SYS_EVENT_CAUSED_BY(event)));
  }
  return err;
}

/* Inline listener of nFAULT. No dispatcher wraps a listener's error, so it carries the device id here. */
static SE_MUST_USE err_h device_event_handler(const sys_event_t* event, void* handle) {
  SYS_DEV_CTX_FROM(drv_ctx_t, c, handle);
  err_h err = handle_fault(event, c);
  return SYS_DEV_WRAP(err, SYS_DEV_GET_ID(c));
}

/* ---- H-bridge contract ---- */

static SE_MUST_USE err_h hbridge_set_drive(void* handle, uint8_t channel, float magnitude) {
  SYS_DEV_CTX_FROM(drv_ctx_t, c, handle);
  SE_CHECK_IN_RANGE(channel, 0, DRV_CHANNELS(c) - 1);
  return chip_set_drive(c, channel, magnitude);
}

static SE_MUST_USE err_h hbridge_brake(void* handle, uint8_t channel) {
  SYS_DEV_CTX_FROM(drv_ctx_t, c, handle);
  SE_CHECK_IN_RANGE(channel, 0, DRV_CHANNELS(c) - 1);
  return chip_brake(c, channel);
}

static SE_MUST_USE err_h hbridge_coast(void* handle, uint8_t channel) {
  SYS_DEV_CTX_FROM(drv_ctx_t, c, handle);
  SE_CHECK_IN_RANGE(channel, 0, DRV_CHANNELS(c) - 1);
  return chip_coast(c, channel);
}

static SE_MUST_USE err_h hbridge_get_current_mA(void* handle, uint8_t channel, int32_t* out_mA) {
  SYS_DEV_CTX_FROM(drv_ctx_t, c, handle);
  SE_CHECK_NOT_NULL(out_mA);
  *out_mA = 0;
  SE_CHECK_IN_RANGE(channel, 0, DRV_CHANNELS(c) - 1);

  SE_TRY(read_channel_current_mA(c, channel, out_mA));

  /* Automatic OCP guard: a failed fault dispatch is returned instead of the OCP error */
  const uint32_t limit = c->current_limit_mA[channel];
  if (limit > 0 && *out_mA > 0 && (uint32_t)*out_mA > limit) {
    SE_TRY(mark_channel_fault(c, channel, SYS_HBRIDGE_FAULT_OVERCURRENT));
    SE_TRY(publish_channel_fault(c, channel, 0));
    SE_FAIL(ERR_BASE_INVALID_STATE, 0);
  }
  return NULL;
}

static const sys_hbridge_contract_t s_drv8962_hbridge_contract = {
    .set_drive = hbridge_set_drive, .brake = hbridge_brake, .coast = hbridge_coast, .get_current_mA = hbridge_get_current_mA};

/* ---- Operations of the device ---- */

static SE_MUST_USE err_h op_set_current_limit(void* handle, const uint8_t* args, size_t len, sys_device_reply_t* reply) {
  (void)len, (void)reply;
  SYS_DEV_CTX_FROM(drv_ctx_t, c, handle);
  SYS_DEVICE_OP_ARGS(drv8962_set_current_limit_t, a, args);
  SE_CHECK_IN_RANGE(a.channel, 0, DRV_CHANNELS(c) - 1);
  c->current_limit_mA[a.channel] = a.limit_mA;
  return chip_apply_current_limit(c, a.channel);
}

static SE_MUST_USE err_h op_clear_fault(void* handle, const uint8_t* args, size_t len, sys_device_reply_t* reply) {
  (void)len, (void)reply;
  SYS_DEV_CTX_FROM(drv_ctx_t, c, handle);
  SYS_DEVICE_OP_ARGS(drv8962_channel_t, a, args);
  SE_CHECK_IN_RANGE(a.channel, 0, DRV_CHANNELS(c) - 1);

  /* nSLEEP 30 us reset pulse clears the internal fault latches */
  if (sys_io_pin_is_valid(c->nsleep)) {
    SE_TRY(sys_io_set_level(c->nsleep, false));
    ets_delay_us(30);
    SE_TRY(sys_io_set_level(c->nsleep, true));
    ets_delay_us(1000); /* 1 ms wake delay */
  }

  c->channels[a.channel].is_fault = false;
  c->channels[a.channel].fault_reason = SYS_HBRIDGE_FAULT_NONE;
  return chip_brake(c, a.channel);
}

static SE_MUST_USE err_h op_get_fault(void* handle, const uint8_t* args, size_t len, sys_device_reply_t* reply) {
  (void)len;
  SYS_DEV_CTX_FROM(drv_ctx_t, c, handle);
  SYS_DEVICE_OP_ARGS(drv8962_channel_t, a, args);
  SE_CHECK_IN_RANGE(a.channel, 0, DRV_CHANNELS(c) - 1);

  const drv8962_fault_t fault = {.fault = c->channels[a.channel].is_fault, .reason = (uint8_t)c->channels[a.channel].fault_reason};
  return sys_device_reply_put(reply, &fault, sizeof(fault));
}

static const sys_device_op_t s_ops[] = {
    SYS_DEVICE_OP(DRV8962_OP_SET_CURRENT_LIMIT, drv8962_set_current_limit_t, op_set_current_limit),
    SYS_DEVICE_OP(DRV8962_OP_CLEAR_FAULT, drv8962_channel_t, op_clear_fault),
    SYS_DEVICE_OP_RO(DRV8962_OP_GET_FAULT, drv8962_channel_t, op_get_fault),
};

/* ---- Lifecycle ---- */

/* Returns every dependency pin (possibly SYS_IO_PIN_NONE) to its unconfigured state. */
static SE_MUST_USE err_h reset_dependency_pins(const drv_ctx_t* c) {
  err_h err = NULL;
  for (int i = 0; i < 4; i++) {
    if (sys_io_pin_is_valid(c->in[i])) SYS_DEV_TEARDOWN_STEP(err, sys_io_reset(c->in[i]));
    if (sys_io_pin_is_valid(c->en[i])) SYS_DEV_TEARDOWN_STEP(err, sys_io_reset(c->en[i]));
    if (sys_io_pin_is_valid(c->ipropi[i])) SYS_DEV_TEARDOWN_STEP(err, sys_io_reset(c->ipropi[i]));
  }
  if (sys_io_pin_is_valid(c->nsleep)) SYS_DEV_TEARDOWN_STEP(err, sys_io_reset(c->nsleep));
  if (sys_io_pin_is_valid(c->nfault)) SYS_DEV_TEARDOWN_STEP(err, sys_io_reset(c->nfault));
  if (sys_io_pin_is_valid(c->vref)) SYS_DEV_TEARDOWN_STEP(err, sys_io_reset(c->vref));
  return err;
}

// Doubles as the install rollback path: each step is gated on having actually
// run, and no step may early-return - teardown must always free everything.
static SE_MUST_USE err_h device_uninstall(void* handle) {
  SYS_DEV_CTX_FROM(drv_ctx_t, c, handle);
  err_h err = NULL;

  IF_SYS_DEV_STEP_DONE(c, DRV_STEP_NFAULT_SUB) {
    sys_io_intr_config_t disable_intr = {.mode = SYS_IO_INTR_DISABLE};
    SYS_DEV_TEARDOWN_STEP(err, sys_io_configure_intr(c->nfault, &disable_intr));
    SYS_DEV_TEARDOWN_STEP(err, sys_event_unsubscribe(c->nfault_sub, false));
  }
  IF_SYS_DEV_STEP_DONE(c, DRV_STEP_STARTED) {
    for (uint8_t i = 0; i < DRV_CHANNELS(c); i++) {
      SYS_DEV_TEARDOWN_STEP(err, chip_coast(c, i));
    }
  }
  // start configures pins one by one, so a partial start still needs them reset
  SYS_DEV_TEARDOWN_STEP(err, reset_dependency_pins(c));

  free(c);
  return err;
}

static SE_MUST_USE err_h device_reset(void* handle) {
  SYS_DEV_CTX_FROM(drv_ctx_t, c, handle);
  err_h err = NULL;
  for (uint8_t i = 0; i < DRV_CHANNELS(c); i++) {
    SYS_DEV_TEARDOWN_STEP(err, chip_brake(c, i));
  }
  return err;
}

// Fault safe state: every channel must be reached even if one fails.
static SE_MUST_USE err_h device_suspend(void* handle) {
  SYS_DEV_CTX_FROM(drv_ctx_t, c, handle);
  err_h err = NULL;
  for (uint8_t i = 0; i < DRV_CHANNELS(c); i++) {
    SYS_DEV_TEARDOWN_STEP(err, chip_coast(c, i));
  }
  return err;
}

// Channels stay coasted after a suspend; the next drive command re-engages them.
static SE_MUST_USE err_h device_resume(void* handle) {
  (void)handle;
  return NULL;
}

/* Pin mode for a dependency pin. Fixed-function outputs (PCA9685 channels, DAC53202 outputs) have no
   modes: their IO contract has no set_mode, which is fine here. */
static SE_MUST_USE err_h prepare_pin(sys_io_pin_ref_t pin) {
  err_h err = sys_io_set_mode(pin);
  err_h root = err ? SE_get_error_root(err) : NULL;
  if (root && root->tag == ERR_DEV_FEATURE_UNAVAILABLE) {
    SE_release(err);
    return NULL;
  }
  return err;
}

static SE_MUST_USE err_h device_install(const void* cfg_blob, void** out_device_handle) {
  const d_drv8962_cfg_t* cfg = (const d_drv8962_cfg_t*)cfg_blob;
  SE_CHECK_IN_RANGE(cfg->topology, DRV8962_TOPOLOGY_2_FULL_BRIDGES, DRV8962_TOPOLOGY_4_HALF_BRIDGES);

  // The cfg is read only here: what the device keeps goes into its own state.
  drv_ctx_t* c = (drv_ctx_t*)calloc(1, sizeof(drv_ctx_t));
  SE_CHECK_IF_ALLOCATED(c);
  c->base.device_id = cfg->device_id;
  c->mutex = xSemaphoreCreateMutexStatic(&c->mutex_buf);
  c->topology = cfg->topology;
  c->nsleep = cfg->nsleep_pin;
  c->nfault = cfg->nfault_pin;
  c->vref = cfg->vref_pin;
  c->in[0] = cfg->in1_pin, c->in[1] = cfg->in2_pin, c->in[2] = cfg->in3_pin, c->in[3] = cfg->in4_pin;
  c->en[0] = cfg->en1_pin, c->en[1] = cfg->en2_pin, c->en[2] = cfg->en3_pin, c->en[3] = cfg->en4_pin;
  c->ipropi[0] = cfg->ipropi1_pin, c->ipropi[1] = cfg->ipropi2_pin, c->ipropi[2] = cfg->ipropi3_pin, c->ipropi[3] = cfg->ipropi4_pin;
  c->pwm_freq_Hz = cfg->pwm_freq_Hz ? cfg->pwm_freq_Hz : DRV8962_DEFAULT_FREQ_HZ;
  for (int i = 0; i < 4; i++) {
    c->ripropi_ohms[i] = cfg->ripropi_ohms[i] ? cfg->ripropi_ohms[i] : DRV8962_DEFAULT_RIPROPI_OHMS;
    c->current_limit_mA[i] = cfg->current_limit_mA[i];
  }
  c->turn_off_at_ocp = cfg->turn_off_at_ocp != 0;
  err_h err = NULL;

  /* 1. IN pins as PWM */
  for (int i = 0; i < 4; i++) {
    if (!sys_io_pin_is_valid(c->in[i])) continue;
    SYS_DEV_INSTALL_STEP(prepare_pin(c->in[i]), "IN pin mode");
    SYS_DEV_INSTALL_STEP(sys_io_set_pwm_frequency(c->in[i], c->pwm_freq_Hz), "IN pin frequency");
    SYS_DEV_INSTALL_STEP(sys_io_set_pwm_duty(c->in[i], 0), "IN pin duty");
  }

  /* 2. EN pins as digital outputs */
  for (int i = 0; i < 4; i++) {
    if (!sys_io_pin_is_valid(c->en[i])) continue;
    SYS_DEV_INSTALL_STEP(prepare_pin(c->en[i]), "EN pin mode");
    SYS_DEV_INSTALL_STEP(sys_io_set_level(c->en[i], true), "EN pin high");
  }

  /* 3. nSLEEP: drive HIGH to wake up */
  if (sys_io_pin_is_valid(c->nsleep)) {
    SYS_DEV_INSTALL_STEP(prepare_pin(c->nsleep), "nSLEEP pin mode");
    SYS_DEV_INSTALL_STEP(sys_io_set_level(c->nsleep, true), "nSLEEP pin high");
    vTaskDelay(pdMS_TO_TICKS(2)); /* tWAKE: 1.2 ms max before inputs are taken */
  }

  /* 4. nFAULT interrupt */
  if (sys_io_pin_is_valid(c->nfault)) {
    SYS_DEV_INSTALL_STEP(prepare_pin(c->nfault), "nFAULT pin mode");
    SYS_DEV_INSTALL_STEP_BIT(c, DRV_STEP_NFAULT_SUB, sys_io_subscribe_pin(c->nfault, device_event_handler, c, &c->nfault_sub), "nFAULT subscribe");
    sys_io_intr_config_t intr_cfg = {.mode = SYS_IO_INTR_MODE_FALLING_EDGE};
    SYS_DEV_INSTALL_STEP(sys_io_configure_intr(c->nfault, &intr_cfg), "nFAULT interrupt");
  }

  /* 5. IPROPI ADC pins */
  for (int i = 0; i < 4; i++) {
    if (sys_io_pin_is_valid(c->ipropi[i])) SYS_DEV_INSTALL_STEP(prepare_pin(c->ipropi[i]), "IPROPI pin mode");
  }

  /* 6. VREF DAC pin: must be driven (0.05..3.3 V) - a floating VREF sets an undefined chopping threshold */
  if (sys_io_pin_is_valid(c->vref)) {
    SYS_DEV_INSTALL_STEP(prepare_pin(c->vref), "VREF pin mode");
    SYS_DEV_INSTALL_STEP(chip_apply_current_limit(c, 0), "VREF level");
  }

  /* Brake all channels by default */
  for (uint8_t i = 0; i < DRV_CHANNELS(c); i++) {
    SYS_DEV_INSTALL_STEP(chip_brake(c, i), "brake");
  }
  SYS_DEV_STEP_DONE(c, DRV_STEP_STARTED);

  *out_device_handle = c;
  return NULL;

fail:
  SYS_DEV_INSTALL_FAIL(err, out_device_handle, device_uninstall, c);
  return NULL;
}

static const uint8_t s_pin_refs[] = {
    offsetof(d_drv8962_cfg_t, in1_pin), offsetof(d_drv8962_cfg_t, in2_pin), offsetof(d_drv8962_cfg_t, in3_pin), offsetof(d_drv8962_cfg_t, in4_pin),
    offsetof(d_drv8962_cfg_t, en1_pin), offsetof(d_drv8962_cfg_t, en2_pin), offsetof(d_drv8962_cfg_t, en3_pin), offsetof(d_drv8962_cfg_t, en4_pin),
    offsetof(d_drv8962_cfg_t, ipropi1_pin), offsetof(d_drv8962_cfg_t, ipropi2_pin), offsetof(d_drv8962_cfg_t, ipropi3_pin),
    offsetof(d_drv8962_cfg_t, ipropi4_pin), offsetof(d_drv8962_cfg_t, nsleep_pin), offsetof(d_drv8962_cfg_t, nfault_pin),
    offsetof(d_drv8962_cfg_t, vref_pin),
};

const sys_device_class_t g_drv8962_class = {
    .type_id = DRV8962_TYPE_ID,
    .cfg_size = sizeof(d_drv8962_cfg_t),
    SYS_DEVICE_PINS(s_pin_refs),
    .name = "DRV8962_BRIDGE_DRIVER",
    .contracts = {[SYS_DEVICE_CONTRACT_HBRIDGE] = &s_drv8962_hbridge_contract},
    .ops = {.install = device_install,
        .uninstall = device_uninstall,
        .reset = device_reset,
        .suspend = device_suspend,
        .resume = device_resume},
    SYS_DEVICE_OPS(s_ops),
};
