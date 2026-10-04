#include <stdlib.h>
#include "device_servo.h"
#include "sys_device.h"
#include "sys_error.h"

#define OWNER OWNER_DEVICE

#define SERVO_FREQUENCY_HZ 50
#define SERVO_PERIOD_US 20000
#define SERVO_DUTY_TICKS 4096  /* 12-bit duty of the IO contract (PCA9685, LEDC) */
#define SERVO_ANGLE_LIMIT_DEG 360
enum { SERVO_STEP_PWM = 0 };

#define SERVO_BYTE_MAX 255
#define SERVO_SPEED_MAX_X10 1000  /* 100 percent in tenths */

typedef struct servo_ctx_t {
  sys_device_base_t base;  // must be first
  d_servo_cfg_t cfg;
  sys_io_pin_ref_t pwm_pin;  // cfg.pwm_pin, converted once at install
  int16_t trim;              // degrees (angle) or us (continuous), added to every move
  uint16_t pulse_us;         // pulse last sent, 0 = pin not driven
} servo_ctx_t;

// The one place that talks to the pin: clamps to the configured range and converts to duty.
static SE_MUST_USE err_h servo_send(servo_ctx_t* c, int32_t pulse_us) {
  if (pulse_us < c->cfg.min_us) pulse_us = c->cfg.min_us;
  if (pulse_us > c->cfg.max_us) pulse_us = c->cfg.max_us;
  SE_TRY(sys_io_set_pwm_duty(c->pwm_pin, (uint32_t)pulse_us * SERVO_DUTY_TICKS / SERVO_PERIOD_US));
  c->pulse_us = (uint16_t)pulse_us;
  return NULL;
}

static SE_MUST_USE err_h servo_stop(servo_ctx_t* c) {
  SE_TRY(sys_io_set_pwm_duty(c->pwm_pin, 0));
  c->pulse_us = 0;
  return NULL;
}

// Moves to a position in tenths of the natural unit (degrees or percent); the caller has range-checked it.
static SE_MUST_USE err_h servo_move(servo_ctx_t* c, int32_t pos_x10) {
  const d_servo_cfg_t* cfg = &c->cfg;
  int32_t span_us = (int32_t)cfg->max_us - cfg->min_us;
  if (cfg->servo_type == SERVO_TYPE_ANGLE) {
    int32_t lo = (int32_t)cfg->angle_min_deg * 10, hi = (int32_t)cfg->angle_max_deg * 10;
    if (cfg->invert) pos_x10 = lo + hi - pos_x10;
    pos_x10 += (int32_t)c->trim * 10;
    return servo_send(c, cfg->min_us + (pos_x10 - lo) * span_us / (hi - lo));
  }
  // Continuous: stop is the middle of the pulse range (moved by the trim), speed runs outwards from beyond the deadband.
  if (cfg->invert) pos_x10 = -pos_x10;
  int32_t stop_us = cfg->min_us + span_us / 2 + c->trim;
  if (pos_x10 == 0) return servo_send(c, stop_us);
  int32_t mag = pos_x10 < 0 ? -pos_x10 : pos_x10;
  int32_t reach = cfg->deadband_us + mag * (span_us / 2 - cfg->deadband_us) / SERVO_SPEED_MAX_X10;
  return servo_send(c, pos_x10 < 0 ? stop_us - reach : stop_us + reach);
}

