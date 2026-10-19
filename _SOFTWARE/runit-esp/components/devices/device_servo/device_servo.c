#include "device_servo.h"

#define PWM_FREQUENCY_HZ 50u
#define PERIOD_US        20000u
#define ANGLE_LIMIT_DEG  360
#define BYTE_MAX         255
#define SPEED_MAX_X10    1000  // 100 percent in tenths
#define SPEED_MAX_PCT    100

typedef struct {
  sys_device_base_t base;  // first: id, suspended (no pulse)
  bool pin_used;           // the PWM pin was made ours on its device (released on uninstall)
  bool pwm_ready;          // the 50 Hz frequency is set on the pin
  servo_cfg_t cfg;
  int16_t trim;       // degrees (angle) or us (continuous), added to every move
  uint16_t pulse_us;  // pulse last sent, 0 = pin not driven
} servo_t;

// The one place that talks to the pin: clamps to the configured range and converts to duty.
static err_h servo_send(servo_t* c, int32_t pulse_us) {
  if (pulse_us < c->cfg.min_us) {
    pulse_us = c->cfg.min_us;
  }
  if (pulse_us > c->cfg.max_us) {
    pulse_us = c->cfg.max_us;
  }
  ERR_TRY(sys_io_set_pwm_duty(c->cfg.pwm_pin, (uint32_t)pulse_us * IO_PWM_DUTY_MAX / PERIOD_US));
  c->pulse_us = (uint16_t)pulse_us;
  return NULL;
}

static err_h servo_stop(servo_t* c) {
  ERR_TRY(sys_io_set_pwm_duty(c->cfg.pwm_pin, 0));
  c->pulse_us = 0;
  return NULL;
}

// Moves to a position in tenths of the natural unit (degrees or percent); the caller has range-checked it.
static err_h servo_move(servo_t* c, int32_t pos_x10) {
  const servo_cfg_t* cfg = &c->cfg;
  const int32_t span_us = (int32_t)cfg->max_us - cfg->min_us;
  if (cfg->servo_type == SERVO_TYPE_ANGLE) {
    const int32_t lo = (int32_t)cfg->angle_min_deg * 10;
    const int32_t hi = (int32_t)cfg->angle_max_deg * 10;
    if (cfg->invert) {
      pos_x10 = lo + hi - pos_x10;
    }
    pos_x10 += (int32_t)c->trim * 10;
    return servo_send(c, cfg->min_us + (pos_x10 - lo) * span_us / (hi - lo));
  }
  // Continuous: stop is the middle of the pulse range (moved by the trim), speed runs outwards from beyond the deadband.
  if (cfg->invert) {
    pos_x10 = -pos_x10;
  }
  const int32_t stop_us = cfg->min_us + span_us / 2 + c->trim;
  if (pos_x10 == 0) {
    return servo_send(c, stop_us);
  }
  const int32_t mag = pos_x10 < 0 ? -pos_x10 : pos_x10;
  const int32_t reach = cfg->deadband_us + mag * (span_us / 2 - cfg->deadband_us) / SPEED_MAX_X10;
  return servo_send(c, pos_x10 < 0 ? stop_us - reach : stop_us + reach);
}

// Op value -> position in tenths. Byte mode spreads 0..255 over the whole range.
static err_h servo_decode(const servo_t* c, int16_t value_x10, int32_t* pos_x10) {
  const servo_cfg_t* cfg = &c->cfg;
  const bool angle = cfg->servo_type == SERVO_TYPE_ANGLE;
  if (cfg->byte_mode) {
    const int32_t byte = ((int32_t)value_x10 + 5) / 10;
    SYS_DEV_CHECK_IN_RANGE(c->base.id, byte, 0, BYTE_MAX);
    if (angle) {
      *pos_x10 = (int32_t)cfg->angle_min_deg * 10 +
                 byte * ((int32_t)cfg->angle_max_deg - cfg->angle_min_deg) * 10 / BYTE_MAX;
    } else {  // 127 and 128 both stop
      *pos_x10 = byte <= 127 ? -(127 - byte) * SPEED_MAX_X10 / 127 : (byte - 128) * SPEED_MAX_X10 / 127;
    }
    return NULL;
  }
  const int32_t lo = angle ? (int32_t)cfg->angle_min_deg * 10 : -SPEED_MAX_X10;
  const int32_t hi = angle ? (int32_t)cfg->angle_max_deg * 10 : SPEED_MAX_X10;
  SYS_DEV_CHECK_IN_RANGE(c->base.id, value_x10, lo, hi);
  *pos_x10 = value_x10;
  return NULL;
}

/* ---- Own ops ---- */

SYS_DEVICE_HANDLER(home_h, sys_device_none_t) {
  (void)args;
  (void)out;
  (void)out_cap;
  (void)out_len;
  servo_t* c = ctx;
  ERR_TRY(sys_device_check_active(&c->base));
  return servo_move(c, (int32_t)c->cfg.default_pos * 10);
}

// Takes effect with the next move, the servo stays where it is. Not an output write: allowed while suspended.
SYS_DEVICE_HANDLER(set_trim_h, servo_trim_t) {
  (void)out;
  (void)out_cap;
  (void)out_len;
  servo_t* c = ctx;
  SYS_DEV_CHECK_IN_RANGE(c->base.id, args->trim, -SERVO_TRIM_MAX, SERVO_TRIM_MAX);
  c->trim = args->trim;
  return NULL;
}

SYS_DEVICE_HANDLER(set_h, servo_set_t) {
  (void)out;
  (void)out_cap;
  (void)out_len;
  servo_t* c = ctx;
  ERR_TRY(sys_device_check_active(&c->base));
  int32_t pos_x10;
  ERR_TRY(servo_decode(c, args->value_x10, &pos_x10));
  return servo_move(c, pos_x10);
}

