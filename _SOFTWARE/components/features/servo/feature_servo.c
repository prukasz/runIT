#include "feature_servo.h"
#include "feature_registry.h"
#include "sys_io.h"
#include <math.h>

#define TAG "FEAT_SERVO"
#define OWNER OWNER_SYS_ERRORS_BASE

#define SERVO_DEFAULT_FREQ_HZ    50
#define SERVO_DEFAULT_PULSE_MIN  500
#define SERVO_DEFAULT_PULSE_MAX  2500

/* Teardown function invoked automatically by feature_remove / feature_remove_all */
static void servo_teardown(void* data) {
  feature_servo_t* s = (feature_servo_t*)data;
  if (s && s->is_attached) {
    /* Set PWM duty to 0 to safely de-energize the servo */
    sys_io_set_pwm_duty(s->device_id, s->pin_num, 0);
    s->is_attached = false;
  }
}

static uint32_t servo_calc_duty(const feature_servo_t* s, float effective_angle) {
  /* Clamp effective angle strictly within bounds */
  if (effective_angle < s->angle_min) effective_angle = s->angle_min;
  if (effective_angle > s->angle_max) effective_angle = s->angle_max;

  /* Handle inversion */
  float normalized = 0.0f;
  float angle_range = s->angle_max - s->angle_min;
  if (angle_range > 0.0001f) {
    if (s->inverted) {
      normalized = (s->angle_max - effective_angle) / angle_range;
    } else {
      normalized = (effective_angle - s->angle_min) / angle_range;
    }
  }

  /* Calculate pulse width in microseconds */
  float pulse_us = (float)s->pulse_min_us + normalized * (float)(s->pulse_max_us - s->pulse_min_us);

  /* Total period in microseconds = 1,000,000 / frequency_hz */
  float period_us = 1000000.0f / (float)s->frequency_hz;

  /*
   * Hardware Duty scale:
   * PCA9685 max duty value is 4096 (12-bit).
   * For standard 12-bit PWM: duty = (pulse_us / period_us) * 4096.
   * At 50Hz, period is 20,000 us.
   */
  float duty_counts = (pulse_us / period_us) * 4096.0f;
  if (duty_counts < 0.0f) duty_counts = 0.0f;
  if (duty_counts > 4096.0f) duty_counts = 4096.0f;

  return (uint32_t)lroundf(duty_counts);
}

static err_h servo_apply_angle(feature_servo_t* s, float logical_angle) {
  float effective_angle = logical_angle + s->trim_angle;
  uint32_t duty = servo_calc_duty(s, effective_angle);

  SE_RET_IF_ERR(sys_io_set_pwm_duty(s->device_id, s->pin_num, duty));
  s->last_angle = logical_angle;
  s->is_attached = true;
  return NULL;
}

err_h feature_servo_create(uint8_t feature_id, const feature_servo_t* config) {
  SE_CHECK_NOT_NULL(config);

  if (config->angle_max <= config->angle_min) {
    SE_RET_ERR(ERR_INVALID_VAL_F, .val = config->angle_max, .min = config->angle_min, .max = 360.0f);
  }

  feature_servo_t* s = NULL;
  SE_RET_IF_ERR(feature_alloc(feature_id, sizeof(feature_servo_t), servo_teardown, (void**)&s));

  /* Copy configuration */
  *s = *config;

  /* Apply sensible defaults if zero */
  if (s->frequency_hz == 0) {
    s->frequency_hz = SERVO_DEFAULT_FREQ_HZ;
  }
  if (s->pulse_min_us == 0) {
    s->pulse_min_us = SERVO_DEFAULT_PULSE_MIN;
  }
  if (s->pulse_max_us == 0) {
    s->pulse_max_us = SERVO_DEFAULT_PULSE_MAX;
  }

  /* Configure hardware PWM frequency */
  SE_RET_IF_ERR(sys_io_set_pwm_frequency(s->device_id, s->pin_num, s->frequency_hz));

  /* Move to default position */
  s->last_angle = s->default_angle;
  s->is_attached = false;

  return servo_apply_angle(s, s->default_angle);
}

err_h feature_servo_set_angle(uint8_t feature_id, float angle) {
  feature_servo_t* s = (feature_servo_t*)feature_get_by_id(feature_id);
  if (!s) {
    SE_RET_ERR(ERR_BASE_NOT_FOUND, 0);
  }

  return servo_apply_angle(s, angle);
}

err_h feature_servo_get_angle(uint8_t feature_id, float* out_angle) {
  SE_CHECK_NOT_NULL(out_angle);

  feature_servo_t* s = (feature_servo_t*)feature_get_by_id(feature_id);
  if (!s) {
    SE_RET_ERR(ERR_BASE_NOT_FOUND, 0);
  }

  *out_angle = s->last_angle;
  return NULL;
}

err_h feature_servo_set_trim(uint8_t feature_id, float trim_angle) {
  feature_servo_t* s = (feature_servo_t*)feature_get_by_id(feature_id);
  if (!s) {
    SE_RET_ERR(ERR_BASE_NOT_FOUND, 0);
  }

  s->trim_angle = trim_angle;
  /* Re-apply angle if currently attached */
  if (s->is_attached) {
    return servo_apply_angle(s, s->last_angle);
  }
  return NULL;
}

err_h feature_servo_get_trim(uint8_t feature_id, float* out_trim_angle) {
  SE_CHECK_NOT_NULL(out_trim_angle);

  feature_servo_t* s = (feature_servo_t*)feature_get_by_id(feature_id);
  if (!s) {
    SE_RET_ERR(ERR_BASE_NOT_FOUND, 0);
  }

  *out_trim_angle = s->trim_angle;
  return NULL;
}

err_h feature_servo_go_home(uint8_t feature_id) {
  feature_servo_t* s = (feature_servo_t*)feature_get_by_id(feature_id);
  if (!s) {
    SE_RET_ERR(ERR_BASE_NOT_FOUND, 0);
  }

  return servo_apply_angle(s, s->default_angle);
}

err_h feature_servo_attach(uint8_t feature_id) {
  feature_servo_t* s = (feature_servo_t*)feature_get_by_id(feature_id);
  if (!s) {
    SE_RET_ERR(ERR_BASE_NOT_FOUND, 0);
  }

  return servo_apply_angle(s, s->last_angle);
}

err_h feature_servo_detach(uint8_t feature_id) {
  feature_servo_t* s = (feature_servo_t*)feature_get_by_id(feature_id);
  if (!s) {
    SE_RET_ERR(ERR_BASE_NOT_FOUND, 0);
  }

  SE_RET_IF_ERR(sys_io_set_pwm_duty(s->device_id, s->pin_num, 0));
  s->is_attached = false;
  return NULL;
}

