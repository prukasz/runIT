#include <stdlib.h>
#include "device_servo.h"
#include "sys_device.h"
#include "sys_error.h"

#define OWNER OWNER_DEVICE

#define SERVO_FREQUENCY_HZ 50
#define SERVO_PERIOD_US 20000
#define SERVO_DUTY_TICKS 4096  /* 12-bit duty of the IO contract (PCA9685, LEDC) */
#define SERVO_MAX_OFFSET_US 500

enum { SERVO_STEP_PWM = 0 };

typedef struct servo_ctx_t {
  sys_device_base_t base;  // must be first
  d_servo_cfg_t cfg;
  sys_io_pin_ref_t pwm_pin;  // cfg.pwm_pin, converted once at install
  int16_t offset_us;
  uint16_t pulse_us;  // pulse last sent, 0 = pin not driven
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

/* ---- Operations ---- */

static SE_MUST_USE err_h op_home(void* handle, const uint8_t* args, size_t len, sys_device_reply_t* reply) {
  (void)args, (void)len, (void)reply;
  SYS_DEV_CTX_FROM(servo_ctx_t, c, handle);
  return servo_send(c, (int32_t)c->cfg.home_us + c->offset_us);
}

static SE_MUST_USE err_h op_set_offset(void* handle, const uint8_t* args, size_t len, sys_device_reply_t* reply) {
  (void)len, (void)reply;
  SYS_DEV_CTX_FROM(servo_ctx_t, c, handle);
  SYS_DEVICE_OP_ARGS(servo_set_offset_t, a, args);
  SE_CHECK_IN_RANGE((int)a.offset_us, -SERVO_MAX_OFFSET_US, SERVO_MAX_OFFSET_US);
  c->offset_us = a.offset_us;  // takes effect with the next move, the servo stays where it is
  return NULL;
}

static SE_MUST_USE err_h op_set_angle(void* handle, const uint8_t* args, size_t len, sys_device_reply_t* reply) {
  (void)len, (void)reply;
  SYS_DEV_CTX_FROM(servo_ctx_t, c, handle);
  SYS_DEVICE_OP_ARGS(servo_set_angle_t, a, args);
  SE_CHECK_IN_RANGE((uint32_t)a.angle_deg, 0, 180);
  int32_t span = (int32_t)c->cfg.max_us - c->cfg.min_us;
  return servo_send(c, c->cfg.min_us + span * a.angle_deg / 180 + c->offset_us);
}

static SE_MUST_USE err_h op_get_pulse(void* handle, const uint8_t* args, size_t len, sys_device_reply_t* reply) {
  (void)args, (void)len;
  SYS_DEV_CTX_FROM(servo_ctx_t, c, handle);
  return sys_device_reply_put(reply, &c->pulse_us, sizeof(c->pulse_us));
}

// The whole interface of the device on one screen.
static const sys_device_op_t s_ops[] = {
    SYS_DEVICE_OP(SERVO_OP_HOME, servo_no_args_t, op_home),
    SYS_DEVICE_OP(SERVO_OP_SET_OFFSET, servo_set_offset_t, op_set_offset),
    SYS_DEVICE_OP(SERVO_OP_SET_ANGLE, servo_set_angle_t, op_set_angle),
    SYS_DEVICE_OP_RO(SERVO_OP_GET_PULSE, servo_no_args_t, op_get_pulse),
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
  c->offset_us = 0;
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

  SE_CHECK_IN_RANGE((uint32_t)cfg->max_us, 1, SERVO_PERIOD_US);
  SE_CHECK_IN_RANGE((uint32_t)cfg->min_us, 0, cfg->max_us);
  SE_CHECK_IN_RANGE((uint32_t)cfg->home_us, cfg->min_us, cfg->max_us);

  servo_ctx_t* c = (servo_ctx_t*)calloc(1, sizeof(servo_ctx_t));
  SE_CHECK_IF_ALLOCATED(c);
  c->cfg = *cfg;
  c->base.device_id = cfg->device_id;
  err_h err = NULL;

  c->pwm_pin = c->cfg.pwm_pin;
  SYS_DEV_INSTALL_STEP_BIT(c, SERVO_STEP_PWM, sys_io_set_pwm_frequency(c->pwm_pin, SERVO_FREQUENCY_HZ), "pwm frequency");

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
