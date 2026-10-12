#pragma once

#include "sys_device.h"
#include "sys_io.h"

//#device
//@alias Servo
//@description Hobby servo on a PWM pin of another device: an angle servo moves to an angle, a continuous (360 degree) servo runs at a speed from -100 to 100. Pulse limits, trim, direction and the position it takes at start-up are set here.
//@protocol pwm
//@tags servo pwm motion
//@value POSITION @when byte_mode=1 @title Byte @range 0..255
//@value POSITION @when servo_type=$SERVO_TYPE_CONTINUOUS @title Speed @unit % @range -100..100
//@value POSITION @when servo_type=$SERVO_TYPE_ANGLE @title Angle @unit deg @range angle_min_deg..angle_max_deg

#ifdef __cplusplus
extern "C" {
#endif

//#ref-enum @alias Servo Type
typedef enum servo_type_e {
  SERVO_TYPE_ANGLE = 0,  //@alias Angle servo @description Moves to an angle between the angle limits and holds it
  SERVO_TYPE_CONTINUOUS = 1,  //@alias Continuous (360) servo @description Turns at a speed from -100 to 100 (0 stops) instead of going to an angle
} servo_type_e;

/**
 * @brief Hobby servo on one PWM pin of another device (a PCA9685 channel, an ESP GPIO). It drives the pin only through
 * the IO contract (50 Hz, SET_PWM_FREQ + SET_PWM_DUTY), so any PWM-capable device works. The PWM frequency of a
 * PCA9685 is one for all its channels: a servo on it sets 50 Hz for the others too.
 *
 * "Position" is one number whose unit follows the type: ANGLE degrees, angle_min_deg..angle_max_deg; CONTINUOUS a speed
 * in percent, -100..100 (0 = stop). With `byte_mode` the SET op takes the position as one byte instead (0..255 spans
 * the whole range; for a continuous servo 127 and 128 both stop, 0 is -100 and 255 is +100). `default_pos` is always in
 * the natural unit, never a byte. At create the servo moves to `default_pos`.
 *
 * CREATE = [0x05][id u16][op 0x0000][type u16 = SYS_DEVICE_TYPE_SERVO][servo_cfg_t] (packed, also the wire form).
 */
typedef struct __attribute__((packed)) {
  sys_io_pin_t pwm_pin;  //@alias PWM pin @description Any pin that can output PWM; the servo is driven through the IO contract @required @default-mode $SYS_IO_MODE_PWM
  uint8_t servo_type;  //@alias Servo type @description An angle servo moves to a position, a continuous one turns at a speed @enum-ref servo_type_e @default $SERVO_TYPE_ANGLE
  uint8_t byte_mode;  //@alias Byte steering @description Steer with one byte: 0 to 255 is the whole range @choices 0=Off,1=On @default 0
  uint8_t invert;  //@alias Reverse direction @description Mirrors the movement, for a servo mounted the other way round @choices 0=Off,1=On @default 0
  int16_t angle_min_deg;  //@alias Angle at min pulse @description Angle servo only @unit deg @min -360 @max 360 @default 0
  int16_t angle_max_deg;  //@alias Angle at max pulse @description Angle servo only; above the minimum @unit deg @min -360 @max 360 @default 180
  int16_t default_pos;  //@alias Default position @description Where the servo goes at create and on Home: degrees (angle) or percent (continuous) @min -360 @max 360 @default 90
  uint16_t min_us;  //@alias Min pulse @description The lowest pulse ever sent (500 and 1000 are common) @unit us @min 400 @max 2600 @default 500
  uint16_t max_us;  //@alias Max pulse @description The highest pulse ever sent (2000 and 2500 are common) @unit us @min 400 @max 2600 @default 2500
  uint16_t deadband_us;  //@alias Deadband @description Continuous servo: the pulse width around stop in which it does not move @unit us @min 0 @max 200 @default 0
} servo_cfg_t;

/* Own ops */
#define SERVO_OP_HOME SYS_DEVICE_OWN_OP(0x00)      // go to default_pos (+ trim)
#define SERVO_OP_SET_TRIM SYS_DEVICE_OWN_OP(0x01)  // servo_trim_t: added to every target, applied by the next move
#define SERVO_OP_SET SYS_DEVICE_OWN_OP(0x02)       // servo_set_t: move to a position
#define SERVO_OP_GET_PULSE SYS_DEVICE_OWN_OP(0x03) // -> u16 us, the pulse last sent (0 = not driven)
#define SERVO_OP_RELEASE SYS_DEVICE_OWN_OP(0x04)   // stop the pulse: the servo goes limp; the next SET / HOME drives it again

#define SERVO_TRIM_MAX 500  // +-: degrees for an angle servo (the pulse range limits it anyway), us for a continuous one

typedef struct __attribute__((packed)) {
  int16_t trim;  //@alias Trim @description Added to every target: degrees for an angle servo, microseconds for a continuous one @min -500 @max 500
} servo_trim_t;

// The position in tenths: 905 = 90.5 degrees, -250 = -25 percent. In byte mode the value is the byte x10 (0..2550).
typedef struct __attribute__((packed)) {
  int16_t value_x10;  //@alias Position @description In tenths: 905 is 90.5 degrees or -250 is -25 percent; in byte mode the byte times ten
} servo_set_t;

#ifdef __cplusplus
}
#endif
