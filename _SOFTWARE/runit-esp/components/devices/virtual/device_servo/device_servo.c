/*
 * Servo: a hobby servo on a PWM pin of any device with the IO contract (an ESP GPIO, a PCA9685 channel), at 50 Hz.
 * The whole device is this file: nothing includes it, the rest of the firmware reaches it by its ID and its own ops.
 * An angle servo moves to an angle between its limits; a continuous one runs at a speed, -100..100 %, stopped in the
 * middle of the pulse range. Positions are tenths of the unit on the wire (905 = 90.5 degrees); in byte mode 0..255
 * (x10) spreads over the whole range.
 */
#include "sys_device_driver.h"
#include "sys_io_driver.h"

SYS_DEVICE_TYPE_DECLARE(SERVO, 0x0048)

/** @brief TRIM's value: added to every target, degrees for an angle servo, us for a continuous one. */
typedef struct __attribute__((packed)) {
  int16_t trim;  /**< Trim added to target (degrees or microseconds) */
} servo_trim_t;

/** @brief TARGET's value: a position in tenths (905 = 90.5 degrees, -250 = -25 percent; byte mode: byte x 10). */
typedef struct __attribute__((packed)) {
  int16_t value_x10;  /**< Target position in tenths of unit */
} servo_set_t;

/*******************************************************************************
 * OWN_SERVO : own operations
 *   OP_HOME    - to default_pos (+ trim)
 *   OP_TRIM    - the trim, applied by the next move
 *   OP_TARGET  - move to a position
 *   OP_PULSE   - the pulse last sent in us (0 = not driven)
 *   OP_RELEASE - no pulse: the servo goes limp until the next move
 ******************************************************************************/
#define OWN_SERVO_OPERATIONS(X, P)                         \
  X(P, OP_HOME,    0x00, cmd_none_t, cmd_none_t,   0)      \
  X(P, OP_TRIM,    0x01, cmd_none_t, servo_trim_t, 0)      \
  X(P, OP_TARGET,  0x02, cmd_none_t, servo_set_t,  0)      \
  X(P, OP_PULSE,   0x03, cmd_none_t, uint16_t,     0)      \
  X(P, OP_RELEASE, 0x04, cmd_none_t, cmd_none_t,   0)

SYS_DEVICE_OWN_DECLARE(OWN_SERVO)

//#ref-enum @alias Servo Type
typedef enum servo_type_e {
  SERVO_TYPE_ANGLE = 0,       //@alias Angle servo @description Angle servo: moves to angle between limits
  SERVO_TYPE_CONTINUOUS = 1,  //@alias Continuous (360) servo @description Continuous rotation (360) servo: speed -100..100
} servo_type_e;

/** @brief CREATE's struct (wire format). */
typedef struct __attribute__((packed)) {
  sys_io_pin_t pwm_pin;   /**< PWM output pin */
  uint8_t servo_type;     /**< Servo type (servo_type_e) */
  uint8_t byte_mode;      /**< 1 = 0..255 byte position mode, 0 = natural units */
  uint8_t invert;         /**< 1 = reversed movement direction */
  int16_t angle_min_deg;  /**< Angle at minimum pulse (degrees) */
  int16_t angle_max_deg;  /**< Angle at maximum pulse (degrees) */
  int16_t default_pos;    /**< Start-up position in natural units */
  uint16_t min_us;        /**< Minimum pulse width (us) */
  uint16_t max_us;        /**< Maximum pulse width (us) */
  uint16_t deadband_us;   /**< Continuous servo deadband around 0 (us) */
} servo_cfg_t;

#define SERVO_TRIM_MAX   500  // +-: the pulse range limits it anyway
#define PWM_FREQUENCY_HZ 50u
#define PERIOD_US        20000
#define ANGLE_LIMIT_DEG  360
#define BYTE_MAX         255
#define SPEED_MAX_X10    1000  // 100 percent in tenths
#define SPEED_MAX_PCT    100

