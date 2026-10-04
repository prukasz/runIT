#pragma once
#include "sys_device.h"
#include "sys_error.h"
#include "sys_io.h"
#include "servo_types.h"

#define SERVO_TYPE_ID 0x48  /* the byte after 0x00 in a create frame: [0x00][0x48][d_servo_cfg_t] */

//#device device_servo
//  @title       Servo
//  @description Hobby servo on a PWM pin of another device: an angle servo moves to an angle, a continuous (360 degree) servo runs at a speed from -100 to 100. Pulse limits, trim, direction and the position it takes at start-up are set here.
//  @protocol    pwm
//  @tags        servo pwm motion
//  @type-id     SERVO_TYPE_ID

/**
 * @brief Hobby servo on one PWM pin of another device (PCA9685 channel, ESP LEDC pin).
 *
 * The servo has no contract of its own: its functions are operations of the device,
 * called through the packet router as [op][device_id][args] (SYS_DEVICE.MD, Packet Router).
 * It drives the pin only through the IO contract, so any PWM-capable device works (50 Hz).
 * The cfg is also the wire struct of the create frame (packed, device_id first).
 * At install the servo moves to `default_pos`.
 *
 * "Position" means one number whose unit follows the type:
 *   ANGLE      degrees, angle_min_deg..angle_max_deg
 *   CONTINUOUS speed in percent, -100..100 (0 = stop)
 * With `byte_mode` the operations take the position as one byte instead (0..255 spans the whole range;
 * for a continuous servo 127 and 128 both stop, 0 is -100 and 255 is +100).
 * `default_pos` is always in the natural unit, never a byte.
 */
typedef struct __packed {
  uint8_t device_id;        //@max CONFIG_SYS_DEVICE_MAX_ID
  sys_io_pin_ref_t pwm_pin; //@alias PWM Pin @note Any pin that can output PWM: a PCA9685 channel or an ESP GPIO. On a lower-ID device.
  uint8_t servo_type;       //@alias Servo Type @enum-ref servo_type_e @one-of [$SERVO_TYPE_ANGLE, $SERVO_TYPE_CONTINUOUS] @default $SERVO_TYPE_ANGLE
  uint8_t byte_mode;        //@alias Byte Steering @one-of [0, 1] @default 0 @note Steer with one byte (0..255 is the whole range) instead of degrees or percent.
  uint8_t invert;           //@alias Reverse Direction @one-of [0, 1] @default 0 @note Mirrors the movement, for a servo mounted the other way round.
  int16_t angle_min_deg;    //@alias Angle at Min Pulse @unit deg @min -360 @max 360 @default 0 @note Angle servo only.
  int16_t angle_max_deg;    //@alias Angle at Max Pulse @unit deg @min -360 @max 360 @default 180 @note Angle servo only. Must be above the minimum angle.
  int16_t default_pos;      //@alias Default Position @min -360 @max 360 @default 90 @note Where the servo goes at start-up and on Home: degrees (angle servo) or speed in percent (continuous servo, 0 stops).
  uint16_t min_us;          //@alias Min Pulse @unit us @min 400 @max 2600 @default 500 @note The lowest pulse ever sent. Servos differ: 500 and 1000 are common.
  uint16_t max_us;          //@alias Max Pulse @unit us @min 400 @max 2600 @default 2500 @note The highest pulse ever sent. Servos differ: 2000 and 2500 are common.
  uint16_t deadband_us;     //@alias Deadband @unit us @min 0 @max 200 @default 0 @note Continuous servo: the pulse width around stop in which it does not move. Any speed other than 0 starts beyond it.
} d_servo_cfg_t;

/* Operations (op byte of the router frame). Args are packed little endian. */
#define SERVO_OP_HOME 0x80      /* go to default_pos (+ trim) */
#define SERVO_OP_SET_TRIM 0x81  /* trim added to every target, applied by the next move: degrees (angle) or us (continuous) */
#define SERVO_OP_SET 0x82       /* move to a position, see servo_set_t */
#define SERVO_OP_GET_PULSE 0x83 /* read-only: the pulse last sent, uint16 us (0 = not driven) */
#define SERVO_OP_RELEASE 0x84   /* stop the pulse: the servo goes limp; the next SET / HOME drives it again */

#define SERVO_TRIM_MAX 500  /* +-: degrees for an angle servo (limited by the pulse range anyway), us for a continuous one */

typedef struct __packed {
  int16_t trim;
} servo_set_trim_t;

/* The position in tenths: 905 = 90.5 degrees, -250 = -25 percent. In byte mode the value is the byte x10 (0..2550, rounded). */
typedef struct __packed {
  int16_t value_x10;
} servo_set_t;

/* Ops without arguments */
typedef struct __packed {
} servo_no_args_t;

/** The servo device class: register with sys_device_register_class(), create with SYS_DEVICE_CREATE(&g_servo_class, &cfg). */
extern const sys_device_class_t g_servo_class;