// Operation value -> position in tenths. Byte mode spreads 0..255 over the whole range.
static SE_MUST_USE err_h servo_decode(const servo_ctx_t* c, int16_t value_x10, int32_t* pos_x10) {
  const d_servo_cfg_t* cfg = &c->cfg;
  bool angle = cfg->servo_type == SERVO_TYPE_ANGLE;
  if (cfg->byte_mode) {
    int32_t byte = ((int32_t)value_x10 + 5) / 10;
    SE_CHECK_IN_RANGE((int)byte, 0, SERVO_BYTE_MAX);
    if (angle) {
      *pos_x10 = (int32_t)cfg->angle_min_deg * 10 + byte * ((int32_t)cfg->angle_max_deg - cfg->angle_min_deg) * 10 / SERVO_BYTE_MAX;
    } else {  // 127 and 128 both stop
      *pos_x10 = byte <= 127 ? -(127 - byte) * SERVO_SPEED_MAX_X10 / 127 : (byte - 128) * SERVO_SPEED_MAX_X10 / 127;
    }
    return NULL;
  }
  if (angle) {
    SE_CHECK_IN_RANGE((int)value_x10, (int)cfg->angle_min_deg * 10, (int)cfg->angle_max_deg * 10);
  } else {
    SE_CHECK_IN_RANGE((int)value_x10, -SERVO_SPEED_MAX_X10, SERVO_SPEED_MAX_X10);
  }
  *pos_x10 = value_x10;
  return NULL;
}

/* ---- Operations ---- */

static SE_MUST_USE err_h op_home(void* handle, const uint8_t* args, size_t len, sys_device_reply_t* reply) {
  (void)args, (void)len, (void)reply;
  SYS_DEV_CTX_FROM(servo_ctx_t, c, handle);
  return servo_move(c, (int32_t)c->cfg.default_pos * 10);
}

static SE_MUST_USE err_h op_set_trim(void* handle, const uint8_t* args, size_t len, sys_device_reply_t* reply) {
  (void)len, (void)reply;
  SYS_DEV_CTX_FROM(servo_ctx_t, c, handle);
  SYS_DEVICE_OP_ARGS(servo_set_trim_t, a, args);
  SE_CHECK_IN_RANGE((int)a.trim, -SERVO_TRIM_MAX, SERVO_TRIM_MAX);
  c->trim = a.trim;  // takes effect with the next move, the servo stays where it is
  return NULL;
}

static SE_MUST_USE err_h op_set(void* handle, const uint8_t* args, size_t len, sys_device_reply_t* reply) {
  (void)len, (void)reply;
  SYS_DEV_CTX_FROM(servo_ctx_t, c, handle);
  SYS_DEVICE_OP_ARGS(servo_set_t, a, args);
  int32_t pos_x10;
  SE_TRY(servo_decode(c, a.value_x10, &pos_x10));
  return servo_move(c, pos_x10);
}

static SE_MUST_USE err_h op_get_pulse(void* handle, const uint8_t* args, size_t len, sys_device_reply_t* reply) {
  (void)args, (void)len;
  SYS_DEV_CTX_FROM(servo_ctx_t, c, handle);
  return sys_device_reply_put(reply, &c->pulse_us, sizeof(c->pulse_us));
}

static SE_MUST_USE err_h op_release(void* handle, const uint8_t* args, size_t len, sys_device_reply_t* reply) {
  (void)args, (void)len, (void)reply;
  SYS_DEV_CTX_FROM(servo_ctx_t, c, handle);
  return servo_stop(c);
}

// The whole interface of the device on one screen.
static const sys_device_op_t s_ops[] = {
    SYS_DEVICE_OP(SERVO_OP_HOME, servo_no_args_t, op_home),
    SYS_DEVICE_OP(SERVO_OP_SET_TRIM, servo_set_trim_t, op_set_trim),
    SYS_DEVICE_OP(SERVO_OP_SET, servo_set_t, op_set),
    SYS_DEVICE_OP_RO(SERVO_OP_GET_PULSE, servo_no_args_t, op_get_pulse),
    SYS_DEVICE_OP(SERVO_OP_RELEASE, servo_no_args_t, op_release),
};

/* ---- Lifecycle ---- */

// Doubles as the install rollback path: no step may early-return.
static SE_MUST_USE err_h device_uninstall(void* handle) {
  SYS_DEV_CTX_FROM(servo_ctx_t, c, handle);
  err_h err = NULL;
  IF_SYS_DEV_STEP_DONE(c, SERVO_STEP_PWM) {
    SYS_DEV_TEARDOWN_STEP(err, servo_stop(c));
  }
  free(c);
  return err;
}