typedef struct {
  uint8_t id;
  sys_io_claim_t pwm;  // the PWM pin, its duty written with the IO ops
  servo_cfg_t cfg;
  int16_t trim;
  uint16_t pulse_us;   // pulse last sent, 0 = not driven; kept over a suspend for resume
} servo_t;

#define SYS_DEVICE_CTX servo_t

static err_h servo_duty(servo_t* c, int32_t pulse_us) {
  return SYS_IO_WRITE(c->cfg.pwm_pin, OP_PWM_DUTY, .duty = (uint32_t)pulse_us * IO_PWM_DUTY_MAX / PERIOD_US);
}

// The one place a pulse is sent: clamped to the configured range.
static err_h servo_send(servo_t* c, int32_t pulse_us) {
  pulse_us = pulse_us < c->cfg.min_us ? c->cfg.min_us : pulse_us > c->cfg.max_us ? c->cfg.max_us : pulse_us;
  ERR_TRY(servo_duty(c, pulse_us));
  c->pulse_us = (uint16_t)pulse_us;
  return NULL;
}

static err_h servo_stop(servo_t* c) {
  ERR_TRY(servo_duty(c, 0));
  c->pulse_us = 0;
  return NULL;
}

// Moves to a position in tenths of the unit; the caller range-checked it.
static err_h servo_move(servo_t* c, int32_t pos_x10) {
  const servo_cfg_t* cfg = &c->cfg;
  const int32_t span_us = (int32_t)cfg->max_us - cfg->min_us;
  if (cfg->servo_type == SERVO_TYPE_ANGLE) {
    const int32_t lo = (int32_t)cfg->angle_min_deg * 10;
    const int32_t hi = (int32_t)cfg->angle_max_deg * 10;
    pos_x10 = (cfg->invert ? lo + hi - pos_x10 : pos_x10) + (int32_t)c->trim * 10;
    return servo_send(c, cfg->min_us + (pos_x10 - lo) * span_us / (hi - lo));
  }
  // Continuous: stop in the middle (moved by the trim), the speed outwards from the deadband.
  pos_x10 = cfg->invert ? -pos_x10 : pos_x10;
  const int32_t stop_us = cfg->min_us + span_us / 2 + c->trim;
  const int32_t mag = pos_x10 < 0 ? -pos_x10 : pos_x10;
  const int32_t reach = mag == 0 ? 0 : cfg->deadband_us + mag * (span_us / 2 - cfg->deadband_us) / SPEED_MAX_X10;
  return servo_send(c, pos_x10 < 0 ? stop_us - reach : stop_us + reach);
}

// TARGET's value as a position in tenths.
static err_h servo_decode(const servo_t* c, int16_t value_x10, int32_t* pos_x10) {
  const servo_cfg_t* cfg = &c->cfg;
  const bool angle = cfg->servo_type == SERVO_TYPE_ANGLE;
  if (cfg->byte_mode) {
    const int32_t byte = ((int32_t)value_x10 + 5) / 10;
    ERR_CHECK_RANGE(byte, 0, BYTE_MAX);
    if (angle) {
      *pos_x10 = cfg->angle_min_deg * 10 + byte * (cfg->angle_max_deg - cfg->angle_min_deg) * 10 / BYTE_MAX;
    } else {  // 127 and 128 both stop
      *pos_x10 = byte <= 127 ? -(127 - byte) * SPEED_MAX_X10 / 127 : (byte - 128) * SPEED_MAX_X10 / 127;
    }
    return NULL;
  }
  if (angle) {
    ERR_CHECK_RANGE(value_x10, cfg->angle_min_deg * 10, cfg->angle_max_deg * 10);
  } else {
    ERR_CHECK_RANGE(value_x10, -SPEED_MAX_X10, SPEED_MAX_X10);
  }
  *pos_x10 = value_x10;
  return NULL;
}

/* ---- Own ops ---- */

OP_DECLARE_WRITE(OWN_SERVO, OP_HOME) {
  return servo_move(c, (int32_t)c->cfg.default_pos * 10);
}

OP_DECLARE_WRITE(OWN_SERVO, OP_TRIM) {
  ERR_CHECK_RANGE(args->trim, -SERVO_TRIM_MAX, SERVO_TRIM_MAX);
  c->trim = args->trim;
  return NULL;
}

