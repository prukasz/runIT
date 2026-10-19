#pragma once

#include "sys_device.h"
#include "sys_io.h"


#ifdef __cplusplus
extern "C" {
#endif

//#ref-enum @alias Servo Type
typedef enum servo_type_e {
  SERVO_TYPE_ANGLE = 0,  //@alias Angle servo @description Angle servo: moves to angle between limits
  SERVO_TYPE_CONTINUOUS = 1,  //@alias Continuous (360) servo @description Continuous rotation (360) servo: speed -100..100
} servo_type_e;

/**
 * @brief Hobby servo configuration struct (wire format for CREATE).
 *
 * Driven through the IO contract (50 Hz PWM).
 */
typedef struct __attribute__((packed)) {
  sys_io_pin_t pwm_pin;      /**< PWM output pin */
  uint8_t servo_type;        /**< Servo type (servo_type_e) */
  uint8_t byte_mode;         /**< 1 = 0..255 byte position mode, 0 = natural units */
  uint8_t invert;            /**< 1 = reversed movement direction */
  int16_t angle_min_deg;     /**< Angle at minimum pulse (degrees) */
  int16_t angle_max_deg;     /**< Angle at maximum pulse (degrees) */
  int16_t default_pos;       /**< Start-up position in natural units */
  uint16_t min_us;           /**< Minimum pulse width (us) */
  uint16_t max_us;           /**< Maximum pulse width (us) */
  uint16_t deadband_us;      /**< Continuous servo deadband around 0 (us) */
} servo_cfg_t;

/* Own ops */
#define SERVO_OP_HOME      SYS_DEVICE_OWN_OP(0x00)      // go to default_pos (+ trim)
#define SERVO_OP_SET_TRIM  SYS_DEVICE_OWN_OP(0x01)  // servo_trim_t: added to every target, applied by the next move
#define SERVO_OP_SET       SYS_DEVICE_OWN_OP(0x02)       // servo_set_t: move to a position
#define SERVO_OP_GET_PULSE SYS_DEVICE_OWN_OP(0x03) // -> u16 us, the pulse last sent (0 = not driven)
#define SERVO_OP_RELEASE   SYS_DEVICE_OWN_OP(0x04)   // stop the pulse: the servo goes limp; the next SET / HOME drives it again

#define SERVO_TRIM_MAX 500  // +-: degrees for an angle servo (the pulse range limits it anyway), us for a continuous one

typedef struct __attribute__((packed)) {
  int16_t trim;  /**< Trim added to target (degrees or microseconds) */
} servo_trim_t;

/** Position in tenths: 905 = 90.5 degrees, -250 = -25 percent. In byte mode: byte x 10. */
typedef struct __attribute__((packed)) {
  int16_t value_x10;  /**< Target position in tenths of unit */
} servo_set_t;

#ifdef __cplusplus
}
#endif