// A read: allowed while suspended (0 then).
SYS_DEVICE_HANDLER(get_pulse_h, sys_device_none_t) {
  (void)args;
  const servo_t* c = ctx;
  const uint16_t pulse = c->base.suspended ? 0 : c->pulse_us;
  return cmd_reply(out, out_cap, out_len, &pulse, sizeof(pulse));
}

SYS_DEVICE_HANDLER(release_h, sys_device_none_t) {
  (void)args;
  (void)out;
  (void)out_cap;
  (void)out_len;
  servo_t* c = ctx;
  ERR_TRY(sys_device_check_active(&c->base));
  return servo_stop(c);
}

/* ---- Lifecycle ---- */

// Releases the hardware; REMOVE and the create rollback: no pulse (the servo goes limp).
static err_h uninstall(void* ctx) {
  servo_t* c = ctx;
  err_h err = NULL;
  if (c->pwm_ready) {
    ERR_KEEP_FIRST(err, servo_stop(c));
    c->pwm_ready = false;
  }
  if (c->pin_used) {
    ERR_KEEP_FIRST(err, sys_io_release(c->cfg.pwm_pin));
    c->pin_used = false;
  }
  return err;
}

DEVICE_WRITE(RESET) {
  servo_t* c = ctx;
  ERR_TRY(sys_device_check_active(&c->base));
  c->trim = 0;
  return servo_stop(c);
}

// Fault safe state: no pulse, the servo goes limp. The last pulse is remembered for RESUME.
static err_h suspend_hook(void* ctx) {
  servo_t* c = ctx;
  const uint16_t last = c->pulse_us;
  ERR_TRY(servo_stop(c));
  c->pulse_us = last;
  return NULL;
}

// A servo that was driven gets its last pulse back. A failing resume leaves it suspended.
static err_h resume_hook(void* ctx) {
  servo_t* c = ctx;
  if (c->pulse_us != 0) {
    ERR_TRY(servo_send(c, c->pulse_us));
  }
  return NULL;
}

/* ---- Create ---- */

static err_h check_cfg(const servo_t* c, const servo_cfg_t* cfg) {
  const uint16_t id = c->base.id;
  SYS_DEV_CHECK_IN_RANGE(id, cfg->servo_type, 0, SERVO_TYPE_CONTINUOUS);
  ERR_CHECK(cfg->byte_mode <= 1 && cfg->invert <= 1, ERR_CMD_BAD_ARG);
  SYS_DEV_CHECK_IN_RANGE(id, cfg->max_us, 1, PERIOD_US);
  SYS_DEV_CHECK_IN_RANGE(id, cfg->min_us, 0, cfg->max_us - 1);
  if (cfg->servo_type == SERVO_TYPE_ANGLE) {
    SYS_DEV_CHECK_IN_RANGE(id, cfg->angle_max_deg, -ANGLE_LIMIT_DEG, ANGLE_LIMIT_DEG);
    SYS_DEV_CHECK_IN_RANGE(id, cfg->angle_min_deg, -ANGLE_LIMIT_DEG, cfg->angle_max_deg - 1);
    SYS_DEV_CHECK_IN_RANGE(id, cfg->default_pos, cfg->angle_min_deg, cfg->angle_max_deg);
  } else {
    SYS_DEV_CHECK_IN_RANGE(id, cfg->default_pos, -SPEED_MAX_PCT, SPEED_MAX_PCT);
    const uint32_t half_us = (uint32_t)(cfg->max_us - cfg->min_us) / 2;
    const uint32_t deadband_max = half_us ? half_us - 1 : 0;
    SYS_DEV_CHECK_IN_RANGE(id, cfg->deadband_us, 0, deadband_max);
  }
  return NULL;
}

static err_h create(void* ctx, const void* cfg_in) {
  servo_t* c = ctx;
  const servo_cfg_t* cfg = cfg_in;
  ERR_TRY(check_cfg(c, cfg));
  c->cfg = *cfg;
  ERR_CHECK(sys_io_pin_valid(c->cfg.pwm_pin), ERR_DEV_BAD_PIN, .id = c->base.id, .pin = c->cfg.pwm_pin.pin);
  ERR_TRY(sys_device_use(&c->base, c->cfg.pwm_pin.device));

  ERR_TRY(sys_io_prepare(c->cfg.pwm_pin, SYS_IO_MODE_PWM));  // a PCA9685 channel has no modes: fine
  c->pin_used = true;
  ERR_TRY(sys_io_set_pwm_frequency(c->cfg.pwm_pin, PWM_FREQUENCY_HZ));
  c->pwm_ready = true;
  return servo_move(c, (int32_t)c->cfg.default_pos * 10);
}

/* ---- Type ---- */

static const sys_device_op_t k_ops[] = {
    DEVICE_ENTRY_W(RESET),
    /* Device own ops */
    {SERVO_OP_HOME, NULL, home_h},
    {SERVO_OP_TRIM, NULL, set_trim_h},
    {SERVO_OP_TARGET, NULL, set_h},
    {SERVO_OP_PULSE, get_pulse_h, NULL},
    {SERVO_OP_RELEASE, NULL, release_h},
};

SYS_DEVICE_TYPE_DEFINE(.type = SYS_DEVICE_TYPE_SERVO, .cfg_size = sizeof(servo_cfg_t), .ctx_size = sizeof(servo_t),
                       .create = create, .uninstall = uninstall, .suspend = suspend_hook, .resume = resume_hook,
                       SYS_DEVICE_OPS(k_ops))