OP_DECLARE_WRITE(OWN_SERVO, OP_TARGET) {
  int32_t pos_x10;
  ERR_TRY(servo_decode(c, args->value_x10, &pos_x10));
  return servo_move(c, pos_x10);
}

// 0 while suspended: no pulse goes out.
OP_DECLARE_READ(OWN_SERVO, OP_PULSE) {
  return CMD_REPLY(OWN_SERVO, OP_PULSE, sys_device_self_suspended() ? 0 : c->pulse_us);
}

OP_DECLARE_WRITE(OWN_SERVO, OP_RELEASE) {
  return servo_stop(c);
}

/* ---- Lifecycle ---- */

OP_DECLARE_WRITE(CONTRACT_SYS_DEVICE, OP_RESET) {
  c->trim = 0;
  return servo_stop(c);
}

// No pulse, the servo goes limp; the last pulse is kept for resume.
static err_h suspend(servo_t* c) {
  return servo_duty(c, 0);
}

static err_h resume(servo_t* c) {
  return c->pulse_us != 0 ? servo_send(c, c->pulse_us) : NULL;
}

static err_h check_cfg(const servo_cfg_t* cfg) {
  ERR_CHECK_RANGE(cfg->servo_type, SERVO_TYPE_ANGLE, SERVO_TYPE_CONTINUOUS);
  ERR_CHECK(cfg->byte_mode <= 1 && cfg->invert <= 1, ERR_BAD_ARG);
  ERR_CHECK_RANGE(cfg->max_us, 1, PERIOD_US);
  ERR_CHECK_RANGE(cfg->min_us, 0, cfg->max_us - 1);
  if (cfg->servo_type == SERVO_TYPE_ANGLE) {
    ERR_CHECK_RANGE(cfg->angle_max_deg, -ANGLE_LIMIT_DEG, ANGLE_LIMIT_DEG);
    ERR_CHECK_RANGE(cfg->angle_min_deg, -ANGLE_LIMIT_DEG, cfg->angle_max_deg - 1);
    ERR_CHECK_RANGE(cfg->default_pos, cfg->angle_min_deg, cfg->angle_max_deg);
  } else {
    const int32_t half_us = (cfg->max_us - cfg->min_us) / 2;
    ERR_CHECK_RANGE(cfg->default_pos, -SPEED_MAX_PCT, SPEED_MAX_PCT);
    ERR_CHECK_RANGE(cfg->deadband_us, 0, half_us > 0 ? half_us - 1 : 0);
  }
  return NULL;
}

static err_h create(uint8_t id, servo_t* c, const servo_cfg_t* cfg) {
  ERR_TRY(check_cfg(cfg));
  ERR_CHECK(sys_io_pin_valid(cfg->pwm_pin), ERR_DEV_BAD_PIN, .id = id, .pin = cfg->pwm_pin.pin);
  c->id = id;
  c->cfg = *cfg;
  ERR_TRY(sys_io_claim(id, &c->pwm, cfg->pwm_pin, SYS_IO_MODE_PWM, 0, 0));  // released: no pulse
  ERR_TRY(SYS_IO_WRITE(cfg->pwm_pin, OP_PWM_FREQ, .frequency_Hz = PWM_FREQUENCY_HZ));
  return servo_move(c, (int32_t)cfg->default_pos * 10);
}

static const sys_device_op_t k_ops[] = {
    OP_ENTRY_W(CONTRACT_SYS_DEVICE, OP_RESET),
    OP_ENTRY_W(OWN_SERVO, OP_HOME),   OP_ENTRY_W(OWN_SERVO, OP_TRIM),     OP_ENTRY_W(OWN_SERVO, OP_TARGET),
    OP_ENTRY_R(OWN_SERVO, OP_PULSE),  OP_ENTRY_W(OWN_SERVO, OP_RELEASE),
};

SYS_DEVICE_TYPE_DEFINE(SERVO, servo_cfg_t, create, suspend, resume, k_ops)