static SE_MUST_USE err_h device_reset(void* handle) {
  SYS_DEV_CTX_FROM(servo_ctx_t, c, handle);
  c->trim = 0;
  return servo_stop(c);
}

// Fault safe state: no pulse, the servo goes limp.
static SE_MUST_USE err_h device_suspend(void* handle) {
  SYS_DEV_CTX_FROM(servo_ctx_t, c, handle);
  uint16_t last = c->pulse_us;
  SE_TRY(servo_stop(c));
  c->pulse_us = last;  // remembered for resume
  return NULL;
}

static SE_MUST_USE err_h device_resume(void* handle) {
  SYS_DEV_CTX_FROM(servo_ctx_t, c, handle);
  return c->pulse_us ? servo_send(c, c->pulse_us) : NULL;
}

static SE_MUST_USE err_h device_install(const void* cfg_blob, void** out_device_handle) {
  const d_servo_cfg_t* cfg = (const d_servo_cfg_t*)cfg_blob;

  SE_CHECK_IN_RANGE((uint32_t)cfg->servo_type, 0, SERVO_TYPE_CONTINUOUS);
  SE_CHECK_IN_RANGE((uint32_t)cfg->byte_mode, 0, 1);
  SE_CHECK_IN_RANGE((uint32_t)cfg->invert, 0, 1);
  SE_CHECK_IN_RANGE((uint32_t)cfg->max_us, 1, SERVO_PERIOD_US);
  SE_CHECK_IN_RANGE((uint32_t)cfg->min_us, 0, cfg->max_us - 1);  // a servo needs a pulse range
  if (cfg->servo_type == SERVO_TYPE_ANGLE) {
    SE_CHECK_IN_RANGE((int)cfg->angle_max_deg, -SERVO_ANGLE_LIMIT_DEG, SERVO_ANGLE_LIMIT_DEG);
    SE_CHECK_IN_RANGE((int)cfg->angle_min_deg, -SERVO_ANGLE_LIMIT_DEG, (int)cfg->angle_max_deg - 1);
    SE_CHECK_IN_RANGE((int)cfg->default_pos, (int)cfg->angle_min_deg, (int)cfg->angle_max_deg);
  } else {
    SE_CHECK_IN_RANGE((int)cfg->default_pos, -100, 100);
    uint32_t half_us = (uint32_t)(cfg->max_us - cfg->min_us) / 2;
    SE_CHECK_IN_RANGE((uint32_t)cfg->deadband_us, 0, half_us ? half_us - 1 : 0);
  }

  servo_ctx_t* c = (servo_ctx_t*)calloc(1, sizeof(servo_ctx_t));
  SE_CHECK_IF_ALLOCATED(c);
  c->cfg = *cfg;
  c->base.device_id = cfg->device_id;
  err_h err = NULL;

  c->pwm_pin = c->cfg.pwm_pin;
  SYS_DEV_INSTALL_STEP_BIT(c, SERVO_STEP_PWM, sys_io_set_pwm_frequency(c->pwm_pin, SERVO_FREQUENCY_HZ), "pwm frequency");
  SYS_DEV_INSTALL_STEP(servo_move(c, (int32_t)c->cfg.default_pos * 10), "default position");

  *out_device_handle = c;
  return NULL;

fail:
  SYS_DEV_INSTALL_FAIL(err, out_device_handle, device_uninstall, c);
  return NULL;
}

static const uint8_t s_pin_refs[] = {offsetof(d_servo_cfg_t, pwm_pin)};

// No contract: only the lifecycle and the operations above.
const sys_device_class_t g_servo_class = {
    .type_id = SERVO_TYPE_ID,
    .cfg_size = sizeof(d_servo_cfg_t),
    SYS_DEVICE_PINS(s_pin_refs),
    .name = "SERVO",
    .ops = {.install = device_install,
        .uninstall = device_uninstall,
        .reset = device_reset,
        .suspend = device_suspend,
        .resume = device_resume},
    SYS_DEVICE_OPS(s_ops),
};
